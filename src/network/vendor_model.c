
#include "../../include/vendor_model.h"

#include <bluetooth/mesh/scene.h>
#include <bluetooth/mesh/scene_srv.h> /* Standard Scene Server */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> /* Fix rand() */
#include <string.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/bluetooth/mesh/main.h>
#include <zephyr/bluetooth/mesh/msg.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "../../include/app_device.h" /* For Device Type - Renamed to avoid conflict */
#include "../../include/curtain.h"
#include "../../include/fast_provision.h"
#include "../../include/led_ev.h" /* Fix send_led_evt_to_mcu */
#include "../../include/net_message.h"
#include "../../include/network.h" /* For network_get_provisioner_address, etc. */
#include "../../include/normal_provision.h"
#include "../../include/sw_binding.h"
#include "../../include/timestamp.h"
#include "../../include/utilities.h" /* For common definitions */
#include "../../include/vendor.h" /* For Vendor Company ID and other common definitions */
#include "../../include/vendor_model.h"
#include "mesh/access.h" /* For bt_mesh_model_sub_store() */

/* Fix for implicit declaration if hidden in system headers */
const struct bt_mesh_comp *bt_mesh_comp_get(void);

LOG_MODULE_REGISTER(vendor_model, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                          TX COMPLETION GATE                                */
/******************************************************************************/

/**
 * Semaphore-based TX flow control.
 * Ensures previous bt_mesh_model_send() completes before the next one starts.
 * Initial count = 1 (available), max count = 1.
 */
static K_SEM_DEFINE(tx_sem, 1, 1);

/** TX completion timeout (ms). Prevents deadlock if callback never fires. */
#define TX_COMPLETION_TIMEOUT_MS 200

static void mesh_tx_end_cb(int err, void *cb_data) {
  if (err) {
    LOG_WRN("TX end callback error: %d", err);
  }
  k_sem_give(&tx_sem);
}

static const struct bt_mesh_send_cb mesh_tx_send_cb = {
    .start = NULL,
    .end = mesh_tx_end_cb,
};

/**
 * @brief Wait for previous TX to complete, with timeout and deadlock recovery.
 */
static void tx_gate_acquire(void) {
  if (k_sem_take(&tx_sem, K_MSEC(TX_COMPLETION_TIMEOUT_MS)) != 0) {
    LOG_WRN("TX gate timeout (%d ms), forcing release",
            TX_COMPLETION_TIMEOUT_MS);
    k_sem_reset(&tx_sem);
    k_sem_give(&tx_sem);
    k_sem_take(&tx_sem, K_NO_WAIT);
  }
}

/**
 * @brief Release TX gate when send fails (callback won't fire).
 */
static void tx_gate_release(void) { k_sem_give(&tx_sem); }

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/
#if LM_SUB_GROUP_ENABLE
bool vd_config_model_sub_set_flag = 0;

const uint16_t sig_model_share_sub_adr[] = {
    BT_MESH_MODEL_ID_GEN_LEVEL_SRV,
};
#endif

static vendor_handle_func_t ptr_vendor_handle_func = NULL;

void vendor_handle_func_callback_init(vendor_handle_func_t func) {
  ptr_vendor_handle_func = func;
}

/******************************************************************************/
/*                               FUNCTIONS                                    */
/******************************************************************************/

static inline uint16_t get_opcode(uint32_t model_op) {
  return (uint16_t)(model_op & 0xFF);
}

/* Global model pointer captured from handlers */
static const struct bt_mesh_model *global_config_model;

/**
 * @brief Helper to capture model instance
 */
static void capture_model(const struct bt_mesh_model *model) {
  if (!global_config_model) {
    global_config_model = model;
  }
}

static int handle_vd_config_node_set_ack(const struct bt_mesh_model *model,
                                         struct bt_mesh_msg_ctx *ctx,
                                         struct net_buf_simple *buf) {
  int err = 0;
  capture_model(model);
  LOG_INF("VD_CONFIG_NODE_SET_ACK (0xF8) received from 0x%04X", ctx->addr);

  if (!model) {
    LOG_ERR("Invalid model");
    return -1;
  }
  vd_config_node_ack_t *config_node = (vd_config_node_ack_t *)buf->data;
  /* Derive element index from the element address that received this msg */
  uint16_t primary_addr = network_get_unicast_address();
  const struct bt_mesh_elem *elem =
      bt_mesh_model_elem((struct bt_mesh_model *)model);
  int model_idx = (elem && primary_addr != 0xFFFF)
                      ? (int)(elem->rt->addr - primary_addr)
                      : 0;
  /* Payload length = buf total minus the 2-byte header (msg_type + code) */
  int data_len = (int)buf->len - 2;
  if (data_len < 0) {
    data_len = 0;
  }
  if (ctx->recv_dst < ADR_FIXED_GROUP_START) {
    if (config_node->msg_type == CONFIG_NODE_SET) {
      switch (config_node->code) {
      case VD_CONFIG_GROUP_ASSOCIATION:
        binding_handle_nw_message(CONFIG_NODE_SET, config_node->data);
        break;
      case VD_CONFIG_TIMESTAMP:
        timestamp_set(model_idx, config_node->data, data_len);
        break;
      default:
        err = curtain_cfg_handle_set_message(model_idx, config_node->data,
                                             data_len, config_node->code, true);
        break;
      }
    } else if (config_node->msg_type == CONFIG_NODE_GET) {
      switch (config_node->code) {
      case VD_CONFIG_GROUP_ASSOCIATION:
        binding_handle_nw_message(CONFIG_NODE_GET, config_node->data);
        break;
      default:
        err = curtain_cfg_handle_get_message(model_idx, config_node->data,
                                             data_len, config_node->code);
        break;
      }
    }
    if (err) {
      LOG_WRN("sw_cfg handler returned %d (code=0x%02X)", err,
              config_node->code);
    }
  } else {
    LOG_INF("handle_vd_config_fixed_group_logic");
    timestamp_set(model_idx, config_node->data, (int)buf->len - 2);
  }
  return 0;
}

/**
 * @brief Send VD_CONFIG_NODE_STATUS (0xF9)
 *
 * @param src_addr Source element address
 * @param dst_addr Destination address (0 for publish)
 * @param data     Payload data
 * @param len      Payload length
 * @return 0 on success
 */
int vendor_model_send_config_status(uint16_t src_addr, uint16_t dst_addr,
                                    uint8_t *data, size_t len) {
  /* Find the correct model by element address */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    LOG_ERR("No composition data");
    return -ENODEV;
  }

  /* Calculate element index from source address */
  uint16_t primary_addr = network_get_unicast_address();
  int elem_idx = src_addr - primary_addr;
  if (elem_idx < 0 || elem_idx >= comp->elem_count) {
    LOG_ERR("Invalid element index: %d", elem_idx);
    return -EINVAL;
  }

  /* Find vendor model in the element */
  const struct bt_mesh_elem *elem = &comp->elem[elem_idx];
  const struct bt_mesh_model *model = NULL;
  for (int i = 0; i < elem->vnd_model_count; i++) {
    if (elem->vnd_models[i].vnd.id == BT_MESH_MODEL_ID_VND_CONFIG_NODE) {
      model = &elem->vnd_models[i];
      break;
    }
  }

  if (!model) {
    LOG_ERR("Vendor model not found on element %d", elem_idx);
    return -ENODEV;
  }

  struct bt_mesh_msg_ctx ctx = {
      .addr = dst_addr,
      .app_idx = 0,
      .send_ttl = BT_MESH_TTL_DEFAULT,
  };

  NET_BUF_SIMPLE_DEFINE(msg, 64);
  bt_mesh_model_msg_init(&msg, VD_CONFIG_NODE_STATUS);
  net_buf_simple_add_mem(&msg, data, len);

  tx_gate_acquire();
  int err = bt_mesh_model_send(model, &ctx, &msg, &mesh_tx_send_cb, NULL);
  if (err) {
    LOG_ERR("Failed to send config status: %d", err);
    tx_gate_release();
  }
  return err;
}

