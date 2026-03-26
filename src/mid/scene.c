
#include "../../include/scene.h"

#include <bluetooth/mesh/gen_onoff_srv.h>
#include <errno.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/bluetooth/mesh/main.h>
#include <zephyr/bluetooth/mesh/msg.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include "../../include/network.h"
#include "../../include/relay.h"
#include "../../include/sw_binding.h"
#include "../../include/vendor.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(scene_mid, CONFIG_LOG_DEFAULT_LEVEL);

/* Custom Scene Server Implementation - Hijacking SDK Logic */
struct bt_mesh_model_pub my_scene_pub;
model_scene_t my_model_scene;

/* Scene Process Tracking (like Telink) */
typedef struct {
  uint16_t current_scene;
  uint16_t target_scene;
} scene_proc_t;

static scene_proc_t scene_proc[ELE_CNT];

typeScene_handleGetTargetLevel pvScene_handleGetTargetLevel = NULL;
typeScene_setUpSceneResponseDelay pvScene_setUpSceneResponseDelay = NULL;

typeScene_handleSetLevel pvScene_handleSetLevel = NULL;

/*************************************************/

/* Scene Callback init */
void scene_callback_init(
    typeScene_handleGetTargetLevel handleGetLevel,
    typeScene_setUpSceneResponseDelay setUpSceneResponseDelay,
    typeScene_handleSetLevel handleSetLevel) {
  if (handleGetLevel != NULL) {
    pvScene_handleGetTargetLevel = handleGetLevel;
  }
  if (setUpSceneResponseDelay != NULL) {
    pvScene_setUpSceneResponseDelay = setUpSceneResponseDelay;
  }
  if (handleSetLevel != NULL) {
    pvScene_handleSetLevel = handleSetLevel;
  }
}

/* Set scene active state */
static void scene_active_set(int idx, uint16_t scene_id, int trans_flag) {
  if (idx >= ELE_CNT)
    return;
  scene_proc_t *p = &scene_proc[idx];
  if (trans_flag) {
    p->current_scene = 0;
    p->target_scene = scene_id;
  } else {
    p->current_scene = scene_id;
    p->target_scene = 0;
  }
}

#define SCENE_SETTINGS_KEY "bt/mesh/scene/data"

