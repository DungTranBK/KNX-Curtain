/*
 * curtain.c
 *
 *  Created on: Feb 17, 2021
 *      Author: DungTranBK
 */
/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "../../include/curtain.h"

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>

#include "../../include/led.h"
#include "../../include/net_message.h"
#include "../../include/relay.h"
#include "../../include/sw_binding.h"
#include "../../include/utilities.h"
#include "../../include/vendor.h"

LOG_MODULE_REGISTER(curtain, CONFIG_LOG_DEFAULT_LEVEL);

typeCurtain_updateCurtainCurrentPosition
    pvCurtain_updateCurtainCurrentPosition = NULL;

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

static bool en_binding_and_execution[ELE_CNT] = {
    false,
#if ELE_CNT > 1
    false,
#endif
};

const uint8_t opcode_msg_auto_send[] = {VD_CONFIG_CURTAIN_TYPE_OPT,
                                        VD_CONFIG_CURTAIN_LIMIT_TIME};

static cz_auto_send_t cz_auto_send_st = {.en = false,
                                         .send_last_t = 0,
                                         .op_index = 0,
                                         .btn_idx = 0,
                                         .delay_st = 0,
                                         .en_delay = false,
                                         .send_join = false};

// DT99 curtain
#if DEVICE_TYPE == DEV_CURTAIN
static uint8_t g_tempcloseOrOpenState[NUMBER_CURTAIN] = {CURTAIN_STATE_STOP,
#if NUMBER_CURTAIN > 1
                                                         CURTAIN_STATE_STOP
#endif
};
#endif

static Curtain_str curtainData[ELE_CNT];
static bool curtain_initialized = false;

typedef struct {
  uint8_t key;
  uint8_t type[ELE_CNT];
  uint32_t limit_time[ELE_CNT];
  uint8_t rsv[10];
} com_opt_t;

typedef com_opt_t cz_opt_t;
typedef com_opt_t rd_opt_t;

#if DEVICE_TYPE == DEV_CURTAIN
static cz_opt_t cz_opt;
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
static rd_opt_t rd_opt;
#endif

typedef struct {
  uint8_t present_level;
  uint8_t target_level;
  uint8_t state;
} curtain_binding_par_t;

static curtain_binding_par_t cz_binding_par[ELE_CNT] = {
    {0, 0, ST_UNKNOWN},
#if ELE_CNT > 1
    {0, 0, ST_UNKNOWN},
#endif
};

curtain_level_t cz_level_st[ELE_CNT] = {
    {0x00, 0x00, ST_UNKNOWN},
#if ELE_CNT > 1
    {0x00, 0x00, ST_UNKNOWN},
#endif
};

static u16 last_level_binding[ELE_CNT] = {MAX_U16};

/* Settings keys for NVS persistence */
#define SETTINGS_CURTAIN_BASE "curtain"
#define SETTINGS_CURTAIN_POS "curtain/pos"
#define SETTINGS_CURTAIN_OPT "curtain/opt"

/* Temporary buffers used during settings_load_subtree */
static uint32_t settings_load_pos[ELE_CNT];
static bool settings_pos_loaded;
static bool settings_opt_loaded;

static int curtain_settings_set(const char* name, size_t len,
                                settings_read_cb read_cb, void* cb_arg);

SETTINGS_STATIC_HANDLER_DEFINE(curtain, SETTINGS_CURTAIN_BASE, NULL,
                               curtain_settings_set, NULL, NULL);

static int curtain_settings_set(const char* name, size_t len,
                                settings_read_cb read_cb, void* cb_arg) {
  const char* next;
  int rc;

  if (settings_name_steq(name, "pos", &next) && !next) {
    if (len != sizeof(settings_load_pos)) {
      LOG_INF("settings[pos] size mismatch: expect %u, got %u",
              (unsigned)sizeof(settings_load_pos), (unsigned)len);
      return -EINVAL;
    }
    rc = read_cb(cb_arg, &settings_load_pos, sizeof(settings_load_pos));
    if (rc >= 0) {
      settings_pos_loaded = true;
      LOG_INF("settings[pos] loaded OK: %u, %u", settings_load_pos[0],
              settings_load_pos[1]);
      return 0;
    }
    LOG_INF("settings[pos] read_cb failed: %d", rc);
    return rc;
  }

  if (settings_name_steq(name, "opt", &next) && !next) {
    if (len != sizeof(cz_opt)) {
      LOG_INF("settings[opt] size mismatch: expect %u, got %u",
              (unsigned)sizeof(cz_opt), (unsigned)len);
      return -EINVAL;
    }
    rc = read_cb(cb_arg, &cz_opt, sizeof(cz_opt));
    if (rc >= 0) {
      settings_opt_loaded = true;
      LOG_INF(
          "settings[opt] loaded OK: key=0x%02X, type=[%d,%d], limit=[%u,%u]",
          cz_opt.key, cz_opt.type[0], cz_opt.type[1], cz_opt.limit_time[0],
          cz_opt.limit_time[1]);
      return 0;
    }
    LOG_INF("settings[opt] read_cb failed: %d", rc);
    return rc;
  }

  return -ENOENT;
}
/******************************************************************************/
/*                       PRIVATE FUNCTION DECLERATION                         */
/******************************************************************************/
static void curtain_response_curtain_type(int idx, u16 src_adr, u16 dst_adr);
static void curtain_response_limit_time(int idx, u16 src_adr, u16 dst_adr);

static int curtain_config_enable_auto_send(void);
static void Curtain_UpdateCurtainPosition(CurtainNumber_enum CT_No);
static void curtain_update_device_status(u16 curtain_mask);
static uint8_t curtain_get_curtain_position_per_char(CurtainNumber_enum CT_No,
                                                     uint8_t* pos_present,
                                                     uint8_t* pos_target,
                                                     uint8_t* state);
static void curtain_set_command(CurtainNumber_enum CT_No,
                                CurtainCmd_enum curtainCmd);

static void Curtain_RestoreCurrentPosition(void);
static void Curtain_StoreCurrentPosition(uint8_t CT_No);

static void Curtain_SetDefaultAllOption(void);
static void Curtain_RestoreAllOption(void);
static void Curtain_StoreAllOption(void);

static void curtain_goto_position(CurtainNumber_enum CT_No,
                                  uint8_t positionPerChar);

static void curtain_control_binding_group(uint8_t model_idx);

/******************************************************************************/
/*                            EXPORT FUNCTION                                 */
/******************************************************************************/

