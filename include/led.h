/*
 * LED Control Module - Refactored for WS2812
 *
 * Provides high-level LED control API using WS2812 RGB LEDs
 * Based on Telink periph_led implementation
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LED_H_
#define LED_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "utilities.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

/* LED Colors */
typedef enum {
  LED_COLOR_WHITE = 0,
  LED_COLOR_RED = 1,
  LED_COLOR_GREEN = 2,
  LED_COLOR_BLUE = 3,
  LED_COLOR_PINK = 4,
  LED_COLOR_YELLOW = 5,
  LED_COLOR_CYAN = 6,
  LED_COLOR_END,
  LED_COLOR_NONE = 0xFF
} LedColor_enum;

/* LED Modes */
typedef enum {
  LED_MODE_OFF = 0,
  LED_MODE_ON = 1,
  LED_MODE_BLINK = 2,
  LED_MODE_BLINK_FOREVER = 3,
  LED_MODE_REFRESH = 4,
  LED_MODE_NONE = 0xFF
} LedMode_enum;

/* LED Last State (after blink completes) */
typedef enum {
  LAST_STATE_REFRESH_LED = 0,
  LAST_STATE_ON_WHITE = 1,
  LAST_STATE_ON_RED = 2,
  LAST_STATE_ON_GREEN = 3,
  LAST_STATE_ON_BLUE = 4,
  LAST_STATE_ON_PINK = 5,
  LAST_STATE_ON_YELLOW = 6,
  LAST_STATE_ON_CYAN = 7,
  LAST_STATE_COLOR_NONE = 8,
  LAST_STATE_REFRESH_LED_DELAY = 9
} LedLastState_enum;

/* Special LED Commands (backward compatibility) */
typedef enum {
  CMD_REFRESH_LED,
  CMD_ON_BLUE,
  CMD_ON_RED,
  CMD_ON_PINK,
  CMD_OFF_ALL,
  CMD_BLINK_BLUE,
  CMD_BLINK_RED,
  CMD_BLINK_PINK,
  CMD_BLINK_RED_BLUE,
  UNKNOWN_CMD_LED
} SpecialLed_enum;

/* LED Command Structure */
typedef struct {
  LedMode_enum ledMode;
  uint16_t ledMask; /* Bit mask for LEDs */
  LedColor_enum ledColor;
  uint8_t blinkTime; /* Number of blinks */
  LedLastState_enum lastState;
  uint16_t blinkInterval; /* Blink interval in ms */
  uint16_t delayTimeRefresh;
} led_command_t;

/* Default LED Command */
#define COMMAND_LED_DEFAULT                                                    \
  {LED_MODE_BLINK, 0xFFFF, LED_COLOR_RED, 4, LAST_STATE_REFRESH_LED, 300}

/* Blink State */
typedef enum {
  BLINK_IDLE,
  BLINK_ACTIVE,
} LedBlinkFlag_enum;

/* Blink Initialization Structure */
typedef struct {
  LedColor_enum toggleLedState;
  uint32_t toggleledLastTime;
  LedBlinkFlag_enum toggleLedFlag;
  uint8_t toggleLedTimes;
  LedLastState_enum toggleLedLastState;
  uint32_t toggleLedInterval;
} led_blink_init_t;

/* RGB struct (GRB order for WS2812) */
typedef struct {
  uint8_t g;
  uint8_t r;
  uint8_t b;
} rgb_t;

typedef struct {
  uint8_t enable;
  uint32_t disable_st_time_ms;
  uint32_t disable_timeout;
} led_queue_par_t;

/* Define */
#define BUF_LED_CMD_SIZE 10
#define DEFAULT_BLINK_INTERVAL 300
#define FAST_BLINK_INTERVAL 180

/* Refresh LED Callback Function Type */
typedef void (*typeLed_HandleRefreshLedCallbackFunc)(uint16_t ledMask);

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/

/**
 * @brief Initialize LED module
 * @return 0 on success, negative on error
 */
int led_init(void);

/**
 * @brief Turn on LED with specific color
 * @param ledMask LED mask (bit 0-3 for LEDs 0-3)
 * @param ledState Special LED command
 */
