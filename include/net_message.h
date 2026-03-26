/*
 * net_message.h
 *
 * Ported from Telink ONE_WIRE_SWITCH net_message.h to Nordic nRF Connect SDK.
 *
 * Handles mesh network on/off message processing, binding group control,
 * scene execution triggers, status publishing, and relay control dispatch.
 *
 *  Created on: Sep 19, 2020
 *      Author: DungTran BK
 */

#ifndef NET_MESSAGE_H_
#define NET_MESSAGE_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "app_device.h"
#include "utilities.h"

/******************************************************************************/
/*                       EXPORT TYPE AND DEFINITION                           */
/******************************************************************************/

typedef struct {
  u8 st_present;
  u8 st_from_nwk;
  u8 binding_st;
  u32 binding_active_last_t;
} sw_control_t;

typedef struct {
  int16_t present;
  int16_t target;
} status_common_t;

typedef struct {
  uint16_t src;
  uint16_t dst;
  uint16_t opcode;
} nwk_message_para_t;

#define CONTROL_BDG_SAME_ST_CNT_MAX 0x3

/**
 * Compatibility struct replacing Telink mesh_cb_fun_par_t.
 * Provides model/message context to net_message handlers.
 */
typedef struct {
  int model_idx; /* Element/relay index (0..ELE_CNT-1) */
  u16 adr_src;   /* Source address of the incoming message  */
  u16 adr_dst;   /* Destination address of the message      */
  u32 op_rsp;
} net_msg_cb_par_t;

extern u8 incomming_st[ELE_CNT];
extern nwk_message_para_t nwk_message_para[ELE_CNT];

typedef void (*typeMessage_handle_control_curtain_by_app)(u8 idx, u8 cmd_id,
                                                          u8 position);

typedef uint8_t (*typeMessage_get_curtain_level)(uint8_t idx);

/******************************************************************************/
/*                              EXPORT FUNCTION                               */
/******************************************************************************/
void net_message_callback_init(typeMessage_handle_control_curtain_by_app func,
                               typeMessage_get_curtain_level func_1);
typedef int (*net_msg_tx_cb_t)(uint8_t idx, uint16_t adr_src, uint16_t adr_dst,
                               uint32_t opcode, uint8_t *uuid, void *model);
typedef void (*net_msg_publish_status_delay_cb_t)(uint8_t idx,
                                                  uint32_t delay_time);

void net_msg_register_tx_cb(net_msg_tx_cb_t cb);
void net_msg_register_publish_status_delay_cb(
    net_msg_publish_status_delay_cb_t cb);

u16 net_message_get_target_state(void);
int handle_mesh_cmd_sig_g_level_set(u8 *par, int par_len,
                                    net_msg_cb_par_t *cb_par);
int my_handle_mesh_cmd_sig_g_level_get(u8 *par, int par_len,
                                       net_msg_cb_par_t *cb_par);
int send_on_off_status(u8 idx, u8 status);
int func_handle_control_message(u8 *par, int par_len, net_msg_cb_par_t *cb_par,
                                u16 op);
u8 get_incomming_st(u16 idx);
void set_incomming_st(u16 idx, bool st);
nwk_message_para_t *net_msg_get_control_message_parameter(int idx);
void net_msg_init(void);
uint8_t net_msg_get_present_state(uint8_t idx);
int net_msg_update_control_message_parameter(int idx, uint16_t src,
                                             uint16_t dst, uint16_t opcode);
void net_message_publish_status_delay(uint8_t idx, uint32_t delay_time);
void net_message_handle_state_change(u8 model_idx, u8 status);

void net_message_set_scene_active_flag(uint8_t model_idx, bool flag);

#endif /* NET_MESSAGE_H_ */
