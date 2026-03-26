/*
 * sw_binding.c
 *
 * Ported from Telink ONE_WIRE_SWITCH sw_binding.c to Nordic nRF Connect SDK.
 *
 * API Mapping:
 *   flash_read_page / flash_write_page / flash_erase_sector
 *       → Zephyr settings subsystem (settings_save_one / settings_load)
 *   model_sig_g_onoff_level.onoff_srv[idx].com.sub_list
 *       → bt_mesh_model_find() + iterating model->groups[]
 *   ele_adr_primary + model_idx
 *       → bt_mesh_primary_addr() + model_idx
 *   ADR_UNASSIGNED
 *       → BT_MESH_ADDR_UNASSIGNED (0x0000)
 *   mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, ...)
 *       → mesh_tx_cmd_rsp (already ported in vendor_model.c)
 *   send_led_evt_to_mcu / send_scene_led_evt_to_mcu
 *       → sw_blink_led_config / sw_blink_led_scene_config (in sw_config.c)
 *
 *  Created on: Jan 28, 2021
 *      Author: DungTranBK
 */

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "../../include/sw_binding.h"

#include <string.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "../../include/app_device.h"
#include "../../include/led.h"
#include "../../include/utilities.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(sw_binding, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

binding_para_t binding_para_st[ELE_CNT] = {
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#if ELE_CNT > 1
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#endif
#if ELE_CNT > 2
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#endif
#if ELE_CNT > 3
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#endif
#if ELE_CNT > 4
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#endif
#if ELE_CNT > 5
    {.en = false, .group_dst = BT_MESH_ADDR_UNASSIGNED},
#endif
};

control_binding_t control_binding_st[ELE_CNT];

u8 force_control_binding[ELE_CNT] = {
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

/******************************************************************************/
/*                         SETTINGS SUBSYSTEM (NVS)                          */
/******************************************************************************/

#define SW_BINDING_SETTINGS_KEY "sw_bind/para"

/**
 * @brief Save binding_para_st to NVS via settings subsystem.
 *        Equivalent to Telink flash_write_page(FLASH_ADR_BINDING_PARAMS, ...).
 */
static void store_binding_para(void) {
  int rc = settings_save_one(SW_BINDING_SETTINGS_KEY, &binding_para_st,
                             sizeof(binding_para_st));
  if (rc != 0) {
    LOG_ERR("store_binding_para: settings_save_one failed (%d)", rc);
  } else {
    LOG_DBG("store_binding_para: saved");
  }
}

/**
 * @brief Reset binding_para_st to defaults and persist.
 *        Equivalent to Telink store_default_binding_params().
 */
static void store_default_binding_params(void) {
  foreach_arr(i, binding_para_st) {
    binding_para_st[i].en = false;
    binding_para_st[i].group_dst = BT_MESH_ADDR_UNASSIGNED;
  }
  store_binding_para();
}

/**
 * @brief Settings handler — called by settings_load() during init.
 *        Equivalent to Telink restore_binding_para().
 */
static int sw_binding_settings_set(const char *name, size_t len,
                                   settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  if (settings_name_steq(name, "para", &next) && !next) {
    if (len != sizeof(binding_para_st)) {
      LOG_WRN("sw_binding: size mismatch, resetting to defaults");
      store_default_binding_params();
      return -EINVAL;
    }
    int rc = read_cb(cb_arg, &binding_para_st, sizeof(binding_para_st));
    if (rc < 0) {
      LOG_ERR("sw_binding: read_cb failed (%d)", rc);
      return rc;
    }
    /* Validate: if any entry is corrupted (0xFF from erased flash), reset */
    foreach_arr(i, binding_para_st) {
      if (binding_para_st[i].en == 0xFF) {
        LOG_WRN("sw_binding: corrupted NVS, resetting to defaults");
        store_default_binding_params();
        return 0;
      }
      if ((binding_para_st[i].group_dst >= ADR_FIXED_GROUP_START) ||
          ((binding_para_st[i].group_dst < ADR_GROUP_START_POINT) &&
           (binding_para_st[i].group_dst != BT_MESH_ADDR_UNASSIGNED))) {
        LOG_WRN("sw_binding: invalid group_dst[%d]=0x%04x, resetting", i,
                binding_para_st[i].group_dst);
        store_default_binding_params();
        return 0;
      }
    }
    LOG_DBG("sw_binding: parameters restored from NVS");
  }
  return 0;
}

static struct settings_handler sw_binding_settings_hdlr = {
    .name = "sw_bind",
    .h_set = sw_binding_settings_set,
};

/******************************************************************************/
/*                          PRIVATE FUNCTIONS                                 */
/******************************************************************************/

/**
 * @brief Initialize control_binding_st to defaults.
 */
static void control_binding_init(void) {
  foreach_arr(i, control_binding_st) {
    control_binding_st[i].st = G_ONOFF_RSV;
    control_binding_st[i].cnt_same = 0;
    control_binding_st[i].op = G_ONOFF_SET;
  }
}

/**
 * @brief LED feedback: blink relay LED (blue).
 */
void sw_blink_led_config(uint16_t mask, uint8_t status) {
  uint8_t cmd_led = (status == SUCCESS) ? CMD_BLINK_BLUE : CMD_BLINK_RED;
  led_blink(mask, cmd_led, 2, LAST_STATE_REFRESH_LED, 250);
}

/**
 * @brief Check whether group_adr is in the subscription list of the GenOnOff
 *        server model for element model_idx.
 *        Replaces Telink's model_sig_g_onoff_level.onoff_srv[idx].com.sub_list
 * check.
 * @param model_idx  Element index
 * @param group_adr  Group address to check
 * @return true if subscribed, false otherwise
 */
static bool is_group_subscribed(int model_idx, u16 group_adr) {
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp || model_idx >= (int)comp->elem_count) {
    return false;
  }
  const struct bt_mesh_elem *elem = &comp->elem[model_idx];
  for (int m = 0; m < elem->model_count; m++) {
    if (elem->models[m].id == BT_MESH_MODEL_ID_GEN_LEVEL_SRV) {
      /* Check all subscription slots */
      for (int s = 0; s < elem->models[m].groups_cnt; s++) {
        if (elem->models[m].groups[s] == group_adr) {
          return true;
        }
      }
      break;
    }
  }
  return false;
}