/* ============================================================================
 * Handler Binding Locally
 * ============================================================================
 */

/* ============================================================================
 * Handler for Light Opt Get, Status
 * ============================================================================
 */
/**
 * @func   cb_vd_dev_opt_set
 * @brief  None
 * @param
 * @retval
 */
static int cb_vd_device_opt_status(const struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf) {
  return 0;
}

/**
 * @func   cb_vd_dev_opt_get
 * @brief  None
 * @param
 * @retval
 */
typedef struct {
  u8 device_uuid[16];
  u8 mac[6];
  u16 product_id;
} light_opt_t;

static int cb_vd_device_opt_get(const struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct net_buf_simple *buf) {
  LOG_INF("\n cb_vd_dev_opt_get");
  int err = -1;
  uint8_t mac[6];
  if (fast_provision_get_mac_for_bt_id(mac)) {
    light_opt_t rsp;
    memcpy(&rsp.device_uuid, get_device_uuid(), 16);
    memcpy(&rsp.mac, &mac, 6);
    rsp.product_id = LM_PID_MESH;
    err = mesh_tx_cmd_rsp(VD_LIGHT_OPT_STATUS, (u8 *)&rsp, sizeof(rsp),
                          network_get_unicast_address(), ctx->addr, 0, 0);
  }
  return err;
}

/* ============================================================================
 * Handler for Light Status Get, Status
 * ============================================================================

/**
 * @func   cb_vd_light_state_status
 * @brief  None
 * @param
 * @retval
 */
int cb_vd_light_state_status(const struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx,
                             struct net_buf_simple *buf) {
  return 0;
}
/**
 * @func   cb_vd_light_state_get
 * @brief  None
 * @param
 * @retval
 */
int cb_vd_light_state_get(const struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct net_buf_simple *buf) {
  int model_idx = bt_mesh_model_elem(model) - bt_mesh_comp_get()->elem;
  if (model_idx < 0 || model_idx >= ELE_CNT) {
    return -EINVAL;
  }
  if (ctx->recv_dst < ADR_GROUP_START_POINT) {
    net_message_publish_status_delay(model_idx, 0);
  } else {
    net_message_publish_status_delay(model_idx, rand() % TIMER_30S);
  }
  return 0;
}

/* ============================================================================
 * Handler for Connfiguration common parameters
 * ============================================================================
 */