/**
 * @func    curtain_config_auto_send_proc
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_config_auto_send_proc(void) {
  if (cz_auto_send_st.en == true) {
    if (clock_time_exceed_ms(cz_auto_send_st.send_last_t,
                             TIMER_1S + sys_rand32_get() % TIMER_1S)) {
      if (cz_auto_send_st.op_index >= sizeof(cz_auto_send_st)) {
        cz_auto_send_st.en = false;
        return;
      }
      if (opcode_msg_auto_send[cz_auto_send_st.op_index] ==
          VD_CONFIG_CURTAIN_TYPE_OPT) {
        curtain_response_curtain_type(
            cz_auto_send_st.btn_idx,
            bt_mesh_primary_addr() + cz_auto_send_st.btn_idx,
            GATEWAY_UNICAST_ADDR);
        if (++cz_auto_send_st.btn_idx >= ELE_CNT) {
          cz_auto_send_st.op_index++;
          cz_auto_send_st.btn_idx = 0;
        }
      } else if (opcode_msg_auto_send[cz_auto_send_st.op_index] ==
                 VD_CONFIG_CURTAIN_LIMIT_TIME) {
        curtain_response_limit_time(
            cz_auto_send_st.btn_idx,
            bt_mesh_primary_addr() + cz_auto_send_st.btn_idx,
            GATEWAY_UNICAST_ADDR);
        if (++cz_auto_send_st.btn_idx >= ELE_CNT) {
          cz_auto_send_st.op_index++;
          cz_auto_send_st.btn_idx = 0;
        }
      } else {
        cz_auto_send_st.op_index++;
      }
      cz_auto_send_st.send_last_t = clock_time_ms();
    }
  }
  if (cz_auto_send_st.en_delay == true) {
    if (clock_time_exceed_ms(cz_auto_send_st.delay_st, TIMER_30S)) {
      cz_auto_send_st.en_delay = false;
      curtain_config_enable_auto_send();
    }
  }
}

/**
 * @func    curtain_config_enable_auto_send
 * @brief
 * @param   None
 * @retval  None
 */
static int curtain_config_enable_auto_send(void) {
  if (cz_auto_send_st.en == false) {
    cz_auto_send_st.en = true;
    cz_auto_send_st.send_last_t = clock_time_ms();
    if (cz_auto_send_st.send_last_t >= TIMER_1S) {
      cz_auto_send_st.send_last_t -= TIMER_1S;
    }
    cz_auto_send_st.op_index = 0;
    cz_auto_send_st.btn_idx = 0;
    return 0;
  }
  return -1;
}

/**
 * @func    curtain_setup_enable_send_config_delay
 * @brief
 * @param
 * @retval  None
 */
void curtain_setup_enable_send_config_delay(bool en, bool join_flag) {
  cz_auto_send_st.send_join = join_flag & 0x01;
  if (en == true) {
    cz_auto_send_st.en_delay = true;
    cz_auto_send_st.delay_st = clock_time_ms();
  } else {
    cz_auto_send_st.en_delay = false;
  }
}

/**
 * @func    curtain_is_valid
 * @brief
 * @param
 * @retval  None
 */
static bool curtain_is_valid(uint8_t cz_type) {
  if (cz_type < CZ_TYPE_UNKNOWN) {
    return true;
  }
  return false;
}

/**
 * @func    curtain_response_curtain_type
 * @brief
 * @param
 * @retval  None
 */
static void curtain_response_curtain_type(int idx, u16 src_adr, u16 dst_adr) {
  if (idx < ELE_CNT) {
    curtain_type_rsp_t curtain_type_rsp;
    curtain_type_rsp.op = VD_CONFIG_CURTAIN_TYPE_OPT;
    curtain_type_rsp.endpoint = idx + 1;
    curtain_type_rsp.cz_type = cz_opt.type[idx];
    mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (uint8_t*)&curtain_type_rsp,
                    sizeof(curtain_type_rsp_t), src_adr, dst_adr, 0, 0);
  }
}

/**
 * @func    curtain_get_curtain_type
 * @brief
 * @param
 * @retval  None
 */
int curtain_handle_get_curtain_type(uint8_t* par, int par_len,
                                    mesh_cb_fun_par_t* cb_par) {
  curtain_type_get_t* p_get = (curtain_type_get_t*)par;
  uint8_t idx = p_get->endpoint - 1;
  if (idx < ELE_CNT) {
    curtain_response_curtain_type(idx, cb_par->adr_dst, GATEWAY_UNICAST_ADDR);
    return -1;
  }
  return 0;
}

/**
 * @func    curtain_set_curtain_type
 * @brief
 * @param
 * @retval  None
 */
int curtain_handle_set_curtain_type(uint8_t model_idx, uint8_t* par,
                                    int par_len, bool notify_led_en) {
  LOG_HEXDUMP_INF(par, par_len, "curtain_handle_set_curtain_type: ");
  curtain_type_set_t* p_set = (curtain_type_set_t*)par;
  uint8_t idx = p_set->endpoint - 1;
  if (idx < ELE_CNT) {
    if (notify_led_en == true) {
      if (curtain_is_valid(p_set->cz_type) == true) {
        LED_push_blink_with_Interval_to_fifo(2, LED_COLOR_BLUE, 200);
      } else {
        LED_push_blink_with_Interval_to_fifo(2, LED_COLOR_RED, 200);
        return -1;
      }
    }
    if (p_set->cz_type == cz_opt.type[idx]) {
      curtain_response_curtain_type(idx, bt_mesh_primary_addr() + model_idx,
                                    GATEWAY_UNICAST_ADDR);
      return 0;
    }
    cz_opt.type[idx] = p_set->cz_type;
    Curtain_StoreAllOption();
    curtain_response_curtain_type(idx, bt_mesh_primary_addr() + model_idx,
                                  GATEWAY_UNICAST_ADDR);
    curtain_set_command(idx, CURTAIN_CMD_STOP);
    return 0;
  }
  return -1;
}

/**
 * @func    curtain_response_limit_time
 * @brief
 * @param
 * @retval  None
 */
static void curtain_response_limit_time(int idx, u16 src_adr, u16 dst_adr) {
  if (idx < ELE_CNT) {
    limit_time_rsp_t limit_time_rsp;
    limit_time_rsp.op = VD_CONFIG_CURTAIN_LIMIT_TIME;
    limit_time_rsp.mode = 0;  // Normal mode
    limit_time_rsp.limit_time_ms = cz_opt.limit_time[idx];
    mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (uint8_t*)&limit_time_rsp,
                    sizeof(limit_time_rsp_t), src_adr, dst_adr, 0, 0);
  }
}

/**
 * @func    curtain_cfg_get_mcu_opt
 * @brief
 * @param
 * @retval  None
 */
int curtain_handle_get_limit_time(uint8_t model_idx, uint8_t* par,
                                  int par_len) {
  curtain_response_limit_time(model_idx, bt_mesh_primary_addr() + model_idx,
                              GATEWAY_UNICAST_ADDR);
  return 0;
}

/**
 * @func    curtain_cfg_get_mcu_opt
 * @brief
 * @param
 * @retval  None
 */