/**
 * @brief Enable binding for a given element to a group address.
 *        Validates that the model is subscribed to the group.
 *        Ported from Telink binding_enable().
 * @param model_idx  Element index
 * @param group_adr  Group address to bind to
 * @return BINDING_SUCCESS, GROUP_NOT_SET, or MODEL_IDX_INVALID
 */
static int binding_enable(int model_idx, u16 group_adr) {
  if (model_idx >= NUMBER_INPUT) {
    return MODEL_IDX_INVALID;
  }

  if (!is_group_subscribed(model_idx, group_adr)) {
    LOG_WRN("binding_enable: idx=%d group 0x%04x not subscribed", model_idx,
            group_adr);
    return GROUP_NOT_SET;
  }

  binding_para_st[model_idx].group_dst = group_adr;
  binding_para_st[model_idx].en = true;
  store_binding_para();
  LOG_INF("binding_enable: idx=%d → group 0x%04x OK", model_idx, group_adr);
  return BINDING_SUCCESS;
}

/**
 * @brief Disable binding for a given element.
 *        Ported from Telink binding_disable().
 */
static int binding_disable(int model_idx) {
  binding_para_st[model_idx].en = false;
  store_binding_para();
  LOG_INF("binding_disable: idx=%d", model_idx);
  return 0;
}

/**
 * @brief Send binding config status response to the gateway.
 *        Ported from Telink binding_handle_setup_message() response block.
 */
static void send_binding_response(int model_idx, int st, u16 src_ele_adr) {
  binding_msg_response_t rsp;
  rsp.op = VD_CONFIG_GROUP_ASSOCIATION;
  rsp.st = (u8)st;
  rsp.ele_adr = src_ele_adr;
  rsp.en = binding_para_st[model_idx].en;
  rsp.group_adr = binding_para_st[model_idx].group_dst;
  mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (u8 *)&rsp, sizeof(rsp), src_ele_adr,
                  GATEWAY_UNICAST_ADDR, 0, 0);
}

/**
 * @brief Handle binding SET message: enable or disable binding.
 *        Ported from Telink binding_handle_setup_message().
 * @param cfg  Pointer to cfg_binding_format_t parsed from vendor message
 */
