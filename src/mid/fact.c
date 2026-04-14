/*
 * fact.c
 *
 *  Created on: Sep 30, 2024
 *      Author: DungTranBK
 */
/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "../../include/fact.h"
#include "../../include/fact_common.h"

#include <string.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include "../../include/app_device.h"
#include "../../include/default_network.h"
#include "../../include/fast_provision.h"
#include "../../include/led.h"
#include "../../include/network.h"
#include "../../include/relay.h"
#include "../../include/utilities.h"
#include "../../include/vendor.h"
#include "../../include/vendor_model.h"
#include "mesh/foundation.h"


#ifdef ENABLE_FACT_MID_LOG
LOG_MODULE_REGISTER(fact, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(fact, LOG_LEVEL_NONE);
#endif

extern uint8_t tbl_mac[6]; // defined in main.c
const struct bt_mesh_comp *bt_mesh_comp_get(void);

/******************************************************************************/
/*                              Private variable                              */
/******************************************************************************/
/* ---- Fact Network Keys (same index as default network, different content) --
 */
static const uint8_t fact_net_key[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                         0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
                                         0xdd, 0xee, 0xff, 0x00};
static const uint8_t fact_app_key[16] = {0x01, 0x12, 0x23, 0x34, 0x45, 0x56,
                                         0x67, 0x78, 0x89, 0x9a, 0xab, 0xbc,
                                         0xcd, 0xde, 0xef, 0xf0};
static fact_reset_serials_t fact_rst_serials[] = {
    {1, 15}, {1, 15}, {1, 15}, {1, 15}, {1, 15}, {1, 15},
};

static fact_mode_par_t fact_mode_par = {.is_init = false};

static int8_t s_current_tx_power_dbm = BT_HCI_VS_LL_TX_POWER_LEVEL_NO_PREF;

static uint8_t reset_cnt = 0;

static result_par_t result_par;

// Compare Condition
#define FACT_IS_PASS(x) ((x >= (FACT_TEST_MESSAGE_CNT - 3)) ? 1 : 0)

static fact_push_msg_delay_t fact_push_msg_delay = {.enable = 0};
static fact_par_t fact_par = {.enable = 0, .done = 0};
static fact_test_rf_msg_t fact_rf_msg;

/* Method 2: button-press trigger tracking */
static fact_btn_trigger_t fact_btn_trigger = {.window_active = true};

/* ---- Settings (NVS) replace flash_user_store/restore ---- */
#define FACT_SETTINGS_KEY "fact/rst_cnt"

/******************************************************************************/
/*                                callbacks functions                         */
/******************************************************************************/
typeFact_getKnxTestStatus pvFact_getKnxTestStatus = NULL;
typeFact_handleFactEvent pvFact_handleFactEvent = NULL;
typeFact_handleExitfactMode pvFact_handleExitfactMode = NULL;

/******************************************************************************/
/*                            Private function declarations                   */
/******************************************************************************/
static int fact_check_match_trigger_val(void);
static void fact_increase_reset_cnt(void);
static void fact_clear_reset_cnt(void);
static int fact_reset_cnt_check(void);
static void fact_calculator_result(void);

/******************************************************************************/
/*                               Exported function                            */
/******************************************************************************/
/**
 * @brief Enable fact network:
 *        1. Remove existing subnet if present
 *        2. Full mesh bring-up with fact keys via
 * default_network_enable_with_keys() (includes: bind appkey, set element addr,
 * start mesh)
 */
static void fact_network_enable(void) {
  /* Delete existing subnet (default or previous fact) if present */
  if (bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX)) {
    LOG_INF("fact_network_enable: removing existing subnet 0x%04x",
            DEFAULT_NETWORK_SUBNET_INDEX);
    del_tmp_keys();
  }

  int ret = default_network_enable_with_keys(fact_net_key, fact_app_key);
  if (ret != 0) {
    LOG_ERR("fact_network_enable: failed (err %d)", ret);
  } else {
    LOG_INF("fact_network_enable: fact network ready");
    /* Force set element address - same as default_network_set_temp_element_addr
     */
    extern fast_prov_par_t *g_fast_prov;
    const struct bt_mesh_comp *comp = bt_mesh_comp_get();
    if (comp && comp->elem_count > 0) {
      uint16_t addr = 0;
      if (g_fast_prov && g_fast_prov->mac_addr_info.default_addr != 0) {
        addr = g_fast_prov->mac_addr_info.default_addr;
      } else {
        addr = (tbl_mac[0] + (tbl_mac[1] << 8)) & 0x7FFF;
        if (addr == 0)
          addr = 1;
      }
      for (int i = 0; i < comp->elem_count; i++) {
        if (comp->elem[i].rt) {
          comp->elem[i].rt->addr = addr + i;
        }
      }
      LOG_INF("fact: elem addr=0x%04x", addr);

      /* Bind AppKey to VND_CONFIG_NODE model (handles 0xF8 response) */
      for (int i = 0; i < comp->elem_count; i++) {
        const struct bt_mesh_model *cfg_model =
            bt_mesh_model_find_vnd(&comp->elem[i], VENDOR_COMPANY_ID_TELINK,
                                   BT_MESH_MODEL_ID_VND_CONFIG_NODE);
        if (cfg_model) {
          struct bt_mesh_model *m = (struct bt_mesh_model *)cfg_model;
          if (!bt_mesh_model_has_key(m, DEFAULT_APPKEY_INDEX)) {
            for (int j = 0; j < m->keys_cnt; j++) {
              if (m->keys[j] == BT_MESH_KEY_UNUSED) {
                m->keys[j] = DEFAULT_APPKEY_INDEX;
                LOG_INF("fact: bound AppKey to VND_CONFIG_NODE on elem[%d]", i);
                break;
              }
            }
          }
        }
      }
    }
  }
}

