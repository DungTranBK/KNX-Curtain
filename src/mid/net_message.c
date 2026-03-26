/*
 * net_message.c
 *
 * Ported from Telink ONE_WIRE_SWITCH net_message.c to Nordic nRF Connect SDK.
 *
 * Handles mesh network on/off message processing, binding group control,
 * scene execution triggers, status publishing, and relay control dispatch.
 *
 *  Created on: Sep 19, 2020
 *      Author: DungTran BK
 */

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "../../include/net_message.h"

#include <bluetooth/mesh/gen_onoff_srv.h>
#include <bluetooth/mesh/models.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/bluetooth/mesh/main.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "../../include/key_report.h"
#include "../../include/led.h"
#include "../../include/relay.h"
#include "../../include/sw_auto.h"
#include "../../include/sw_binding.h"
#include "../../include/utilities.h"
#include "../../include/vendor_model.h"
#include "mesh/access.h"

LOG_MODULE_REGISTER(net_message, CONFIG_LOG_DEFAULT_LEVEL);

typeMessage_handle_control_curtain_by_app
    pvMessage_handle_control_curtain_by_app = NULL;

typeMessage_get_curtain_level pvMessage_get_curtain_level = NULL;

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

u8 incomming_st[ELE_CNT] = {
    G_ONOFF_RSV,
#if ELE_CNT > 1
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 2
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 3
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 4
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 5
    G_ONOFF_RSV,
#endif
};

u8 scene_active_arr[ELE_CNT] = {
    false,
#if ELE_CNT > 1
    false,
#endif
#if ELE_CNT > 2
    false,
#endif
#if ELE_CNT > 3
    false,
#endif
#if ELE_CNT > 4
    false,
#endif
#if ELE_CNT > 5
    false,
#endif
};

static u8 gen_execution_scene_st[ELE_CNT] = {
    G_ONOFF_RSV,
#if ELE_CNT > 1
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 2
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 3
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 4
    G_ONOFF_RSV,
#endif
#if ELE_CNT > 5
    G_ONOFF_RSV,
#endif
};

static u8 state_of_group_binding[ELE_CNT] = {
    SUB_UNKNOWN,
#if ELE_CNT > 1
    SUB_UNKNOWN,
#endif
#if ELE_CNT > 2
    SUB_UNKNOWN,
#endif
#if ELE_CNT > 3
    SUB_UNKNOWN,
#endif
};

/* Mesh status and parameter state - moved from mesh_node.c */
nwk_message_para_t nwk_message_para[ELE_CNT];
static status_common_t status_common[ELE_CNT];

/* Status Report Delay Infrastructure */
struct status_report_work {
  struct k_work_delayable work;
  uint8_t model_idx;
};

static struct status_report_work status_report_works[ELE_CNT];

static void status_report_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct status_report_work *sr_work =
      CONTAINER_OF(dwork, struct status_report_work, work);

  /* Fetch current state and send status */
  if (pvMessage_get_curtain_level != NULL) {
    u8 level = pvMessage_get_curtain_level(sr_work->model_idx);
    mesh_tx_cmd_rsp(G_LEVEL_STATUS, &level, 1,
                    bt_mesh_primary_addr() + sr_work->model_idx,
                    GATEWAY_UNICAST_ADDR, NULL, NULL);
    LOG_INF("\n ########## Send Curtain Level: %d, %d", sr_work->model_idx,
            level);
  }
}

/******************************************************************************/
/*                          PRIVATE FUNCTIONS DECLARATION                     */
/******************************************************************************/
static void send_on_off_control_to_ble_group(u8 model_idx, bool status);

/******************************************************************************/
/*                           EXPORT FUNCTIONS                                 */
/******************************************************************************/

void net_message_callback_init(typeMessage_handle_control_curtain_by_app func,
                               typeMessage_get_curtain_level func_1) {
  if (func != NULL) {
    pvMessage_handle_control_curtain_by_app = func;
  }
  if (func_1 != NULL) {
    pvMessage_get_curtain_level = func_1;
  }
}

void net_msg_init(void) {
  /* Initialize status report delay work */
  for (int i = 0; i < ELE_CNT; i++) {
    status_report_works[i].model_idx = i;
    k_work_init_delayable(&status_report_works[i].work,
                          status_report_work_handler);
  }
}

void net_message_publish_status_delay(uint8_t idx, uint32_t delay_time) {
  if (idx >= ELE_CNT) {
    return;
  }

  /* Validate struct initialized */
  if (status_report_works[idx].model_idx == idx) {
    /* If delay is 0, Zephyr triggers work right away. */
    k_work_reschedule(&status_report_works[idx].work, K_MSEC(delay_time));
    LOG_DBG("Status report scheduled for Element %d in %u ms", idx, delay_time);
  }
}

uint8_t net_msg_get_present_state(uint8_t idx) {
  if (idx < ELE_CNT) {
    return (uint8_t)status_common[idx].present;
  }
  return 0;
}

/**
 * @func    net_message_get_target_state
 * @brief   Get current relay target state bitmask (all relays)
 * @retval  Relay target state bitmask
 */