/* Settings Handler to Load Data */
static int scene_settings_set(const char *name, size_t len,
                              settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  int rc;

  if (settings_name_steq(name, "data", &next) && !next) {
    if (len != sizeof(my_model_scene.data)) {
      LOG_ERR("Settings size mismatch");
      return -EINVAL;
    }
    rc = read_cb(cb_arg, &my_model_scene.data, sizeof(my_model_scene.data));
    if (rc < 0) {
      return rc;
    }
    LOG_INF("Restored Scene Data from Flash");
    return 0;
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(my_scene, "bt/mesh/scene", NULL,
                               scene_settings_set, NULL, NULL);

/* Helper to Save Data */
static void save_scenes(void) {
  int err = settings_save_one(SCENE_SETTINGS_KEY, &my_model_scene.data,
                              sizeof(my_model_scene.data));
  if (err) {
    LOG_ERR("Failed to save scenes: %d", err);
  } else {
    LOG_INF("Scenes saved to flash");
  }
}

/**
 * @brief Send Scene Register Status response (like Telink)
 * @param idx Element index
 * @param ele_adr Element unicast address
 * @param dst_adr Destination address (source of request)
 * @param st Status code
 * @return 0 on success
 */
int mesh_tx_cmd_scene_reg_st(uint8_t idx, uint16_t ele_adr, uint16_t dst_adr,
                             uint8_t st) {
  if (idx >= ELE_CNT)
    return -EINVAL;

  LOG_INF("Scene Reg Status: idx=%d, st=%d, current=0x%04X, dst=0x%04X", idx,
          st, scene_proc[idx].current_scene, dst_adr);

  /* Use mesh_tx_cmd_rsp from vendor_model to send response */
  uint8_t rsp_buf[3 + SCENE_CNT_MAX * 2];
  int len = 0;

  rsp_buf[len++] = VD_SCENE_REG_STATUS; /* Sub-opcode */
  rsp_buf[len++] = st;                  /* Status */
  rsp_buf[len++] = scene_proc[idx].current_scene & 0xFF;
  rsp_buf[len++] = (scene_proc[idx].current_scene >> 8) & 0xFF;

  /* Add list of all stored scene IDs for this element */
  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    uint16_t id = my_model_scene.data[idx][i].id;
    if (id != 0) {
      rsp_buf[len++] = id & 0xFF;
      rsp_buf[len++] = (id >> 8) & 0xFF;
    }
  }

  return mesh_tx_cmd_rsp(VD_SCENE_RESPONSE, rsp_buf, len, ele_adr, dst_adr, 0,
                         0);
}

/**
 * @brief Send Scene Status response (like Telink mesh_tx_cmd_scene_st)
 * Used for SCENE_GET - returns current/target scene ID
 * @param idx Element index
 * @param ele_adr Element unicast address
 * @param dst_adr Destination address (source of request)
 * @param st Status code
 * @return 0 on success
 */
int mesh_tx_cmd_scene_st(uint8_t idx, uint16_t ele_adr, uint16_t dst_adr,
                         uint8_t st) {
  if (idx >= ELE_CNT)
    return -EINVAL;

  uint16_t current = scene_proc[idx].current_scene;
  uint16_t target = scene_proc[idx].target_scene;

  LOG_INF(
      "Scene Status: idx=%d, st=%d, current=0x%04X, target=0x%04X, dst=0x%04X",
      idx, st, current, target, dst_adr);

  /* Build response: [SubOp][Status][CurrentID:2][TargetID:2][RemainTime] */
  uint8_t rsp_buf[8];
  int len = 0;

  rsp_buf[len++] = VD_SCENE_STATUS; /* Sub-opcode */
  rsp_buf[len++] = st;              /* Status */
  rsp_buf[len++] = current & 0xFF;
  rsp_buf[len++] = (current >> 8) & 0xFF;
  rsp_buf[len++] = target & 0xFF;
  rsp_buf[len++] = (target >> 8) & 0xFF;
  rsp_buf[len++] = 0x00; /* Remaining time = 0 (no transition) */

  return mesh_tx_cmd_rsp(VD_SCENE_RESPONSE, rsp_buf, len, ele_adr, dst_adr, 0,
                         0);
}

/* API to Store Scene */
int my_scene_store_handler(uint8_t elem_idx, uint16_t scene_id,
                           uint16_t addr_src, uint16_t addr_dst, bool ack) {
  if (scene_id == 0)
    return -EINVAL;
  if (elem_idx >= ELE_CNT)
    return -EINVAL;

  uint16_t primary_addr = network_get_unicast_address();
  int found_idx = -1;
  int empty_idx = -1;
  uint8_t st = 0; /* Success */

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (my_model_scene.data[elem_idx][i].id == scene_id) {
      found_idx = i;
      break;
    }
    if (my_model_scene.data[elem_idx][i].id == 0 && empty_idx == -1) {
      empty_idx = i;
    }
  }

  int target_idx = (found_idx != -1) ? found_idx : empty_idx;

  if (target_idx != -1) {
    scene_data_t *slot = &my_model_scene.data[elem_idx][target_idx];
    slot->id = scene_id;

    /* Capture target level for this element */
    uint8_t level = 0;
    if (pvScene_handleGetTargetLevel != NULL) {
      level = pvScene_handleGetTargetLevel(elem_idx);
    }
    slot->level = level;

    LOG_INF("Stored Scene 0x%04X at elem %d idx %d (level: %d)", scene_id,
            elem_idx, target_idx, level);
    save_scenes();
    scene_active_set(elem_idx, scene_id, 0);
  } else {
    LOG_WRN("Scene Register Full for elem %d", elem_idx);
    st = 1; /* Register Full */
  }

  /* Send response only if ack=true */
  if (ack) {
    mesh_tx_cmd_scene_reg_st(elem_idx, primary_addr + elem_idx, addr_src, st);
  }
  return (st == 0) ? 0 : -ENOMEM;
}

/* API to Delete Scene */
int my_scene_delete_handler(uint8_t elem_idx, uint16_t scene_id,
                            uint16_t addr_src, uint16_t addr_dst, bool ack) {
  if (elem_idx >= ELE_CNT)
    return -EINVAL;

  uint16_t primary_addr = network_get_unicast_address();
  uint8_t st = 0; /* Success */

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (my_model_scene.data[elem_idx][i].id == scene_id) {
      my_model_scene.data[elem_idx][i].id = 0;
      /* Clear current scene if deleted */
      if (scene_proc[elem_idx].current_scene == scene_id) {
        scene_proc[elem_idx].current_scene = 0;
      }
      save_scenes();
      LOG_INF("Deleted Scene 0x%04X at elem %d idx %d", scene_id, elem_idx, i);
      break;
    }
  }

  /* Send response only if ack=true */
  if (ack) {
    mesh_tx_cmd_scene_reg_st(elem_idx, primary_addr + elem_idx, addr_src, st);
  }
  return 0;
}