/**
 * @brief Disable fact network and restore default network if not provisioned.
 */
static void fact_network_disable(void) {
  if (bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX)) {
    LOG_INF("fact_network_disable: removing fact subnet");
    del_tmp_keys();
  }
}

/* NOTE: Mesh ext adv handles are managed internally - only SCAN handle is
 * accessible via VS cmd */

static void set_tx_power_one(uint8_t handle_type, uint16_t handle,
                             int8_t power_dbm) {
  struct bt_hci_cp_vs_write_tx_power_level *cp;
  struct bt_hci_rp_vs_write_tx_power_level *rp;
  struct net_buf *buf, *rsp;
  int err;

  buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, sizeof(*cp));
  if (!buf) {
    return;
  }
  cp = net_buf_add(buf, sizeof(*cp));
  cp->handle_type = handle_type;
  cp->handle = handle;
  cp->tx_power_level = power_dbm;

  err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, buf, &rsp);
  if (err) {
    LOG_DBG("set_tx_power: type=%d handle=%d FAILED (err=%d)", handle_type,
            handle, err);
    return; /* handle may not exist, skip silently */
  }
  rp = (void *)rsp->data;
  LOG_INF("set_tx_power: type=%d handle=%d -> selected=%d dBm", handle_type,
          handle, rp->selected_tx_power);
  net_buf_unref(rsp);
}

static void set_tx_power(int8_t power_dbm) {
  /* Apply to all possible Extended ADV set handles (Mesh uses multiple) */
  for (uint16_t h = 0; h < CONFIG_BT_EXT_ADV_MAX_ADV_SET; h++) {
    set_tx_power_one(BT_HCI_VS_LL_HANDLE_TYPE_ADV, h, power_dbm);
  }
  /* Also apply to SCAN handle */
  set_tx_power_one(BT_HCI_VS_LL_HANDLE_TYPE_SCAN, 0, power_dbm);
  s_current_tx_power_dbm = power_dbm;
}

