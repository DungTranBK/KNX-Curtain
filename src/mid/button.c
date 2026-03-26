/*
 * button.c - Ported from Telink periph_button.c
 *
 * Copyright (c) 2024
 * Author: DungTranBK
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "../../include/button.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../../include/app_device.h"

LOG_MODULE_REGISTER(button, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

/* Callback function pointer */
static typeButton_HandleStateCallbackFunc pvButton_HandleStateCallback = NULL;

static const struct gpio_dt_spec button_specs[NUMBER_BUTTON] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
};

/* Button state parameters for each button */
static opt_button_params_t opt_button_params[NUMBER_BUTTON];

/* Timing constants */
#define OPTION_BUTTON_PRESS_TIME_OUT TIMER_700MS
#define TIMER_700MS 700
#define TIMER_800MS 800

/* Macros for elapsed time and state checking */
#define OPTION_BUTTON_PRESS_ELAPSED_TIME(x)                                    \
  (clock_time_ms() - opt_button_params[x].press_last_t)
#define OPTION_BUTTON_HOLD_TIME(x)                                             \
  (clock_time_ms() - opt_button_params[x].start_hold_t)

/* Scan interval */
static uint32_t scan_interval_ms = BUTTON_NORMAL_SCAN_INTERVAL_MS;
static uint32_t opt_button_scan_timer[NUMBER_BUTTON] = {0};

/******************************************************************************/
/*                       PRIVATE FUNCTION DECLARATIONS                        */
/******************************************************************************/

static void button_call_handle_function(uint8_t idx, uint8_t state);
static uint8_t option_button_state(uint8_t idx);
static uint8_t check_option_button_press(uint8_t idx);
static void option_button_press(uint8_t idx);
static void option_button_press_times(uint8_t idx);

/******************************************************************************/
/*                        EXPORT FUNCTIONS                                    */
/******************************************************************************/

/**
 * @func    button_init
 * @brief   Initialize button GPIO and callback
 * @param   func - Callback function for button events
 */
void button_init(typeButton_HandleStateCallbackFunc func) {
  int ret;

  /* Initialize callback */
  if (func != NULL) {
    pvButton_HandleStateCallback = func;
  }

  /* Configure GPIO for each button */
  for (int i = 0; i < NUMBER_BUTTON; i++) {
    if (!gpio_is_ready_dt(&button_specs[i])) {
      LOG_ERR("Button %d GPIO device not ready", i);
      continue;
    }

    ret = gpio_pin_configure_dt(&button_specs[i], GPIO_INPUT);
    if (ret < 0) {
      LOG_ERR("Failed to configure button %d GPIO: %d", i, ret);
      continue;
    }

    /* Initialize button parameters */
    opt_button_params[i].press_last_t = 0;
    opt_button_params[i].press_cnt = 0;
    opt_button_params[i].press_many_time_f = false;
    opt_button_params[i].start_hold_t = 0;
    opt_button_params[i].prev_state = OPTION_BUTTON_RELEASE;
    opt_button_params[i].hold_step = 0xFF;
    opt_button_params[i].poll_cnt = 0;

    opt_button_scan_timer[i] = 0;

    LOG_DBG("Button %d initialized", i);
  }

  LOG_INF("Button module initialized with %d buttons", NUMBER_BUTTON);
}

/**
 * @func    button_proc
 * @brief   Main button processing function (call periodically from main loop)
 */