/**
 * @brief Handler for VD_CONFIG_NODE_STATUS (0xF9)
 *
 * @param model Model instance
 * @param ctx Message context
 * @param buf Message buffer containing payload
 * @return 0 on success, error code otherwise
 */
static int handle_vd_config_node_status(const struct bt_mesh_model *model,
                                        struct bt_mesh_msg_ctx *ctx,
                                        struct net_buf_simple *buf) {
  return 0;
}

/**
 * @brief Handler: VD_CONFIG_NODE_SET_NOACK (0xF0) - Factory Reset
 *
 * @details
 * This handler receives Factory Reset message from provisioner.
 * - Requests device factory reset and leave network
 * - Can be received from main network (when already provisioned)
 * - Unacknowledged message (no response)
 *
 * @note
 * This handler calls factory_reset_and_reboot() to reset mesh stack and reboot
 * chip
 */
static int cb_vd_config_node_set_noack(const struct bt_mesh_model *model,
                                       struct bt_mesh_msg_ctx *ctx,
                                       struct net_buf_simple *buf) {
  capture_model(model);
  LOG_INF("VD_CONFIG_NODE_SET_NOACK (0xF0) received from 0x%04X", ctx->addr);

  if (ctx->addr != GATEWAY_UNICAST_ADDR) {
    LOG_WRN("Ignored VD_CONFIG_NODE_SET_NOACK from non-gateway address 0x%04X",
            ctx->addr);
    return -EACCES;
  }

  uint16_t primary_addr = network_get_unicast_address();
  int elem_idx = bt_mesh_model_elem((struct bt_mesh_model *)model)->rt->addr -
                 primary_addr;

  if (elem_idx != 0) {
    LOG_WRN("Ignored VD_CONFIG_NODE_SET_NOACK on non-primary element %d",
            elem_idx);
    return -EINVAL;
  }

  uint8_t *par = buf->data;
  if (buf->len < 1) {
    LOG_WRN("Invalid payload length: %d", buf->len);
    return -EINVAL;
  }

  vd_config_node_t *vd_config_node = (vd_config_node_t *)par;

  if (vd_config_node->code == VD_CONFIG_NODE_RST) {
    LOG_INF("VD_CONFIG_NODE_RST: Factory Reset");
    /* Then call factory reset and reboot */
    factory_reset_and_reboot();

  } else if (vd_config_node->code == VD_CONFIG_SET_TTL) {
    LOG_INF("VD_CONFIG_SET_TTL");
    /* TODO: Implement TTL Set logic if needed, mapping to cfg_srv */
    /* Telink calls mesh_cmd_sig_cfg_def_ttl_set */

  } else if (vd_config_node->code == VD_CONFIG_GET_TTL) {
    LOG_INF("VD_CONFIG_GET_TTL");
    /* TODO: Implement TTL Get logic if needed, mapping to cfg_srv */
    /* Telink calls mesh_cmd_sig_cfg_def_ttl_get */

  } else {
    LOG_INF("Config Node Reserved Code: 0x%02X", vd_config_node->code);
  }

  return 0;
}

#if LM_SUB_GROUP_ENABLE
/* ============================================================================
 * Config Model Subscription Handlers
 * ============================================================================
 */

/* Subscription Management Helpers */

static int vd_manual_handle_sub_delete(uint16_t ele_adr, uint16_t model_id,
                                       uint16_t sub_addr) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return -1;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return -1;
  }

  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model =
      (struct bt_mesh_model *)bt_mesh_model_find(elem, model_id);

  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == sub_addr) {
        model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
        if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
          bt_mesh_model_sub_store(model);
        }
        return 0;
      }
    }
  }
  return -1;
}

static void vd_manual_handle_sub_add(uint16_t ele_adr, uint16_t model_id,
                                     uint16_t sub_addr) {
  LOG_INF("vd_manual_handle_sub_add: %d, %d, %d", ele_adr, model_id, sub_addr);
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }
  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model =
      (struct bt_mesh_model *)bt_mesh_model_find(elem, model_id);

  if (model) {
    /* Check if already exists */
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == sub_addr) {
        return; /* Already exists */
      }
    }
    /* Find empty slot */
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
        model->groups[i] = sub_addr;
        /* Trigger storage */
        if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
          bt_mesh_model_sub_store(model);
          LOG_INF("Subscribed to group 0x%04X", sub_addr);
        } else {
          LOG_INF("Failed to subscribe to group 0x%04X", sub_addr);
        }
        return;
      }
    }
    LOG_INF("Failed to subscribe to group 0x%04X", sub_addr);
  }
}

static void vd_manual_handle_sub_overwrite(uint16_t ele_adr, uint16_t model_id,
                                           uint16_t sub_addr) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }

  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model =
      (struct bt_mesh_model *)bt_mesh_model_find(elem, model_id);

  if (model) {
    /* Clear all groups first */
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
    }
    /* Add new group */
    model->groups[0] = sub_addr;
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
      bt_mesh_model_sub_store(model);
    }
  }
}

static void vd_manual_handle_sub_del_all(uint16_t ele_adr, uint16_t model_id) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }

  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model =
      (struct bt_mesh_model *)bt_mesh_model_find(elem, model_id);

  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
    }
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
      bt_mesh_model_sub_store(model);
    }
  }
}

