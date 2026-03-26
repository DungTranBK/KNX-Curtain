/*
 * execution_scene.c
 *
 *  Created on: Apr 15, 2020
 *      Author: DungTran BK
 */

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/

#include "../../include/execution_scene.h"

#include <string.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "../../include/app_device.h"
#include "../../include/led.h"
#include "../../include/network.h"
#include "../../include/utilities.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(execution_scene, CONFIG_LOG_DEFAULT_LEVEL);

#define EXECUTION_SCENE_DBG_EN 1

#ifdef EXECUTION_SCENE_DBG_EN
#define DBG_EXECUTION_SCENE_SEND_STR(x) LOG_INF("%s", x)
#define DBG_EXECUTION_SCENE_SEND_INT(x) LOG_INF("%d", x)
#define DBG_EXECUTION_SCENE_SEND_HEX(x) LOG_INF("0x%x", x)
#define DBG_EXECUTION_SCENE_SEND_BYTE(x) LOG_INF("0x%02x", x)
#else
#define DBG_EXECUTION_SCENE_SEND_STR(x)
#define DBG_EXECUTION_SCENE_SEND_INT(x)
#define DBG_EXECUTION_SCENE_SEND_HEX(x)
#define DBG_EXECUTION_SCENE_SEND_BYTE(x)
#endif

#ifndef EN_EXECUTION_SCENE
#define EN_EXECUTION_SCENE 1
#endif

#if EN_EXECUTION_SCENE
/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

static par_execution_scene_t par_execution_scene[NUMBER_INPUT][EV_BTN_MAX];
static setup_execution_scene_set_t setup_exe_scene_set;

const uint16_t op_sig_support_scene[] = {
    // HSL
    LIGHT_HSL_SET, LIGHT_HSL_SET_NOACK,
    // CTL
    LIGHT_CTL_SET, LIGHT_CTL_SET_NOACK, LIGHT_CTL_TEMP_SET,
    LIGHT_CTL_TEMP_SET_NOACK, LIGHTNESS_SET, LIGHTNESS_SET_NOACK,
    // Generic ON/OFF
    G_ONOFF_SET, G_ONOFF_SET_NOACK,
    // Generic Level
    G_LEVEL_SET, G_LEVEL_SET_NOACK};

#if defined(DEV_TYPE_SEL) && (DEV_TYPE_SEL == G_TYPE_SWITCH_BUTTON)
const uint8_t auto_send_arr[] = {EV_SW_ON, EV_SW_OFF};
#elif defined(DEV_TYPE_SEL) && (DEV_TYPE_SEL == G_TYPE_LM_GATE)
const uint8_t auto_send_arr[] = {EV_SW_ON, EV_SW_OFF};
#else
// Default fallback
const uint8_t auto_send_arr[] = {EV_SW_ON, EV_SW_OFF};
#endif

static send_exe_scene_delay_t send_exe_scene_delay_st = {.send_last_t_ms = 0,
                                                         .en_send = false,
                                                         .btn_key =
                                                             NUMBER_INPUT,
                                                         .send_ev_idx = 0xFF};

#define PAR_SCENE par_execution_scene[key_number][key_event]

#define KEY_EV_START_POINT EV_SW_ON

/******************************************************************************/
/*                        PRIVATE FUNCTIONS DECLERATION                       */
/******************************************************************************/

static int check_valid_event_and_key_number(uint8_t key_number, uint8_t key_ev);

/******************************************************************************/
/*                        EXPORT FUNCTIONS DECLERATION                        */
/******************************************************************************/

/**
 * @func    get_key_event_index
 * @brief
 * @param   None
 * @retval  None
 */
static bool get_key_event_index(uint8_t *index, uint8_t evt) {
  if ((evt == EV_SW_ON) || (evt == EV_SW_OFF)) {
    *index = evt - KEY_EV_START_POINT;
    return true;
  }
  return false;
}

/**
 * @func    restore_execution_scene
 * @brief
 * @param   None
 * @retval  None
 */