void button_proc(void) {
  for (int i = 0; i < NUMBER_BUTTON; i++) {
    /* Determine scan interval based on button state */
    scan_interval_ms = (opt_button_params[i].prev_state == OPTION_BUTTON_PRESS)
                           ? BUTTON_FAST_SCAN_INTERVAL_MS
                           : BUTTON_NORMAL_SCAN_INTERVAL_MS;

    /* Check if scan interval has elapsed */
    if ((clock_time_ms() - opt_button_scan_timer[i]) >= scan_interval_ms) {
      uint8_t current_state = option_button_state(i);

      /* State transition: RELEASE -> PRESS */
      if ((opt_button_params[i].prev_state == OPTION_BUTTON_RELEASE) &&
          (current_state == OPTION_BUTTON_PRESS)) {
        if (check_option_button_press(i) == true) {
          option_button_press(i);
          opt_button_params[i].poll_cnt = 0;
          LOG_DBG("Button %d: PRESS detected", i);
        }
      }
      /* State: PRESS -> PRESS (holding) */
      else if ((opt_button_params[i].prev_state == OPTION_BUTTON_PRESS) &&
               (current_state == OPTION_BUTTON_PRESS)) {
        opt_button_params[i].poll_cnt++;

        /* Initialize hold timer on first hold detection */
        if (opt_button_params[i].hold_step == 0xFF) {
          opt_button_params[i].start_hold_t = clock_time_ms();
          opt_button_params[i].hold_step = 0;
        }

        /* Error protection: reset if held too long */
        if (OPTION_BUTTON_HOLD_TIME(i) > OPTION_BUTTON_ERROR_HOLD_TIME) {
          opt_button_params[i].hold_step = 0xFE;
        }

        /* Process hold steps */
        switch (opt_button_params[i].hold_step) {
        case 0:
          if (opt_button_params[i].poll_cnt >=
              MIN_POLL_COUNTER_TO_CHANGE_HOLD_STEP) {
            opt_button_params[i].hold_step = 1;
          }
          break;

        case 1:
          if (OPTION_BUTTON_HOLD_TIME(i) > TIMER_500MS) {
            opt_button_params[i].hold_step = 2;
            button_call_handle_function(i, HOLD_500MS);
            LOG_DBG("Button %d: HOLD_500MS", i);
          }
          break;

        case 2:
          if (OPTION_BUTTON_HOLD_TIME(i) > TIMER_800MS) {
            opt_button_params[i].hold_step = 3;
            button_call_handle_function(i, HOLD_2S);
            LOG_DBG("Button %d: HOLD_2S", i);
          }
          break;

        case 3:
          if (OPTION_BUTTON_HOLD_TIME(i) > TIMER_5S) {
            opt_button_params[i].hold_step = 4;
            button_call_handle_function(i, HOLD_5S);
            LOG_DBG("Button %d: HOLD_5S", i);
          }
          break;

        case 4:
          if (OPTION_BUTTON_HOLD_TIME(i) > TIMER_10S) {
            opt_button_params[i].hold_step = 5;
            button_call_handle_function(i, HOLD_10S);
            LOG_DBG("Button %d: HOLD_10S", i);
          }
          break;

        case 5:
          if (OPTION_BUTTON_HOLD_TIME(i) > TIMER_15S) {
            opt_button_params[i].hold_step = 6;
            button_call_handle_function(i, HOLD_15S);
            LOG_DBG("Button %d: HOLD_15S", i);
          }
          break;
        }
      }
      /* State: any -> RELEASE (button released) */
      else if ((opt_button_params[i].prev_state == OPTION_BUTTON_RELEASE) &&
               (current_state == OPTION_BUTTON_RELEASE)) {
        /* Check for multi-press events */
        option_button_press_times(i);

        /* Report release or no_press */
        if (opt_button_params[i].hold_step < 0xFF) {
          button_call_handle_function(i, RL_AFTER_PRESS);
          LOG_DBG("Button %d: RELEASE", i);
        } else {
          button_call_handle_function(i, NO_PRESS);
        }
        opt_button_params[i].hold_step = 0xFF;
      }

      /* Update previous state and timer */
      opt_button_params[i].prev_state = current_state;
      opt_button_scan_timer[i] = clock_time_ms();
    }
  }
}

/******************************************************************************/
/*                        PRIVATE FUNCTIONS                                   */
/******************************************************************************/

/**
 * @func    option_button_state
 * @brief   Read current GPIO state of a button
 * @param   idx - Button index
 * @return  OPTION_BUTTON_PRESS (0) or OPTION_BUTTON_RELEASE (1)
 */
static uint8_t option_button_state(uint8_t idx) {
  if (idx >= NUMBER_BUTTON) {
    return OPTION_BUTTON_RELEASE;
  }

  int val = gpio_pin_get_dt(&button_specs[idx]);
  if (val < 0) {
    LOG_ERR("Failed to read button %d GPIO", idx);
    return OPTION_BUTTON_RELEASE;
  }

  /* gpio_pin_get_dt returns 1 if logical active (pressed), 0 if inactive */
  return (val == 1) ? OPTION_BUTTON_PRESS : OPTION_BUTTON_RELEASE;
}

