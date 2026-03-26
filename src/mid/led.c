#include "../../include/led.h"

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "../../include/app_device.h"
#include "../../include/fifo.h"
#include "../../include/network.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(led, CONFIG_LOG_DEFAULT_LEVEL);

/* GPIO LED specs from devicetree - 3 channels of ONE physical LED package */
static const struct gpio_dt_spec red_spec =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green_spec =
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue_spec =
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* Refresh LED callback */
static typeLed_HandleRefreshLedCallbackFunc pvLed_HandleRefreshLed = NULL;

/******************************************************************************/
/*                              CONSTANTS                                     */
/******************************************************************************/

/* LED Brightness Level */
#define LED_BRIGHTNESS_MIN 5
#define LED_BRIGHTNESS_MAX 100
#define LED_BRIGHTNESS_DEFAULT 100

static uint8_t led_brightness = LED_BRIGHTNESS_DEFAULT;

/* Brightness change callback */
typedef void (*led_brightness_change_cb_t)(uint8_t brightness);
static led_brightness_change_cb_t brightness_change_cb = NULL;

#define LED_ON_LEVEL 0xFF

/******************************************************************************/
/*                          PRIVATE DATA                                      */
/******************************************************************************/

/* Individual state for the single physical LED */
static rgb_t m_rgb_state;

/* Blink State for the single physical LED */
static led_blink_init_t blink_state;

/* Command FIFO */
static led_command_t fifoLedBuffer[BUF_LED_CMD_SIZE];
static Fifo_t fifoLedCommands;
static led_command_t cmdIsRunning = COMMAND_LED_DEFAULT;

static led_queue_par_t led_queue_par = {.enable = true};

/******************************************************************************/
/*                         PRIVATE FUNCTIONS                                  */
/******************************************************************************/

/**
 * @brief Control GPIO LEDs based on software state (Single RGB LED Package)
 */
static void led_map_and_control(void) {
  gpio_pin_set_dt(&red_spec, m_rgb_state.r ? 0 : 1);
  gpio_pin_set_dt(&green_spec, m_rgb_state.g ? 0 : 1);
  gpio_pin_set_dt(&blue_spec, m_rgb_state.b ? 0 : 1);
}

/**
 * @brief Blink LED Red
 */
static void led_blink_led_red(void) {
  m_rgb_state.g = 0;
  m_rgb_state.b = 0;
  if (m_rgb_state.r) {
    m_rgb_state.r = 0;
  } else {
    m_rgb_state.r = LED_ON_LEVEL;
  }
  led_map_and_control();
}

/**
 * @brief Blink LED Green
 */
static void led_blink_led_green(void) {
  m_rgb_state.r = 0;
  m_rgb_state.b = 0;
  if (m_rgb_state.g) {
    m_rgb_state.g = 0;
  } else {
    m_rgb_state.g = LED_ON_LEVEL;
  }
  led_map_and_control();
}

/**
 * @brief Blink LED Blue
 */
static void led_blink_led_blue(void) {
  m_rgb_state.r = 0;
  m_rgb_state.g = 0;
  if (m_rgb_state.b) {
    m_rgb_state.b = 0;
  } else {
    m_rgb_state.b = LED_ON_LEVEL;
  }
  led_map_and_control();
}

/**
 * @brief Blink LED Pink
 */
static void led_blink_led_pink(void) {
  m_rgb_state.g = 0;
  if (m_rgb_state.r) {
    m_rgb_state.r = m_rgb_state.b = 0;
  } else {
    m_rgb_state.r = m_rgb_state.b = LED_ON_LEVEL;
  }
  led_map_and_control();
}

/**
 * @brief Blink LED White
 */
static void led_blink_led_white(void) {
  if (m_rgb_state.r || m_rgb_state.g || m_rgb_state.b) {
    m_rgb_state.r = m_rgb_state.g = m_rgb_state.b = 0;
  } else {
    m_rgb_state.r = m_rgb_state.g = m_rgb_state.b = LED_ON_LEVEL;
  }
  led_map_and_control();
}