static void vd_manual_handle_sub_add_vnd(uint16_t ele_adr, uint16_t company_id,
                                         uint16_t model_id, uint16_t sub_addr) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }
  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model = (struct bt_mesh_model *)bt_mesh_model_find_vnd(
      elem, company_id, model_id);
  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == sub_addr) {
        return;
      }
    }
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == BT_MESH_ADDR_UNASSIGNED) {
        model->groups[i] = sub_addr;
        if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
          bt_mesh_model_sub_store(model);
        }
        return;
      }
    }
  }
}

static void vd_manual_handle_sub_del_vnd(uint16_t ele_adr, uint16_t company_id,
                                         uint16_t model_id, uint16_t sub_addr) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }
  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model = (struct bt_mesh_model *)bt_mesh_model_find_vnd(
      elem, company_id, model_id);
  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      if (model->groups[i] == sub_addr) {
        model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
        if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
          bt_mesh_model_sub_store(model);
        }
        return;
      }
    }
  }
}

static void vd_manual_handle_sub_overwrite_vnd(uint16_t ele_adr,
                                               uint16_t company_id,
                                               uint16_t model_id,
                                               uint16_t sub_addr) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }
  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model = (struct bt_mesh_model *)bt_mesh_model_find_vnd(
      elem, company_id, model_id);
  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
    }
    model->groups[0] = sub_addr;
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
      bt_mesh_model_sub_store(model);
    }
  }
}

static void vd_manual_handle_sub_del_all_vnd(uint16_t ele_adr,
                                             uint16_t company_id,
                                             uint16_t model_id) {
  uint16_t primary_addr = network_get_unicast_address();
  if (ele_adr < primary_addr) {
    return;
  }
  uint8_t elem_idx = ele_adr - primary_addr;
  if (elem_idx >= bt_mesh_comp_get()->elem_count) {
    return;
  }
  const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[elem_idx];
  struct bt_mesh_model *model = (struct bt_mesh_model *)bt_mesh_model_find_vnd(
      elem, company_id, model_id);
  if (model) {
    for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
      model->groups[i] = BT_MESH_ADDR_UNASSIGNED;
    }
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
      bt_mesh_model_sub_store(model);
    }
  }
}

static int vendor_model_send_sub_status(uint16_t src_addr, uint16_t dst_addr,
                                        uint8_t *data, size_t len) {
  /* Find the correct model by element address */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    LOG_ERR("No composition data");
    return -ENODEV;
  }

  /* Calculate element index from source address */
  uint16_t primary_addr = network_get_unicast_address();
  int elem_idx = src_addr - primary_addr;
  if (elem_idx < 0 || elem_idx >= comp->elem_count) {
    LOG_ERR("Invalid element index: %d", elem_idx);
    return -EINVAL;
  }

  /* Find vendor model in the element */
  const struct bt_mesh_elem *elem = &comp->elem[elem_idx];
  const struct bt_mesh_model *model = NULL;
  for (int i = 0; i < elem->vnd_model_count; i++) {
    if (elem->vnd_models[i].vnd.id == BT_MESH_MODEL_ID_VND_CONFIG_NODE) {
      model = &elem->vnd_models[i];
      break;
    }
  }

  if (!model) {
    LOG_ERR("Vendor model not found on element %d", elem_idx);
    return -ENODEV;
  }

  struct bt_mesh_msg_ctx ctx = {
      .addr = dst_addr,
      .app_idx = 0,
      .send_ttl = BT_MESH_TTL_DEFAULT,
  };

  NET_BUF_SIMPLE_DEFINE(msg, 64);
  bt_mesh_model_msg_init(&msg, VD_CONFIG_MODEL_SUB_STATUS);
  net_buf_simple_add_mem(&msg, data, len);

  tx_gate_acquire();
  int err = bt_mesh_model_send(model, &ctx, &msg, &mesh_tx_send_cb, NULL);
  if (err) {
    LOG_ERR("Failed to send sub status: %d", err);
    tx_gate_release();
  }
  return err;
}

