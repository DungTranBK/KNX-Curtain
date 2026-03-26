/**
 * @file network.h
 * @brief Network Information Management API
 *
 * @details
 * Header file này cung cấp API để quản lý thông tin mạng chính (main network)
 * của thiết bị.
 *
 * Các chức năng chính:
 * - Lấy thông tin mạng: AppKey index, NetKey index, addresses
 * - Cache AppKey index để tối ưu performance
 * - Cập nhật và reset thông tin mạng
 *
 * @note
 * - Chỉ quản lý thông tin của main network, không quản lý default network
 * - AppKey index được cache tự động để tránh tìm kiếm nhiều lần
 * - Tất cả functions là thread-safe (được gọi từ main thread)
 */

#ifndef NETWORK_H__
#define NETWORK_H__

#include <stdbool.h>
#include <stdint.h>

#include "app_device.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ============================================================================
 * Network Information Getters
 * ============================================================================
 */

/**
 * @brief Lấy Application Key index của main network
 *
 * @return AppKey index nếu tìm thấy, 0xFFFF nếu không tìm thấy
 *
 * @details
 * Hàm này trả về AppKey index đã được bind với models trong main network.
 *
 * Quy trình:
 * 1. Kiểm tra cache trước (nhanh)
 * 2. Nếu cache không có, tìm từ models
 * 3. Cache kết quả để lần sau không cần tìm lại
 *
 * @note
 * - Được sử dụng khi cần gửi message với AppKey (ví dụ: OnOff Status)
 * - Cache được cập nhật tự động khi tìm thấy AppKey
 * - Để cập nhật cache thủ công, gọi network_set_appkey_index()
 */
uint16_t network_get_appkey_index(void);

/**
 * @brief Lấy Network Key index của main network
 *
 * @return NetKey index (thường là 0x0000 - BT_MESH_NET_PRIMARY)
 *
 * @details
 * Main network luôn sử dụng primary subnet (0x0000).
 * Hàm này trả về BT_MESH_NET_PRIMARY.
 *
 * @note
 * - NetKey index của main network luôn là 0x0000
 * - Không cần cache vì giá trị cố định
 */
uint16_t network_get_netkey_index(void);

/**
 * @brief Lấy địa chỉ unicast của thiết bị trong main network
 *
 * @return Unicast address nếu đã provisioned, 0xFFFF nếu chưa
 *
 * @details
 * Trả về primary unicast address của thiết bị trong main network.
 *
 * @note
 * - Chỉ trả về address hợp lệ (không phải BT_MESH_ADDR_UNASSIGNED hoặc 0x0000)
 * - Trả về 0xFFFF nếu thiết bị chưa được provisioned
 */
uint16_t network_get_unicast_address(void);

/**
 * @brief Lấy địa chỉ của provisioner
 *
 * @return Provisioner address, mặc định 0x0001
 *
 * @details
 * Trả về địa chỉ unicast của provisioner trong main network.
 * Địa chỉ này được sử dụng khi gửi messages đến provisioner.
 *
 * @note
 * - Mặc định là 0x0001
 * - Có thể được cập nhật động qua network_update_provisioner_address()
 */
uint16_t network_get_provisioner_address(void);

/**
 * @brief Cập nhật địa chỉ provisioner nếu cần
 *
 * @param addr Địa chỉ mới từ message
 *
 * @details
 * Hàm này được gọi khi nhận message từ provisioner để cập nhật địa chỉ.
 *
 * @note
 * - Chỉ cập nhật nếu địa chỉ hợp lệ (unicast address)
 * - Hiện tại chỉ log, chưa implement cập nhật động
 * - Có thể mở rộng sau nếu cần
 */
void network_update_provisioner_address(uint16_t addr);

/**
 * @brief Kiểm tra main network đã được provisioned chưa
 *
 * @return true nếu đã vào main network, false nếu chưa
 *
 * @details
 * Kiểm tra thiết bị đã được provisioned vào main network (không phải default
 * network).
 *
 * Main network có đặc điểm:
 * - Có primary subnet (0x0000)
 * - Có primary unicast address hợp lệ
 * - Không phải default network (tạm thời)
 *
 * @note
 * - Trả về false nếu chỉ có default network hoặc chưa provisioned
 * - Được sử dụng để quyết định có enable fast provision hay không
 */