/**
 * @brief Set RGB state and update hardware
 */
static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
  m_rgb_state.r = r;
  m_rgb_state.g = g;
  m_rgb_state.b = b;
  led_map_and_control();
}

/**
 * @brief Set LED Color Internal
 */
static void led_set_color_internal(LedColor_enum color) {
  switch (color) {
  case LED_COLOR_RED:
    led_set_rgb(LED_ON_LEVEL, 0, 0);
    break;
  case LED_COLOR_GREEN:
    led_set_rgb(0, LED_ON_LEVEL, 0);
    break;
  case LED_COLOR_BLUE:
    led_set_rgb(0, 0, LED_ON_LEVEL);
    break;
  case LED_COLOR_PINK:
    led_set_rgb(LED_ON_LEVEL, 0, LED_ON_LEVEL);
    break;
  case LED_COLOR_WHITE:
    led_set_rgb(LED_ON_LEVEL, LED_ON_LEVEL, LED_ON_LEVEL);
    break;
  case LED_COLOR_YELLOW:
    led_set_rgb(LED_ON_LEVEL, LED_ON_LEVEL, 0);
    break;
  case LED_COLOR_NONE:
    led_set_rgb(0, 0, 0);
    break;
  default:
    break;
  }
}

/**
 * @brief Set LED color with last state enum
 */
static void led_set_color_last_state(LedLastState_enum color) {
  switch (color) {
  case LAST_STATE_ON_RED:
    led_set_color_internal(LED_COLOR_RED);
    break;
  case LAST_STATE_ON_GREEN:
    led_set_color_internal(LED_COLOR_GREEN);
    break;
  case LAST_STATE_ON_BLUE:
    led_set_color_internal(LED_COLOR_BLUE);
    break;
  case LAST_STATE_ON_PINK:
    led_set_color_internal(LED_COLOR_PINK);
    break;
  case LAST_STATE_ON_WHITE:
    led_set_color_internal(LED_COLOR_WHITE);
    break;
  case LAST_STATE_ON_YELLOW:
    led_set_color_internal(LED_COLOR_YELLOW);
    break;
  case LAST_STATE_COLOR_NONE:
    led_off(0);
    break;
  default:
    break;
  }
}

/**
 * @brief Toggle LED components for blinking
 */
static void led_toggle_color(LedColor_enum color) {
  switch (color) {
  case LED_COLOR_RED:
    m_rgb_state.r = m_rgb_state.r ? 0 : LED_ON_LEVEL;
    m_rgb_state.g = 0;
    m_rgb_state.b = 0;
    break;
  case LED_COLOR_GREEN:
    m_rgb_state.g = m_rgb_state.g ? 0 : LED_ON_LEVEL;
    m_rgb_state.r = 0;
    m_rgb_state.b = 0;
    break;
  case LED_COLOR_BLUE:
    m_rgb_state.b = m_rgb_state.b ? 0 : LED_ON_LEVEL;
    m_rgb_state.r = 0;
    m_rgb_state.g = 0;
    break;
  case LED_COLOR_PINK:
    if (m_rgb_state.r || m_rgb_state.b) {
      m_rgb_state.r = m_rgb_state.b = 0;
    } else {
      m_rgb_state.r = m_rgb_state.b = LED_ON_LEVEL;
    }
    m_rgb_state.g = 0;
    break;
  case LED_COLOR_WHITE:
    if (m_rgb_state.r || m_rgb_state.g || m_rgb_state.b) {
      m_rgb_state.r = m_rgb_state.g = m_rgb_state.b = 0;
    } else {
      m_rgb_state.r = m_rgb_state.g = m_rgb_state.b = LED_ON_LEVEL;
    }
    break;
  default:
    break;
  }
  led_map_and_control();
}

/**
 * @brief Toggle LED (blink implementation)
 */
static void led_toggle(void) { led_toggle_color(blink_state.toggleLedState); }

/* ... FIFO functions remain the same ... */

