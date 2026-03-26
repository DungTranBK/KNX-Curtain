/**
 * @file mesh_node.c
 * @brief Mesh Node Implementation (formerly mesh_message_handler.c)
 * @details Handles mesh composition, initialization, and messages
 */
#include "../../include/mesh_node.h"

#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/scene.h>
#include <bluetooth/mesh/scene_srv.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h> // For settings_load()

#include "../../include/app.h"
#include "../../include/app_device.h"
#include "../../include/fast_provision.h" // For fast_prov_par_t, BT_MESH_MODEL_FAST_PROV_SRV
#include "../../include/led_ev.h"
#include "../../include/net_message.h"
#include "../../include/network.h"
#include "../../include/normal_provision.h" // For bt_mesh_dk_prov_custom_init
#include "../../include/pre_update.h"
#include "../../include/utilities.h"
#include "../../include/vendor.h" // For LM_PID_MESH, VENDOR_COMPANY_ID_TELINK, VD_CONFIG_NODE_SET_NOACK
#include "../../include/vendor_model.h"
#include "mesh/access.h"     // For bt_mesh_comp_get()
#include "mesh/app_keys.h"   // For bt_mesh_app_key_exists()
#include "mesh/foundation.h" // For bt_mesh_primary_addr(), OP_NODE_RESET_STATUS
#include "mesh/mesh.h"       // For BT_MESH_APP_KEY_CB_DEFINE
#include "mesh/settings.h" // For bt_mesh_model_bind_store(), bt_mesh_settings_store_pending()

LOG_MODULE_REGISTER(mesh_node, CONFIG_LOG_DEFAULT_LEVEL);

/* Forward declarations - from main.c */
extern bool is_led_on;
extern void set_led_state(bool state);
extern bool attention;

/* ============================================================================
 * Config Message Handlers
 * ============================================================================
 */

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
/* ============================================================================
 * Node Reset Status - Send network leave message
 * ============================================================================
 */

/* Callback when Node Reset Status send starts */
static void reset_send_start(uint16_t duration, int err, void *cb_data) {
  if (err) {
    LOG_ERR("Node Reset Status send failed: %d", err);
  } else {
    LOG_INF("Node Reset Status send started (duration: %u ms)", duration);
  }
}

/* Callback when Node Reset Status send ends */
static void reset_send_end(int err, void *cb_data) {
  if (err) {
    LOG_ERR("Node Reset Status send failed: %d", err);
  } else {
    LOG_INF("Node Reset Status send completed successfully");
  }
}

/**
 * @brief Send Node Reset Status (0x4A80) to provisioner before leaving network
 *
 * @details
 * This function automatically:
 * - Gets Config Server model from primary element
 * - Creates message context with Device Key (BT_MESH_KEY_DEV_LOCAL)
 * - Uses primary network index (BT_MESH_NET_PRIMARY = 0x0000)
 * - Sends to provisioner address:
 *   + If ctx != NULL: reply to ctx->addr (like SDK node_reset())
 *   + If ctx == NULL: send to network_get_provisioner_address()
 * - After sending, automatically triggers factory reset via callback
 *
 * @param ctx Message context from received message (NULL if called from button)
 *            If ctx != NULL, will reply to ctx->addr (like SDK)
 *            If ctx == NULL, will send to network_get_provisioner_address()
 *
 * @note
 * - Only sends if device has joined main network (already provisioned)
 * - If Config Server model not found or not provisioned, will reset immediately
 * - Message is sent with default TTL and relay enabled
 * - Similar to SDK logic node_reset() in cfg_srv.c
 */
