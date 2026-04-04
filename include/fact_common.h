/*
 * fact_common.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTranBK
 *  Ported to Nordic SDK from Telink ONE_WIRE_SWITCH
 */

#ifndef FACT_COMMON_H_
#define FACT_COMMON_H_

#include "utilities.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*                              EXPORT TYPE                                   */
/******************************************************************************/
typedef struct {
  uint16_t tm_start;
  uint16_t tm_stop;
} fact_reset_serials_t;

/* Trigger source: which method activated factory test */
typedef enum {
  FACT_TRIGGER_POWER_CYCLE = 0, /* Method 1: power on/off 6 times */
  FACT_TRIGGER_BUTTON_PRESS,    /* Method 2: config button 4 presses in 30s */
} fact_trigger_source_t;

/* Button-press trigger tracking (Method 2) */
typedef struct {
  bool window_active; /* true while 30s window from boot is active */
} fact_btn_trigger_t;

typedef struct {
  uint8_t active_flag;
  uint32_t active_timeout;
  uint8_t active_show_result_flag;
  bool rf_is_pass;
  uint32_t active_st_tm;
  bool knx_is_pass;
  uint32_t init_st_tm;
  bool is_init;
  fact_trigger_source_t trigger_source;
} fact_mode_par_t;

typedef struct {
  uint8_t total_cnt;
  uint8_t state;
  uint32_t toggle_st_time;
  uint8_t fact_result;
  uint8_t led_st;
} result_par_t;

typedef struct {
  uint8_t enable;
  uint32_t st_time;
  uint16_t delay_time;
} fact_push_msg_delay_t;

enum {
  FACT_SUCCESS = 0,
  FACT_ERROR = 1,
};

#define RESET_CNT_INVALID 0
#define RESET_TRIGGER_VAL                                                      \
  (sizeof(fact_rst_serials) / sizeof(fact_reset_serials_t) - 1)

#define FACT_ACT_TIMEOUT_MS_DEFAULT TIMER_5Min
#define FACT_SHOW_RESULT_TIME_LEN TIMER_15S
#define FACT_SHOW_RESULT_ST_TIME (TIMER_90S - FACT_SHOW_RESULT_TIME_LEN)
// Show result
#define FACT_TOGGLE_INTERVAL TIMER_500MS
// Change to FACT mode
#define FACT_WAIT_HOST_CONFIRM_TIMEOUT TIMER_10S
#define FACT_RETRY_CHANGE_TO_FACT_INTERVAL                                     \
  ((FACT_WAIT_HOST_CONFIRM_TIMEOUT - TIMER_2S) >> 1)

/* ---- TX Power control  */
#define FACT_POWER_DBM 0
#define TTL_FACT 0

#define VALID_POWER_ON_TIME_MS 1000
#define RESET_CHECK_TIME_BASE_TIME_MS 500

#define FACT_RETRY_TIME_CNT 2

#define FACT_TOTAL_TIME_LENGTH TIMER_70S
#define FACT_TEST_MESSAGE_CNT 5

#define FACT_PUBLISH_TIME_LEN (FACT_TOTAL_TIME_LENGTH / FACT_TEST_MESSAGE_CNT)

/* Method 2 (button press) - fast timing */
#define FACT_BTN_WINDOW_MS TIMER_30S          /* 30s window from boot */
#define FACT_TOTAL_TIME_LENGTH_FAST TIMER_20S /* 20s total test time */
#define FACT_PUBLISH_TIME_LEN_FAST                                             \
  (FACT_TOTAL_TIME_LENGTH_FAST / FACT_TEST_MESSAGE_CNT)
/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* FACT_COMMON_H_ */
