/**
 * @file fast_provision.h
 * @brief Fast Provision Model - Telink Compatible
 * @details Fast Provision cho phép thiết bị chưa provisioned nhận lệnh
 * provisioning qua Default Network với các keys hardcoded.
 *
 * Tương thích với Telink SDK vendor/common/fast_provision_model.h
 */

#ifndef FAST_PROVISION_H__
#define FAST_PROVISION_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Include vendor opcodes và constants */
#include "vendor.h"

/* ============================================================================
 * Fast Provision States
 * ============================================================================
 *
 * State machine cho Fast Provision process:
 *
 * IDLE → RESET_NETWORK → GET_ADDR → SET_ADDR → NET_INFO →
 * CONFIRM → COMPLETE
 *
 * Có thể có timeout hoặc retry ở các bước
 */
enum fast_prov_state {
  FAST_PROV_IDLE = 0,       // Khởi đầu, chờ lệnh
  FAST_PROV_START,          // Bắt đầu fast provision
  FAST_PROV_RESET_NETWORK,  // Đã nhận RESET_NETWORK, mở default network
  FAST_PROV_GET_ADDR,       // Đã nhận ADDR_GET, gửi MAC address
  FAST_PROV_GET_ADDR_RETRY, // Retry GET_ADDR nếu cần
  FAST_PROV_SET_ADDR,       // Đã nhận ADDR_SET, đã lưu unicast address
  FAST_PROV_NET_INFO,       // Đã nhận PROV_DATA_SET, đã lưu network keys
  FAST_PROV_CONFIRM,        // Đã nhận PROV_CONFIRM, đã xác nhận
  FAST_PROV_CONFIRM_OK,     // Xác nhận OK
  FAST_PROV_COMPLETE, // Đã nhận PROV_COMPLETE, đang provision vào main network
  FAST_PROV_TIME_OUT, // Timeout xảy ra
};
/* ============================================================================
 * Fast Provision Timeouts
 * ============================================================================
 * Note: FAST_PROVISION_TIMEOUT_MS đã được định nghĩa trong vendor.h
 */

/* Structure: MAC address and PID response */
typedef struct {
  uint8_t mac[6];
  uint16_t pid;
  uint16_t default_addr; // Current unicast address
  uint16_t addr;         // Alias for default_addr (for compatibility)
} __packed fast_prov_mac_st;

/* Structure: MAC address get request */
typedef struct {
  uint16_t pid;
  uint16_t ele_addr; // Optional element address for retry
} __packed mac_addr_get_t;

/* Structure: MAC address set */
typedef struct {
  uint8_t mac[6];
  uint16_t ele_addr; // New unicast address to assign
} __packed mac_addr_set_t;

/* Structure: Provision network info */
typedef struct {
  uint8_t net_key[16];
  uint16_t key_index;
  uint8_t flags;
  uint8_t iv_index[4];
  uint16_t unicast_address;
} __packed provison_net_info_str;

/* Structure: App key set */
typedef struct {
  uint8_t net_app_idx[3]; // 3 bytes: net_idx (12 bits) + app_idx (12 bits)
  uint8_t app_key[16];
} __packed mesh_appkey_set_t;

/* Structure: Combined provision data */
typedef struct {
  provison_net_info_str pro_data;
  mesh_appkey_set_t appkey_set;
} __packed fast_prov_net_info_t;

/* Structure: Fast Provision context */
typedef struct {
  uint16_t pid;
  uint16_t prov_addr;
  bool get_mac_en;
  bool not_need_prov;
  uint32_t rcv_op; // Changed to uint32_t for 3-byte opcodes
  uint8_t cur_sts;
  uint8_t last_sts;
  bool pending;
  uint16_t delay;
  int64_t start_tick;
  fast_prov_net_info_t net_info;
  fast_prov_mac_st mac_addr_info;
} fast_prov_par_t;

/**
 * @brief Initialize Fast Provision values
 *
 * @details
 * - Kiểm tra trạng thái provision
 * - Set device key mặc định nếu chưa provisioned
 * - Enable MAC address responses
 */
void mesh_fast_prov_val_init(void);

/**
 * @brief Set Fast Provision state
 *
 * @param sts_set New state to set
 * @return 0 on success
 */
int mesh_fast_prov_sts_set(enum fast_prov_state sts_set);

/**
 * @brief Get current Fast Provision state
 *
 * @return Current state
 */
enum fast_prov_state mesh_fast_prov_sts_get(void);

/**
 * @brief Process Fast Provision state machine
 *
 * @details
 * - Xử lý timeout
 * - Xử lý chuyển trạng thái
 * - Xử lý delay giữa các bước
 *
 * Gọi định kỳ từ main loop
 */
void mesh_fast_prov_proc(void);