void send_node_reset_status(void) {
  /* Check if device is in main network */
  if (!network_is_main_network_provisioned()) {
    LOG_WRN("Device not provisioned, skipping Node Reset Status");
    return;
  }

  /* Get Config Server model from primary element */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp || comp->elem_count == 0) {
    LOG_WRN("Mesh composition not found, skipping Node Reset Status");
    return;
  }

  const struct bt_mesh_elem *elem = &comp->elem[0];
  const struct bt_mesh_model *cfg_srv_model =
      bt_mesh_model_find(elem, BT_MESH_MODEL_ID_CFG_SRV);

  if (!cfg_srv_model) {
    LOG_WRN("Config Server model not found, skipping Node Reset Status");
    return;
  }

  /* Create message context with Device Key for Config Server model
   * Config Server model uses Device Key (BT_MESH_KEY_DEV_LOCAL), not AppKey
   * Uses BT_MESH_KEY_DEV_LOCAL similar to SDK cfg_srv_init()
   */
  struct bt_mesh_msg_ctx reset_ctx = {
      .net_idx = BT_MESH_NET_PRIMARY,   /* Primary network index (0x0000) */
      .app_idx = BT_MESH_KEY_DEV_LOCAL, /* Device Key Local for Config Server */
      .addr =
          network_get_provisioner_address(), /* Default provisioner address */
      .send_ttl = BT_MESH_TTL_DEFAULT,
      .send_rel = true, /* Enable relay to ensure message is relayed */
  };

  LOG_INF("Sending Node Reset Status (0x4A80) before factory reset...");
  LOG_INF("  Model: Config Server (0x%04X)", cfg_srv_model->id);
  LOG_INF("  Destination: 0x%04X (Provisioner)", reset_ctx.addr);
  LOG_INF("  NetIdx: 0x%04X, AppIdx: 0x%04X (Device Key)", reset_ctx.net_idx,
          reset_ctx.app_idx);
  LOG_INF("  TTL: %d, Relay: %s", reset_ctx.send_ttl,
          reset_ctx.send_rel ? "enabled" : "disabled");

  /* Create message buffer and send */
  static const struct bt_mesh_send_cb reset_cb = {
      .start = reset_send_start,
      .end = reset_send_end,
  };

  BT_MESH_MODEL_BUF_DEFINE(msg, OP_NODE_RESET_STATUS, 0);
  bt_mesh_model_msg_init(&msg, OP_NODE_RESET_STATUS);

  int ret =
      bt_mesh_model_send(cfg_srv_model, &reset_ctx, &msg, &reset_cb, NULL);
  if (ret != 0) {
    LOG_ERR("Unable to send Node Reset Status: %d", ret);
  } else {
    LOG_INF("Node Reset Status message queued for transmission");
  }
}

/* ============================================================================
 * Message Hook
 * ============================================================================
 */

void mesh_message_hook(uint32_t opcode, struct bt_mesh_msg_ctx *ctx,
                       struct net_buf_simple *buf) {
  /* NULL pointer checks */
  if (ctx == NULL || buf == NULL) {
    return;
  }

  /* Diagnostic Log: Print all incoming opcodes */
  LOG_INF("Mesh RX: Opcode=0x%04X, Src=0x%04X, Dst=0x%04X, Len=%u", opcode,
          ctx->addr, ctx->recv_dst, buf->len);

  /* Only log important Config messages */
  if ((opcode & 0xFF00) == 0x8000) {
    /* Log important Config messages */
    if (opcode == OP_NODE_RESET_STATUS ||
        (opcode & 0xFF) == 0x80 || /* Config messages */
        (opcode & 0xFF) == 0x81) {
      LOG_INF("Config message: opcode=0x%04X, src=0x%04X", opcode & 0xFFFF,
              ctx->addr);
    }
  }
}

/* ============================================================================
 * AppKey Binding Management
 * ============================================================================
 */

/**
 * @brief Bind AppKey for a specific model
 *
 * @param model Model to bind
 * @param app_idx Application Key index
 *
 * @return true if successful, false if bind failed
 *
 * @details
 * - Only bind if model is not already bound with this AppKey
 * - Save binding to settings to restore after reboot
 */
