/*
 * inter_provision.h
 *
 *  Created on: Mar 16, 2021
 *      Author: DungTranBK
 *  Ported to Nordic SDK by Antigravity
 */
#ifndef INTER_PROVISION_H_
#define INTER_PROVISION_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "utilities.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>


/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

typedef enum {
  INTER_START_PROV = 0,
  INTER_ADD_DEV_KEY = 1,
  INTER_ADD_NODE_INFO = 2,
  INTER_ADD_APPKEY = 3,
  INTER_PROV_RESULT = 4,
  INTER_PROV_INVALID = 5
} prov_step_enum;

typedef struct {
  bool is_working;
  uint8_t step_prov;
  uint32_t step_last_t;
  uint32_t timeout;
} inter_provision_t;

typedef struct {
  uint8_t cmd_id;
  uint8_t protocol;
  uint8_t step;
  uint8_t par;
} __attribute__((packed)) inter_prov_rx_t;

typedef struct {
  uint8_t cmd_id;
  uint8_t protocol;
  uint8_t prov_step;
  uint8_t state;
  uint8_t nw_mode;
} __attribute__((packed)) inter_prov_step_response_t;

typedef struct {
  uint8_t cmd_id;
  uint8_t protocol;
  uint8_t prov_step;
  uint8_t state;
  uint8_t nw_mode;
  uint8_t uuid[16];
  uint8_t mac[6];
  uint16_t cid;
  uint16_t vid;
  uint16_t pid;
  uint16_t adr;
  uint8_t ele_cnt;
} __attribute__((packed)) inter_prov_result_response_t;

#define CONFIG_LEN_ALL_KEY 16
#define LEN_INTER_PROV_ADD_DEV_KEY 16
#define INTER_PROV_STEP_TIMEOUT_MS 10000

extern uint8_t network_mode_actived;

/******************************************************************************/
/*                             EXPORT FUNCTIONS                               */
/******************************************************************************/

int inter_prov_is_working(void);
void inter_proc_loop_task(void);
int inter_prov_handle_rx_msg(uint8_t *par, int len);
void inter_prov_params_init(void);

#endif /* INTER_PROVISION_H_ */