static int handle_vd_config_model_sub_set(const struct bt_mesh_model *model,
                                          struct bt_mesh_msg_ctx *ctx,
                                          struct net_buf_simple *buf) {
  capture_model(model);
  LOG_INF("VD_CONFIG_MODEL_SUB_SET (0xF1) received from 0x%04X", ctx->addr);

  uint8_t *par = buf->data;

  /* Parse little-endian values */
  uint8_t sub_type = par[0];
  uint16_t ele_adr = sys_get_le16(&par[1]);
  uint16_t sub_adr = sys_get_le16(&par[3]);

  LOG_INF("Parsed: Type=0x%02x, Ele=0x%04x, Sub=0x%04x", sub_type, ele_adr,
          sub_adr);

  vd_config_model_sub_status rsp;
  rsp.sub_adr = sub_adr;
  rsp.status =
      (sub_type < VD_CFG_MODEL_SUB_STATUS) ? ST_SUCCESS : ST_UNSPEC_ERR;

  if (rsp.status == ST_UNSPEC_ERR) {
    LOG_INF("ST_UNSPEC_ERR: Type 0x%x", sub_type);
    return -1;
  }

  if (rsp.status == ST_SUCCESS) {
    if (sub_type == VD_CFG_MODEL_SUB_ADD) {
      uint16_t primary = network_get_unicast_address();
      uint8_t elem_count = bt_mesh_comp_get()->elem_count;
      bool can_sub = false;

      /* Validate and check only the targeted element */
      if (ele_adr >= primary && ele_adr < (primary + elem_count)) {
        uint8_t target_idx = ele_adr - primary;
        const struct bt_mesh_elem *elem = &bt_mesh_comp_get()->elem[target_idx];
        /* Check first SIG model (e.g. Generic OnOff) */
        struct bt_mesh_model *sig_mod =
            (struct bt_mesh_model *)bt_mesh_model_find(
                elem, sig_model_share_sub_adr[0]);

        if (sig_mod) {
          for (int i = 0; i < CONFIG_BT_MESH_MODEL_GROUP_COUNT; i++) {
            uint16_t temp = sig_mod->groups[i];
            if (temp == sub_adr || temp == 0xFFFF ||
                temp == BT_MESH_ADDR_UNASSIGNED) {
              can_sub = true;
              break;
            }
          }
        }
      } else {
        rsp.status = ST_INVALID_ADR;
        LOG_WRN("ST_INVALID_ADR: Target element 0x%04x out of range", ele_adr);
        can_sub = false;
      }

      if (rsp.status == ST_SUCCESS && !can_sub) {
        LOG_INF("\n Group cant not set");
        rsp.status = ST_CAN_NOT_SET;
      }
    }
  }

  /* Send Response using VD_CONFIG_MODEL_SUB_STATUS (0xF2) */
  uint8_t rsp_data[3];
  rsp_data[0] = rsp.status;
  sys_put_le16(rsp.sub_adr, &rsp_data[1]);

  uint8_t len_rsp = (sub_type <= VD_CFG_MODEL_SUB_OVERWRITE) ? 3 : 1;
  vendor_model_send_sub_status(ctx->recv_dst, ctx->addr, rsp_data, len_rsp);

  LOG_INF("Response sent with status 0x%02X", rsp.status);

  if (rsp.status != ST_SUCCESS)
    return -1;

  uint16_t primary = network_get_unicast_address();
  uint8_t elem_count = bt_mesh_comp_get()->elem_count;

  /* Validate Element Address */
  if (ele_adr >= primary && ele_adr < (primary + elem_count)) {
    switch (sub_type) {
    case VD_CFG_MODEL_SUB_ADD:
    case VD_CFG_MODEL_SUB_DEL:
    case VD_CFG_MODEL_SUB_OVERWRITE:
    case VD_CFG_MODEL_SUB_DEL_ALL: {
      /* Iterate Shared SIG Models */
      for (int i = 0; i < ARRAY_SIZE(sig_model_share_sub_adr); i++) {
        uint16_t mod_id = sig_model_share_sub_adr[i];

        if (sub_type == VD_CFG_MODEL_SUB_ADD) {
          vd_manual_handle_sub_add(ele_adr, mod_id, sub_adr);
        } else if (sub_type == VD_CFG_MODEL_SUB_DEL) {
          vd_manual_handle_sub_delete(ele_adr, mod_id, sub_adr);
        } else if (sub_type == VD_CFG_MODEL_SUB_OVERWRITE) {
          vd_manual_handle_sub_overwrite(ele_adr, mod_id, sub_adr);
        } else if (sub_type == VD_CFG_MODEL_SUB_DEL_ALL) {
          vd_manual_handle_sub_del_all(ele_adr, mod_id);
        }
      }

      /* Also execute for Vendor Config Node Model (0x0211:0x0001) */
      if (sub_type == VD_CFG_MODEL_SUB_ADD) {
        vd_manual_handle_sub_add_vnd(ele_adr, VENDOR_COMPANY_ID_TELINK,
                                     BT_MESH_MODEL_ID_VND_CONFIG_NODE, sub_adr);
      } else if (sub_type == VD_CFG_MODEL_SUB_DEL) {
        vd_manual_handle_sub_del_vnd(ele_adr, VENDOR_COMPANY_ID_TELINK,
                                     BT_MESH_MODEL_ID_VND_CONFIG_NODE, sub_adr);
      } else if (sub_type == VD_CFG_MODEL_SUB_OVERWRITE) {
        vd_manual_handle_sub_overwrite_vnd(ele_adr, VENDOR_COMPANY_ID_TELINK,
                                           BT_MESH_MODEL_ID_VND_CONFIG_NODE,
                                           sub_adr);
      } else if (sub_type == VD_CFG_MODEL_SUB_DEL_ALL) {
        vd_manual_handle_sub_del_all_vnd(ele_adr, VENDOR_COMPANY_ID_TELINK,
                                         BT_MESH_MODEL_ID_VND_CONFIG_NODE);
      }

      /* LED Feedback */
      uint16_t led_mask = 1 << (ele_adr - primary);
      if (sub_type == VD_CFG_MODEL_SUB_ADD ||
          sub_type == VD_CFG_MODEL_SUB_OVERWRITE) {
        led_ev_handle(LED_CMD_SET_SUBSCRIPTION, led_mask);
      } else {
        led_ev_handle(LED_CMD_DEL_SUBSCRIPTION, led_mask);
      }
    } break;

    case VD_CFG_MODEL_SUB_GET:
      /* Handle Get if needed */
      break;
    }
  }

  return 0;
}

