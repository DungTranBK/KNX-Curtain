/**
 * @file mesh_message_handler.h
 * @brief Mesh Message Handler
 * @details Xử lý các mesh messages và hooks:
 *          - Message hook để intercept messages
 *          - OnOff model handlers
 *          - Config messages handlers
 */

#ifndef MESH_MESSAGE_HANDLER_H__
#define MESH_MESSAGE_HANDLER_H__

#include <bluetooth/mesh/models.h> // For bt_mesh_onoff_srv, bt_mesh_onoff_set, bt_mesh_onoff_status, bt_mesh_onoff_cli
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Message Hook
 * ============================================================================
 */

/**
 * @brief Hook để intercept mesh messages
 *
 * @param opcode Message opcode
 * @param ctx Message context
 * @param buf Message buffer
 *
 * @details
 * - Hook này được gọi TRƯỚC khi SDK xử lý message
 * - Xử lý các message đặc biệt không có handler riêng
 * - Fast Provision messages đã có handlers riêng trong fast_provision.c
 */
void mesh_message_hook(uint32_t opcode, struct bt_mesh_msg_ctx *ctx,
                       struct net_buf_simple *buf);

/* ============================================================================
 * OnOff Status Sender
 * ============================================================================
 */

/**
 * @brief Gửi OnOff Status message đến provisioner
 *
 * @param model_idx Model index (endpoint index)
 * @param on_off Trạng thái on/off (true = ON, false = OFF)
 *
 * @return 0 nếu thành công, error code nếu thất bại
 *
 * @details
 * - Tự động lấy AppKey index từ network
 * - Đảm bảo OnOff Server model đã được bind
 * - Tạo message context và status message
 * - Gửi message đến provisioner
 */
int mesh_send_onoff_status(uint8_t model_idx, bool on_off);

/* ============================================================================
 * AppKey Binding Management
 * ============================================================================
 */
/**
 * @brief Bind tất cả models với Application Key
 *
 * @param app_idx Application Key index cần bind
 *
 * @details
 * - Bind AppKey cho tất cả SIG và vendor models trong tất cả elements
 * - Sử dụng direct key binding (giống Telink) thay vì Config Client
 * - Chỉ bind nếu chưa được bind trước đó
 * - Tương thích với cả Fast Provision và Normal Provision
 */
void bind_all_models_with_appkey(uint16_t app_idx);

/**
 * @brief Khôi phục AppKey binding sau khi settings load
 *
 * @details
 * - Load settings từ flash
 * - Đợi mesh stack xử lý xong settings
 * - Khôi phục AppKey index từ network module
 * - Bind AppKey cho tất cả models nếu tìm thấy
 * - Chỉ thực hiện nếu đã vào main network
 */
void restore_appkey_binding_after_settings_load(void);

/* ============================================================================
 * Mesh Stack Initialization
 * ============================================================================
 */

/**
 * @brief Khởi tạo mesh stack
 *
 * @return 0 nếu thành công, error code nếu thất bại
 *
 * @details
 * - Tự động khởi tạo tất cả context cần thiết (Config Client, Fast Provision,
 * Provisioning callbacks)
 * - Khởi tạo tất cả model instances (OnOff, Health, Config, Fast Provision)
 * - Tạo elements và composition
 * - Khởi tạo mesh stack với bt_mesh_init()
 * - Register message hook
 * - Tất cả logic khởi tạo mesh được tập trung trong hàm này
 */
int mesh_initialize(void);

/* ============================================================================
 * Node Reset Status - Gửi bản tin rời mạng
 * ============================================================================
 */

/**
 * @brief Gửi Node Reset Status (0x4A80) đến provisioner trước khi rời mạng
 *
 * @details
 * Hàm này tự động:
 * - Lấy Config Server model từ primary element
 * - Tạo message context với Device Key (BT_MESH_KEY_DEV_LOCAL)
 * - Sử dụng primary network index (BT_MESH_NET_PRIMARY = 0x0000)
 * - Gửi đến provisioner address (0x0001)
 * - Sau khi gửi xong, tự động trigger factory reset thông qua callback
 *
 * @note
 * - Chỉ gửi nếu device đã vào main network (đã provisioned)
 * - Nếu không tìm thấy Config Server model hoặc chưa provisioned, sẽ reset ngay
 * - Message được gửi với TTL default và relay enabled
 * - Tương tự logic SDK node_reset() trong cfg_srv.c
 * - Có thể gọi từ bất kỳ đâu (F0 handler, Button 3, factory_reset_and_reboot(),
 * ...)
 */
/**
 * @brief Gửi Node Reset Status đến provisioner
 *
 * @param ctx Message context từ message nhận được (NULL nếu gọi từ button)
 *            Nếu ctx != NULL, sẽ reply về ctx->addr (giống SDK)
 *            Nếu ctx == NULL, sẽ gửi đến network_get_provisioner_address()
 */
void send_node_reset_status(void);

#ifdef __cplusplus
}
#endif

#endif /* MESH_MESSAGE_HANDLER_H__ */
