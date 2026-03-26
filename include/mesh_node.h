/**
 * @file mesh_node.h
 * @brief Mesh Node Header (formerly mesh_message_handler.h)
 * @details Handles mesh messages and hooks
 */

#ifndef MESH_NODE_H__
#define MESH_NODE_H__

#include <bluetooth/mesh/models.h>  // For bt_mesh_onoff_srv, bt_mesh_onoff_set, etc.
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#include "net_message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Message Hook
 * ============================================================================
 */
void mesh_message_hook(uint32_t opcode, struct bt_mesh_msg_ctx* ctx,
                       struct net_buf_simple* buf);

/* ============================================================================
 * OnOff Model Logic (Manual Implementation Providers)
 * ============================================================================
 */

/**
 * @brief Initialize message parameters for all elements
 */
void mesh_node_init(void);

/**
 * @brief Update control message parameters for an element
 */
int mesh_node_update_control_message_parameter(int idx, uint16_t src,
                                               uint16_t dst, uint16_t opcode);

/**
 * @brief Get control message parameters for an element
 */
nwk_message_para_t* mesh_node_get_control_message_parameter(int idx);

/**
 * @brief Handle OnOff Set logic
 */
int mesh_node_handle_onoff_set_logic(struct bt_mesh_model* model,
                                     struct bt_mesh_msg_ctx* ctx,
                                     struct net_buf_simple* buf, bool ack);

/**
 * @brief Send G_ONOFF_ST command
 */
int mesh_tx_cmd_g_onoff_st(uint8_t idx, uint16_t adr_src, uint16_t adr_dst,
                           uint32_t opcode, uint8_t* uuid, void* model);

/**
 * @brief Trigger status report delay
 */
void mesh_node_publish_status_delay(uint8_t idx, uint32_t delay_time);

/**
 * @brief Periodic processing for mesh node
 */
void mesh_node_proc(void);

/* ============================================================================
 * AppKey Binding Management
 * ============================================================================
 */
void bind_all_models_with_appkey(uint16_t app_idx);
void restore_appkey_binding_after_settings_load(void);

/* ============================================================================
 * Mesh Stack Initialization
 * ============================================================================
 */
int mesh_initialize(void);

/* ============================================================================
 * Node Reset Status
 * ============================================================================
 */
void send_node_reset_status(void);

#ifdef __cplusplus
}
#endif

#endif /* MESH_NODE_H__ */