static bool bind_model_with_appkey(struct bt_mesh_model *model,
                                   uint16_t app_idx) {
  if (bt_mesh_model_has_key(model, app_idx)) {
    return false; /* Already bound */
  }

  /* Find empty slot to bind */
  for (int j = 0; j < model->keys_cnt; j++) {
    if (model->keys[j] == BT_MESH_KEY_UNUSED) {
      model->keys[j] = app_idx;
      /* Save binding to settings to restore after reboot */
      if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        bt_mesh_model_bind_store(model);
      }
      return true; /* Bind successful */
    }
  }

  return false; /* No empty slots */
}

/**
 * @brief Bind all models with Application Key - similar to Telink
 * appkey_bind_all()
 *
 * @param app_idx Application Key index to bind (usually AppKey of main network)
 *
 * @details
 * - Bind AppKey to all SIG and vendor models in all elements
 * - Use direct key binding (like Telink) instead of Config Client
 * - Only bind if not already bound (find empty slot, do not overwrite)
 * - Automatically save settings after binding
 * - Compatible with both Fast Provision and Normal Provision
 *
 * @note
 * - Fast Provision model can be bound with BOTH AppKeys:
 *   + DEFAULT_APPKEY_INDEX (0x0001): For default network (bound in
 * default_network_bind_appkey)
 *   + AppKey of main network (app_idx): Bound here
 *   + AppKey 0x0000: Automatically bound if app_idx != 0x0000 and AppKey 0x0000
 * exists (for C5 messages)
 * - Other models (OnOff, Health, Config) only bind with main network
 * - Need CONFIG_BT_MESH_MODEL_KEY_COUNT=2 so Fast Provision model has enough
 * slots
 *
 * @side_effects
 * - Update AppKey index in network module if bind successful
 * - Commit settings to save bindings to flash
 */
void bind_all_models_with_appkey(uint16_t app_idx) {
  bool any_binding_changed = false;
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    return;
  }

  LOG_INF("Binding AppKey 0x%04X to all models (main network)", app_idx);

  /* Bind AppKey to all models in all elements */
  for (int elem_idx = 0; elem_idx < comp->elem_count; elem_idx++) {
    const struct bt_mesh_elem *elem = &comp->elem[elem_idx];
    LOG_INF("Element %d: %d SIG models, %d Vendor models", elem_idx,
            elem->model_count, elem->vnd_model_count);

    /* Bind standard models (OnOff, Health, Config, etc.) - only for main
     * network
     */
    for (int i = 0; i < elem->model_count; i++) {
      struct bt_mesh_model *model = (struct bt_mesh_model *)&elem->models[i];
      if (bind_model_with_appkey(model, app_idx)) {
        any_binding_changed = true;
      }
    }

    /* Bind vendor models (Fast Provision, etc.) - can bind with both default
     * and main network */
    for (int i = 0; i < elem->vnd_model_count; i++) {
      struct bt_mesh_model *model =
          (struct bt_mesh_model *)&elem->vnd_models[i];
      if (bind_model_with_appkey(model, app_idx)) {
        any_binding_changed = true;
      }
    }
  }

  /* Fast Provision model needs to bind additional AppKey 0x0000 if:
   * - app_idx != 0x0000 (already bound app_idx)
   * - AppKey 0x0000 exists in mesh stack
   * - Provisioner can send C5 with AppKey 0x0000
   */
  if (app_idx != 0x0000 && bt_mesh_app_key_exists(0x0000)) {
    if (comp->elem_count > 0) {
      const struct bt_mesh_elem *elem = &comp->elem[0];
      const struct bt_mesh_model *fast_prov_mod =
          bt_mesh_model_find_vnd(elem, FAST_PROV_VENDOR_COMPANY_ID,
                                 BT_MESH_MODEL_ID_VND_FAST_PROV_SRV);
      if (!fast_prov_mod) {
        fast_prov_mod = bt_mesh_model_find_vnd(
            elem, FAST_PROV_VENDOR_COMPANY_ID, BT_MESH_MODEL_ID_VND_FAST_PROV);
      }
      if (fast_prov_mod) {
        struct bt_mesh_model *m = (struct bt_mesh_model *)fast_prov_mod;
        if (!bt_mesh_model_has_key(m, 0x0000)) {
          if (bind_model_with_appkey(m, 0x0000)) {
            LOG_INF("  Fast Provision model also bound to AppKey 0x0000 (for "
                    "C5 messages)");
            any_binding_changed = true;
          }
        }
      }
    }
  }

  /* Update AppKey index in network module and save settings */
  if (any_binding_changed) {
    network_set_appkey_index(app_idx);
    LOG_INF("AppKey 0x%04X bound to all models", app_idx);

    /* Commit settings to save bindings to flash */
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
      bt_mesh_settings_store_pending();
    }
  }
}