u16 net_message_get_target_state(void) { return relay_get_target_state(); }

/**
 * @func    net_message_handle_state_change
 * @brief   Handle relay state change: scene execution, binding group
 *          notification, and status publish to gateway.
 *          Ported from Telink net_message_handle_state_change().
 * @param   model_idx  Element/model index (0..ELE_CNT-1)
 * @param   status     New on/off state (G_ON / G_OFF)
 * @retval  0 on success, -1 on invalid index
 */
void net_message_handle_state_change(u8 model_idx, u8 status) {
  if (model_idx >= ELE_CNT) {
    return;
  }
  LOG_INF("net_message_handle_state_change: idx=%d, st=%d", model_idx, status);

  if (status < G_ONOFF_RSV) {
    /* --- Scene Execution --- */
#if EN_EVT_ON_OFF_CONTROL_EXECUTION
    uint8_t key_code = (status == G_ON) ? EV_SW_ON : EV_SW_OFF;
    if ((status != gen_execution_scene_st[model_idx]) &&
        (gen_execution_scene_st[model_idx] != G_ONOFF_RSV)) {
      nwk_message_para_t *msg_para =
          net_msg_get_control_message_parameter(model_idx);
      if (msg_para->dst < ADR_FIXED_GROUP_START) {
        execution_scene_active(model_idx, key_code);
        LOG_INF("execution_scene_active: idx=%d, st=%d, ex_st=%d", model_idx,
                status, gen_execution_scene_st[model_idx]);
      }
    }
    gen_execution_scene_st[model_idx] = status;
#endif

    /* --- Binding Group Control --- */
#if SWITCH_ENABLE_BINDING
    if (state_of_group_binding[model_idx] == SUB_UNKNOWN) {
      state_of_group_binding[model_idx] = status ^ 1;
    }
    send_on_off_control_to_ble_group(model_idx, status);
#endif

    /* --- Status Publish --- */
    {
      /* Local state check for status distribution */
      nwk_message_para_t *msg_para =
          net_msg_get_control_message_parameter(model_idx);
      uint16_t dst_addr = msg_para ? msg_para->dst : 0;

      if (dst_addr < ADR_GROUP_START_POINT) {
#if EN_CHECK_SOURCE_CONTROL
        if (get_incomming_st(model_idx) != status) {
          net_message_publish_status_delay(model_idx, 0);
          set_incomming_st(model_idx, status);
          LOG_INF("send_on_off_status:-1: %d", status);
        } else
#endif
        {
          net_message_publish_status_delay(model_idx, 0);
          LOG_INF("send_on_off_status:-2: %d", status);
        }
      } else {
        if (dst_addr < ADR_FIXED_GROUP_START) {
          net_message_publish_status_delay(model_idx,
                                           TIMER_1S + (rand() % TIMER_3S));
          LOG_INF("send_on_off_status:-3: %d", status);
        } else {
          net_message_publish_status_delay(model_idx,
                                           TIMER_5S + (rand() % TIMER_30S));
          LOG_INF("send_on_off_status:-4: %d", status);
        }
        /* Reset dst to self address */
        net_msg_update_control_message_parameter(
            model_idx, get_ele_addr_base_btn_idx(model_idx),
            get_ele_addr_base_btn_idx(model_idx), G_ONOFF_STATUS);
      }

      auto_reset_time_trans(model_idx, status);
    }
  }
}

/**
 * @func    handle_mesh_cmd_sig_g_on_off_set
 * @brief   Handle incoming SIG Generic OnOff SET / SET UNACKED message.
 *          Called from mesh_node.c handle_onoff_set_logic().
 * @param   par      Message payload (mesh_cmd_g_onoff_set_t)
 * @param   par_len  Payload length
 * @param   cb_par   Message context (model_idx, adr_src, adr_dst)
 * @retval  0 on success, -1 if control is blocked
 */
int handle_mesh_cmd_sig_g_level_set(u8 *par, int par_len,
                                    net_msg_cb_par_t *cb_par) {
  LOG_INF("handle_mesh_cmd_sig_g_level_set: idx=%d, op=0x%04x",
          cb_par->model_idx, cb_par->op_rsp);
  if ((cb_par->op_rsp != STATUS_NONE) &&
      (cb_par->adr_dst < ADR_GROUP_START_POINT)) {
    net_message_publish_status_delay(cb_par->model_idx, TIMER_20MS);
  }
  // Check invalid model index
  if (cb_par->model_idx >= ELE_CNT) {
    LOG_ERR("Invalid model index: %d", cb_par->model_idx);
    return -1;
  }
  // infinite reversal protection
  foreach (i, NUMBER_CURTAIN) {
    if (cb_par->adr_src == (bt_mesh_primary_addr() + i)) {
      return -1;
    }
  }
  vd_cmd_g_level_set_t *p_level_set = (vd_cmd_g_level_set_t *)par;
  uint8_t target_level = p_level_set->level;
  net_msg_update_control_message_parameter(cb_par->model_idx, cb_par->adr_src,
                                           cb_par->adr_dst, G_LEVEL_SET);
  // Call handler
  if (pvMessage_handle_control_curtain_by_app != NULL) {
    pvMessage_handle_control_curtain_by_app(
        cb_par->model_idx, p_level_set->type, p_level_set->level);
    LOG_INF("CURTAIN CONTROL: %d, %d", p_level_set->type, target_level);
  }
  // Led notify
  if (led_get_blink_led_flag() == BLINK_IDLE && led_fifo_is_empty() == true) {
    LED_push_blink_with_Interval_to_fifo(1, LED_COLOR_BLUE, 200);
  }
  return 0;
}