int curtain_handle_set_limit_time(uint8_t model_idx, uint8_t* par, int par_len,
                                  bool notify_led_en) {
  LOG_HEXDUMP_INF(par, par_len, "curtain_handle_set_limit_time: ");
  limit_time_set_t* p_set = (limit_time_set_t*)par;
  if (model_idx < ELE_CNT) {
    if (notify_led_en == true) {
      if (p_set->limit_time >= TIMER_1S && p_set->limit_time <= TIMER_5Min) {
        LED_push_blink_with_Interval_to_fifo(2, LED_COLOR_BLUE, 200);
      } else {
        LED_push_blink_with_Interval_to_fifo(2, LED_COLOR_RED, 200);
        return -1;
      }
    }
    if (p_set->limit_time == cz_opt.limit_time[model_idx]) {
      curtain_response_limit_time(model_idx, bt_mesh_primary_addr() + model_idx,
                                  GATEWAY_UNICAST_ADDR);
      return 0;
    }
    cz_opt.limit_time[model_idx] = p_set->limit_time;
    Curtain_StoreAllOption();
    curtain_response_limit_time(model_idx, bt_mesh_primary_addr() + model_idx,
                                GATEWAY_UNICAST_ADDR);
    return 0;
  }
  return -1;
}

/**
 * @func    sw_cfg_handle_get_message
 * @brief
 * @param
 * @retval  None
 */
int curtain_cfg_handle_get_message(int model_idx, uint8_t* par, int par_len,
                                   uint8_t cmd) {
  if (model_idx >= ELE_CNT) {
    return -1;
  }
  switch (cmd) {
    case VD_CONFIG_CURTAIN_TYPE_OPT:
      curtain_response_curtain_type(
          model_idx, bt_mesh_primary_addr() + model_idx, GATEWAY_UNICAST_ADDR);
      break;
    case VD_CONFIG_CURTAIN_LIMIT_TIME:
      curtain_response_limit_time(model_idx, bt_mesh_primary_addr() + model_idx,
                                  GATEWAY_UNICAST_ADDR);
      break;
    default:
      return -1;
  }
  return 0;
}

/**
 * @func    curtain_cfg_handle_set_message
 * @brief
 * @param
 * @retval  None
 */
int curtain_cfg_handle_set_message(int model_idx, uint8_t* par, int par_len,
                                   uint8_t cmd, bool notify_led_en) {
  if (model_idx >= ELE_CNT) {
    return -1;
  }
  switch (cmd) {
    case VD_CONFIG_CURTAIN_TYPE_OPT:
      curtain_handle_set_curtain_type(model_idx, par, par_len, notify_led_en);
      break;
    case VD_CONFIG_CURTAIN_LIMIT_TIME:
      curtain_handle_set_limit_time(model_idx, par, par_len, notify_led_en);
      break;
    default:
      return -1;
  }
  return 0;
}

/**
 * @func    curtain_cfg_get_mcu_opt
 * @brief
 * @param
 * @retval  None
 */
void curtain_response_st_to_gateway(uint8_t idx) {
  if (idx < ELE_CNT) {
    mesh_tx_cmd_rsp(G_LEVEL_STATUS, (uint8_t*)&cz_level_st[idx].present_level,
                    1, bt_mesh_primary_addr() + idx, GATEWAY_UNICAST_ADDR, 0,
                    0);
  }
}

/**
 * @func    curtain_update_present_level
 * @brief
 * @param
 * @retval  None
 */
void curtain_update_present_level(uint8_t idx, uint8_t level) {
  if (idx < ELE_CNT) {
    cz_level_st[idx].present_level = level;  // 0 - 0xFF
  }
}

/**
 * @func    curtain_get_target_position
 * @brief
 * @param
 * @retval  None
 */
uint8_t curtain_get_target_position(uint8_t idx) {
  if (idx < ELE_CNT) {
    return cz_level_st[idx].target_level;
  }
  return 0;
}

/**
 * @func    curtain_get_position
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_set_level(uint8_t idx, uint8_t pos) {
  curtain_control_curtain_by_app(idx, CURTAIN_CONTROL_ID_RUN, pos);
  // Led notify
  if (led_get_blink_led_flag() == BLINK_IDLE && led_fifo_is_empty() == true) {
    LED_push_blink_with_Interval_to_fifo(1, LED_COLOR_BLUE, 200);
  }
}

/**
 * @func    curtain_handle_refresh_led
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_handle_refresh_led(u16 mask) {
  if (bt_mesh_is_main_network_provisioned()) {
    led_set_color(0, LED_COLOR_BLUE);
  } else {
    led_set_color(0, LED_COLOR_RED);
  }
}

/******************************************************************************************
  CURTAIN CONTROL
 ******************************************************************************************/

/**
 * @func   Curtain_RestorePosition
 * @brief  None
 * @param
 * @retval None
 */
static void Curtain_RestoreCurrentPosition(void) {
  settings_pos_loaded = false;
  int load_rc = settings_load_subtree(SETTINGS_CURTAIN_POS);
  LOG_INF("RestorePos: load_subtree rc=%d, loaded=%d", load_rc,
          settings_pos_loaded);

  bool is_default = false;
  foreach (i, ELE_CNT) {
    uint32_t pos = settings_pos_loaded ? settings_load_pos[i] : 0;
    if (pos > TIMER_5Min) {
      pos = 0;
      is_default = true;
    }
    curtainData[i].currentPosition = pos;
  }
  if (is_default || !settings_pos_loaded) {
    LOG_INF("RestorePos: using defaults (is_default=%d, loaded=%d)", is_default,
            settings_pos_loaded);
    Curtain_StoreCurrentPosition(0);
  }
  LOG_INF("RESTORE CURRENT POS: [%u, %u]", curtainData[0].currentPosition,
          curtainData[1].currentPosition);
}

/**
 * @func   Curtain_StorePosition
 * @brief  None
 * @param
 * @retval None
 */
static void Curtain_StoreCurrentPosition(uint8_t CT_No) {
  uint32_t current_position[ELE_CNT];
  foreach (i, ELE_CNT) {
    current_position[i] = curtainData[i].currentPosition;
  }
  int rc = settings_save_one(SETTINGS_CURTAIN_POS, &current_position,
                             sizeof(current_position));
  LOG_INF("StorePos: save_one rc=%d, pos=[%u, %u]", rc, current_position[0],
          current_position[1]);
}

/**
 * @func   Curtain_SetDefaultAllOption
 * @brief  None
 * @param
 * @retval None
 */
static void Curtain_SetDefaultAllOption(void) {
  cz_opt.key = KEY_FLASH;
  foreach (i, ELE_CNT) {
    cz_opt.type[i] = HOZ_DZ4W;
    cz_opt.limit_time[i] = LIMIT_TIME_DEFAULT_MS;
  }
  Curtain_StoreAllOption();
}

/**
 * @func   Curtain_RestoreAllOption
 * @brief  None
 * @param
 * @retval None
 */