/**
 * @brief Callback when AppKey is added to mesh stack storage
 *
 * @param app_idx Application Key index added
 * @param net_idx Network Key index
 * @param evt Event type (BT_MESH_KEY_ADDED or BT_MESH_KEY_DELETED)
 *
 * @details
 * - This is the main flow to bind model - called after Config Server has added
 * AppKey to storage
 * - Only bind if not previously bound to avoid conflict
 * - Triggered when Telink provisioner adds AppKey for this node
 *
 * @side_effects
 * - Bind models with AppKey if not bound
 * - Update AppKey index in network module
 */
/* Static flag to coordinate between prov_complete and app_key_evt
 * Start with false so boot restoration does not trigger blink
 */
static void telink_appkey_event_handler(uint16_t app_idx, uint16_t net_idx,
                                        enum bt_mesh_key_evt evt) {
  if (evt != BT_MESH_KEY_ADDED) {
    return;
  }

  /* Bind AppKey to all models when AppKey is added */
  /* Called both when initial provisioning and when loading from settings */
  if (network_is_main_network_provisioned()) {
    bind_all_models_with_appkey(app_idx);
    /* Note: "AppKey bound to all models" log already printed in
     * bind_all_models_with_appkey() */
    network_set_appkey_index(app_idx); /* Cache AppKey index */
  }
}

/* Register AppKey event callback */
BT_MESH_APP_KEY_CB_DEFINE(telink_appkey_event_handler);

/**
 * @brief Restore AppKey binding after settings load
 *
 * @details
 * - Load settings from flash
 * - Wait for mesh stack to process settings
 * - Restore AppKey index from network module
 * - Bind AppKey to all models if found
 * - Only perform if already joined main network
 */
void restore_appkey_binding_after_settings_load(void) {
  if (!IS_ENABLED(CONFIG_SETTINGS)) {
    return;
  }

  settings_load();

  /* Wait for settings to load so AppKey is loaded into storage */
  k_sleep(K_MSEC(1000));

  /* Only restore if already joined main network */
  if (!network_is_main_network_provisioned()) {
    return;
  }

  /* Try to get AppKey index from network module (auto-find from models) */
  uint16_t app_idx = network_get_appkey_index();

  /* Bind AppKey to all models if found */
  if (app_idx != 0xFFFF) {
    bind_all_models_with_appkey(app_idx);
  }
}

/* ============================================================================
 * Health Model Callbacks
 * ============================================================================
 */

/**
 * @brief Callback when Health Server requests attention (error occurs)
 *
 * @param mod Health Server model pointer
 *
 * @details Turn on attention blink so user realizes there is an error
 */
static void attention_on(const struct bt_mesh_model *mod) { attention = true; }

/**
 * @brief Callback when Health Server turns off attention (error handled)
 *
 * @param mod Health Server model pointer
 *
 * @details Turn off attention blink
 */
static void attention_off(const struct bt_mesh_model *mod) {
  attention = false;
}

