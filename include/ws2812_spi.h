/*
 * WS2812 LED Driver via SPI
 *
 * Uses SPI bit-banging to control WS2812/WS2812B RGB LEDs
 * SPI frequency: 4MHz for timing approximation
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WS2812_SPI_H_
#define WS2812_SPI_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration */
#define WS2812_NUM_LEDS 4        /* Number of LEDs in the strip */
#define WS2812_COLOR_ORDER_GRB 1 /* WS2812 uses GRB byte order */

/* Color structure */
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} ws2812_rgb_t;

/**
 * @brief Initialize WS2812 driver and SPI peripheral
 * @return 0 on success, negative error code on failure
 */
int ws2812_init(void);

/**
 * @brief Update LED strip with external RGB buffer
 * @param pixels Array of RGB values
 * @param count Number of LEDs
 * @return 0 on success, negative error code on failure
 */
int ws2812_update(const ws2812_rgb_t* pixels, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_SPI_H_ */