static void Curtain_RestoreAllOption(void) {
  settings_opt_loaded = false;
  int load_rc = settings_load_subtree(SETTINGS_CURTAIN_OPT);
  LOG_INF("RestoreOpt: load_subtree rc=%d, loaded=%d", load_rc,
          settings_opt_loaded);

  if (!settings_opt_loaded || cz_opt.key != KEY_FLASH) {
    LOG_INF("RestoreOpt: using defaults (loaded=%d, key=0x%02X)",
            settings_opt_loaded, cz_opt.key);
    Curtain_SetDefaultAllOption();
  }
  foreach (i, ELE_CNT) {
    curtainData[i].curtainLimitTime = cz_opt.limit_time[i];
  }

  LOG_INF("RestoreOpt: TYPE=[%d, %d], LIMIT=[%u, %u]", cz_opt.type[0],
          cz_opt.type[1], cz_opt.limit_time[0], cz_opt.limit_time[1]);
}

/**
 * @func   Curtain_StoreAllOption
 * @brief  None
 * @param
 * @retval None
 */
static void Curtain_StoreAllOption(void) {
  cz_opt.key = KEY_FLASH;
  int rc = settings_save_one(SETTINGS_CURTAIN_OPT, &cz_opt, sizeof(cz_opt));
  if (rc) {
    LOG_ERR("Failed to save curtain options: %d", rc);
  }
  foreach (i, ELE_CNT) {
    curtainData[i].curtainLimitTime = cz_opt.limit_time[i];
  }
}

/**
 * @func   curtain_set_command
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 *         BYTE: Command ID
 * @retval None
 */
static void curtain_set_command(CurtainNumber_enum CT_No,
                                CurtainCmd_enum curtainCmd) {
  if (curtainData[CT_No].currentCmd == CURTAIN_CMD_IDLE) {
    curtainData[CT_No].currentCmd = curtainCmd;
  } else {
    curtainData[CT_No].bufferCmd = curtainCmd;
  }
  if (curtainCmd == CURTAIN_CMD_STOP) {
    curtainData[CT_No].curtainState = CURTAIN_STATE_WAIT_NEXT_CMD;
  }
}

/**
 * @func   curtain_control_relay
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 *         BYTE: State of All output control RL
 * @retval None
 */
static void curtain_control_relay(CurtainNumber_enum CT_No, uint8_t open,
                                  uint8_t close, uint8_t stop) {
  if (CT_No == 0) {
    uint8_t idx_relay = 0;
    relay_control_directly(idx_relay + 0, open);
    relay_control_directly(idx_relay + 1, stop);
    relay_control_directly(idx_relay + 2, close);
    LOG_INF("**************Control RL: %d, %d, %d", open, stop, close);
  } else {
    LOG_INF("Curtain %d not support", CT_No);
  }
}

/**
 * @func   curtain_run_stop_command
 * @brief  None
 * @param  CurtainNumber_enum: Curtain Number
 * @retval BYTE: Handle Stop Command Status
 */
static uint8_t curtain_run_stop_command(CurtainNumber_enum CT_No) {
  switch (curtainData[CT_No].cmdStep) {
    case 0:
      curtainData[CT_No].enableCalculatorCurtainPosition = false;
      // curtainData[CT_No].lastButtonState = _STOP_BUTTON_PRESS;
      curtainData[CT_No].curtainState = CURTAIN_STATE_STOP;
#ifdef CZ_EXTENDED_STATE
      curtainData[CT_No].extended_state = EX_ST_STOP;
#endif
#ifdef THREE_TOUCH
#ifdef HOLD_FOR_TOUCH
      stopLedBlinkStartTime = clock_time_ms();
#endif
      Curtain_RefreshLedDelay(TIMER_500MS, CURTAIN_0_MASK);
#else
      // curtain_handle_refresh_led((uint16_t)(1 << CT_No));
#endif
      Curtain_StoreCurrentPosition(CT_No);
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == HOZ_DZ3W) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      } else if (cz_opt.type[CT_No] == HOZ_DT99) {
        if (g_tempcloseOrOpenState[CT_No] == CURTAIN_STATE_OPEN) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (g_tempcloseOrOpenState[CT_No] == CURTAIN_STATE_CLOSE) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        }
      } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
        curtain_control_relay(CT_No, ON, ON, OFF);
      } else if (cz_opt.type[CT_No] == VER_220V) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      if (rd_type == RD_S3DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (rd_type == RD_S4DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      }
#endif
      curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
      curtainData[CT_No].cmdStep = 1;
      // Update Level
      Curtain_UpdateCurtainPosition(CT_No);
      break;
    case 1:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, ON, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
          g_tempcloseOrOpenState[CT_No] = CURTAIN_STATE_STOP;
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#endif
        curtainData[CT_No].cmdStep = 0;
#ifdef CZ_EXTENDED_STATE
        curtainData[CT_No].extended_state = EX_ST_STOPED;
#endif
        return CURRENT_CMD_HANDLE_DONE;
      }
      break;
    default:
      curtainData[CT_No].cmdStep = 0;
      break;
  }
  return CURRENT_CMD_HANDLE_NOT_DONE;
}

/**
 * @func   curtain_run_close_command
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval BYTE: Handle Close Command Status
 */
static uint8_t curtain_run_close_command(CurtainNumber_enum CT_No) {
  switch (curtainData[CT_No].cmdStep) {
    case 0:
#ifdef CZ_EXTENDED_STATE
      curtainData[CT_No].extended_state = EX_ST_START_CLOSE;
#endif
      curtainData[CT_No].curtainState = CURTAIN_STATE_CLOSE;
      curtainData[CT_No].enableCalculatorCurtainPosition = false;
      curtainData[CT_No].destinationPositionToGoTo =
          curtainData[CT_No].tempDestinationPositionToGoTo;
      // curtain_handle_refresh_led((uint16_t)(1 << CT_No));
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == HOZ_DZ3W) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      } else if (cz_opt.type[CT_No] == HOZ_DT99) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
        curtain_control_relay(CT_No, ON, ON, OFF);
      } else if (cz_opt.type[CT_No] == VER_220V) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      if (rd_type == RD_S3DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (rd_type == RD_S4DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      }
#endif
      curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
      curtainData[CT_No].cmdStep = 1;
      // Update Level
      if (curtainData[CT_No].currentPosition !=
          curtainData[CT_No].curtainLimitTime) {
        Curtain_UpdateCurtainPosition(CT_No);
      }
      break;
    case 1:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#endif
        curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
        curtainData[CT_No].cmdStep = 2;
      }
      break;
    case 2:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
        curtainData[CT_No].enableCalculatorCurtainPosition = true;
        curtainData[CT_No].curtainRunningStartTime = clock_time_ms();
        curtainData[CT_No].startPosition = curtainData[CT_No].currentPosition;
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == VER_220V) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, ON, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        }
#endif
        curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
        curtainData[CT_No].cmdStep = 3;
      }
      break;
    case 3:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
#ifdef CZ_EXTENDED_STATE
        curtainData[CT_No].extended_state = EX_ST_CLOSING;
#endif
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
          g_tempcloseOrOpenState[CT_No] = CURTAIN_STATE_CLOSE;
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#endif
        curtainData[CT_No].cmdStep = 0;
        return CURRENT_CMD_HANDLE_DONE;
      }
      break;
    default:
      curtainData[CT_No].cmdStep = 0;
      break;
  }
  return CURRENT_CMD_HANDLE_NOT_DONE;
}