/**
 * @brief Callback when receiving Health Status from another node
 *
 * @param cli Health Client pointer
 * @param addr Address of node sending Health Status
 * @param test_id Test ID
 * @param cid Company ID
 * @param faults Array containing fault codes
 * @param fault_count Number of faults
 *
 * @details Only log warning if there are faults, no further processing
 */
static void health_current_status(struct bt_mesh_health_cli *cli, uint16_t addr,
                                  uint8_t test_id, uint16_t cid,
                                  uint8_t *faults, size_t fault_count) {
  if (fault_count > 0) {
    LOG_WRN("Health faults from 0x%04x: test_id=0x%02x, cid=0x%04x, count=%zu",
            addr, test_id, cid, fault_count);
  }
}

/* ============================================================================
 * Mesh Model Instances (Full Definitions)
 * ============================================================================
 */

/* Health Server callbacks */
static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .attn_on = attention_on,
    .attn_off = attention_off,
};

/** Health Server instance */
static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

/** Health Client instance */
static struct bt_mesh_health_cli health_cli = {
    .current_status = health_current_status,
};

/** Health publication */
BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* ============================================================================
 * Generic Default Transition Time Server Instance
 * ============================================================================
 */
static void dtt_update_handler(struct bt_mesh_dtt_srv *srv,
                               struct bt_mesh_msg_ctx *ctx, uint32_t old_time,
                               uint32_t new_time) {
  LOG_INF("DTT updated: %u -> %u ms", old_time, new_time);
}

static struct bt_mesh_dtt_srv gen_dtt_srv =
    BT_MESH_DTT_SRV_INIT(dtt_update_handler);

/* ============================================================================
 * Mesh Stack Initialization
 * ============================================================================
 */

////////////////////////// Level Server //////////////////////////

/* Config Client instance */
static struct bt_mesh_cfg_cli cfg_cli = {0};

/* Generic OnOff Client instance */
static struct bt_mesh_onoff_cli onoff_cli = BT_MESH_ONOFF_CLI_INIT(NULL);

/* Fast Provision context */
static fast_prov_par_t fast_prov_ctx = {0};

/* Manual OnOff Handlers */
static int handle_level_get(struct bt_mesh_model *model,
                            struct bt_mesh_msg_ctx *ctx,
                            struct net_buf_simple *buf) {
  int model_idx = bt_mesh_model_elem(model) - bt_mesh_comp_get()->elem;
  if (model_idx < 0 || model_idx >= ELE_CNT) {
    return -EINVAL;
  }

  net_msg_cb_par_t cb_par = {
      .model_idx = model_idx,
      .adr_src = ctx->addr,
      .adr_dst = ctx->recv_dst,
  };

  return my_handle_mesh_cmd_sig_g_level_get(NULL, 0, &cb_par);
}

static int handle_level_set(struct bt_mesh_model *model,
                            struct bt_mesh_msg_ctx *ctx,
                            struct net_buf_simple *buf, bool ack) {
  int model_idx = bt_mesh_model_elem(model) - bt_mesh_comp_get()->elem;
  if (model_idx < 0 || model_idx >= ELE_CNT) {
    return -EINVAL;
  }
  net_msg_cb_par_t cb_par = {
      .model_idx = model_idx,
      .adr_src = ctx->addr,
      .adr_dst = ctx->recv_dst,
      .op_rsp = 0,
  };
  if (ack == true) {
    cb_par.op_rsp = BT_MESH_MODEL_OP_2(0x82, 0x07);
  }
  if (model_idx == 0 &&
      (ctx->recv_dst >= ADR_FIXED_GROUP_START && ctx->recv_dst <= 0xFFFF)) {
    LOG_INF(
        "LEVEL - Broadcast/Fixed Group 0x%04x on Element 0, distributing...",
        ctx->recv_dst);
    for (uint8_t i = 0; i < ELE_CNT; i++) {
      cb_par.model_idx = i;
      handle_mesh_cmd_sig_g_level_set(buf->data, buf->len, &cb_par);
    }
  } else {
    handle_mesh_cmd_sig_g_level_set(buf->data, buf->len, &cb_par);
  }
  return 0;
}