static void binding_handle_setup_message(cfg_binding_format_t *cfg) {
  int model_idx = cfg->ele_adr - bt_mesh_primary_addr();
  int st = BINDING_ERR_UNKNOWN;

  if (model_idx < 0 || model_idx >= NUMBER_INPUT) {
    LOG_WRN("binding_handle_setup: invalid model_idx=%d", model_idx);
    return;
  }

  if (cfg->en == ENABLE) {
    st = binding_enable(model_idx, cfg->group_adr);
    /* LED feedback */
    u16 led_mask = 1U << (model_idx);
    if (st == BINDING_SUCCESS) {
      sw_blink_led_config(led_mask, SUCCESS);
      LOG_INF("Binding enable success");
    } else {
      sw_blink_led_config(led_mask, FAILURE);
      LOG_INF("Binding enable failed");
    }
  } else if (cfg->en == DISABLE) {
    binding_disable(model_idx);
    st = BINDING_DISABLE;
    sw_blink_led_config(1U << model_idx, FAILURE);
  }

  send_binding_response(model_idx, st, cfg->ele_adr);
}

/**
 * @brief Handle binding GET message: report current binding status.
 *        Ported from Telink binding_handle_get_message().
 * @param msg  Pointer to get_binding_format_t parsed from vendor message
 */
static void binding_handle_get_message(get_binding_format_t *msg) {
  int model_idx = msg->ele_adr - bt_mesh_primary_addr();

  if (model_idx < 0 || model_idx >= NUMBER_INPUT) {
    LOG_WRN("binding_handle_get: invalid model_idx=%d", model_idx);
    return;
  }

  binding_msg_response_t rsp;
  rsp.op = VD_CONFIG_GROUP_ASSOCIATION;
  rsp.st = BINDING_SUCCESS;
  rsp.ele_adr = msg->ele_adr;

  if (binding_para_st[model_idx].en == true) {
    /* Validate the group is still in subscription list */
    if (get_group_binding_adr(model_idx) == BT_MESH_ADDR_UNASSIGNED) {
      rsp.en = false;
    } else {
      rsp.en = true;
    }
  } else {
    rsp.en = false;
  }

  rsp.group_adr = binding_para_st[model_idx].group_dst;

  mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (u8 *)&rsp, sizeof(rsp), msg->ele_adr,
                  GATEWAY_UNICAST_ADDR, 0, 0);
  LOG_DBG("binding_get_response: idx=%d, en=%d, grp=0x%04x", model_idx, rsp.en,
          rsp.group_adr);
}

/******************************************************************************/
/*                          EXPORT FUNCTIONS                                  */
/******************************************************************************/

/**
 * @func    get_group_binding_adr
 * @brief   Get the binding group address for element idx.
 *          Validates that element's GenOnOff model is subscribed to that group.
 *          Ported from Telink get_group_binding_adr().
 * @param   idx  Element index
 * @return  Binding group address if valid, BT_MESH_ADDR_UNASSIGNED otherwise
 */
uint16_t get_group_binding_adr(int idx) {
  if (idx < 0 || idx >= ELE_CNT) {
    return BT_MESH_ADDR_UNASSIGNED;
  }
  if (binding_para_st[idx].en == true) {
    /* Validate: group must still be in the subscription list */
    u16 grp = binding_para_st[idx].group_dst;
    if (is_group_subscribed(idx, grp)) {
      return grp;
    }
  }
  return BT_MESH_ADDR_UNASSIGNED;
}

/**
 * @func    binding_init
 * @brief   Initialize binding module: restore from NVS and init state.
 *          Ported from Telink binding_init().
 */
void binding_init(void) {
  int rc = settings_subsys_init();
  if (rc != 0) {
    LOG_ERR("binding_init: settings_subsys_init failed (%d)", rc);
  }
  rc = settings_register(&sw_binding_settings_hdlr);
  if (rc != 0) {
    LOG_ERR("binding_init: settings_register failed (%d)", rc);
  }
  settings_load_subtree(sw_binding_settings_hdlr.name);
  control_binding_init();
  LOG_INF("binding_init: done");
}

/**
 * @func    binding_handle_nw_message
 * @brief   Dispatch incoming vendor message for group binding.
 *          Called from the vendor model's message handler.
 *          Ported from Telink binding_handle_nw_message().
 * @param   type  CONFIG_NODE_GET or CONFIG_NODE_SET
 * @param   par   Payload buffer
 */
void binding_handle_nw_message(u8 type, u8 *par) {
  if (type == CONFIG_NODE_GET) {
    get_binding_format_t *get_fmt = (get_binding_format_t *)par;
    binding_handle_get_message(get_fmt);
  } else if (type == CONFIG_NODE_SET) {
    cfg_binding_format_t *cfg = (cfg_binding_format_t *)par;
    binding_handle_setup_message(cfg);
  } else {
    LOG_WRN("binding_handle_nw_message: unknown type=0x%02x", type);
  }
}

// End file