/**
 * @func    curtain_run_open_command
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval BYTE: Handle Open Command Status
 */
static uint8_t curtain_run_open_command(CurtainNumber_enum CT_No) {
  switch (curtainData[CT_No].cmdStep) {
    case 0:
#ifdef CZ_EXTENDED_STATE
      curtainData[CT_No].extended_state = EX_ST_START_OPEN;
#endif
      curtainData[CT_No].curtainState = CURTAIN_STATE_OPEN;
      curtainData[CT_No].enableCalculatorCurtainPosition = false;
      curtainData[CT_No].destinationPositionToGoTo =
          curtainData[CT_No].tempDestinationPositionToGoTo;
      // curtain_handle_refresh_led((uint16_t)(1 << CT_No));
      // Relay control
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == HOZ_DZ3W) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      } else if (cz_opt.type[CT_No] == HOZ_DT99) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
        curtain_control_relay(CT_No, ON, ON, OFF);
      } else if (cz_opt.type[CT_No] == VER_220V) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      if (rd_type == RD_S3DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, OFF);
      } else if (rd_type == RD_S4DZ1) {
        curtain_control_relay(CT_No, OFF, OFF, ON);
      }
#endif
      curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
      curtainData[CT_No].cmdStep = 1;
      // Update Level
      if (curtainData[CT_No].currentPosition != 0) {
        Curtain_UpdateCurtainPosition(CT_No);
      }
      break;
    case 1:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, OFF, ON, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#endif
        curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
        curtainData[CT_No].cmdStep = 2;
      }
      break;
    case 2:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
        curtainData[CT_No].enableCalculatorCurtainPosition = true;
        curtainData[CT_No].curtainRunningStartTime = clock_time_ms();
        curtainData[CT_No].startPosition = curtainData[CT_No].currentPosition;
        // Relay control
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == VER_220V) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, ON, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        }
#endif
        curtainData[CT_No].startDelayTimeForCmdStep = clock_time_ms();
        curtainData[CT_No].cmdStep = 3;
      }
      break;

    case 3:
      if (clock_time_get_elapsed_time(
              curtainData[CT_No].startDelayTimeForCmdStep) >
          DEFAULT_PULL_DELAY) {
#ifdef CZ_EXTENDED_STATE
        curtainData[CT_No].extended_state = EX_ST_OPENNING;
#endif
#if DEVICE_TYPE == DEV_CURTAIN
        if (cz_opt.type[CT_No] == HOZ_DZ3W) {
          curtain_control_relay(CT_No, ON, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DZ4W) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        } else if (cz_opt.type[CT_No] == HOZ_DT99) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
          g_tempcloseOrOpenState[CT_No] = CURTAIN_STATE_OPEN;
        } else if (cz_opt.type[CT_No] == HOZ_DZ3WP) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
        if (rd_type == RD_S3DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, ON);
        } else if (rd_type == RD_S4DZ1) {
          curtain_control_relay(CT_No, OFF, OFF, OFF);
        }
#endif
        curtainData[CT_No].cmdStep = 0;
        return CURRENT_CMD_HANDLE_DONE;
      }
      break;

    default:
      curtainData[CT_No].cmdStep = 0;
      break;
  }
  return CURRENT_CMD_HANDLE_NOT_DONE;
}