static int handle_vd_config_model_sub_status(const struct bt_mesh_model *model,
                                             struct bt_mesh_msg_ctx *ctx,
                                             struct net_buf_simple *buf) {
  capture_model(model);
  LOG_INF("VD_CONFIG_MODEL_SUB_STATUS (0xF2) received from 0x%04X", ctx->addr);
  return 0;
}
#endif

#if LM_SCENE_ENABLE
/* ============================================================================
 * Vendor Scene Handlers
 * ============================================================================
 */

typedef struct {
  uint32_t start_time;
  uint16_t time_len;
  bool active;
  uint8_t st;
  uint16_t dst_adr;
} scene_reg_rsp_delay_t;

static scene_reg_rsp_delay_t scene_reg_rsp_delay[ELE_CNT] = {0};

/**
 * @func   scene_reg_response_delay_init
 * @brief  None
 * @param
 * @retval Status code
 */
void scene_reg_response_delay_init(uint8_t idx, uint16_t dst_adr, uint8_t st) {
  if (idx < ELE_CNT) {
    scene_reg_rsp_delay[idx].active = true;
    scene_reg_rsp_delay[idx].st = st;
    scene_reg_rsp_delay[idx].dst_adr = dst_adr;
    scene_reg_rsp_delay[idx].start_time = clock_time_ms();
    scene_reg_rsp_delay[idx].time_len = TIMER_1S + rand() % TIMER_9S;
  }
}

/**
 * @func   vendor_model_proc
 * @brief  None
 * @param
 * @retval Status code
 */
void vendor_model_proc(void) {
  foreach (i, ELE_CNT) {
    if (scene_reg_rsp_delay[i].active == true) {
      if (clock_time_exceed_ms(scene_reg_rsp_delay[i].start_time,
                               scene_reg_rsp_delay[i].time_len)) {
        mesh_tx_cmd_scene_reg_st(i, bt_mesh_primary_addr() + i,
                                 scene_reg_rsp_delay[i].dst_adr,
                                 scene_reg_rsp_delay[i].st);
        scene_reg_rsp_delay[i].active = false;
      }
    }
  }
}

/**
 * @brief Handler for Vendor Scene Request (0xE7)
 *
 * @param model Model
 * @param ctx Context
 * @param buf Buffer
 * @return 0 on success
 */
static int handle_vd_scene_request(const struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf) {
  if (buf->len < 1) {
    LOG_WRN("VD_SCENE_REQUEST: Empty payload");
    return -EINVAL;
  }

  uint8_t sub_opcode = net_buf_simple_pull_u8(buf);
  uint16_t scene_id = 0;
  int err = 0;

  LOG_INF("VD_SCENE_REQUEST (0xF3) SubOp=0x%02X from 0x%04X", sub_opcode,
          ctx->addr);

  /* Calculate element index from the model's element address.
   * NOTE: When messages are sent to broadcast (0xFFFF) or group addresses,
   * ctx->recv_dst does NOT contain the element's unicast address.
   * Each element receives the broadcast independently, so we must determine
   * which element is handling this message by checking the model's element.
   */
  uint16_t primary_addr = network_get_unicast_address();
  const struct bt_mesh_elem *elem = bt_mesh_model_elem(model);
  uint8_t elem_idx = elem->rt->addr - primary_addr;

  switch (sub_opcode) {
  case VD_SCENE_STORE:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_STORE: 0x%04X elem=%d", scene_id, elem_idx);
      err = my_scene_store_handler(elem_idx, scene_id, ctx->addr, ctx->recv_dst,
                                   true);
      /* LED Feedback */
      led_ev_handle(LED_CMD_SET_SCENE, 1 << elem_idx);
    }
    break;

  case VD_SCENE_RECALL:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_RECALL: 0x%04X elem=%d", scene_id, elem_idx);
      err = my_scene_recall(elem_idx, scene_id, ctx->addr, ctx->recv_dst, true);
      if (elem_idx == 0 &&
          (ctx->recv_dst >= ADR_FIXED_GROUP_START && ctx->recv_dst <= 0xFFFF)) {
        for (int i = 1; i < ELE_CNT; i++) {
          my_scene_recall(i, scene_id, ctx->addr, ctx->recv_dst, false);
        }
      }
    }
    break;

  case VD_SCENE_DEL:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_DEL: 0x%04X elem=%d", scene_id, elem_idx);
      err = my_scene_delete_handler(elem_idx, scene_id, ctx->addr,
                                    ctx->recv_dst, true);
      /* LED Feedback */
      led_ev_handle(LED_CMD_DEL_SCENE, 1 << elem_idx);
    }
    break;

  case VD_SCENE_GET:
    LOG_INF("VD_SCENE_GET elem=%d", elem_idx);
    /* Send Scene Status (current + target + remain) */
    mesh_tx_cmd_scene_st(elem_idx, primary_addr + elem_idx, ctx->addr, 0);
    break;

  case VD_SCENE_REG_GET:
    LOG_INF("VD_SCENE_REG_GET elem=%d", elem_idx);
    /* Send Scene Register Status (current + scene ID list) */
    mesh_tx_cmd_scene_reg_st(elem_idx, primary_addr + elem_idx, ctx->addr, 0);
    break;

  default:
    LOG_WRN("Unknown VD_SCENE SubOp: 0x%02X", sub_opcode);
    break;
  }

  /* Response is handled in scene.c API functions */
  return 0;
}