/* (Skipping internal FIFO implementation to save space in chunk, but ensuring
 * logic remains) */

/**
 * @brief Initialize LED module
 */
int led_init(void) {
  int ret;

  /* Initialize GPIO LEDs - Red */
  if (!device_is_ready(red_spec.port)) {
    LOG_ERR("Red LED GPIO device not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&red_spec, GPIO_OUTPUT_INACTIVE);
  if (ret < 0)
    return ret;

  /* Initialize GPIO LEDs - Green */
  if (!device_is_ready(green_spec.port)) {
    LOG_ERR("Green LED GPIO device not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&green_spec, GPIO_OUTPUT_INACTIVE);
  if (ret < 0)
    return ret;

  /* Initialize GPIO LEDs - Blue */
  if (!device_is_ready(blue_spec.port)) {
    LOG_ERR("Blue LED GPIO device not ready");
    return -ENODEV;
  }
  ret = gpio_pin_configure_dt(&blue_spec, GPIO_OUTPUT_INACTIVE);
  if (ret < 0)
    return ret;

  /* Clear all LEDs */
  led_off_all();

  /* Initialize blink state */
  memset(&blink_state, 0, sizeof(blink_state));
  blink_state.toggleLedInterval = DEFAULT_BLINK_INTERVAL;
  blink_state.toggleLedFlag = BLINK_IDLE;

  /* Initialize FIFO */
  FifoInit(&fifoLedCommands, fifoLedBuffer, sizeof(led_command_t),
           BUF_LED_CMD_SIZE);

  /* Restore backlight brightness from flash */
  led_restore_backlight_brightness();

  return 0;
}

/**
 * @brief Set LED Color
 */
void led_set_color(uint8_t ledNbr, LedColor_enum color) {
  if (color == LED_COLOR_NONE) {
    color = LED_COLOR_WHITE; /* Telink default */
  }
  led_set_color_internal(color);
}

/**
 * @brief Turn off a specific LED
 */
void led_off(uint8_t idx) {
  m_rgb_state.r = 0;
  m_rgb_state.g = 0;
  m_rgb_state.b = 0;
  led_map_and_control();
}

/**
 * @brief Turn off all LEDs
 */
void led_off_all(void) {
  memset(&m_rgb_state, 0, sizeof(m_rgb_state));
  led_map_and_control();
}

/**
 * @brief Turn on LEDs with specified mask and color
 * @param ledMask Bitmask of LEDs to turn on (1 = ON, 0 = OFF)
 * @param ledState Special LED state (color or mode)
 */
void led_turn_on(uint16_t ledMask, SpecialLed_enum ledState) {
  led_command_t cmd = COMMAND_LED_DEFAULT;
  cmd.ledMode = LED_MODE_ON;
  cmd.ledMask = ledMask;

  switch (ledState) {
  case CMD_ON_BLUE:
    cmd.ledColor = LED_COLOR_BLUE;
    break;
  case CMD_ON_RED:
    cmd.ledColor = LED_COLOR_RED;
    break;
  case CMD_ON_PINK:
    cmd.ledColor = LED_COLOR_PINK;
    break;
  case CMD_REFRESH_LED:
    cmd.ledMode = LED_MODE_REFRESH;
    break;
  case CMD_OFF_ALL:
    cmd.ledMode = LED_MODE_OFF;
    break;
  default:
    cmd.ledColor = LED_COLOR_WHITE;
    break;
  }

  led_push_led_command_to_fifo(&cmd);
}

/**
 * @brief Turn off LED
 */
void led_turn_off(uint16_t ledMask) {
  led_command_t cmd = COMMAND_LED_DEFAULT;
  cmd.ledMode = LED_MODE_OFF;
  cmd.ledMask = ledMask;
  led_push_led_command_to_fifo(&cmd);
}

/**
 * @brief Blink LED without pushing to FIFO (internal use)
 */
void led_blink_no_queue(uint16_t ledMask, uint8_t blinkTimes,
                        LedLastState_enum lastState, LedColor_enum color,
                        uint16_t blinkInterval) {
  if (blinkTimes > 0) {
    blink_state.toggleLedTimes = blinkTimes * 2;
    blink_state.toggleLedInterval = blinkInterval;
    blink_state.toggleLedState = color;
    blink_state.toggleLedLastState = lastState;
    blink_state.toggleLedFlag = BLINK_ACTIVE;
    blink_state.toggleledLastTime = k_uptime_get_32();

    /* Initial toggle */
    led_toggle();
  }
}

/**
 * @brief LED Toggle Handle (blink state machine)
 */
static void led_toggle_handle(void) {
  static uint32_t ledBlinkScanTimer = 0;
  uint8_t enableRefreshLed = 0;
  uint32_t now = k_uptime_get_32();

  if ((now - ledBlinkScanTimer) > 10) {
    if (blink_state.toggleLedFlag == BLINK_ACTIVE) {
      if (blink_state.toggleLedTimes > 0) {
        if ((now - blink_state.toggleledLastTime) >=
            blink_state.toggleLedInterval) {
          blink_state.toggleledLastTime = now;
          blink_state.toggleLedTimes--;

          if (blink_state.toggleLedTimes == 0) {
            blink_state.toggleLedFlag = BLINK_IDLE;

            if (FifoIsEmpty(&fifoLedCommands)) {
              switch (blink_state.toggleLedLastState) {
              case LAST_STATE_ON_RED:
              case LAST_STATE_ON_BLUE:
              case LAST_STATE_ON_PINK:
              case LAST_STATE_COLOR_NONE:
                led_set_color_last_state(blink_state.toggleLedLastState);
                break;
              case LAST_STATE_REFRESH_LED:
              default:
                enableRefreshLed = 1;
                break;
              }
            }
          } else {
            led_toggle();
          }
        }
      } else {
        blink_state.toggleLedFlag = BLINK_IDLE;
      }
    }
    ledBlinkScanTimer = now;
    if (enableRefreshLed == 1) {
      led_refresh(0xFFFF);
    }
  }
}

/**
 * @brief Blink LED setup
 */
void led_blink(uint16_t ledMask, SpecialLed_enum ledState, uint8_t blinkTimes,
               LedLastState_enum lastState, uint16_t blinkInterval) {
  led_command_t cmd = COMMAND_LED_DEFAULT;
  cmd.ledMode = LED_MODE_BLINK;
  cmd.ledMask = BACKUP_MASK;
  cmd.blinkTime = blinkTimes;
  cmd.lastState = lastState;
  cmd.blinkInterval =
      (blinkInterval == 0) ? DEFAULT_BLINK_INTERVAL : blinkInterval;

  switch (ledState) {
  case CMD_BLINK_BLUE:
    cmd.ledColor = LED_COLOR_BLUE;
    break;
  case CMD_BLINK_RED:
    cmd.ledColor = LED_COLOR_RED;
    break;
  case CMD_BLINK_PINK:
    cmd.ledColor = LED_COLOR_PINK;
    break;
  default:
    cmd.ledColor = LED_COLOR_WHITE;
    break;
  }

  led_push_led_command_to_fifo(&cmd);
}

/**
 * @brief Blink LED with specified color
 */
void led_blink_color(uint16_t ledMask, LedColor_enum color, uint8_t blinkTimes,
                     LedLastState_enum lastState, uint16_t blinkInterval) {
  led_command_t cmd = COMMAND_LED_DEFAULT;
  cmd.ledMode = LED_MODE_BLINK;
  cmd.ledMask = ledMask;
  cmd.ledColor = color;
  cmd.blinkTime = blinkTimes;
  cmd.lastState = lastState;
  cmd.blinkInterval =
      (blinkInterval == 0) ? DEFAULT_BLINK_INTERVAL : blinkInterval;

  led_push_led_command_to_fifo(&cmd);
}

/**
 * @brief Register callback for LED refresh
 */
void led_refresh_callback_init(
    typeLed_HandleRefreshLedCallbackFunc refreshLedCallbackInit) {
  if (refreshLedCallbackInit != NULL) {
    pvLed_HandleRefreshLed = refreshLedCallbackInit;
  }
}

/**
 * @brief Refresh LED
 */
void led_refresh(uint16_t ledMask) {
  if (pvLed_HandleRefreshLed != NULL) {
    pvLed_HandleRefreshLed(ledMask);
  }
}

/**
 * @brief Get blink LED flag
 */
uint8_t led_get_blink_led_flag(void) {
  if (blink_state.toggleLedFlag == BLINK_ACTIVE) {
    return BLINK_ACTIVE;
  }
  return BLINK_IDLE;
}

/**
 * @brief Push LED command to FIFO
 */
uint8_t led_push_led_command_to_fifo(led_command_t *ledCmd) {
  if (FifoPush(&fifoLedCommands, ledCmd)) {
    return 1; /* TRUE */
  }
  return 0; /* FALSE */
}

/**
 * @brief Push fast blink LED command to FIFO
 */
void led_push_fast_blink_led_cmd_to_fifo(uint8_t blinkTime,
                                         LedColor_enum color) {
  led_command_t ledCmd = COMMAND_LED_DEFAULT;
  ledCmd.ledColor = color;
  ledCmd.blinkTime = blinkTime;
  ledCmd.blinkInterval = FAST_BLINK_INTERVAL; /* 180ms as in Telink */
  (void)led_push_led_command_to_fifo(&ledCmd);
}

/**
 * @brief Push normal blink LED command to FIFO
 */
void led_push_normal_blink_led_cmd_to_fifo(uint8_t blinkTime,
                                           LedColor_enum color) {
  led_command_t ledCmd = COMMAND_LED_DEFAULT;
  ledCmd.ledColor = color;
  ledCmd.blinkTime = blinkTime;
  (void)led_push_led_command_to_fifo(&ledCmd);
}

/**
 * @brief Disable current command and queue with timeout
 */
void led_disable_current_command_and_queue_with_timeout(uint16_t ms) {
  led_queue_par.enable = false;
  led_queue_par.disable_st_time_ms = clock_time_ms();
  led_queue_par.disable_timeout = ms;
}

/**
 * @brief Enable LED queue
 */
void led_enable_queue(void) { led_queue_par.enable = true; }

/**
 * @brief Handle LED events (process FIFO commands and manage blinking)
 */
void led_handle_event_function(void) {
  uint8_t blinkFlag = BLINK_IDLE;

  /* Check if LED queue is disabled */
  if (led_queue_par.enable == false) {
    if (clock_time_exceed_ms(led_queue_par.disable_st_time_ms,
                             led_queue_par.disable_timeout)) {
      led_queue_par.enable = true;
    }
    led_toggle_handle();
    return;
  }
  /* Check if any LED is blinking */
  if (blink_state.toggleLedFlag == BLINK_ACTIVE) {
    blinkFlag = BLINK_ACTIVE;
  }
  if (blinkFlag == BLINK_IDLE) {
    /* Process FIFO commands */
    if (!FifoIsEmpty(&fifoLedCommands)) {
      if (FifoPop(&fifoLedCommands, &cmdIsRunning)) {
        if (cmdIsRunning.ledMode == LED_MODE_REFRESH) {
          if (pvLed_HandleRefreshLed != NULL) {
            pvLed_HandleRefreshLed(cmdIsRunning.ledMask);
          }
        } else if (cmdIsRunning.ledMode == LED_MODE_BLINK) {
          led_blink_no_queue(cmdIsRunning.ledMask, cmdIsRunning.blinkTime,
                             cmdIsRunning.lastState, cmdIsRunning.ledColor,
                             cmdIsRunning.blinkInterval);
        } else if (cmdIsRunning.ledMode == LED_MODE_ON) {
          if (cmdIsRunning.ledColor < LED_COLOR_END) {
            if (cmdIsRunning.ledMask != 0) {
              led_set_color(0, cmdIsRunning.ledColor);
            }
          }
        } else if (cmdIsRunning.ledMode == LED_MODE_OFF) {
          if (cmdIsRunning.ledMask != 0) {
            led_off(0);
          }
        }
      }
    }
  }

  /* Handle blink toggle */
  led_toggle_handle();
}

/******************************************************************************/
/*                    BACKLIGHT BRIGHTNESS FUNCTIONS                          */
/******************************************************************************/

/* Settings key for backlight brightness */
#define SETTINGS_KEY_BACKLIGHT "led/bl"

/**
 * @brief Settings handler for backlight brightness
 */
static int backlight_settings_set(const char *name, size_t len,
                                  settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  if (settings_name_steq(name, "lvl", &next) && !next) {
    if (len != sizeof(led_brightness)) {
      return -EINVAL;
    }
    if (read_cb(cb_arg, &led_brightness, sizeof(led_brightness)) < 0) {
      return -EIO;
    }
    LOG_INF("Backlight brightness restored: %d", led_brightness);
    return 0;
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(led_backlight, SETTINGS_KEY_BACKLIGHT, NULL,
                               backlight_settings_set, NULL, NULL);

/**
 * @brief Save backlight brightness to flash
 */
static void save_backlight_brightness(void) {
  int rc = settings_save_one(SETTINGS_KEY_BACKLIGHT "/lvl", &led_brightness,
                             sizeof(led_brightness));
  if (rc) {
    LOG_ERR("Failed to save backlight brightness: %d", rc);
  } else {
    LOG_INF("Backlight brightness saved: %d", led_brightness);
  }
}

/**
 * @brief Register callback for brightness changes
 * @param cb Callback function to be called when brightness changes
 */
void led_register_brightness_callback(void (*cb)(uint8_t)) {
  brightness_change_cb = cb;
}

/**
 * @brief Set backlight brightness
 *
 * @param brightness Brightness level (1-100)
 * @return Current brightness after clamping
 */
uint8_t led_set_brightness(uint8_t brightness) {
  /* Clamp brightness level to valid range */
  if (brightness < LED_BRIGHTNESS_MIN) {
    brightness = LED_BRIGHTNESS_MIN;
  } else if (brightness > LED_BRIGHTNESS_MAX) {
    brightness = LED_BRIGHTNESS_MAX;
  }

  led_brightness = brightness;
  LOG_INF("Backlight brightness set to: %d", led_brightness);

  /* Save to flash */
  save_backlight_brightness();

  /* Notify led*/
  led_push_normal_blink_led_cmd_to_fifo(2, LED_COLOR_BLUE);

  /* Notify via callback (app.c will notify Mesh + KNX) */
  if (brightness_change_cb) {
    brightness_change_cb(led_brightness);
  }

  return led_brightness;
}

/**
 * @brief Get current backlight brightness
 * @return Current brightness level (1-100)
 */
uint8_t led_get_brightness(void) { return led_brightness; }

/**
 * @brief Restore backlight brightness from settings
 */
void led_restore_backlight_brightness(void) {
  int rc = settings_load_subtree(SETTINGS_KEY_BACKLIGHT);
  if (rc) {
    LOG_WRN("Failed to load backlight settings: %d", rc);
  }
}

/**
 * @func   led_fifo_is_empty
 * @brief
 * @param  None
 * @retval None
 */
bool led_fifo_is_empty(void) { return FifoIsEmpty(&fifoLedCommands); }

/**
 * @func   LED_pushNormalBlinkLedWithIntervalCmdToFifo
 * @brief  None
 * @param
 * @retval None
 */
void LED_push_blink_with_Interval_to_fifo(uint8_t blinkTime,
                                          LedColor_enum color,
                                          uint16_t interval) {
  led_command_t led_cmd = COMMAND_LED_DEFAULT;
  led_cmd.ledColor = color;
  led_cmd.blinkTime = blinkTime;
  led_cmd.blinkInterval = interval;
  (void)led_push_led_command_to_fifo(&led_cmd);
}