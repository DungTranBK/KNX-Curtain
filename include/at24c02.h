/*
 * at24c02.h
 *
 *  Created on: Mar 13, 2026
 *      Author: DungTran BK
 */

#ifndef _AT24C02_H_
#define _AT24C02_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>

/* AT24C02 I2C 7-bit address (A0=A1=A2=GND) */
#define AT24C02_I2C_ADDR    0x50

/* AT24C02 memory size: 256 bytes (2 Kbit) */
#define AT24C02_MEM_SIZE    256

/* AT24C02 page size: 8 bytes */
#define AT24C02_PAGE_SIZE   8

/**
 * @brief Initialize AT24C02 driver with the I2C device
 * @param dev Pointer to the I2C device
 */
void at24c02_init(const struct device *dev);

/**
 * @brief Check if AT24C02 is present on the I2C bus
 *
 * Sends a write-address byte to the AT24C02 and checks for ACK.
 *
 * @return true  AT24C02 responded (ACK received)
 * @return false AT24C02 not found (NACK or error)
 */
bool at24c02_is_present(void);

/**
 * @brief Write a single byte to AT24C02
 * @param mem_addr Memory address (0x00 - 0xFF)
 * @param data     Byte to write
 * @return 0 on success, negative errno on failure
 */
int at24c02_write_byte(uint8_t mem_addr, uint8_t data);

/**
 * @brief Read a single byte from AT24C02
 * @param mem_addr Memory address (0x00 - 0xFF)
 * @param data     Pointer to store the read byte
 * @return 0 on success, negative errno on failure
 */
int at24c02_read_byte(uint8_t mem_addr, uint8_t *data);

/**
 * @brief Write multiple bytes to AT24C02 (handles page boundaries)
 * @param mem_addr Starting memory address
 * @param data     Pointer to data buffer
 * @param len      Number of bytes to write
 * @return 0 on success, negative errno on failure
 */
int at24c02_write(uint8_t mem_addr, const uint8_t *data, uint16_t len);

/**
 * @brief Read multiple bytes from AT24C02
 * @param mem_addr Starting memory address
 * @param data     Pointer to buffer to store read data
 * @param len      Number of bytes to read
 * @return 0 on success, negative errno on failure
 */
int at24c02_read(uint8_t mem_addr, uint8_t *data, uint16_t len);

#endif /* _AT24C02_H_ */