static int handle_vd_scene_request_noack(const struct bt_mesh_model *model,
                                         struct bt_mesh_msg_ctx *ctx,
                                         struct net_buf_simple *buf) {
  if (buf->len < 1) {
    LOG_WRN("VD_SCENE_REQUEST_NOACK: Empty payload");
    return -EINVAL;
  }

  uint8_t sub_opcode = net_buf_simple_pull_u8(buf);
  uint16_t scene_id = 0;

  LOG_INF("VD_SCENE_REQUEST_NOACK (0xF4) SubOp=0x%02X from 0x%04X", sub_opcode,
          ctx->addr);

  /* Calculate element index from the model's element address.
   * NOTE: When messages are sent to broadcast (0xFFFF) or group addresses,
   * ctx->recv_dst does NOT contain the element's unicast address.
   * Each element receives the broadcast independently, so we must determine
   * which element is handling this message by checking the model's element.
   */
  uint16_t primary_addr = network_get_unicast_address();
  const struct bt_mesh_elem *elem = bt_mesh_model_elem(model);
  uint8_t elem_idx = elem->rt->addr - primary_addr;

  switch (sub_opcode) {
  case VD_SCENE_STORE_NOACK:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_STORE_NOACK: 0x%04X elem=%d", scene_id, elem_idx);
      my_scene_store_handler(elem_idx, scene_id, ctx->addr, ctx->recv_dst,
                             false);
    }
    break;

  case VD_SCENE_RECALL_NOACK:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_RECALL_NOACK: 0x%04X elem=%d", scene_id, elem_idx);
      my_scene_recall(elem_idx, scene_id, ctx->addr, ctx->recv_dst, false);
      if (elem_idx == 0 &&
          (ctx->recv_dst >= ADR_FIXED_GROUP_START && ctx->recv_dst <= 0xFFFF)) {
        for (int i = 1; i < ELE_CNT; i++) {
          my_scene_recall(i, scene_id, ctx->addr, ctx->recv_dst, false);
        }
      }
    }
    break;

  case VD_SCENE_DEL_NOACK:
    if (buf->len >= 2) {
      scene_id = net_buf_simple_pull_le16(buf);
      LOG_INF("VD_SCENE_DEL_NOACK: 0x%04X elem=%d", scene_id, elem_idx);
      my_scene_delete_handler(elem_idx, scene_id, ctx->addr, ctx->recv_dst,
                              false);
    }
    break;

  default:
    LOG_WRN("Unknown VD_SCENE_NOACK SubOp: 0x%02X", sub_opcode);
    break;
  }

  return 0;
}

static int handle_vd_scene_response(const struct bt_mesh_model *model,
                                    struct bt_mesh_msg_ctx *ctx,
                                    struct net_buf_simple *buf) {
  LOG_INF("VD_SCENE_RESPONSE received from 0x%04X, len=%d", ctx->addr,
          buf->len);
  /* Client-side: parse and handle scene status if needed */
  return 0;
}
#endif

/* ============================================================================
 * Handle vendor config ACK
 * ============================================================================
 */

/* Config Node vendor model operations - contains factory reset and other
 * config commands */
const struct bt_mesh_model_op config_node_op[] = {
    {
        VD_LIGHT_STATE_GET,
        BT_MESH_LEN_MIN(0),
        cb_vd_light_state_get,
    },
    {
        VD_LIGHT_STATE_STATUS,
        BT_MESH_LEN_MIN(0),
        cb_vd_light_state_status,
    },
    {
        VD_LIGHT_OPT_GET,
        BT_MESH_LEN_MIN(0),
        cb_vd_device_opt_get,
    },
    {
        VD_LIGHT_OPT_STATUS,
        BT_MESH_LEN_MIN(0),
        cb_vd_device_opt_status,
    },
    {
        VD_CONFIG_NODE_SET_NOACK,
        BT_MESH_LEN_MIN(0),
        cb_vd_config_node_set_noack,
    },
#if LM_SUB_GROUP_ENABLE
    {
        VD_CONFIG_MODEL_SUB_SET,
        BT_MESH_LEN_MIN(0),
        handle_vd_config_model_sub_set,
    },
    {
        VD_CONFIG_MODEL_SUB_STATUS,
        BT_MESH_LEN_MIN(0),
        handle_vd_config_model_sub_status,
    },
#endif
#if LM_SCENE_ENABLE
    {
        VD_SCENE_REQUEST,
        BT_MESH_LEN_MIN(0),
        handle_vd_scene_request,
    },
    {
        VD_SCENE_REQUEST_NOACK,
        BT_MESH_LEN_MIN(0),
        handle_vd_scene_request_noack,
    },
    {
        VD_SCENE_RESPONSE,
        BT_MESH_LEN_MIN(0),
        handle_vd_scene_response,
    },
#endif
    {
        VD_CONFIG_NODE_SET_ACK,
        BT_MESH_LEN_MIN(0),
        handle_vd_config_node_set_ack,
    },
    {
        VD_CONFIG_NODE_STATUS,
        BT_MESH_LEN_MIN(0),
        handle_vd_config_node_status,
    },
    BT_MESH_MODEL_OP_END,
};