/**
 * @brief Khởi tạo NVS filesystem sớm (gọi từ main trước khi enable Bluetooth)
 *
 * @return 0 nếu thành công, error code nếu thất bại
 *
 * @details
 * Hàm này nên được gọi sớm trong main() để đảm bảo NVS sẵn sàng
 * trước khi cần đọc/ghi MAC address.
 */
int fast_provision_nvs_init_early(void);

/**
 * @brief Set device key to default value
 *
 * @details
 * - Device key = MAC address + header
 * - Chỉ dùng khi chưa provisioned
 */
void mesh_device_key_set_default(void);

/**
 * @brief Get element count callback for PID
 *
 * @param pid Product ID
 * @return Number of elements
 */
uint8_t mesh_fast_prov_get_ele_cnt_callback(uint16_t pid);

/**
 * @brief Initialize Fast Provision module - Tất cả logic khởi tạo
 *
 * @details
 * - Lấy context từ model (đã được init trong bt_mesh_init)
 * - Check provisioning status
 * - Setup default network (nếu cần)
 * - Initialize Fast Provision values (device key, state, etc.)
 * - Bind AppKey cho Fast Provision model nếu cần
 * - Start work queue để xử lý state machine định kỳ
 *
 * @note Gọi sau khi bt_mesh_init() trong bt_ready()
 */
void fast_provision_init(void);

/**
 * @brief Start Fast Provision processing work queue
 *
 * @details
 * - Khởi tạo work queue để xử lý state machine định kỳ
 * - Gọi mesh_fast_prov_proc() mỗi 100ms
 *
 * @note Được gọi tự động trong fast_provision_init()
 */
void fast_provision_start(void);

/**
 * @brief Stop Fast Provision processing work queue
 *
 * @details
 * - Cancel work queue để dừng xử lý state machine
 * - Cần gọi trước khi xóa default network keys để tránh SECURE FAULT
 */
void fast_provision_stop(void);

/**
 * @brief Restart Fast Provision (reset state + restart work queue)
 *
 * @details
 * Called when provisioning mode is re-enabled after being disabled.
 * Resets all state machine variables and restarts the work queue
 * so the device can re-join the network without a power cycle.
 */
void fast_provision_restart(void);

/**
 * @brief Check if fast provision is busy (should not be interrupted)
 *
 * @return true if ADDR_SET was received within the last 10 seconds
 */
bool fast_provision_is_busy(void);

/**
 * @brief Bind AppKey to Fast Provision model if needed
 *
 * @param app_idx Application Key index to bind
 * @return 0 on success, negative error code on failure
 */
int fast_provision_bind_appkey(uint16_t app_idx);

/**
 * @brief Fast Provision model op handlers
 */
extern const struct bt_mesh_model_op fast_prov_op[];

/* Fast Provision Model ID - chỉ dùng Server model */
#define BT_MESH_MODEL_ID_VND_FAST_PROV_SRV 0x0000 // Server model

/**
 * @brief Fast Provision Server model instance
 * @note Tương thích với BT_MESH_MODEL_FPROV_SRV trong provisioner
 * @note Chỉ dùng Server model, không dùng Client model
 */
#define BT_MESH_MODEL_FAST_PROV_SRV(_fast_prov)                                \
  BT_MESH_MODEL_VND_CB(FAST_PROV_VENDOR_COMPANY_ID,                            \
                       BT_MESH_MODEL_ID_VND_FAST_PROV_SRV, fast_prov_op, NULL, \
                       BT_MESH_MODEL_USER_DATA(fast_prov_par_t, _fast_prov),   \
                       &fast_prov_cb)

/* Backward compatibility - giữ macro cũ */
#define BT_MESH_MODEL_FAST_PROV(_fast_prov)                                    \
  BT_MESH_MODEL_FAST_PROV_SRV(_fast_prov)

/* Forward declaration */
extern const struct bt_mesh_model_cb fast_prov_cb;

/* ============================================================================
 * Main Network Status Check
 * ============================================================================
 */

/**
 * @brief Device provisioning state - Tương thích với Telink SDK
 *
 * @details Enum định nghĩa trạng thái provisioning của thiết bị
 */
enum {
  STATE_DEV_UNPROV = 0,  /**< Chưa provisioned */
  STATE_DEV_PROVING = 1, /**< Đang trong quá trình provision */
  STATE_DEV_PROVED = 2,  /**< Đã provisioned */
};

/**
 * @brief Get MAC address from fast provision (for SDK use)
 *
 * @param mac Buffer để lưu MAC address (6 bytes)
 * @return true nếu có MAC, false nếu không
 *
 * @details
 * Hàm này được dùng bởi SDK để lấy MAC từ fast provision.
 * Nếu chưa có MAC, sẽ tạo từ hardware ID.
 *
 * EXPORT: Cho phép SDK (addr.c, id.c) gọi hàm này
 */
bool fast_provision_get_mac_for_bt_id(uint8_t *mac);
void fast_provision_save_mac_after_settings_ready(void);
#ifdef __cplusplus
}
#endif

#endif /* FAST_PROVISION_H__ */
