/*
 * fact.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTranBK
 *  Ported to Nordic SDK from Telink ONE_WIRE_SWITCH
 */

#ifndef FACT_H_
#define FACT_H_

#include <stdbool.h>
#include <stdint.h>

#include "utilities.h"

#define FACT_TTL_AND_TX_DBG_EN 0

#ifdef __cplusplus
extern "C" {
#endif

#define TTL_DEFAULT (6)

enum {
  FACT_CONFIRM,
  FACT_ACTIVATE,
  FACT_REFRESH_LED,
};

enum {
  RESULT_FAIL_ALL,
  RESULT_PASS_KNX,
  RESULT_PASS_BLETOOTH,
  RESULT_PASS_ALL,
  RESULT_UNKNOWN,
};

typedef void (*typeFact_handleFactEvent)(uint8_t);
typedef void (*typeFact_handleExitfactMode)(void);

/******************************************************************************/
/*                              EXPORT TYPE                                   */
/* (from Telink: fact_handle.h)                                               */
/******************************************************************************/
typedef struct {
  uint8_t enable;
  uint32_t ac_st_time;
  uint8_t step;
  uint8_t total_retry_cnt;
  uint8_t publish_cnt;
  uint8_t pub_cnt_before;
  uint8_t response_cnt;
  uint8_t active_idx;
  uint8_t done;
} fact_par_t;

typedef struct {
  uint8_t result;
  uint16_t rssi_value;
} test_rf_result_t;

enum {
  FACT_PASS,
  FACT_FAIL,
};

typedef struct {
  uint8_t op;
  uint8_t cnt;
  uint8_t MAC[2];
} fact_test_rf_msg_t;

typedef void (*typeFact_handle_update_result)(bool result);

typedef bool (*typeFact_getKnxTestStatus)(void);

/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/

/* From fact_handle.c */
void ble_fact_callback_init(typeFact_handle_update_result func);
uint8_t fact_is_activate(void);
uint8_t fact_set(uint8_t act);
uint8_t fact_main_function(void);
int fact_handle_config_response(uint8_t *par, int par_len, uint16_t src_addr,
                                uint16_t dst_addr);
void fact_update_result(void);

/* From fact.c */
void fact_init(void);
void fact_handle(void);
uint8_t fact_is_active(void);
void fact_handle_evt_change_callback_init(
    typeFact_handleFactEvent func_handle_event,
    typeFact_handleExitfactMode func_exit);

bool fact_is_show_result(void);

/* Method 2: notify config button press for factory test activation */
void fact_notify_config_button_press(void);

void fact_get_knx_test_status_callback_init(typeFact_getKnxTestStatus func);

#ifdef FACT_DBG_EN
void fact_debug_test_ttl_rf_power(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FACT_H_ */
