/**
 * @file knx_provision.c
 * @brief KNX Provisioning Data Reader Implementation
 * @details Reads FDSK + Serial from raw flash at a fixed offset.
 *          Uses Zephyr flash driver directly (avoids Partition Manager issues).
 */

#include "../../include/knx_provision.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(knx_prov, LOG_LEVEL_DBG);

/* Must match device tree overlay: partition@125000 */
#define KNX_PROVISION_FLASH_OFFSET 0x125000

/**
 * @brief Simple CRC8 (polynomial 0x07, init 0x00)
 */
static uint8_t crc8_calc(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

bool knx_provision_read(knx_provision_data_t* out) {
  if (!out) {
    LOG_ERR("NULL output pointer");
    return false;
  }

  /* Flash driver is on rram_controller parent, not the soc-nv-flash child */
  const struct device* flash_dev = DEVICE_DT_GET(DT_NODELABEL(rram_controller));
  if (!device_is_ready(flash_dev)) {
    LOG_ERR("Flash device not ready");
    return false;
  }

  LOG_DBG("Reading %d bytes from flash offset 0x%08X", (int)sizeof(*out),
          KNX_PROVISION_FLASH_OFFSET);

  int rc = flash_read(flash_dev, KNX_PROVISION_FLASH_OFFSET, out, sizeof(*out));
  if (rc != 0) {
    LOG_ERR("flash_read() failed: rc=%d", rc);
    return false;
  }

  /* Dump raw bytes for debugging */
  LOG_HEXDUMP_DBG((const uint8_t*)out, sizeof(*out), "RAW provision data:");
  LOG_DBG("  Magic:   0x%08X (expected 0x%08X)", out->magic, KNX_PROV_MAGIC);
  LOG_DBG("  Version: 0x%02X (expected 0x%02X)", out->version,
          KNX_PROV_VERSION);

  /* Validate magic */
  if (out->magic != KNX_PROV_MAGIC) {
    LOG_ERR("Magic mismatch: got 0x%08X, expected 0x%08X", out->magic,
            KNX_PROV_MAGIC);
    LOG_ERR("Flash likely erased (0xFF) or not programmed at offset 0x%08X",
            KNX_PROVISION_FLASH_OFFSET);
    return false;
  }

  /* Validate version */
  if (out->version != KNX_PROV_VERSION) {
    LOG_ERR("Version mismatch: got 0x%02X, expected 0x%02X", out->version,
            KNX_PROV_VERSION);
    return false;
  }

  /* Validate CRC8 */
  uint8_t calc_crc = crc8_calc((const uint8_t*)out, sizeof(*out) - 1);
  LOG_DBG("  CRC8: stored=0x%02X, calculated=0x%02X", out->crc8, calc_crc);
  if (calc_crc != out->crc8) {
    LOG_ERR("CRC mismatch: calculated=0x%02X, stored=0x%02X", calc_crc,
            out->crc8);
    return false;
  }

  LOG_INF("Provision: Valid data found!");
  LOG_HEXDUMP_INF(out->serial, KNX_PROV_SERIAL_LEN, "Serial:");
  LOG_HEXDUMP_INF(out->fdsk, KNX_PROV_FDSK_LEN, "FDSK:");

  return true;
}

uint32_t knx_provision_get_bau_number(const knx_provision_data_t* data) {
  /* bauNumber = serial[2..5] (4 bytes device ID, big-endian) */
  return ((uint32_t)data->serial[2] << 24) | ((uint32_t)data->serial[3] << 16) |
         ((uint32_t)data->serial[4] << 8) | ((uint32_t)data->serial[5]);
}