/**
 * @func   curtain_handle_command
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void curtain_handle_command(CurtainNumber_enum CT_No) {
  uint8_t currentCmdHandlerStatus = CURRENT_CMD_HANDLE_NOT_DONE;

  if (curtainData[CT_No].currentCmd != CURTAIN_CMD_IDLE) {
    switch (curtainData[CT_No].currentCmd) {
      case CURTAIN_CMD_OPEN:
        currentCmdHandlerStatus = curtain_run_open_command(CT_No);
        break;
      case CURTAIN_CMD_CLOSE:
        currentCmdHandlerStatus = curtain_run_close_command(CT_No);
        break;
      case CURTAIN_CMD_STOP:
        currentCmdHandlerStatus = curtain_run_stop_command(CT_No);
        break;
    }
    if (currentCmdHandlerStatus == CURRENT_CMD_HANDLE_DONE) {
      curtainData[CT_No].currentCmd = CURTAIN_CMD_IDLE;
    }
  }
  if (curtainData[CT_No].currentCmd == CURTAIN_CMD_IDLE) {
    if (curtainData[CT_No].bufferCmd != CURTAIN_CMD_IDLE) {
      curtainData[CT_No].currentCmd = curtainData[CT_No].bufferCmd;
      curtainData[CT_No].bufferCmd = CURTAIN_CMD_IDLE;
    }
  }
}

/**
 * @func   Curtain_UpdateCurtainPosition
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void Curtain_UpdateCurtainPosition(CurtainNumber_enum CT_No) {
  curtain_update_device_status((uint16_t)(1 << CT_No));
  if (pvCurtain_updateCurtainCurrentPosition != NULL) {
    pvCurtain_updateCurtainCurrentPosition(CT_No,
                                           curtain_get_curret_level(CT_No));
  }
}

/**
 * @func   Curtain_SetStopCmdAndSendLevel
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void Curtain_SetStopCmdIfBufferIsIdle(CurtainNumber_enum CT_No) {
  if (curtainData[CT_No].bufferCmd == CURTAIN_CMD_IDLE) {
    curtain_set_command(CT_No, CURTAIN_CMD_STOP);
  }
}

/**
 * @func   Curtain_HandleLimitPosition
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void Curtain_HandleLimitPosition(CurtainNumber_enum CT_No) {
  curtainData[CT_No].enableCalculatorCurtainPosition = false;
#if DEVICE_TYPE == DEV_CURTAIN
  if (cz_opt.type[CT_No] != VER_220V) {
    Curtain_StoreCurrentPosition(CT_No);
    if (curtainData[CT_No].bufferCmd == CURTAIN_CMD_IDLE) {
      curtainData[CT_No].curtainState = CURTAIN_STATE_STOP;
      // curtain_handle_refresh_led((uint16_t)(1 << CT_No));
      Curtain_UpdateCurtainPosition(CT_No);
    }
  } else {
    Curtain_SetStopCmdIfBufferIsIdle(CT_No);
  }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
  Curtain_StoreCurrentPosition(CT_No);
  if (curtainData[CT_No].bufferCmd == CURTAIN_CMD_IDLE) {
    curtainData[CT_No].curtainState = CURTAIN_STATE_STOP;
    curtain_handle_refresh_led((uint16_t)(1 << CT_No));
    Curtain_UpdateCurtainPosition(CT_No);
  }
#endif
}

/**
 * @func   curtain_handle_goto_specified_position
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void curtain_handle_goto_specified_position(CurtainNumber_enum CT_No) {
  if (curtainData[CT_No].destinationPositionToGoTo != DESTINATION_NULL) {
    if (curtainData[CT_No].curtainState == CURTAIN_STATE_CLOSE) {
      if (curtainData[CT_No].currentPosition >=
          curtainData[CT_No].destinationPositionToGoTo) {
        curtainData[CT_No].currentPosition += CURTAIN_CORRECTION_ERROR_VAL;
        Curtain_SetStopCmdIfBufferIsIdle(CT_No);
        LOG_INF("GotoSpecifiedPosition: MIN");
      }
    } else if (curtainData[CT_No].curtainState == CURTAIN_STATE_OPEN) {
      if (curtainData[CT_No].currentPosition <=
          curtainData[CT_No].destinationPositionToGoTo) {
        curtainData[CT_No].currentPosition += CURTAIN_CORRECTION_ERROR_VAL;
        Curtain_SetStopCmdIfBufferIsIdle(CT_No);
        LOG_INF("GotoSpecifiedPosition: MAX");
      }
    }
  }
}

/**
 * @func   curtain_handle_min_max_position
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void curtain_handle_min_max_position(CurtainNumber_enum CT_No) {
  switch (curtainData[CT_No].curtainState) {
    case CURTAIN_STATE_OPEN:
      curtainData[CT_No].currentPosition =
          curtainData[CT_No].startPosition -
          clock_time_get_elapsed_time(
              curtainData[CT_No].curtainRunningStartTime);
      if ((int32_t)curtainData[CT_No].currentPosition <= 0) {
        curtainData[CT_No].currentPosition = 0;
        curtainData[CT_No].sendStep = MIN_STEP;
#ifdef CZ_EXTENDED_STATE
        curtainData[CT_No].extended_state = EX_ST_OPENNED;
#endif
        Curtain_HandleLimitPosition(CT_No);  // Min Position
      }
      break;

    case CURTAIN_STATE_CLOSE:
      curtainData[CT_No].currentPosition =
          curtainData[CT_No].startPosition +
          clock_time_get_elapsed_time(
              curtainData[CT_No].curtainRunningStartTime);
      if (curtainData[CT_No].currentPosition >=
          curtainData[CT_No].curtainLimitTime) {
        curtainData[CT_No].currentPosition =
            curtainData[CT_No].curtainLimitTime;
        curtainData[CT_No].sendStep = MAX_STEP;
#ifdef CZ_EXTENDED_STATE
        curtainData[CT_No].extended_state = EX_ST_CLOSED;
#endif
        Curtain_HandleLimitPosition(CT_No);  // Max Position
      }
      break;
  }
}

/**
 * @func   Curtain_HandleSendLevel
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void Curtain_HandleSendLevel(CurtainNumber_enum CT_No) {
  uint8_t i, j;
  uint32_t stepToSendLevel;
  uint32_t temp;
  stepToSendLevel = curtainData[CT_No].curtainLimitTime / 7;

  for (i = 0; i < 7; i++) {
    temp = i * stepToSendLevel;
    j = (uint8_t)(i + 1);
    if (curtainData[CT_No].currentPosition != 0) {
      if ((curtainData[CT_No].currentPosition >= temp) &&
          (curtainData[CT_No].currentPosition < (temp + stepToSendLevel))) {
        if (curtainData[CT_No].sendStep != j) {
          switch (curtainData[CT_No].curtainState) {
            case CURTAIN_STATE_OPEN:
              if (curtainData[CT_No].currentPosition <=
                  (temp + (stepToSendLevel >> 1))) {
                Curtain_UpdateCurtainPosition(CT_No);
                curtainData[CT_No].sendStep = j;
              }
              break;

            case CURTAIN_STATE_CLOSE:
              if (curtainData[CT_No].currentPosition >=
                  (temp + (stepToSendLevel >> 1))) {
                Curtain_UpdateCurtainPosition(CT_No);
                curtainData[CT_No].sendStep = j;
              }
              break;
          }
        }
        return;
      }
    }
  }
}

/**
 * @func   curtain_goto_position
 * @brief  Curtain go to specified position
 * @param  CurtainNumber_enum: Curtain Position
 *         BYTE: Position Per Char (0x00 to 0xFF)
 * @retval None
 */
static void curtain_goto_position(CurtainNumber_enum CT_No,
                                  uint8_t positionPerChar) {
  curtainData[CT_No].tempDestinationPositionToGoTo =
      (curtainData[CT_No].curtainLimitTime * positionPerChar +
       CURTAIN_CORRECTION_ERROR_VAL) /
      0xFF;

  if (curtainData[CT_No].tempDestinationPositionToGoTo >=
      curtainData[CT_No].currentPosition) {
#if DEVICE_TYPE == DEV_CURTAIN
    u16 pulse_len = DEFAULT_PULL_DELAY;
    if (cz_opt.type[CT_No] != VER_220V) {
      pulse_len = 2 * DEFAULT_PULL_DELAY;
    }
    if ((curtainData[CT_No].tempDestinationPositionToGoTo -
         curtainData[CT_No].currentPosition) < pulse_len) {
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
    if ((curtainData[CT_No].tempDestinationPositionToGoTo -
         curtainData[CT_No].currentPosition) < (2 * DEFAULT_PULL_DELAY)) {
#endif

      if (curtainData[CT_No].curtainState != CURTAIN_STATE_CLOSE) {
        curtain_set_command(CT_No, CURTAIN_CMD_STOP);
      } else {
        curtainData[CT_No].destinationPositionToGoTo =
            curtainData[CT_No].tempDestinationPositionToGoTo;
      }
    } else {
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == VER_220V) {
        if (curtainData[CT_No].curtainState != CURTAIN_STATE_CLOSE) {
          curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
        } else {
          curtainData[CT_No].destinationPositionToGoTo =
              curtainData[CT_No].tempDestinationPositionToGoTo;
        }
      } else {
        curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
      }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR

      curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
#endif
      // curtainData[CT_No].lastButtonState = _CLOSE_BUTTON_PRESS;
    }
  } else if (curtainData[CT_No].tempDestinationPositionToGoTo <
             curtainData[CT_No].currentPosition) {
#if DEVICE_TYPE == DEV_CURTAIN
    u16 pulse = DEFAULT_PULL_DELAY;
    if (cz_opt.type[CT_No] != VER_220V) {
      pulse = 2 * DEFAULT_PULL_DELAY;
    }
    if ((curtainData[CT_No].currentPosition -
         curtainData[CT_No].tempDestinationPositionToGoTo) < pulse) {
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
    if ((curtainData[CT_No].currentPosition -
         curtainData[CT_No].tempDestinationPositionToGoTo) <
        (2 * DEFAULT_PULL_DELAY)) {
#endif
      if (curtainData[CT_No].curtainState != CURTAIN_STATE_OPEN) {
        curtain_set_command(CT_No, CURTAIN_CMD_STOP);
      } else {
        curtainData[CT_No].destinationPositionToGoTo =
            curtainData[CT_No].tempDestinationPositionToGoTo;
      }
    } else {
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == VER_220V) {
        if (curtainData[CT_No].curtainState != CURTAIN_STATE_OPEN) {
          curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
        } else {
          curtainData[CT_No].destinationPositionToGoTo =
              curtainData[CT_No].tempDestinationPositionToGoTo;
        }
      } else {
        curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
      }

#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
#endif
      // curtainData[CT_No].lastButtonState = _OPEN_BUTTON_PRESS;
    }
  }
}