/* API to Recall Scene */
int my_scene_recall(uint8_t elem_idx, uint16_t scene_id, uint16_t addr_src,
                    uint16_t addr_dst, bool ack) {
  if (elem_idx >= ELE_CNT)
    return -EINVAL;

  uint16_t primary_addr = network_get_unicast_address();
  scene_data_t *found = NULL;
  uint8_t st = 2; /* Not Found */

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (my_model_scene.data[elem_idx][i].id == scene_id) {
      found = &my_model_scene.data[elem_idx][i];
      break;
    }
  }

  if (found) {
    LOG_INF("Recalling Scene 0x%04X on elem %d: OnOff=%d", scene_id, elem_idx,
            found->lightness_s16 ? 1 : 0);
    // Set Scene active flag
    net_message_set_scene_active_flag(elem_idx, true);
    // FORCE control switch binding by scene recall
    force_control_binding[elem_idx] = true;
    // Update control message parameter
    net_msg_update_control_message_parameter(elem_idx, addr_src, addr_dst,
                                             G_LEVEL_SET);
    int err = -1;
    if (pvScene_handleSetLevel != NULL) {
      pvScene_handleSetLevel(elem_idx, found->level);
      err = 0;
    }
    if (err == 0) {
      scene_active_set(elem_idx, scene_id, 0);
      st = 0; /* Success */
    }
  } else {
    LOG_WRN("Scene 0x%04X not found for elem %d", scene_id, elem_idx);
  }

  /* Send Scene Status response only if ack=true */
  if (ack) {
    mesh_tx_cmd_scene_reg_st(elem_idx, primary_addr + elem_idx, addr_src, st);
  }
  return (st == 0) ? 0 : -ENOENT;
}

static int my_scene_store(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct net_buf_simple *buf) {
  uint16_t scene_id = net_buf_simple_pull_le16(buf);
  LOG_INF("My Scene Store: 0x%04X", scene_id);

  if (scene_id == 0)
    return -EINVAL;

  model_scene_t *srv = (model_scene_t *)model->rt->user_data;
  /* Find slot */
  scene_data_t *slot = NULL;
  scene_data_t *empty = NULL;

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (srv->data[0][i].id == scene_id) {
      slot = &srv->data[0][i];
      break;
    }
    if (srv->data[0][i].id == 0 && !empty) {
      empty = &srv->data[0][i];
    }
  }

  if (!slot)
    slot = empty;

  if (slot) {
    slot->id = scene_id;
    uint8_t onoff = 0;
    if (get_onoff_state(bt_mesh_model_elem(model)->rt->addr, &onoff) == 0) {
      slot->level = onoff ? 255 : 0;       // Simplified mapping
      slot->lightness_s16 = onoff ? 1 : 0; // Simplified
      LOG_INF("Stored Scene 0x%04X: OnOff=%d", scene_id, onoff);

      /* Trigger Save */
      save_scenes();

      /* Respond with Register Status */
      NET_BUF_SIMPLE_DEFINE(msg, 2 + SCENE_CNT_MAX * 2 + 4);
      bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_REGISTER_STATUS);
      net_buf_simple_add_u8(&msg, 0x00);     // Success
      net_buf_simple_add_le16(&msg, 0x0000); // Current Scene (Unknown)
      net_buf_simple_add_le16(&msg, scene_id);

      if (bt_mesh_model_send(model, ctx, &msg, NULL, NULL)) {
        LOG_ERR("Unable to send Scene Register Status");
      }
    }
  } else {
    LOG_WRN("Scene Register Full");
    /* Respond with Register Full */
    NET_BUF_SIMPLE_DEFINE(msg, 4);
    bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_REGISTER_STATUS);
    net_buf_simple_add_u8(&msg, 0x01); // Full
    net_buf_simple_add_le16(&msg, 0x0000);
    net_buf_simple_add_le16(&msg, scene_id);
    bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
  }
  return 0;
}

