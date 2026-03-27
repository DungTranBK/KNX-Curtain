/**
 * @file knx_provision.h
 * @brief KNX Provisioning Data Reader
 * @details Reads FDSK and Serial Number from a dedicated raw flash partition.
 *          Data is written during manufacturing via J-Link.
 *
 * Flash Format (28 bytes):
 *   Offset 0x00: Magic       "KNXP" (4 bytes)
 *   Offset 0x04: Version     0x01   (1 byte)
 *   Offset 0x05: Serial      MfgID(2B) + DeviceID(4B) = 6 bytes
 *   Offset 0x0B: FDSK        16 bytes (random, unique per device)
 *   Offset 0x1B: CRC8        1 byte (over bytes 0x00..0x1A)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNX_PROV_MAGIC 0x4B4E5850U /* "KNXP" in little-endian */
#define KNX_PROV_VERSION 0x01
#define KNX_PROV_SERIAL_LEN 6
#define KNX_PROV_FDSK_LEN 16

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t version;
  uint8_t serial[KNX_PROV_SERIAL_LEN];
  uint8_t fdsk[KNX_PROV_FDSK_LEN];
  uint8_t crc8;
} knx_provision_data_t;

/**
 * @brief Read and validate provisioning data from flash partition.
 * @param[out] out  Pointer to struct to fill with provisioned data.
 * @return true if valid provisioning data was found, false otherwise.
 */
bool knx_provision_read(knx_provision_data_t* out);

/**
 * @brief Extract 32-bit bauNumber from provisioned serial number.
 * @details Uses serial[2..5] (the 4-byte device ID portion).
 * @param data  Pointer to valid provisioning data.
 * @return 32-bit bauNumber for KNX stack.
 */
uint32_t knx_provision_get_bau_number(const knx_provision_data_t* data);

#ifdef __cplusplus
}
#endif