/**
 * @func   curtain_set_command
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 * @retval None
 */
static void Curtain_SetStopCommand(CurtainNumber_enum CT_No) {
  curtain_set_command(CT_No, CURTAIN_CMD_STOP);
}

/**
 * @func   Curtain_UpdateStatus
 * @brief
 * @param  None
 * @retval None
 */
static void curtain_update_device_status(u16 curtain_mask) {
  // TODO
  uint8_t pos_present, pos_target, state;
  foreach (i, NUMBER_CURTAIN) {
    if (((curtain_mask >> i) & 0x01) == 0x01) {
      curtain_get_curtain_position_per_char(i, &pos_present, &pos_target,
                                            &state);
      net_message_publish_status_delay(i, 0);

      LOG_INF("curtain_update_device_status: %d, P: %d, T: %d, ST: %d", i,
              pos_present, pos_target, state);

      // For binding
      curtain_update_present_level(i, pos_present);
      cz_level_st[i].target_level = pos_target;
      cz_level_st[i].state = state;

      // Binding
      if (en_binding_and_execution[i] == true) {
        curtain_control_binding_group(i);
      } else {
        en_binding_and_execution[i] = true;
        // POWER ON
        if (cz_binding_par[i].state == ST_UNKNOWN) {
          cz_binding_par[i].state = state;
        }
      }
    }
  }
}

/**
 * @func   curtain_control_curtain_by_app
 * @brief
 * @param  CurtainNumber_enum: Curtain Number
 *         CurtainControlId_enum: Command ID
 *         BYTE: Curtain Position To Go
 * @retval None
 */
void curtain_control_curtain_by_app(CurtainNumber_enum CT_No,
                                    CurtainControlId_enum CT_CmdId,
                                    uint8_t positionPerChar) {
  LOG_INF("curtain_control_curtain_by_app: CT_No=%d, CmdId=%d, pos=%d", CT_No,
          CT_CmdId, positionPerChar);

  if (CT_CmdId == CURTAIN_CONTROL_ID_STOP) {
    LOG_INF("STOP");
#ifdef THREE_TOUCH
    if (LED_GetBitBlinkLedStatus() == BLINK_IDLE) {
      Curtain_OpenLedOff(CURTAIN_0);
      Curtain_CloseLedOff(CURTAIN_0);
      Curtain_StopLedOn(CURTAIN_0);
    }
#endif
    curtain_update_device_status((u16)(1 << CT_No));
    curtain_set_command(CT_No, CURTAIN_CMD_STOP);
  } else if (CT_CmdId == CURTAIN_CONTROL_ID_RUN) {
    LOG_INF("RUN");
    if (positionPerChar == _MAX_POSITION) {
      curtainData[CT_No].tempDestinationPositionToGoTo = DESTINATION_NULL;
      // curtainData[CT_No].lastButtonState = _CLOSE_BUTTON_PRESS;
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == VER_220V) {
        if ((curtainData[CT_No].curtainState != CURTAIN_STATE_CLOSE) ||
            (curtainData[CT_No].bufferCmd != CURTAIN_CMD_IDLE)) {
          curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
        }
        curtainData[CT_No].destinationPositionToGoTo = DESTINATION_NULL;
      } else {
        curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
      }
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      curtain_set_command(CT_No, CURTAIN_CMD_CLOSE);
#endif
      LOG_INF("...MAX");

    } else if (positionPerChar == _MIN_POSITION) {
      curtainData[CT_No].tempDestinationPositionToGoTo = DESTINATION_NULL;
      // curtainData[CT_No].lastButtonState = _OPEN_BUTTON_PRESS;
#if DEVICE_TYPE == DEV_CURTAIN
      if (cz_opt.type[CT_No] == VER_220V) {
        if ((curtainData[CT_No].curtainState != CURTAIN_STATE_OPEN) ||
            (curtainData[CT_No].bufferCmd != CURTAIN_CMD_IDLE)) {
          curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
        }
        curtainData[CT_No].destinationPositionToGoTo = DESTINATION_NULL;
      } else {
        curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
      }
      LOG_INF("...MIN");
#elif DEVICE_TYPE == DEV_ROLLING_DOOR
      curtain_set_command(CT_No, CURTAIN_CMD_OPEN);
#endif
    } else {
      curtain_goto_position(CT_No, positionPerChar);
      LOG_INF("...Specific Position: %d", positionPerChar);
    }
  }
}

/**
 * @func   curtain_get_curret_level
 * @brief
 * @param  None
 * @retval Curtain position per char
 */
uint8_t curtain_get_curret_level(uint8_t CT_No) {
  if (CT_No < NUMBER_CURTAIN) {
    return (uint8_t)(((curtainData[CT_No].currentPosition) * 0xFF) /
                     curtainData[CT_No].curtainLimitTime);
  }
  return 0;
}

/**
 * @func   curtain_get_curtain_position_per_char
 * @brief
 * @param  None
 * @retval Curtain position per char
 */
static uint8_t curtain_get_curtain_position_per_char(CurtainNumber_enum CT_No,
                                                     uint8_t* pos_present,
                                                     uint8_t* pos_target,
                                                     uint8_t* state) {
  if (CT_No < NUMBER_CURTAIN) {
    *pos_present = (uint8_t)(((curtainData[CT_No].currentPosition) * 0xFF) /
                             curtainData[CT_No].curtainLimitTime);
    // Modify extended_state
    if (curtainData[CT_No].extended_state == EX_ST_STOP ||
        curtainData[CT_No].extended_state == EX_ST_STOPED) {
      if (curtainData[CT_No].currentPosition ==
          curtainData[CT_No].curtainLimitTime) {
        curtainData[CT_No].extended_state = EX_ST_CLOSED;
      } else if (curtainData[CT_No].currentPosition == 0) {
        curtainData[CT_No].extended_state = EX_ST_OPENNED;
      }
    }
    *state = curtainData[CT_No].extended_state;
    if (curtainData[CT_No].destinationPositionToGoTo == DESTINATION_NULL) {
      if (curtainData[CT_No].curtainState == CURTAIN_STATE_CLOSE) {
        *pos_target = 0xFF;
      } else if (curtainData[CT_No].curtainState == CURTAIN_STATE_OPEN) {
        *pos_target = 0;
      } else {
        *pos_target = *pos_present;
      }
    } else {
      *pos_target =
          (uint8_t)(((curtainData[CT_No].destinationPositionToGoTo) * 0xFF) /
                    curtainData[CT_No].curtainLimitTime);
    }
    return 1;
  }
  return 0;
}

/**
 * @func   curtain_variables_init
 * @brief
 * @param  None
 * @retval None
 */