const struct bt_mesh_model_op level_srv_op[] = {
    {BT_MESH_MODEL_OP_2(0x82, 0x05), 0, handle_level_get},
    {BT_MESH_MODEL_OP_2(0x82, 0x06), 2, handle_level_set},
    {BT_MESH_MODEL_OP_2(0x82, 0x07), 2, handle_level_set},
    BT_MESH_MODEL_OP_END,
};

BT_MESH_MODEL_PUB_DEFINE(level_pub_0, NULL, 3);
BT_MESH_MODEL_PUB_DEFINE(level_pub_1, NULL, 3);
BT_MESH_MODEL_PUB_DEFINE(level_pub_2, NULL, 3);
BT_MESH_MODEL_PUB_DEFINE(level_pub_3, NULL, 3);

/* Manual OnOff Functions */
void mesh_node_init(void) { net_msg_init(); }

nwk_message_para_t *mesh_node_get_control_message_parameter(int idx) {
  return net_msg_get_control_message_parameter(idx);
}

void mesh_node_publish_status_delay(uint8_t idx, uint32_t delay_time) {
  net_message_publish_status_delay(idx, delay_time);
}

void mesh_node_proc(void) { /* Placeholder for periodic tasks */ }

/**
 * @brief Callback when provisioning completes
 *
 * @param net_idx Network index
 * @param src Source address (provisioner)
 *
 * @details
 * This callback is called when provisioning completes, but can be:
 * - Default network (temporary, subnet 0x0001) - NO blink LED
 * - Main network (permanent, subnet 0x0000) - Blink LED to indicate success
 *
 * Only blink LED when successfully joining main network to avoid confusion.
 */
static void prov_complete(uint16_t net_idx, uint16_t src) {
  LOG_INF("Provisioning complete: net_idx=0x%04X, provisioner=0x%04X", net_idx,
          src);
  if (network_is_main_network_provisioned()) {
    LOG_INF("Main network provisioned successfully");

#if EN_PROVISIONING_TOGGLE
    /* Exit provision mode immediately after stack is provisioned */
    provisioning_stop_quietly();
#endif

    if (network_can_blink_provision()) {
      led_ev_handle(LED_PROVISION_SUCCESS, CONFIG_LED_MASK_BLUETOOTH);
      curtain_setup_enable_send_config_delay(true, true);
      pre_update_reset_time_after_join();
    } else {
      LOG_INF(
          "Startup restoration detected - skipping Provision Success blink");
    }

    /* IMPORTANT: Disable fast provision when normal provisioning successful
     * to avoid conflict and ensure only one provisioning method active
     */
    extern fast_prov_par_t *g_fast_prov;
    if (g_fast_prov) {
      g_fast_prov->not_need_prov = true;
      LOG_INF("Fast provision disabled after normal provisioning success");
    }

  } else {
    LOG_INF("Default network provisioned (not main network)");
  }
}

/** Provisioning callbacks */
static struct bt_mesh_dk_prov_callbacks dk_prov_cb = {
    .prov_complete = prov_complete,
};

/* ============================================================================
 * Mesh Composition
 * ============================================================================
 */

/* Config Node vendor model operations - contains factory reset and other config
 * commands */
/* Config Node vendor model operations - moved to vendor_model.c */

/* Fast Provision vendor model - must be declared after fast_prov_ctx */
/* Each element needs its own model instances for proper runtime context */
static struct bt_mesh_model vnd_models_0[] = {
    BT_MESH_MODEL_FAST_PROV_SRV(&fast_prov_ctx),
    BT_MESH_MODEL_VND(FAST_PROV_VENDOR_COMPANY_ID,
                      BT_MESH_MODEL_ID_VND_CONFIG_NODE, config_node_op, NULL,
                      NULL),
};