/* Read TX power from the first valid handle found (returns -127 if all fail) */
static int8_t read_tx_power(void) {
  struct bt_hci_cp_vs_read_tx_power_level *cp;
  struct bt_hci_rp_vs_read_tx_power_level *rp;
  struct net_buf *buf, *rsp;
  int err;

  /* 1. Try ADV handles first */
  for (uint16_t h = 0; h < CONFIG_BT_EXT_ADV_MAX_ADV_SET; h++) {
    buf = bt_hci_cmd_create(BT_HCI_OP_VS_READ_TX_POWER_LEVEL, sizeof(*cp));
    if (!buf) {
      continue;
    }
    cp = net_buf_add(buf, sizeof(*cp));
    cp->handle_type = BT_HCI_VS_LL_HANDLE_TYPE_ADV;
    cp->handle = h;
    err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_TX_POWER_LEVEL, buf, &rsp);
    if (err) {
      continue;
    }
    rp = (void *)rsp->data;
    int8_t power = rp->tx_power_level;
    net_buf_unref(rsp);
    return power;
  }

  /* 2. Fallback: try SCAN handle (this one is accessible) */
  buf = bt_hci_cmd_create(BT_HCI_OP_VS_READ_TX_POWER_LEVEL, sizeof(*cp));
  if (buf) {
    cp = net_buf_add(buf, sizeof(*cp));
    cp->handle_type = BT_HCI_VS_LL_HANDLE_TYPE_SCAN;
    cp->handle = 0;
    err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_TX_POWER_LEVEL, buf, &rsp);
    if (!err) {
      rp = (void *)rsp->data;
      int8_t power = rp->tx_power_level;
      net_buf_unref(rsp);
      return power;
    }
  }
  return -127;
}

/**
 * @func    fact_is_activate
 */
uint8_t fact_is_activate(void) { return fact_par.enable; }

/**
 * @func    fact_set
 */
uint8_t fact_set(uint8_t act) {
  if (act) {
    fact_par.enable = 1;
    fact_par.done = 0;
    fact_par.ac_st_time = clock_time_ms();
    fact_par.publish_cnt = 0;
    fact_par.response_cnt = 0;
    fact_par.pub_cnt_before = 0;
    fact_par.active_idx = MAX_U8;
    fact_network_enable(); // swap to fact net/app keys — must before
    // set_tx_power
    set_tx_power(FACT_POWER_DBM); // ADV handles now exist after network enable
    LOG_INF("----------------------------------------------fact_set: act=%d",
            act);
    foreach (i, RELAY_COUNT) {
      relay_control_directly(i, G_OFF);
    }
    fact_par.relay_st = G_OFF;
  } else {
    fact_par.enable = 0;
    set_tx_power(BT_HCI_VS_LL_TX_POWER_LEVEL_NO_PREF);
    fact_network_disable(); // remove fact keys, restore default if needed
  }
  return fact_par.enable;
}

/**
 * @func    fact_handle_config_response
 */
int fact_handle_config_response(uint8_t *par, int par_len, uint16_t src_addr,
                                uint16_t dst_addr) {
  LOG_INF("fact_handle_config_response: src=0x%04x dst=0x%04x len=%d", src_addr,
          dst_addr, par_len);
  LOG_HEXDUMP_INF(par, par_len, "raw data:");

  uint16_t ele_adr_primary = network_get_unicast_address();
  /* In fact mode, device is not provisioned → use elem[0].rt->addr */
  if (ele_adr_primary == 0xFFFF || ele_adr_primary == 0x0000) {
    const struct bt_mesh_comp *comp = bt_mesh_comp_get();
    if (comp && comp->elem_count > 0 && comp->elem[0].rt) {
      ele_adr_primary = comp->elem[0].rt->addr;
    }
  }
  LOG_INF("fact: ele_adr_primary=0x%04x, dst_addr=0x%04x, match=%d",
          ele_adr_primary, dst_addr, (dst_addr == ele_adr_primary));
  if (dst_addr == ele_adr_primary) {
    LOG_HEXDUMP_INF(par, par_len, "par:");
    if (par_len < 3) {
      LOG_ERR("fact: par_len too short: %d", par_len);
      return 0;
    }
    uint8_t resp_cnt = par[0];
    uint8_t *resp_mac = &par[1];
    LOG_INF("fact: resp cnt=%d MAC=%02x:%02x, local MAC=%02x:%02x", resp_cnt,
            resp_mac[0], resp_mac[1], tbl_mac[0], tbl_mac[1]);

    uint8_t match_flag = 1;
    foreach (i, 2) {
      if (resp_mac[i] != tbl_mac[i]) {
        match_flag = 0;
        break;
      }
    }
    // Compare MAC
    if (match_flag) {
      LOG_INF("MAC match");
      if (resp_cnt == fact_par.pub_cnt_before) {
        if (fact_par.active_idx != resp_cnt) {
          fact_par.response_cnt++;
          fact_par.active_idx = resp_cnt;
          LOG_INF("pass: response_cnt=%d", fact_par.response_cnt);
        } else {
          LOG_INF("RETRY...");
        }
      }
    } else {
      LOG_INF("MAC not match");
    }
  } else {
    return -1;
  }
  return 0;
}