static void curtain_variables_init(void) {
  for (uint8_t i = 0; i < NUMBER_CURTAIN; i++) {
    curtainData[i].enableCalculatorCurtainPosition = false;
    curtainData[i].cmdStep = 0;
    curtainData[i].curtainMode = RUNNING_NORMAL;
    curtainData[i].sendStep = SEND_STEP_NULL;
    curtainData[i].currentCmd = CURTAIN_CMD_IDLE;
    curtainData[i].bufferCmd = CURTAIN_CMD_IDLE;
#ifdef HOLD_FOR_TOUCH
    curtainData[i].enableControlCurtain = false;
#endif
    // Stop Curtain
    curtainData[i].tempDestinationPositionToGoTo = DESTINATION_NULL;
    curtainData[i].destinationPositionToGoTo = DESTINATION_NULL;
    curtain_set_command(i, CURTAIN_CMD_STOP);
    curtainData[i].curtainState = CURTAIN_STATE_STOP;
#ifdef CZ_EXTENDED_STATE
    curtainData[i].extended_state = EX_ST_STOPED;
#endif
  }
  // Restore
  Curtain_RestoreCurrentPosition();
  Curtain_RestoreAllOption();
}

/**
 * @func    curtain_process_function
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_proc(void) {
  if (!curtain_initialized) {
    return;
  }
  for (uint8_t CT_No = 0; CT_No < NUMBER_CURTAIN; CT_No++) {
    // Control Handle
    curtain_handle_command(CT_No);
    // Handle Go To Position
    if (curtainData[CT_No].enableCalculatorCurtainPosition == true) {
      curtain_handle_goto_specified_position(CT_No);
      Curtain_HandleSendLevel(CT_No);
      curtain_handle_min_max_position(CT_No);
    }
  }
  curtain_config_auto_send_proc();
}

/**
 * @func    curtain_callback_init
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_callback_init(typeCurtain_updateCurtainCurrentPosition func) {
  if (func != NULL) {
    pvCurtain_updateCurtainCurrentPosition = func;
  }
}

/**
 * @func    curtain_init
 * @brief
 * @param   None
 * @retval  None
 */
void curtain_init(void) {
  curtain_variables_init();
  net_message_callback_init(curtain_control_curtain_by_app,
                            curtain_get_curret_level);
  curtain_initialized = true;
}

/**
 * @func    curtain_update_binding_para
 * @brief
 * @param
 * @retval  None
 */
static void curtain_update_binding_para(uint8_t model_idx) {
  if (model_idx < ELE_CNT) {
    // Update binding parameters
    cz_binding_par[model_idx].present_level =
        cz_level_st[model_idx].present_level;
    cz_binding_par[model_idx].target_level =
        cz_level_st[model_idx].target_level;
    cz_binding_par[model_idx].state = cz_level_st[model_idx].state;
  }
}

/**
 * @func    curtain_control_binding_group
 * @brief   Note: Update curtain parameter first
 * @param
 * @retval  None
 */
static void curtain_control_binding_group(uint8_t model_idx) {
  if (model_idx < ELE_CNT) {
    u16 binding_adr = get_group_binding_adr(model_idx);
    if (binding_adr != BT_MESH_ADDR_UNASSIGNED) {
      bool en_binding = false;
      vd_cmd_g_level_set_t vd_level_set;
      // CLEAN force control
      if (nwk_message_para[model_idx].dst == binding_adr) {
        force_control_binding[model_idx] = false;
      }
      // POWER ON
      if (cz_binding_par[model_idx].state == ST_UNKNOWN) {
        cz_binding_par[model_idx].state = ST_STOPED;
        return;
      }
      // Check Binding
      if (((nwk_message_para[model_idx].dst != binding_adr) &&
           (nwk_message_para[model_idx].dst < BT_MESH_ADDR_PROXIES)) ||
          (force_control_binding[model_idx] == true)) {
        if (cz_binding_par[model_idx].state != cz_level_st[model_idx].state) {
          if ((cz_level_st[model_idx].state == ST_START_OPEN) ||
              (cz_level_st[model_idx].state == ST_START_CLOSE) ||
              (cz_level_st[model_idx].state == ST_STOP)) {
            en_binding = true;
          }
          if (en_binding == true) {
            curtain_update_binding_para(model_idx);
            // INTERNAL
            foreach (i, ELE_CNT) {
              if (i != model_idx) {
                u16 temp_binding = get_group_binding_adr(i);
                if ((temp_binding != BT_MESH_ADDR_UNASSIGNED) &&
                    (temp_binding == binding_adr)) {
                  // MANUAL CONTROL
                  net_msg_update_control_message_parameter(
                      i, bt_mesh_primary_addr() + model_idx, binding_adr,
                      G_ONOFF_SET);
                  // Send to MCU
                  uint8_t cmd_id = CURTAIN_CONTROL_ID_RUN;
                  if (cz_level_st[model_idx].state == ST_STOP) {
                    cmd_id = CURTAIN_CONTROL_ID_STOP;
                  }
                  curtain_control_curtain_by_app(
                      i, cmd_id, cz_binding_par[model_idx].target_level);
                }
              }
            }
          }
        }
      } else if ((nwk_message_para[model_idx].dst == binding_adr) ||
                 (nwk_message_para[model_idx].dst >= BT_MESH_ADDR_PROXIES)) {
        cz_binding_par[model_idx].state = cz_level_st[model_idx].state;
        cz_binding_par[model_idx].present_level =
            cz_level_st[model_idx].present_level;
        cz_binding_par[model_idx].target_level =
            cz_level_st[model_idx].target_level;
      }
      // Binding control
      if (en_binding == true) {
        uint8_t type = CURTAIN_CONTROL_ID_RUN;
        if (cz_level_st[model_idx].state == ST_STOP) {
          // type = CURTAIN_CMD_STOP;
        }
        vd_level_set.type = type;
        vd_level_set.level = cz_level_st[model_idx].target_level;
        vd_level_set.tid = 0;
        vd_level_set.transit_t = vd_level_set.delay = 0;

        uint8_t len = sizeof(vd_cmd_g_level_set_t);
        uint8_t par[len + 1];
        memcpy((uint8_t*)&par, (uint8_t*)&vd_level_set, len);
        if (force_control_binding[model_idx] == true) {
          par[len] = 0x0;
          len++;
          force_control_binding[model_idx] = false;
        }
        if (last_level_binding[model_idx] != vd_level_set.level) {
          mesh_tx_cmd_rsp(G_LEVEL_SET_NOACK, (uint8_t*)par, len,
                          bt_mesh_primary_addr() + model_idx, binding_adr, 0,
                          0);
        }
      }
    }
  }
}

void curtain_set_opt(uint8_t idx, uint8_t type, uint32_t limit_time_ms) {
  if (idx < ELE_CNT) {
    cz_opt.type[idx] = type;
    cz_opt.limit_time[idx] = limit_time_ms;
    curtainData[idx].curtainLimitTime = limit_time_ms;
    LOG_INF("KNX SYNC: Curtain %d, Type=%d, LimitTime=%u ms", idx, type,
            limit_time_ms);
  }
}
