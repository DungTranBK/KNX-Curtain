/*
 * fact.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTranBK
 *  Ported to Nordic SDK
 */

#ifndef FACT_H_
#define FACT_H_

#include <stdbool.h>
#include <stdint.h>

#include "utilities.h"


#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*                              EXPORT TYPE                                   */
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
/******************************************************************************/
/*                              EXPORT FUNCTION                               */
/******************************************************************************/
uint8_t fact_is_activate(void);
uint8_t fact_set(uint8_t act);
uint8_t fact_main_function(void);
int fact_handle_config_response(uint8_t* par, int par_len, uint16_t src_addr,
                                uint16_t dst_addr);

#ifdef __cplusplus
}
#endif

#endif /* FACT_H_ */