/**
 * @func    fact_update_result
 */
void fact_update_result(void) {
  test_rf_result_t test_rf_result;
  /* RF */
  if (FACT_IS_PASS(fact_par.response_cnt)) {
    test_rf_result.result = FACT_PASS;
    fact_mode_par.rf_is_pass = true;
    LOG_INF("FACT_PASS");
  } else {
    test_rf_result.result = FACT_FAIL;
    fact_mode_par.rf_is_pass = false;
    LOG_INF("FACT_FAIL");
  }
  test_rf_result.rssi_value = 0;
  /* KNX */
  if (pvFact_getKnxTestStatus() != NULL) {
    fact_mode_par.knx_is_pass = pvFact_getKnxTestStatus();
  }
}

/**
 * @func    fact_push_message_delay
 */
static void fact_push_message_delay(void) {
  if (fact_push_msg_delay.enable == 1) {
    if (clock_time_exceed_ms(fact_push_msg_delay.st_time,
                             fact_push_msg_delay.delay_time)) {
      fact_par.pub_cnt_before = fact_par.publish_cnt;
      fact_rf_msg.op = VD_FACT_TEST_RF;
      fact_rf_msg.cnt = fact_par.publish_cnt;
      memcpy(fact_rf_msg.MAC, tbl_mac, sizeof(fact_rf_msg.MAC));

      /* Find Fast Provision model on element 0 (bound to DEFAULT_APPKEY_INDEX)
       */
      const struct bt_mesh_comp *comp = bt_mesh_comp_get();
      const struct bt_mesh_model *model = NULL;
      if (comp && comp->elem_count > 0) {
        const struct bt_mesh_elem *elem = &comp->elem[0];
        model = bt_mesh_model_find_vnd(elem, VENDOR_COMPANY_ID_TELINK,
                                       BT_MESH_MODEL_ID_VND_FAST_PROV_SRV);
      }

      if (model) {
        /* Ensure model is bound to fact network AppKey */
        struct bt_mesh_model *m = (struct bt_mesh_model *)model;
        if (!bt_mesh_model_has_key(m, DEFAULT_APPKEY_INDEX)) {
          for (int j = 0; j < m->keys_cnt; j++) {
            if (m->keys[j] == BT_MESH_KEY_UNUSED) {
              m->keys[j] = DEFAULT_APPKEY_INDEX;
              LOG_INF("fact: bound AppKey 0x%04x to model",
                      DEFAULT_APPKEY_INDEX);
              break;
            }
          }
        }

        struct bt_mesh_msg_ctx ctx = {
            .net_idx = DEFAULT_NETWORK_SUBNET_INDEX,
            .app_idx = DEFAULT_APPKEY_INDEX,
            .addr = GATEWAY_UNICAST_ADDR,
            .send_ttl = TTL_FACT,
        };

        LOG_INF("fact: sending TTL=%d dst=0x%04x", ctx.send_ttl, ctx.addr);
        foreach (i, FACT_RETRY_TIME_CNT) {
          NET_BUF_SIMPLE_DEFINE(msg, 64);
          bt_mesh_model_msg_init(&msg, VD_CONFIG_NODE_STATUS);
          net_buf_simple_add_mem(&msg, (uint8_t *)&fact_rf_msg,
                                 sizeof(fact_test_rf_msg_t));
          int err = bt_mesh_model_send(model, &ctx, &msg, NULL, NULL);
          if (err) {
            LOG_ERR("fact bt_mesh_model_send err: %d", err);
          }
        }
      } else {
        LOG_ERR("fact: VND_CONFIG_NODE model not found");
      }

      LOG_INF("PUBLISH TEST MSG: cnt=%d delay=%d ms TTL=%d",
              fact_par.publish_cnt, fact_push_msg_delay.delay_time, TTL_FACT);
      fact_push_msg_delay.enable = 0;
    }
  }
}