static int my_scene_recall_handler(struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf) {
  uint16_t scene_id = net_buf_simple_pull_le16(buf);
  LOG_INF("My Scene Recall: 0x%04X", scene_id);

  /* Calculate element index from model */
  uint16_t primary_addr = network_get_unicast_address();
  const struct bt_mesh_elem *elem = bt_mesh_model_elem(model);
  uint8_t elem_idx = elem->rt->addr - primary_addr;

  int err =
      my_scene_recall(elem_idx, scene_id, ctx->addr, ctx->recv_dst, false);

  if (err == 0) {
    /* Publish Status */
    NET_BUF_SIMPLE_DEFINE(msg, 4);
    bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_STATUS);
    net_buf_simple_add_u8(&msg, 0x00);       // Success
    net_buf_simple_add_le16(&msg, scene_id); // Current
    net_buf_simple_add_le16(&msg, 0x0000);   // Target (None)

    bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
  } else {
    NET_BUF_SIMPLE_DEFINE(msg, 4);
    bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_STATUS);
    net_buf_simple_add_u8(&msg, 0x02); // Not Found
    net_buf_simple_add_le16(&msg, 0x0000);
    net_buf_simple_add_le16(&msg, 0x0000);
    bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
  }
  return 0;
}

static int my_scene_get(struct bt_mesh_model *model,
                        struct bt_mesh_msg_ctx *ctx,
                        struct net_buf_simple *buf) {
  NET_BUF_SIMPLE_DEFINE(msg, 6);
  bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_STATUS);
  net_buf_simple_add_u8(&msg, 0x00);
  net_buf_simple_add_le16(&msg, 0x0000); // Current
  net_buf_simple_add_le16(&msg, 0x0000); // Target

  bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
  return 0;
}

static int my_scene_register_get(struct bt_mesh_model *model,
                                 struct bt_mesh_msg_ctx *ctx,
                                 struct net_buf_simple *buf) {
  model_scene_t *srv = (model_scene_t *)model->rt->user_data;

  NET_BUF_SIMPLE_DEFINE(msg, 3 + SCENE_CNT_MAX * 2);
  bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_REGISTER_STATUS);
  net_buf_simple_add_u8(&msg, 0x00);
  net_buf_simple_add_le16(&msg, 0x0000); // Current

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (srv->data[0][i].id != 0) {
      net_buf_simple_add_le16(&msg, srv->data[0][i].id);
    }
  }

  bt_mesh_model_send(model, ctx, &msg, NULL, NULL);
  return 0;
}

static int my_scene_delete(struct bt_mesh_model *model,
                           struct bt_mesh_msg_ctx *ctx,
                           struct net_buf_simple *buf) {
  uint16_t scene_id = net_buf_simple_pull_le16(buf);
  model_scene_t *srv = (model_scene_t *)model->rt->user_data;

  for (int i = 0; i < SCENE_CNT_MAX; i++) {
    if (srv->data[0][i].id == scene_id) {
      srv->data[0][i].id = 0; /* Delete */
      break;
    }
  }

  /* Trigger Save */
  save_scenes();

  NET_BUF_SIMPLE_DEFINE(msg, 2 + SCENE_CNT_MAX * 2 + 4);
  bt_mesh_model_msg_init(&msg, BT_MESH_SCENE_OP_REGISTER_STATUS);
  net_buf_simple_add_u8(&msg, 0x00);     // Success
  net_buf_simple_add_le16(&msg, 0x0000); // Current
  net_buf_simple_add_le16(&msg, scene_id);
  bt_mesh_model_send(model, ctx, &msg, NULL, NULL);

  return 0;
}

const struct bt_mesh_model_op my_scene_op[] = {
    {BT_MESH_SCENE_OP_GET, BT_MESH_LEN_EXACT(0), my_scene_get},
    {BT_MESH_SCENE_OP_REGISTER_GET, BT_MESH_LEN_EXACT(0),
     my_scene_register_get},
    {BT_MESH_SCENE_OP_RECALL, BT_MESH_LEN_MIN(2), my_scene_recall_handler},
    {BT_MESH_SCENE_OP_STORE, BT_MESH_LEN_EXACT(2), my_scene_store},
    {BT_MESH_SCENE_OP_DELETE, BT_MESH_LEN_EXACT(2), my_scene_delete},
    BT_MESH_MODEL_OP_END,
};

/**
 * @brief Delete all scenes
 */
void delete_all_scene(void) {
  memset(&my_model_scene.data, 0, sizeof(my_model_scene.data));
  /* Reset current/target tracking */
  for (int i = 0; i < ELE_CNT; i++) {
    scene_proc[i].current_scene = 0;
    scene_proc[i].target_scene = 0;
  }
  save_scenes();
  LOG_INF("All scenes deleted");
}