int mesh_tx_cmd_g_onoff_st(uint8_t idx, uint16_t adr_src, uint16_t adr_dst,
                           uint32_t opcode, uint8_t *uuid, void *model) {
  uint8_t on_off_st = net_msg_get_present_state(idx);
  return mesh_tx_cmd_rsp(opcode, &on_off_st, 1, adr_src, adr_dst, uuid, model);
}

/**
 * @func    send_on_off_status
 * @brief   Send Generic OnOff Status to the gateway unicast address.
 *          Ported from Telink send_on_off_status().
 * @param   idx     Element index
 * @param   status  G_ON or G_OFF
 * @retval  0 on success, -1 on invalid args
 */
int send_on_off_status(u8 idx, u8 status) {
  if ((idx < ELE_CNT) && (status < G_ONOFF_RSV)) {
    status_common[idx].present = status;
    nwk_message_para_t *cb_par = net_msg_get_control_message_parameter(idx);
    uint16_t adr_src = get_ele_addr_base_btn_idx(idx);
    return mesh_tx_cmd_g_onoff_st(idx, adr_src, GATEWAY_UNICAST_ADDR,
                                  G_ONOFF_STATUS, NULL, NULL);
  }
  return -1;
}

/**
 * @func    my_handle_mesh_cmd_sig_g_on_off_get
 * @brief   Handle incoming SIG Generic OnOff GET message.
 *          Publishes current status immediately (delay=0).
 *          Ported from Telink my_handle_mesh_cmd_sig_g_on_off_get().
 * @param   par      Message payload (unused)
 * @param   par_len  Payload length (unused)
 * @param   cb_par   Message context
 * @retval  0
 */
int my_handle_mesh_cmd_sig_g_level_get(u8 *par, int par_len,
                                       net_msg_cb_par_t *cb_par) {
  ARG_UNUSED(par);
  ARG_UNUSED(par_len);
  /* Publish current status immediately */
  /* TODO BLE
  send_on_off_status(cb_par->model_idx,
                     relay_get_target_state_by_index(cb_par->model_idx));
  */
  return 0;
}

/**
 * @func    send_get_device_status_manual
 * @brief   Publish current relay status for a given element index
 *          immediately (no delay). Called on manual/button control.
 *          Ported from Telink send_get_device_status_manual().
 * @param   model_idx  Element index
 */
void send_get_device_status_manual(u8 model_idx) {
  LOG_DBG("send_get_device_status_manual: idx=%d", model_idx);
  if (model_idx < ELE_CNT) {
    net_message_publish_status_delay(model_idx, 0);
  }
}

/**
 * @func    net_message_set_scene_active_flag
 * @brief   Set scene active flag for a given element index.
 *          Called by scene module when scene recall starts.
 * @param   model_idx  Element index
 */
void net_message_set_scene_active_flag(uint8_t model_idx, bool flag) {
  if (model_idx < ELE_CNT) {
    scene_active_arr[model_idx] = flag;
  }
}

/**
 * @func    get_incomming_st
 * @brief   Get the last known incoming on/off state for an element.
 * @param   model_idx  Element index
 * @retval  G_ON, G_OFF, or G_ONOFF_RSV if invalid
 */
u8 get_incomming_st(u16 model_idx) {
  if (model_idx < ELE_CNT) {
    return incomming_st[model_idx];
  }
  return G_ONOFF_RSV;
}

/**
 * @func    set_incomming_st
 * @brief   Update the incoming state for an element.
 * @param   model_idx  Element index
 * @param   st         New state (G_ON or G_OFF)
 */
void set_incomming_st(u16 model_idx, bool st) {
  if (model_idx < ELE_CNT) {
    incomming_st[model_idx] = st;
  }
}

/**
 * @func    net_msg_update_control_message_parameter
 * @brief   Update stored mesh message parameters for an element.
 */
int net_msg_update_control_message_parameter(int idx, uint16_t src,
                                             uint16_t dst, uint16_t opcode) {
  if (idx >= 0 && idx < ELE_CNT) {
    nwk_message_para[idx].src = src;
    nwk_message_para[idx].dst = dst;
    nwk_message_para[idx].opcode = opcode;
    return 0;
  }
  return -1;
}

/**
 * @func    net_msg_get_control_message_parameter
 * @brief   Get stored mesh message parameters for an element.
 */
nwk_message_para_t *net_msg_get_control_message_parameter(int idx) {
  if (idx >= 0 && idx < ELE_CNT) {
    return &nwk_message_para[idx];
  }
  return NULL;
}

// End file