/**
 * @func    fact_main_function
 */
uint8_t fact_main_function(void) {
  if (fact_par.enable) {
    /* Select timing based on trigger source */
    uint32_t total_time =
        (fact_mode_par.trigger_source == FACT_TRIGGER_BUTTON_PRESS)
            ? FACT_TOTAL_TIME_LENGTH_FAST
            : FACT_TOTAL_TIME_LENGTH;
    uint32_t publish_interval =
        (fact_mode_par.trigger_source == FACT_TRIGGER_BUTTON_PRESS)
            ? FACT_PUBLISH_TIME_LEN_FAST
            : FACT_PUBLISH_TIME_LEN;

    if (fact_par.done) {
      if (clock_time_exceed_ms(fact_par.ac_st_time, TIMER_5Min)) {
        fact_set(0);
      }
      return fact_par.enable;
    }
    // Push message delay
    if (!FACT_IS_PASS(fact_par.response_cnt)) {
      fact_push_message_delay();
    }
    // Timeout
    if (clock_time_exceed_ms(fact_par.ac_st_time, total_time)) {
      fact_par.done = 1;
      fact_update_result();
      fact_mode_par.active_show_result_flag = 1;

      LOG_INF("response_cnt=%d, pass_threshold=%d (trigger=%d)",
              fact_par.response_cnt, (FACT_TEST_MESSAGE_CNT * 7) / 10,
              fact_mode_par.trigger_source);
    } else {
      uint32_t next_step_time = (fact_par.publish_cnt * publish_interval);
      if (clock_time_exceed_ms(fact_par.ac_st_time, next_step_time)) {
        if (fact_par.publish_cnt <= FACT_TEST_MESSAGE_CNT) {
          // Publish test message
          uint32_t max_rand = (publish_interval > TIMER_500MS)
                                  ? (publish_interval - TIMER_500MS)
                                  : 1;
          fact_push_msg_delay.st_time = clock_time_ms();
          fact_push_msg_delay.delay_time = sys_rand32_get() % max_rand;
          fact_push_msg_delay.enable = 1;
        }
        fact_par.publish_cnt++;
        LOG_INF("*** STEP PUB *** publish_cnt=%d (interval=%u ms)",
                fact_par.publish_cnt, publish_interval);
      }
    }
  }

#ifdef FACT_DBG_EN
  static uint32_t tmp = 0;
  static uint16_t cnt;
  if (clock_time_exceed_ms(tmp, TIMER_1S)) {
    tmp = clock_time_ms();
    LOG_INF("LOOP_CNT=%d enable=%d", cnt++, fact_par.enable);
  }
#endif
  return fact_par.enable;
}

static int fact_settings_set(const char *key, size_t len,
                             settings_read_cb read_cb, void *cb_arg) {
  ARG_UNUSED(key);
  if (len != sizeof(reset_cnt)) {
    return -EINVAL;
  }
  return read_cb(cb_arg, &reset_cnt, sizeof(reset_cnt));
}

SETTINGS_STATIC_HANDLER_DEFINE(fact, "fact", NULL, fact_settings_set, NULL,
                               NULL);

static void fact_reset_cnt_store(void) {
  settings_save_one(FACT_SETTINGS_KEY, &reset_cnt, sizeof(reset_cnt));
}

static void fact_reset_cnt_restore(void) { settings_load_subtree("fact"); }

/**
 * @func   fact_is_active
 */
uint8_t fact_is_active(void) { return fact_mode_par.active_flag; }