void led_turn_on(uint16_t ledMask, SpecialLed_enum ledState);

/**
 * @brief Turn off LEDs
 * @param ledMask LED mask
 */
void led_turn_off(uint16_t ledMask);

/**
 * @brief Blink LED with special command
 * @param ledMask LED mask
 * @param ledState Special LED state (CMD_BLINK_BLUE, etc.)
 * @param blinkTimes Number of blinks
 * @param lastState State after blinking completes
 * @param blinkInterval Interval between blinks (ms)
 */
void led_blink(uint16_t ledMask, SpecialLed_enum ledState, uint8_t blinkTimes,
               LedLastState_enum lastState, uint16_t blinkInterval);

/**
 * @brief Blink LED with specific color
 * @param ledMask LED mask
 * @param color LED color
 * @param blinkTimes Number of blinks
 * @param lastState State after blinking completes
 * @param blinkInterval Interval between blinks (ms)
 */
void led_blink_color(uint16_t ledMask, LedColor_enum color, uint8_t blinkTimes,
                     LedLastState_enum lastState, uint16_t blinkInterval);

/**
 * @brief Set LED color directly
 * @param ledNbr LED number (0-3)
 * @param color LED color
 */
void led_set_color(uint8_t ledNbr, LedColor_enum color);

/**
 * @brief Turn off all LEDs
 */
void led_off_all(void);

/**
 * @brief Turn off specific LED
 * @param ledNbr LED number (0-3)
 */
void led_off(uint8_t ledNbr);

/**
 * @brief Refresh LEDs to last known state
 * @param ledMask LED mask
 */
void led_refresh(uint16_t ledMask);

/**
 * @brief Main LED event handler (call periodically)
 * Processes blink state machine and FIFO commands
 */
void led_handle_event_function(void);

/**
 * @brief Get current blink flag status
 * @return BLINK_IDLE or BLINK_ACTIVE
 */
uint8_t led_get_blink_led_flag(void);

/**
 * @brief Push LED command to FIFO
 * @param ledCmd Pointer to LED command
 * @return 0 on success, -1 if FIFO full
 */
uint8_t led_push_led_command_to_fifo(led_command_t *ledCmd);

/**
 * @brief Push normal blink command (300ms interval)
 * @param blinkTime Number of blinks
 * @param color LED color
 */
void led_push_normal_blink_led_cmd_to_fifo(uint8_t blinkTime,
                                           LedColor_enum color);

/**
 * @brief Push fast blink command (200ms interval)
 * @param blinkTime Number of blinks
 * @param color LED color
 */
void led_push_fast_blink_led_cmd_to_fifo(uint8_t blinkTime,
                                         LedColor_enum color);

/**
 * @brief Disable current LED command and queue with timeout
 * @param ms Timeout in milliseconds
 */
void led_disable_current_command_and_queue_with_timeout(uint16_t ms);

/**
 * @brief Enable LED queue
 */
void led_enable_queue(void);

/**
 * @brief Register callback for brightness changes
 * @param cb Callback function to be called when brightness changes
 */
void led_register_brightness_callback(void (*cb)(uint8_t));

/**
 * @brief Set backlight brightness
 * @param brightness Brightness level (1-100)
 * @return Current brightness after clamping
 */
uint8_t led_set_brightness(uint8_t brightness);

/**
 * @brief Get current backlight brightness
 * @return Current brightness level (1-100)
 */
uint8_t led_get_brightness(void);

/**
 * @brief Restore backlight brightness from settings
 */
void led_restore_backlight_brightness(void);

/**
 * @brief Register callback for LED refresh event (called when refresh command
 * received)
 * @param cb Callback function
 */
void led_refresh_callback_init(typeLed_HandleRefreshLedCallbackFunc cb);

void led_blink_no_queue(uint16_t ledMask, uint8_t blinkTimes,
                        LedLastState_enum lastState, LedColor_enum ledColor,
                        uint16_t blinkInterval);

bool led_fifo_is_empty(void);

void LED_push_blink_with_Interval_to_fifo(uint8_t blinkTime,
                                          LedColor_enum color,
                                          uint16_t interval);

#ifdef __cplusplus
}
#endif

#endif /* LED_H_ */