/* Stub functions to resolve linker errors */
void vd_get_device_status(void *cb_par) {
  LOG_WRN("vd_get_device_status not implemented");
}

void ble_ota_manual_get(uint16_t adr, uint8_t *par, uint16_t len,
                        void *cb_par) {
  LOG_WRN("ble_ota_manual_get not implemented");
}

/* More Stubs */
/* sw_cfg_set_mcu_opt implementation moved to config.c */
int dim_cfg_set_mcu_opt(uint16_t model_idx, uint8_t *data, int len,
                        uint8_t code) {
  return 0;
}
int dim_cfg_get_mcu_opt(uint16_t model_idx, uint8_t *data, int len,
                        uint8_t code) {
  return 0;
}

void vd_send_master_led_control_rsp(uint16_t src, uint8_t status) {}
void ble_ota_manual_set(uint16_t model_idx, uint8_t *data, int len,
                        void *cb_par) {}

/**
 * @brief Send vendor command response (or report)
 * API
 *
 * @param op Opcode (1 byte)
 * @param par Payload data
 * @param len Payload length
 * @param adr_src Source address (Element address) used to identify model
 * instance
 * @param adr_dst Destination address
 * @param uuid UUID pointer (Unused in this port)
 * @param model Model pointer (Unused, we find model by adr_src)
 *
 * @return 0 on success, error code on failure
 */
int mesh_tx_cmd_rsp(uint32_t opcode, uint8_t *par, uint32_t len,
                    uint16_t adr_src, uint16_t adr_dst, uint8_t *uuid,
                    void *model_ptr) {
  /* 1. Find element index from adr_src */
  uint16_t primary_addr = network_get_unicast_address();
  if (primary_addr == 0xFFFF) {
    LOG_ERR("Device not provisioned, cannot send mesh_tx_cmd_rsp");
    return -EAGAIN;
  }

  /* Valid index check */
  if (adr_src == 0xFFFF || adr_src < primary_addr) {
    LOG_ERR("Invalid source address 0x%04X (Primary: 0x%04X)", adr_src,
            primary_addr);
    return -EINVAL;
  }

  int elem_idx = adr_src - primary_addr;
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp || elem_idx >= comp->elem_count) {
    LOG_ERR("Invalid element index %d (Count: %d)", elem_idx,
            comp ? comp->elem_count : 0);
    return -EINVAL;
  }

  const struct bt_mesh_elem *elem = &comp->elem[elem_idx];

  /* 2. Opcode Analysis & Model Discovery */
  uint32_t full_op = opcode;
  struct bt_mesh_model *model = (struct bt_mesh_model *)model_ptr;
  bool is_vendor = false;

  if (opcode >= 0xC0 && opcode <= 0xFF) {
    /* Telink-style 1-byte vendor opcode -> full 3-byte Mesh opcode */
    full_op = BT_MESH_MODEL_OP_3(opcode, VENDOR_COMPANY_ID_TELINK);
    is_vendor = true;
  } else if (opcode > 0xFFFF) {
    /* Full opcode provided, check if vendor */
    is_vendor = true;
  }

  if (!model) {
    if (is_vendor) {
      /* Try find Config Node Vendor Model (0x0001) */
      model = (struct bt_mesh_model *)bt_mesh_model_find_vnd(
          elem, VENDOR_COMPANY_ID_TELINK, BT_MESH_MODEL_ID_VND_CONFIG_NODE);
    } else {
      /* For SIG commands, find a compatible proxy model */
      model = (struct bt_mesh_model *)bt_mesh_model_find(
          elem, BT_MESH_MODEL_ID_GEN_LEVEL_SRV);
      if (!model) {
        model = (struct bt_mesh_model *)bt_mesh_model_find(
            elem, BT_MESH_MODEL_ID_GEN_LEVEL_CLI);
      }
    }
  }

  if (!model) {
    LOG_INF("No suitable model found for opcode 0x%06X in element %d", full_op,
            elem_idx);
    return -ENODEV;
  }

  /* 3. Setup Context */
  struct bt_mesh_msg_ctx ctx = {
      .net_idx = network_get_netkey_index(),
      .app_idx = network_get_appkey_index(),
      .addr = adr_dst,
      .send_ttl = BT_MESH_TTL_DEFAULT,
  };

  if (ctx.app_idx == 0xFFFF) {
    LOG_ERR("No AppKey index cached, cannot send");
    return -EACCES;
  }

  /* 4. Create Buffer & Send */
  LOG_INF("Tx Mesh: Op=0x%06X, Src=0x%04X, Dst=0x%04X, Len=%d", full_op,
          adr_src, adr_dst, len);

  NET_BUF_SIMPLE_DEFINE(msg, 64 + BT_MESH_MIC_SHORT);
  bt_mesh_model_msg_init(&msg, full_op);
  net_buf_simple_add_mem(&msg, par, len);

  tx_gate_acquire();
  int err = bt_mesh_model_send(model, &ctx, &msg, &mesh_tx_send_cb, NULL);

  LOG_HEXDUMP_INF(msg.data, msg.len, "Tx Mesh: ");

  if (err) {
    LOG_ERR("bt_mesh_model_send err: %d", err);
    tx_gate_release();
  } else {
    LOG_INF("bt_mesh_model_send success");
  }
  return err;
}