/**
 * @func   fact_is_show_result
 */
bool fact_is_show_result(void) { return fact_mode_par.active_show_result_flag; }

/**
 * @func   fact_handle_result  (internal callback from fact_update_result)
 */
static void fact_handle_result(bool result) {
  if (result == FACT_SUCCESS) {
    fact_mode_par.rf_is_pass = true;
  } else {
    fact_mode_par.rf_is_pass = false;
  }
  LOG_INF("fact_handle_result: rf_is_pass=%d", fact_mode_par.rf_is_pass);
}

/**
 * @func   fact_handle_confirm_sucess
 */
static void fact_handle_confirm_sucess(void) {
  fact_mode_par.active_flag = 1;
  fact_mode_par.active_timeout = FACT_ACT_TIMEOUT_MS_DEFAULT;
  fact_mode_par.active_st_tm = clock_time_ms();
  if (pvFact_handleFactEvent != NULL) {
    pvFact_handleFactEvent(FACT_ACTIVATE);
  }
}

/**
 * @func   fact_init
 */
void fact_init(void) {
  memset(&fact_mode_par, 0, sizeof(fact_mode_par_t));
  result_par.led_st = 0;
  // Power On
  fact_reset_cnt_restore();
  if (reset_cnt > RESET_TRIGGER_VAL) {
    reset_cnt = RESET_CNT_INVALID;
    fact_reset_cnt_store();
  }
  fact_mode_par.init_st_tm = clock_time_ms();
  fact_mode_par.is_init = true;

  /* Method 2: init button trigger window */
  fact_btn_trigger.window_active = true;
}

/**
 * @func   fact_notify_config_button_press
 * @brief  Method 2: called by app.c on PRESS_FOUR_TIME event (config button
 *         pressed 4 times). Activates fact mode if within 30s from boot and
 *         device is not provisioned.
 */
void fact_notify_config_button_press(void) {
  /* Guard: window must be active */
  if (!fact_btn_trigger.window_active) {
    return;
  }
  /* Guard: already in fact mode */
  if (fact_par.enable || fact_mode_par.active_flag) {
    return;
  }
  /* Guard: device already provisioned - no fact mode */
  if (network_is_main_network_provisioned()) {
    fact_btn_trigger.window_active = false;
    return;
  }
  /* Guard: 30s window expired */
  if (clock_time_exceed_ms(fact_mode_par.init_st_tm, FACT_BTN_WINDOW_MS)) {
    fact_btn_trigger.window_active = false;
    return;
  }

  /* Activate fact mode via method 2 (PRESS_FOUR_TIME) */
  LOG_INF("fact: config button 4-press detected, activating fast test mode");
  fact_btn_trigger.window_active = false;

  /* Disable provisioning mode if active */
#if EN_PROVISIONING_TOGGLE
  provisioning_stop(false);
#endif

  fact_mode_par.trigger_source = FACT_TRIGGER_BUTTON_PRESS;
  bool confirm = fact_set(true);
  if (confirm) {
    fact_handle_confirm_sucess();
    LOG_INF("GO_TO_TEST_MODE: SUCCESS (BUTTON_PRESS, fast=15s)");
  }
}

/**
 * @func   fact_get_knx_test_status_callback_init
 */
void fact_get_knx_test_status_callback_init(typeFact_getKnxTestStatus func) {
  if (func != NULL) {
    pvFact_getKnxTestStatus = func;
  }
}

/**
 * @func   fact_handle_evt_change_callback_init
 */
void fact_handle_evt_change_callback_init(
    typeFact_handleFactEvent func_handle_event,
    typeFact_handleExitfactMode func_exit) {
  if (func_handle_event != NULL) {
    pvFact_handleFactEvent = func_handle_event;
  }
  if (func_exit != NULL) {
    pvFact_handleExitfactMode = func_exit;
  }
}

/**
 * @func   fact_check_match_trigger_val
 */
