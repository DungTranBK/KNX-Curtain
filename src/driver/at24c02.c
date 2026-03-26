/*
 * at24c02.c
 *
 *  Created on: Mar 13, 2026
 *      Author: DungTran BK
 */

#include "../../include/at24c02.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(at24c02, CONFIG_LOG_DEFAULT_LEVEL);

/* Write cycle time for AT24C02 (max 5ms per datasheet) */
#define AT24C02_WRITE_CYCLE_MS 5

static const struct device* i2c_dev;

/**
 * @brief Initialize AT24C02 driver
 */
void at24c02_init(const struct device* dev) {
  i2c_dev = dev;
  if (!device_is_ready(i2c_dev)) {
    LOG_ERR("I2C device not ready");
    return;
  }
}

/**
 * @brief Check if AT24C02 is present on the I2C bus
 *
 * Performs a 0-byte write (address-only) to AT24C02.
 * If ACK is received -> device is present.
 */
bool at24c02_is_present(void) {
  if (i2c_dev == NULL) {
    LOG_ERR("AT24C02 not initialized");
    return false;
  }

  /*
   * i2c_write with len=0 sends only the address byte.
   * If the device ACKs, it returns 0 -> present.
   * If NACK or bus error, it returns non-zero -> not present.
   */
  uint8_t dummy;
  int ret = i2c_write(i2c_dev, &dummy, 0, AT24C02_I2C_ADDR);

  if (ret == 0) {
    LOG_INF("AT24C02 detected at address 0x%02X", AT24C02_I2C_ADDR);
    return true;
  } else {
    LOG_WRN("AT24C02 not found at address 0x%02X (err %d)", AT24C02_I2C_ADDR,
            ret);
    return false;
  }
}

/**
 * @brief Write a single byte to AT24C02
 */
int at24c02_write_byte(uint8_t mem_addr, uint8_t data) {
  uint8_t buf[2] = {mem_addr, data};
  int ret;

  ret = i2c_write(i2c_dev, buf, sizeof(buf), AT24C02_I2C_ADDR);
  if (ret != 0) {
    LOG_ERR("AT24C02 write byte failed at 0x%02X (err %d)", mem_addr, ret);
    return ret;
  }

  /* Wait for write cycle to complete */
  k_msleep(AT24C02_WRITE_CYCLE_MS);
  return 0;
}

/**
 * @brief Read a single byte from AT24C02
 */
int at24c02_read_byte(uint8_t mem_addr, uint8_t* data) {
  int ret;

  /* Write memory address, then read 1 byte */
  ret = i2c_write_read(i2c_dev, AT24C02_I2C_ADDR, &mem_addr, sizeof(mem_addr),
                       data, 1);
  if (ret != 0) {
    LOG_ERR("AT24C02 read byte failed at 0x%02X (err %d)", mem_addr, ret);
  }
  return ret;
}

/**
 * @brief Write multiple bytes to AT24C02 with page-boundary handling
 *
 * AT24C02 has 8-byte pages. Writes that cross page boundaries
 * will wrap around within the same page, so we split into
 * page-aligned chunks.
 */
int at24c02_write(uint8_t mem_addr, const uint8_t* data, uint16_t len) {
  int ret;
  uint16_t offset = 0;

  while (offset < len) {
    /* Calculate how many bytes remain in the current page */
    uint8_t page_remaining =
        AT24C02_PAGE_SIZE - ((mem_addr + offset) % AT24C02_PAGE_SIZE);
    uint8_t chunk =
        (len - offset) < page_remaining ? (len - offset) : page_remaining;

    /* Build buffer: [mem_addr, data...] */
    uint8_t buf[AT24C02_PAGE_SIZE + 1];
    buf[0] = mem_addr + offset;
    memcpy(&buf[1], &data[offset], chunk);

    ret = i2c_write(i2c_dev, buf, chunk + 1, AT24C02_I2C_ADDR);
    if (ret != 0) {
      LOG_ERR("AT24C02 write failed at 0x%02X (err %d)", mem_addr + offset,
              ret);
      return ret;
    }

    /* Wait for write cycle */
    k_msleep(AT24C02_WRITE_CYCLE_MS);
    offset += chunk;
  }

  return 0;
}

/**
 * @brief Read multiple bytes from AT24C02
 */
int at24c02_read(uint8_t mem_addr, uint8_t* data, uint16_t len) {
  int ret;

  ret = i2c_write_read(i2c_dev, AT24C02_I2C_ADDR, &mem_addr, sizeof(mem_addr),
                       data, len);
  if (ret != 0) {
    LOG_ERR("AT24C02 read failed at 0x%02X, len=%d (err %d)", mem_addr, len,
            ret);
  }
  return ret;
}
