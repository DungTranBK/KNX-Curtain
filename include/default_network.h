/**
 * @file default_network.h
 * @brief Default Network Management for Fast Provision
 * @details Quản lý mạng mặc định với keys hardcoded để thiết bị chưa
 * provisioned có thể nhận messages fast provision từ provisioner.
 *
 * Tương thích với Telink SDK default_network.c
 */

#ifndef DEFAULT_NETWORK_H__
#define DEFAULT_NETWORK_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Default Network Keys - Đồng bộ với Telink SDK
 *
 * ⚠️ QUAN TRỌNG: Keys này PHẢI giống với provisioner để có thể giao tiếp!
 */

// Default NetKey (16 bytes) - Telink Standard
#define DEFAULT_NET_KEY_SIZE 16
extern const uint8_t default_net_key[DEFAULT_NET_KEY_SIZE];

// Default AppKey (16 bytes) - Telink Standard
#define DEFAULT_APP_KEY_SIZE 16
extern const uint8_t default_app_key[DEFAULT_APP_KEY_SIZE];

// Default Device Key (16 bytes) - Khi chưa provisioned
#define DEFAULT_DEV_KEY_SIZE 16
extern const uint8_t default_dev_key[DEFAULT_DEV_KEY_SIZE];

// Key Indices
#define DEFAULT_NETKEY_INDEX 0x0001
#define DEFAULT_APPKEY_INDEX 0x0001

// Default Network Subnet Index
#define DEFAULT_NETWORK_SUBNET_INDEX 0x0001

// Default Network Timeout (seconds)
#define DEFAULT_NETWORK_DEFAULT_TIME_S 70 // 70 seconds

/**
 * @brief Check if default network is currently active
 *
 * @return true if default network is active, false otherwise
 */
bool default_network_is_present(void);

/**
 * @brief Process default network - called periodically
 *
 * @details
 * - Nếu chưa provisioned: mở default network (set_tmp_keys)
 * - Nếu đã provisioned: xóa default network sau timeout
 */
void proc_default_network(void);

/**
 * @brief Set temporary keys (default network keys)
 *
 * @param enable true to enable default network, false to disable
 *
 * @details
 * - Thêm NetKey và AppKey mặc định vào stack
 * - Bind AppKey vào tất cả models
 * - Cho phép thiết bị nhận messages từ default network
 */
void set_tmp_keys(bool enable);

/**
 * @brief Delete temporary keys (default network keys)
 *
 * @details
 * - Xóa NetKey và AppKey mặc định
 * - Unbind AppKey khỏi models
 * - Đóng default network
 */
void del_tmp_keys(void);

/**
 * @brief Manually set default network state
 *
 * @param enable true to enable, false to disable
 * @param time_s timeout in seconds (0 = use default timeout)
 *
 * @details
 * - Enable/disable default network manually
 * - Set timeout for automatic cleanup
 */
void set_default_network_manual(bool enable, uint16_t time_s);

/**
 * @brief Check and delete default network on power on
 *
 * @details
 * - Kiểm tra nếu default network còn tồn tại trong flash
 * - Xóa nếu tồn tại (cleanup)
 */
void check_and_del_default_power_on(void);

/**
 * @brief Khởi tạo monitor default network
 *
 * @details
 * Bắt đầu monitor work để in thông tin default network mỗi 10 giây.
 * Thông tin bao gồm:
 * - Trạng thái mở/đóng
 * - Timeout và thời gian còn lại
 * - Keys (NetKey, AppKey, Subnet)
 * - Provision state
 */
void default_network_monitor_init(void);

/* Hàm get_provision_state() và is_main_network_provisioned() đã được di chuyển
 * sang fast_provision.h */
/* Include fast_provision.h để sử dụng các hàm này */

/**
 * @brief In thông tin tất cả các mạng (Subnets)
 */
void default_network_print_info(void);

#ifdef __cplusplus
}
#endif

#endif /* DEFAULT_NETWORK_H__ */