static int fact_check_match_trigger_val(void) {
  if (reset_cnt == RESET_TRIGGER_VAL) {
    fact_clear_reset_cnt();
    bool confirm = false;
    if (network_is_main_network_provisioned() == true) {
      fact_set(false);
    } else {
      /* Disable provisioning mode if active */
#if EN_PROVISIONING_TOGGLE
      provisioning_stop(false);
#endif

      fact_mode_par.trigger_source = FACT_TRIGGER_POWER_CYCLE;
      confirm = fact_set(true);
    }
    if (confirm == true) {
      fact_handle_confirm_sucess();
      LOG_INF("GO_TO_TEST_MODE: SUCCESS (POWER_CYCLE)");
    }
    return 0;
  }
  return -1;
}

/**
 * @func   fact_increase_reset_cnt
 */
static void fact_increase_reset_cnt(void) {
  if (++reset_cnt > RESET_TRIGGER_VAL) {
    reset_cnt = RESET_CNT_INVALID;
  }
  fact_reset_cnt_store();
}

/**
 * @func   fact_clear_reset_cnt
 */
static void fact_clear_reset_cnt(void) {
  reset_cnt = RESET_CNT_INVALID;
  fact_reset_cnt_store();
}

/**
 * @func   fact_reset_cnt_check
 */
static int fact_reset_cnt_check(void) {
  static uint8_t clear_st = 2;
  if (0 == clear_st) {
    return 0;
  }
  if ((2 == clear_st) &&
      clock_time_exceed_ms(fact_mode_par.init_st_tm,
                           (uint32_t)fact_rst_serials[reset_cnt].tm_start *
                               1000)) {
    clear_st--;
    int trigger = fact_check_match_trigger_val();
    if (trigger != 0) {
      fact_increase_reset_cnt();
    } else {
      clear_st = 0;
      fact_clear_reset_cnt();
    }
    LOG_INF("***********************\nreset_cnt (trigger check): "
            "%d\n***********************",
            reset_cnt);
  }
  if ((1 == clear_st) &&
      clock_time_exceed_ms(fact_mode_par.init_st_tm,
                           (uint32_t)fact_rst_serials[reset_cnt].tm_stop *
                               1000)) {
    clear_st = 0;
    fact_clear_reset_cnt();
    LOG_INF("###############\nreset_cnt (tm_stop cleared): %d\n###############",
            reset_cnt);
  }
  return 0;
}

/**
 * @func   fact_show_led_result
 *
 * @note   Telink: led_push_led_command_to_fifo with LED_MODE_ON/BLINK/OFF
 *         Nordic: same led_push_led_command_to_fifo API exists
 */
static void fact_show_led_result(void) {
  static uint8_t cnt_show = 0;
  if (cnt_show++ >= 3) {
    result_par.led_st = (result_par.led_st ^ 1) & 1;
    cnt_show = 0;
  } else {
    return;
  }
  led_command_t led_cmd = COMMAND_LED_DEFAULT;
  led_cmd.ledMask = 0xFFFF; // BACKUP_MASK_RL -> all LEDs
  // Led color
  if (result_par.led_st) {
    if (fact_mode_par.rf_is_pass == true || fact_mode_par.knx_is_pass == true) {
      led_cmd.ledMode = LED_MODE_ON;
      led_cmd.blinkTime = 1;
      if (fact_mode_par.rf_is_pass == true &&
          fact_mode_par.knx_is_pass == true) {
        led_cmd.ledColor = LED_COLOR_PINK;
      } else if (fact_mode_par.rf_is_pass == true) {
        led_cmd.ledColor = LED_COLOR_BLUE;
      } else if (fact_mode_par.knx_is_pass == true) {
        led_cmd.ledColor = LED_COLOR_RED;
      }
    } else {
      led_cmd.ledColor = LED_COLOR_RED;
      led_cmd.ledMode = LED_MODE_BLINK;
      led_cmd.blinkInterval = TIMER_200MS; // ~TIMER_150MS
      led_cmd.lastState = LAST_STATE_COLOR_NONE;
      led_cmd.blinkTime = 2;
    }
  } else {
    led_cmd.ledMode = LED_MODE_OFF;
  }
  (void)led_push_led_command_to_fifo(&led_cmd);
}