static void restore_execution_scene(void) {
  // Zephyr Settings Load
  int rc = settings_load_subtree("exe/sc");
  if (rc) {
    LOG_ERR("Failed to load settings: %d", rc);
  } else {
    LOG_INF("Execution scene restored");
  }
}

static int execution_scene_settings_set(const char *name, size_t len,
                                        settings_read_cb read_cb,
                                        void *cb_arg) {
  const char *next;
  if (settings_name_steq(name, "data", &next) && !next) {
    if (len != sizeof(par_execution_scene)) {
      return -EINVAL;
    }
    if (read_cb(cb_arg, &par_execution_scene, sizeof(par_execution_scene)) <
        0) {
      return -EIO;
    }
    return 0;
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(execution_scene, "exe/sc", NULL,
                               execution_scene_settings_set, NULL, NULL);

/**
 * @func    save_execution_scene_to_flash
 * @brief
 * @param   None
 * @retval  None
 */
static void save_execution_scene_to_flash(void) {
  int rc = settings_save_one("exe/sc/data", &par_execution_scene,
                             sizeof(par_execution_scene));
  if (rc) {
    LOG_ERR("Failed to save settings: %d", rc);
  } else {
    LOG_INF("Execution scene saved");
  }
}

/**
 * @func    delete_all_execution_scene
 * @brief
 * @param   None
 * @retval  None
 */
void delete_all_execution_scene(void) {
  memset(&par_execution_scene, 0xFF, sizeof(par_execution_scene));
  // Save To Flash
  save_execution_scene_to_flash();
}

/**
 * @func    delete_execution_scene
 * @brief
 * @param   None
 * @retval  None
 */
static void delete_execution_scene(uint8_t key_number, uint8_t key_idx) {
  memset(&par_execution_scene[key_number][key_idx], 0xFF,
         sizeof(par_execution_scene_t));
  save_execution_scene_to_flash();
}

/**
 * @func    check_valid_event_and_key_number
 * @brief
 * @param   None
 * @retval  None
 */
static int check_valid_event_and_key_number(uint8_t key_number,
                                            uint8_t key_ev) {
  if ((key_number >= NUMBER_INPUT) ||
      ((key_ev != EV_SW_ON) && (key_ev != EV_SW_OFF))) {
    return -1;
  }
  return 0;
}

/**
 * @func    execution_scene_blink_led_status
 * @brief
 * @param   None
 * @retval  None
 */
static void execution_scene_blink_led_status(uint16_t mask, int color) {
  // Placeholder for LED blink logic
  led_blink_color(BACKUP_MASK, (LedColor_enum)color, 2, LAST_STATE_REFRESH_LED,
                  TIMER_200MS);
  LOG_INF("LED Blink: Mask=0x%04X, Color=%d", BACKUP_MASK, color);
}
/**
 * @func   execution_scene_handle_vendor_setup_execution_scene_set
 * @brief  None
 * @param
 * @retval Status code
 */
static int handle_vendor_setup_execution_scene_set(uint8_t *par, int par_len,
                                                   mesh_cb_fun_par_t *cb_par) {
  int err = 0;
  uint8_t index = 0;
  // Copy from Message Type And Message Id To Number Of Destination Address
  memcpy(&setup_exe_scene_set.msg_type_and_id, &par[index], 5);
  index += 4;

  uint8_t key_number = setup_exe_scene_set.setup_event_st.key_nbr;
  uint8_t key_ev = setup_exe_scene_set.setup_event_st.key_event;

  uint8_t ev_idx = 0;
  (void)get_key_event_index(&ev_idx, key_ev);

  switch (setup_exe_scene_set.msg_type_and_id_st.msg_type) {
  case MSG_ADD: {
    DBG_EXECUTION_SCENE_SEND_STR("\n MSG_ADD");

    if (check_valid_event_and_key_number(key_number, key_ev) == 0) {
      if (setup_exe_scene_set.scene_id == SCENE_ID_INVALID) {
        DBG_EXECUTION_SCENE_SEND_STR("\n SCENE_ID_INVALID");
        if (setup_exe_scene_set.nums_dest > MAX_SCENE_DEST_ADR) {
          // Message Invalid
          err = -1;
        } else {
          // Delete, Not Save To Flash
          memset(&par_execution_scene[key_number][ev_idx], 0xFF,
                 sizeof(par_execution_scene_t));
          par_execution_scene[key_number][ev_idx].btn_event = key_ev;
          par_execution_scene[key_number][ev_idx].scene_id =
              setup_exe_scene_set.scene_id;

          uint8_t payload_len;

          foreach (i, setup_exe_scene_set.nums_dest) {
            par_execution_scene[key_number][ev_idx].control_infor[i].dest_addr =
                par[index + 1] | (uint16_t)(par[index + 2] << 8);
            index += 2;
            payload_len = par[++index];
            if (payload_len > MAX_PAYLOAD_LEN)
              payload_len = MAX_PAYLOAD_LEN;
            memcpy(&par_execution_scene[key_number][ev_idx]
                        .control_infor[i]
                        .payload,
                   &par[++index], payload_len);
            par_execution_scene[key_number][ev_idx]
                .control_infor[i]
                .payload_len = payload_len;
            index += (payload_len - 1);

#ifdef EXECUTION_SCENE_DBG_EN
            DBG_EXECUTION_SCENE_SEND_STR("\n Payload Len: ");
            DBG_EXECUTION_SCENE_SEND_INT(payload_len);
            DBG_EXECUTION_SCENE_SEND_STR("\n Payload: ");
            for (uint8_t j = 0; j < payload_len; j++) {
              DBG_EXECUTION_SCENE_SEND_BYTE(
                  par_execution_scene[key_number][key_ev]
                      .control_infor[i]
                      .payload[j]);
              DBG_EXECUTION_SCENE_SEND_STR(" ");
            }
#endif
          }
        }
        par_execution_scene[key_number][ev_idx].trans_time = par[++index];
        DBG_EXECUTION_SCENE_SEND_STR("\n trans time: ");
        DBG_EXECUTION_SCENE_SEND_BYTE(
            par_execution_scene[key_number][key_ev].trans_time);
      } else {
        // SINGLE SCENE
        if (setup_exe_scene_set.scene_id != 0xFFFF) {
          DBG_EXECUTION_SCENE_SEND_STR("\n SCENE_ID_VALID");
          // Delete
          delete_execution_scene(key_number, ev_idx);
          par_execution_scene[key_number][ev_idx].btn_event = key_ev;
          par_execution_scene[key_number][ev_idx].scene_id =
              setup_exe_scene_set.scene_id;
          par_execution_scene[key_number][ev_idx].trans_time = par[5];
        }
        // MULTIPLE SCENE
        else {
          if ((setup_exe_scene_set.nums_scene != 0) &&
              (setup_exe_scene_set.nums_scene <= MAX_SCENE_DEST_ADR)) {
            DBG_EXECUTION_SCENE_SEND_STR("\n MULTI SCENE");
            delete_execution_scene(key_number, ev_idx);

            par_execution_scene[key_number][ev_idx].btn_event = key_ev;
            par_execution_scene[key_number][ev_idx].scene_id =
                setup_exe_scene_set.scene_id;

            foreach (i, setup_exe_scene_set.nums_scene) {
              par_execution_scene[key_number][ev_idx]
                  .scene_control[i]
                  .in_scene_id =
                  par[index + 1] | (uint16_t)(par[index + 2] << 8);
              index += 2;
              par_execution_scene[key_number][ev_idx]
                  .scene_control[i]
                  .in_trans_t = par[++index];
              par_execution_scene[key_number][ev_idx]
                  .scene_control[i]
                  .in_delay_t = par[++index];
            }
          }
          par_execution_scene[key_number][ev_idx].trans_time = par[++index];
        }
      }

      DBG_EXECUTION_SCENE_SEND_STR("\n___check_ev: ");
      DBG_EXECUTION_SCENE_SEND_BYTE(setup_exe_scene_set.event);

      // Flash
      save_execution_scene_to_flash();
    } else {
      err = -1;
    }
    // Response Add
    execution_scene_add_rsp_str exe_scene_add_rsp;
    exe_scene_add_rsp.msg_type_and_id_st.msg_id =
        setup_exe_scene_set.msg_type_and_id_st.msg_id;
    exe_scene_add_rsp.msg_type_and_id_st.msg_type = MSG_ADD_RSP;
    exe_scene_add_rsp.status_code =
        ((err == 0) ? (EX_STATUS_SUCCESS) : (EX_STATUS_FAIL));
    exe_scene_add_rsp.event = setup_exe_scene_set.event;
    exe_scene_add_rsp.scene_id = setup_exe_scene_set.scene_id;

    err = mesh_tx_cmd_rsp(
        VD_SETUP_EXECUTION_SCENE_STATUS, (uint8_t *)&exe_scene_add_rsp,
        sizeof(execution_scene_add_rsp_str),
        network_get_unicast_address(), // Replaced ele_adr_primary
        GATEWAY_UNICAST_ADDR, 0, 0);

    uint8_t color_st = LED_COLOR_RED;
    if (exe_scene_add_rsp.status_code == EX_STATUS_SUCCESS) {
      color_st = LED_COLOR_BLUE;
    }
    execution_scene_blink_led_status(1 << key_number, color_st);
    break;
  }

  case MSG_DELETE: {
    DBG_EXECUTION_SCENE_SEND_STR("\n MSG_DELETE: ");

    execution_scene_del_rsp_str exe_scene_del_rsp;
    exe_scene_del_rsp.event = setup_exe_scene_set.event;

    if (check_valid_event_and_key_number(key_number, key_ev) == 0) {
      delete_execution_scene(key_number, ev_idx);
      DBG_EXECUTION_SCENE_SEND_STR("\n DELETE COMPLETE");
    } else {
      // Message Invalid
      err = -1;
    }
    // Response Delete
    exe_scene_del_rsp.msg_type_and_id_st.msg_id =
        setup_exe_scene_set.msg_type_and_id_st.msg_id;
    exe_scene_del_rsp.msg_type_and_id_st.msg_type = MSG_DELETE_RSP;
    exe_scene_del_rsp.status_code =
        ((err == 0) ? (EX_STATUS_SUCCESS) : (EX_STATUS_FAIL));

    err = mesh_tx_cmd_rsp(
        VD_SETUP_EXECUTION_SCENE_STATUS, (uint8_t *)&exe_scene_del_rsp,
        sizeof(execution_scene_del_rsp_str), network_get_unicast_address(),
        GATEWAY_UNICAST_ADDR, 0, 0);
    uint8_t color_st = LED_COLOR_RED;
    if (exe_scene_del_rsp.status_code == EX_STATUS_SUCCESS) {
      color_st = LED_COLOR_BLUE;
    }
    execution_scene_blink_led_status(1 << key_number, color_st);
    break;
  }

  case MSG_GET: {
    DBG_EXECUTION_SCENE_SEND_STR("\n MSG_GET");

    uint8_t rsp[84];
    uint8_t len = 0;
    uint8_t idx = 0;
    header_execution_scene_get_rsp_t header_get_rsp_st;

    header_get_rsp_st.msg_type_and_id_st.msg_id =
        setup_exe_scene_set.msg_type_and_id_st.msg_id;
    header_get_rsp_st.msg_type_and_id_st.msg_type = MSG_GET_RSP;
    header_get_rsp_st.event = setup_exe_scene_set.event;

    if (check_valid_event_and_key_number(key_number, key_ev) != 0) {
      // Event Not Configuration Before, return fail
      DBG_EXECUTION_SCENE_SEND_STR("\n EX_STATUS_FAIL, Event Invalid ");
      header_get_rsp_st.status_code = EX_STATUS_FAIL;
      len = 3;
      memcpy(&rsp, &header_get_rsp_st, len);
    } else {
      if (par_execution_scene[key_number][ev_idx].btn_event == 0xFF) {
        // Event Not Configuration Before, return fail
        DBG_EXECUTION_SCENE_SEND_STR("\n EX_STATUS_NOT_SET_BEFORE ");
        header_get_rsp_st.status_code = EX_STATUS_NOT_SET_BEFORE;
        len = 3;
        memcpy(&rsp, &header_get_rsp_st, len);
      } else {
        /*
         * Response payload:
         * - Message type and message id: 1 byte
         * - Status Code: 1 byte
         * - Event: 1 byte
         * - Scene ID: 2 byte
         * - Number of destination address: 1 byte
         * - Control Information: variable
         * - Transition Time: 1 byte
         * - Additional Condition: variable
         */
        DBG_EXECUTION_SCENE_SEND_STR("\n EX_STATUS_SUCCESS ");
        header_get_rsp_st.status_code = EX_STATUS_SUCCESS;
        header_get_rsp_st.scene_id =
            par_execution_scene[key_number][ev_idx].scene_id;

        // SINGLE SCENE OR CONTROL DIRECTLY

        if (header_get_rsp_st.scene_id != 0xFFFF) {
          header_get_rsp_st.nbr_of_dest = 0;

          foreach (i, MAX_SCENE_DEST_ADR) {
            if (par_execution_scene[key_number][ev_idx]
                    .control_infor[i]
                    .payload_len > MAX_PAYLOAD_LEN) {
              continue;
            }
            header_get_rsp_st.nbr_of_dest++;
          }
          idx = sizeof(header_execution_scene_get_rsp_t);
          len = idx;
          memcpy(&rsp, &header_get_rsp_st, idx);

          foreach (i, header_get_rsp_st.nbr_of_dest) {
            // Destination Address
            rsp[idx++] = (uint8_t)par_execution_scene[key_number][ev_idx]
                             .control_infor[i]
                             .dest_addr;
            rsp[idx++] = (uint8_t)(par_execution_scene[key_number][ev_idx]
                                       .control_infor[i]
                                       .dest_addr >>
                                   8);
            // payload len
            rsp[idx++] = par_execution_scene[key_number][ev_idx]
                             .control_infor[i]
                             .payload_len;
            len += 3;
            // payload
            foreach (j, par_execution_scene[key_number][ev_idx]
                            .control_infor[i]
                            .payload_len) {
              uint8_t *tmp = (uint8_t *)&par_execution_scene[key_number][ev_idx]
                                 .control_infor[i]
                                 .payload;
              rsp[idx++] = *(tmp + j);
              len++;
            }
          }
        }

        // MULTI SCENE
        else {
          header_get_rsp_st.nbr_of_scene = 0;
          foreach (i, MAX_SCENE_DEST_ADR) {
            if (par_execution_scene[key_number][ev_idx]
                    .scene_control[i]
                    .in_scene_id == LM_SCENE_ID_INVALID) {
              continue;
            }
            header_get_rsp_st.nbr_of_scene++;
          }
          idx = sizeof(header_execution_scene_get_rsp_t);
          len = idx;
          memcpy(&rsp, &header_get_rsp_st, idx);

          foreach (i, header_get_rsp_st.nbr_of_scene) {
            // Scene ID
            rsp[idx++] = (uint8_t)par_execution_scene[key_number][ev_idx]
                             .scene_control[i]
                             .in_scene_id;
            rsp[idx++] = (uint8_t)(par_execution_scene[key_number][ev_idx]
                                       .scene_control[i]
                                       .in_scene_id >>
                                   8);
            rsp[idx++] = par_execution_scene[key_number][ev_idx]
                             .scene_control[i]
                             .in_trans_t;
            rsp[idx++] = par_execution_scene[key_number][ev_idx]
                             .scene_control[i]
                             .in_delay_t;
            len += 4;
          }
        }
        rsp[len++] = par_execution_scene[key_number][ev_idx].trans_time;
      }
    }

    err = mesh_tx_cmd_rsp(VD_SETUP_EXECUTION_SCENE_STATUS, (uint8_t *)&rsp, len,
                          network_get_unicast_address(), GATEWAY_UNICAST_ADDR,
                          0, 0);
    break;
  }

  case MSG_DELETE_ALL: {
    DBG_EXECUTION_SCENE_SEND_STR("\n MSG_DELETE_ALL");

    delete_all_execution_scene();
    // Response Delete All
    execution_scene_del_all_rsp_str exe_scene_del_all_rsp;
    exe_scene_del_all_rsp.msg_type_and_id_st.msg_id =
        setup_exe_scene_set.msg_type_and_id_st.msg_id;
    exe_scene_del_all_rsp.msg_type_and_id_st.msg_type = MSG_DELETE_ALL_RSP;
    exe_scene_del_all_rsp.status_code = EX_STATUS_SUCCESS;

    err = mesh_tx_cmd_rsp(
        VD_SETUP_EXECUTION_SCENE_STATUS, (uint8_t *)&exe_scene_del_all_rsp,
        sizeof(execution_scene_del_all_rsp_str), network_get_unicast_address(),
        GATEWAY_UNICAST_ADDR, 0, 0);
    execution_scene_blink_led_status(0xFFFF, LED_COLOR_BLUE);
    break;
  }
  }
  return err;
}

/**
 * @func   execution_scene_init
 * @brief  None
 * @param
 * @retval Status code
 */
void execution_scene_init(void) {
  vendor_handle_func_callback_init(handle_vendor_setup_execution_scene_set);
  restore_execution_scene();
}

/**
 * @func   execution_scene_active
 * @brief  None
 * @param
 * @retval Status code
 */
void execution_scene_active(uint8_t key_number, uint8_t key_ev) {
  static uint8_t tid, vd_tid;
  uint8_t ev_idx = 0;

  (void)get_key_event_index(&ev_idx, key_ev);

  if (check_valid_event_and_key_number(key_number, key_ev) == 0) {
    if (par_execution_scene[key_number][ev_idx].btn_event == key_ev) {
      if (par_execution_scene[key_number][ev_idx].scene_id !=
          SCENE_ID_INVALID) {
        if (par_execution_scene[key_number][ev_idx].scene_id != 0xFFFF) {
          // Control Directly By Scene ID
          vendor_scene_recall_t vendor_scene_recall;
          vendor_scene_recall.msg_type = VENDOR_SCENE_RECALL_NOACK;
          vendor_scene_recall.scene_id =
              par_execution_scene[key_number][ev_idx].scene_id;
          vendor_scene_recall.tid = ++vd_tid;
          vendor_scene_recall.trans_time =
              par_execution_scene[key_number][ev_idx].trans_time;
#if defined(CTL_WITH_FLAG_DEFAULT_POSITION_EN) &&                              \
    CTL_WITH_FLAG_DEFAULT_POSITION_EN
          vendor_scene_recall.delay_time = 1;
#else
          vendor_scene_recall.delay_time = 0;
#endif
          mesh_tx_cmd_rsp(VD_SCENE_REQUEST_NOACK,
                          (uint8_t *)&vendor_scene_recall,
                          sizeof(vendor_scene_recall_t),
                          network_get_unicast_address(), 0xFFFF, 0, 0);
        } else {
          foreach (j, MAX_SCENE_DEST_ADR) {
            if (par_execution_scene[key_number][ev_idx]
                    .scene_control[j]
                    .in_scene_id != LM_SCENE_ID_INVALID) {
              // Control Directly By Scene ID
              vendor_scene_recall_t vendor_scene_recall;
              vendor_scene_recall.msg_type = VENDOR_SCENE_RECALL_NOACK;
              vendor_scene_recall.scene_id =
                  par_execution_scene[key_number][ev_idx]
                      .scene_control[j]
                      .in_scene_id;
              vendor_scene_recall.tid = ++vd_tid;
              vendor_scene_recall.trans_time =
                  par_execution_scene[key_number][ev_idx]
                      .scene_control[j]
                      .in_trans_t;
#if defined(CTL_WITH_FLAG_DEFAULT_POSITION_EN) &&                              \
    CTL_WITH_FLAG_DEFAULT_POSITION_EN
              vendor_scene_recall.delay_time = 1;
#else
              vendor_scene_recall.delay_time =
                  par_execution_scene[key_number][ev_idx]
                      .scene_control[j]
                      .in_delay_t;
#endif
              mesh_tx_cmd_rsp(VD_SCENE_REQUEST_NOACK,
                              (uint8_t *)&vendor_scene_recall,
                              sizeof(vendor_scene_recall_t),
                              network_get_unicast_address(), 0xFFFF, 0, 0);
            }
          }
        }
      } else {
        // Control Directly By Opcode
        for (uint8_t i = 0; i < MAX_SCENE_DEST_ADR; i++) {
          if (par_execution_scene[key_number][ev_idx]
                  .control_infor[i]
                  .dest_addr != BT_MESH_ADDR_UNASSIGNED) {
            // Check Payload Len Valid Or Invalid
            if (par_execution_scene[key_number][ev_idx]
                    .control_infor[i]
                    .payload_len > MAX_PAYLOAD_LEN) {
              continue;
            }
            foreach_arr(j, op_sig_support_scene) {
              uint16_t op_sig = (par_execution_scene[key_number][ev_idx]
                                     .control_infor[i]
                                     .payload[0]
                                 << 8) |
                                par_execution_scene[key_number][ev_idx]
                                    .control_infor[i]
                                    .payload[1];
              if (op_sig == op_sig_support_scene[j]) {
                // Sig Opcode
                uint8_t par_control[MAX_PAYLOAD_LEN];
                uint8_t par_len = par_execution_scene[key_number][ev_idx]
                                      .control_infor[i]
                                      .payload_len -
                                  2;

                // Main Par
                memcpy(&par_control,
                       &par_execution_scene[key_number][ev_idx]
                            .control_infor[i]
                            .payload[2],
                       par_len);
                // Tid + Transition Time + Delay Time
                tid++;
                par_control[par_len++] = tid; // Transition Identify
                par_control[par_len++] =
                    par_execution_scene[key_number][ev_idx]
                        .trans_time; // Transition Time (step 100 ms)
#if defined(CTL_WITH_FLAG_DEFAULT_POSITION_EN) &&                              \
    CTL_WITH_FLAG_DEFAULT_POSITION_EN
                par_control[par_len++] = 1; // Delay Time
#else
                par_control[par_len++] = 0; // Delay Time
#endif
                // Control
                mesh_tx_cmd_rsp(op_sig, (uint8_t *)&par_control, par_len,
                                network_get_unicast_address(),
                                par_execution_scene[key_number][ev_idx]
                                    .control_infor[i]
                                    .dest_addr,
                                0, 0);
              }
            }
            // Vendor Opcode
            uint8_t op_vd = par_execution_scene[key_number][ev_idx]
                                .control_infor[i]
                                .payload[0];
            if (op_vd >= START_VENDOR_OPCODE) {
              // Control
              mesh_tx_cmd_rsp(
                  op_vd,
                  (uint8_t *)&par_execution_scene[key_number][ev_idx]
                      .control_infor[i]
                      .payload[3],
                  par_execution_scene[key_number][ev_idx]
                          .control_infor[i]
                          .payload_len -
                      3,
                  network_get_unicast_address(),
                  par_execution_scene[key_number][ev_idx]
                      .control_infor[i]
                      .dest_addr,
                  0, 0);
            }
          }
        }
      }
    }
  }
}
#endif
// End File
