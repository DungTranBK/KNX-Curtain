/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef TIMESTAMP_H_
#define TIMESTAMP_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>  // For time_t
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_SEC UINT32_MAX
#define INVALID_MINUTE UINT16_MAX
#define INVALID_WEEK_DAY 8
#define SET_TIMESTAMP_MAX_INTERVAL_S (21 * 24 * 60 * 60)  // 21 days

#define MININUTE_RANGE_IN_DAY 1439

typedef struct {
  uint8_t cmd;
  uint32_t value;
} timestamp_rsp_t;

typedef struct {
  uint8_t cmd;
  uint16_t dst_adr;
} timestamp_query_t;

// Zephyr doesn't have mesh_cb_fun_par_t in the same way, assume we pass needed
// args or struct For now, keep signature similar to Telink for easier porting
// if wrappers exist, OR adapt to what we have. timestamp_get calls
// mesh_tx_cmd_rsp. We will adapt the prototype.

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/
void timestamp_proc(void);
uint16_t timestamp_get_minute_in_day(void);
uint32_t timestamp_get_second_in_day(void);
uint8_t timestamp_get_week_day(void);
bool timestamp_device_is_have_time(void);

// Helper to set time from a received message
int timestamp_set(uint8_t model_idx, uint8_t* par, uint8_t par_len);

// Helper to get time response (if needed by other modules)
void timestamp_send_query_time(uint16_t src);

void timestamp_get(uint16_t adr_dst, uint16_t adr_src);

#ifdef __cplusplus
}
#endif

#endif /* TIMESTAMP_H_ */