static struct bt_mesh_model vnd_models_1[] = {
    BT_MESH_MODEL_VND(FAST_PROV_VENDOR_COMPANY_ID,
                      BT_MESH_MODEL_ID_VND_CONFIG_NODE, config_node_op, NULL,
                      NULL),
};

static struct bt_mesh_model vnd_models_2[] = {
    BT_MESH_MODEL_VND(FAST_PROV_VENDOR_COMPANY_ID,
                      BT_MESH_MODEL_ID_VND_CONFIG_NODE, config_node_op, NULL,
                      NULL),
};

static struct bt_mesh_model vnd_models_3[] = {
    BT_MESH_MODEL_VND(FAST_PROV_VENDOR_COMPANY_ID,
                      BT_MESH_MODEL_ID_VND_CONFIG_NODE, config_node_op, NULL,
                      NULL),
};

/* SIG Model Lists for each element to ensure stable Composition Data
 * Each element MUST have its own model array to maintain separate runtime
 * contexts.
 */
static struct bt_mesh_model sig_models_0[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_CFG_CLI(&cfg_cli),
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL_HEALTH_CLI(&health_cli),
    BT_MESH_MODEL_DTT_SRV(&gen_dtt_srv),
    BT_MESH_MODEL_ONOFF_CLI(&onoff_cli),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, level_srv_op, &level_pub_0,
                  NULL),
};

static struct bt_mesh_model sig_models_1[] = {
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, level_srv_op, &level_pub_1,
                  NULL),
};

static struct bt_mesh_model sig_models_2[] = {
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, level_srv_op, &level_pub_2,
                  NULL),
};

static struct bt_mesh_model sig_models_3[] = {
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, level_srv_op, &level_pub_3,
                  NULL),
};

/* Element definition with all models - supports 6 elements
 */
static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, sig_models_0, vnd_models_0),
    BT_MESH_ELEM(0, sig_models_1, vnd_models_1),
    BT_MESH_ELEM(0, sig_models_2, vnd_models_2),
    BT_MESH_ELEM(0, sig_models_3, vnd_models_3),
};

/* Mesh composition - define device structure */
static const struct bt_mesh_comp comp = {
    .cid = VENDOR_COMPANY_ID_TELINK, /* Company ID: 0x0211 (Telink) */
    .pid = LM_PID_MESH,              /* Product ID: 0x0890 */
    .vid = 0x3130,                   /* Version ID */
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

/**
 * @brief Initialize mesh stack
 *
 * @return 0 on success, error code on failure
 *
 * @details
 * - Automatically initialize all necessary context (Config Client, Fast
 * Provision, Provisioning callbacks)
 * - Initialize all model instances (OnOff, Health, Config, Fast Provision)
 * - Create elements and composition
 * - Initialize mesh stack with bt_mesh_init()
 * - Register message hook
 */
int mesh_initialize(void) {
  int err;

  /* Initialize mesh stack with composition */
  err = bt_mesh_init(bt_mesh_dk_prov_custom_init(&dk_prov_cb), &comp);
  if (err) {
    LOG_ERR("Mesh init failed: %d", err);
    return err;
  }

  /* Log composition for diagnostics */
  LOG_INF("Mesh stack initialized (CPS Diagnostic):");
  LOG_INF("  Elements: %d", comp.elem_count);
  for (int i = 0; i < comp.elem_count; i++) {
    LOG_INF("  Element %d (loc 0x%04X): %d SIG, %d VND", i, comp.elem[i].loc,
            comp.elem[i].model_count, comp.elem[i].vnd_model_count);
  }

  /* Initialize manual mesh node data */
  mesh_node_init();

  /* Register message hook to intercept messages and update provisioner address
   */
  bt_mesh_msg_cb_set(mesh_message_hook);

  LOG_INF("Mesh stack initialized");

  return 0;
}