/**
 * @func   fact_auto_change_relay_state
 */
static void fact_auto_change_relay_state(void) {
  static uint8_t cnt = 0xFE;
  if (cnt++ >= 10) {
    cnt = 0;
  } else {
    return;
  }
  fact_par.relay_st = (fact_par.relay_st ^ 1) & 1;
  for (int i = 0; i < RELAY_COUNT; i++) {
    relay_control_directly(i, fact_par.relay_st);
  }
}

/**
 * @func   fact_handle
 */
void fact_handle(void) {
  if (fact_mode_par.is_init == false) {
    return;
  }

  /* Method 2: close button window after 30s from boot */
  if (fact_btn_trigger.window_active && !fact_par.enable) {
    if (clock_time_exceed_ms(fact_mode_par.init_st_tm, FACT_BTN_WINDOW_MS)) {
      fact_btn_trigger.window_active = false;
      LOG_INF("fact: button trigger window closed (30s elapsed)");
    }
  }

  fact_main_function();
  if (!clock_time_exceed_ms(0, fact_rst_serials[0].tm_start)) {
    return;
  }
  // calculator reset counter
  fact_reset_cnt_check();
#if FACT_TTL_AND_TX_DBG_EN
  fact_debug_test_ttl_rf_power();
#endif
  // active
  if (fact_mode_par.active_flag) {
    if (fact_mode_par.active_show_result_flag) {

      if (clock_time_exceed_ms(fact_mode_par.active_st_tm,
                               FACT_ACT_TIMEOUT_MS_DEFAULT)) {
        fact_mode_par.active_flag = 0;
        LOG_INF("*** FACT TIMEOUT ***");
        if (pvFact_handleExitfactMode != NULL) {
          pvFact_handleExitfactMode();
        }
        led_refresh(0xFFFF);
      }
      if (fact_mode_par.active_flag == 1) {
        // Show result
        if (clock_time_exceed_ms(result_par.toggle_st_time,
                                 FACT_TOGGLE_INTERVAL)) {
          fact_show_led_result();
          fact_auto_change_relay_state();
          result_par.toggle_st_time = clock_time_ms();
        }
      }
    }
  }
}

#if FACT_TTL_AND_TX_DBG_EN
/**
 * @func   fact_debug_test_ttl_rf_power
 * @brief  Debug function: toggle TTL=0/6 and TX power low/high every 10s
 *         Call this periodically (e.g. from main loop or a k_work)
 */
void fact_debug_test_ttl_rf_power(void) {
  static uint32_t last_t = 0;
  static uint8_t test_on = 0;

  if (!clock_time_exceed_ms(last_t, TIMER_2S)) {
    return;
  }
  last_t = clock_time_ms();

#if 0
  test_on ^= 1;

  if (test_on) {
    /* --- Enter test mode --- */
    bt_mesh_default_ttl_set(0);
    set_tx_power(FACT_POWER_DBM);
  } else {
    /* --- Restore normal mode --- */
    bt_mesh_default_ttl_set(TTL_DEFAULT);
    set_tx_power(BT_HCI_VS_LL_TX_POWER_LEVEL_NO_PREF);
    s_current_tx_power_dbm = BT_HCI_VS_LL_TX_POWER_LEVEL_NO_PREF;
  }
#endif

  /* Read back actual values to verify */
  uint8_t actual_ttl = bt_mesh_default_ttl_get();

  if (s_current_tx_power_dbm == BT_HCI_VS_LL_TX_POWER_LEVEL_NO_PREF) {
    LOG_INF("[FACT_DBG] %s | TTL(actual)=%d | TX_PWR(set)=NO_PREF(HW default)",
            fact_par.enable ? "TEST MODE ON " : "NORMAL MODE  ", actual_ttl);
  } else {
    LOG_INF("[FACT_DBG] %s | TTL(actual)=%d | TX_PWR(set)=%d dBm",
            fact_par.enable ? "TEST MODE ON " : "NORMAL MODE  ", actual_ttl,
            s_current_tx_power_dbm);
  }
}
#endif /* FACT_DBG_EN */