bool network_is_main_network_provisioned(void);

/**
 * @brief Lấy trạng thái provisioning hiện tại của thiết bị
 *
 * @return 0 (UNPROV), 1 (PROVING), hoặc 2 (PROVED)
 *
 * @details Trả về trạng thái mapping từ Nordic sang Telink states.
 */
uint8_t get_provision_state(void);

/* ============================================================================
 * Network Information Setters
 * ============================================================================
 */

/**
 * @brief Set Application Key index đã được bind
 *
 * @param app_idx Application Key index
 *
 * @details
 * Cache AppKey index để sử dụng sau này. Hàm này được gọi khi:
 * - AppKey được bind với models (trong mesh_message_handler.c)
 * - AppKey được add vào mesh stack (trong fast_provision.c)
 *
 * @note
 * - Chỉ cache nếu app_idx != 0xFFFF
 * - Cache này sẽ được sử dụng bởi network_get_appkey_index()
 * - Để xóa cache, gọi network_reset_info()
 */
void network_set_appkey_index(uint16_t app_idx);

/**
 * @brief Reset thông tin mạng (khi reset device)
 *
 * @details
 * Xóa cache AppKey index và reset về trạng thái ban đầu.
 * Hàm này được gọi khi:
 * - Device bị reset (trong main.c)
 * - Mesh configuration bị xóa
 *
 * @note
 * Sau khi reset, network_get_appkey_index() sẽ tự động tìm lại từ models.
 */
void network_reset_info(void);

/**
 * @brief Bật chế độ gia nhập mạng với timeout
 *
 * @details
 * Hàm này bật PB-ADV và PB-GATT provisioning với timeout 5 phút.
 * Sau 5 phút, provisioning sẽ tự động tắt.
 *
 * @note
 * - Chỉ hoạt động khi thiết bị chưa được provision
 * - Nháy LED xanh 3 lần khi bật, LED đỏ 3 lần khi timeout/tắt
 */
#if EN_PROVISIONING_TOGGLE
void network_enable_provisioning_with_timeout(void);
void provisioning_stop_quietly(void);
#endif

/**
 * @brief Factory Reset - Rời mạng main network và reboot chip
 *
 * @details
 * Hàm này thực hiện factory reset hoàn toàn:
 * 1. Nháy LED 1 lần để báo hiệu
 * 2. Reset network info cache (network_reset_info())
 * 3. Reset mesh stack - xóa tất cả keys, addresses trong RAM
 * 4. Xóa persistent settings trong flash
 * 5. Reboot chip để thiết bị hoạt động lại như ban đầu
 *
 * @note
 * - Hàm này được sử dụng bởi Button 3 và Config Node Reset message handler
 * - Sau khi reboot, thiết bị sẽ:
 *   - Quay về trạng thái unprovisioned
 *   - Default network sẽ tự động mở (nếu chưa provisioned)
 *   - Có thể được provision lại từ đầu
 */
void factory_reset_and_reboot(void);

#ifdef __cplusplus
}
#endif

/**
 * @brief Handle Node Reset from Stack Callback
 *
 * @details
 * This function should be called by bt_mesh_prov.reset callback.
 * It handles LED notification, app data cleanup, and schedules reboot.
 */
void network_handle_node_reset(void);

/**
 * @brief Allow or disallow LED blink on provisioning complete
 *
 * @param allow true to allow blink, false to suppress
 *
 * @details
 * Used to suppress LED blink on startup restoration.
 * Should be enabled only when actual provisioning process is started.
 */
void network_allow_provision_blink(bool allow);

/**
 * @brief Check if LED blink is allowed for provisioning complete
 *
 * @return true if allowed, false if suppressed
 */
bool network_can_blink_provision(void);

#endif /* NETWORK_H__ */