/**
 * @func    check_option_button_press
 * @brief   Debounce check - verify button press is stable
 * @param   idx - Button index
 * @return  true if button is genuinely pressed
 */
static uint8_t check_option_button_press(uint8_t idx) {
  if (option_button_state(idx) == OPTION_BUTTON_PRESS) {
    uint8_t bound_cnt = 0;

    for (int i = 0; i < 4; i++) {
      k_sleep(K_MSEC(2));
      if (option_button_state(idx) == OPTION_BUTTON_PRESS) {
        bound_cnt++;
      }
    }

    if (bound_cnt == 4) {
      return true;
    }
  }
  return false;
}

/**
 * @func    option_button_press
 * @brief   Handle button press event
 * @param   idx - Button index
 */
static void option_button_press(uint8_t idx) {
  /* Notify start press */
  if (pvButton_HandleStateCallback != NULL) {
    pvButton_HandleStateCallback(idx, START_PRESS);
  }

  /* Count multi-press within timeout window */
  if (OPTION_BUTTON_PRESS_ELAPSED_TIME(idx) < OPTION_BUTTON_PRESS_TIME_OUT) {
    opt_button_params[idx].press_cnt++;
  } else {
    opt_button_params[idx].press_cnt = 1;
  }

  opt_button_params[idx].press_last_t = clock_time_ms();
  opt_button_params[idx].press_many_time_f = true;
}

/**
 * @func    option_button_press_times
 * @brief   Detect and report multi-press events
 * @param   idx - Button index
 */
static void option_button_press_times(uint8_t idx) {
  if (opt_button_params[idx].press_many_time_f != false) {
    if (OPTION_BUTTON_PRESS_ELAPSED_TIME(idx) > OPTION_BUTTON_PRESS_TIME_OUT) {
      /* Extra window to detect final press count */
      if (OPTION_BUTTON_PRESS_ELAPSED_TIME(idx) >
          (OPTION_BUTTON_PRESS_TIME_OUT + TIMER_200MS)) {
        opt_button_params[idx].press_many_time_f = false;
        return;
      }

      /* Report press count */
      switch (opt_button_params[idx].press_cnt) {
      case 1:
        button_call_handle_function(idx, PRESS_ONE_TIME);
        LOG_DBG("Button %d: PRESS_ONE_TIME", idx);
        break;
      case 2:
        button_call_handle_function(idx, PRESS_TWO_TIME);
        LOG_DBG("Button %d: PRESS_TWO_TIME", idx);
        break;
      case 3:
        button_call_handle_function(idx, PRESS_THREE_TIME);
        LOG_DBG("Button %d: PRESS_THREE_TIME", idx);
        break;
      case 4:
        button_call_handle_function(idx, PRESS_FOUR_TIME);
        LOG_DBG("Button %d: PRESS_FOUR_TIME", idx);
        break;
      case 5:
        button_call_handle_function(idx, PRESS_FIVE_TIME);
        LOG_DBG("Button %d: PRESS_FIVE_TIME", idx);
        break;
      case 6:
        button_call_handle_function(idx, PRESS_SIX_TIME);
        LOG_DBG("Button %d: PRESS_SIX_TIME", idx);
        break;
      case 8:
        button_call_handle_function(idx, PRESS_EIGHT_TIME);
        LOG_DBG("Button %d: PRESS_EIGHT_TIME", idx);
        break;
      case 10:
        button_call_handle_function(idx, PRESS_TEN_TIME);
        LOG_DBG("Button %d: PRESS_TEN_TIME", idx);
        break;
      case 12:
        button_call_handle_function(idx, PRESS_TWELVE_TIME);
        LOG_DBG("Button %d: PRESS_TWELVE_TIME", idx);
        break;
      default:
        break;
      }
      opt_button_params[idx].press_many_time_f = false;
    }
  }
}

/**
 * @func    button_call_handle_function
 * @brief   Call the registered callback with button event
 * @param   idx - Button index
 * @param   state - Button state/event
 */
static void button_call_handle_function(uint8_t idx, uint8_t state) {
  if (pvButton_HandleStateCallback != NULL) {
    pvButton_HandleStateCallback(idx, state);
  }
}