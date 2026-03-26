/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BUTTON_H_
#define BUTTON_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "utilities.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t press_last_t;
  uint8_t press_cnt;
  bool press_many_time_f;
  uint32_t start_hold_t;
  uint8_t prev_state;
  uint8_t hold_step;
  uint8_t poll_cnt;
} opt_button_params_t;

#define OPTION_BUTTON_PRESS 0x00
#define OPTION_BUTTON_RELEASE 0x01

#define MIN_POLL_COUNTER_TO_CHANGE_HOLD_STEP 10
#define OPTION_BUTTON_ERROR_HOLD_TIME TIMER_20S

#define BUTTON_FAST_SCAN_INTERVAL_MS TIMER_10MS
#define BUTTON_NORMAL_SCAN_INTERVAL_MS TIMER_25MS

typedef void (*typeButton_HandleStateCallbackFunc)(uint8_t button_id,
                                                   uint8_t state);
/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/
void button_init(typeButton_HandleStateCallbackFunc func);
void button_proc(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H_ */
