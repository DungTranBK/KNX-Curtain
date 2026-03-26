/**
 * @file vendor.h
 * @brief Vendor Model Opcodes and Constants
 * @details Định nghĩa các opcodes và constants cho vendor models
 *
 * Tổ chức các vendor opcodes theo chức năng để dễ quản lý và tái sử dụng
 */

#ifndef VENDOR_H__
#define VENDOR_H__

#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VENDOR COMPANY IDS
 * ============================================================================
 */

/* Telink Semiconductor Company ID */
#define VENDOR_COMPANY_ID_TELINK 0x0211

/* Mesh Version ID */
#define MESH_VID 0x3130

/* ============================================================================
 * FAST PROVISION OPCODES - Telink Compatible
 * ============================================================================
 *
 * Tất cả opcodes sử dụng 3-byte vendor opcode format:
 * - Byte 1: 0xC[5-D] (opcode base)
 * - Byte 2-3: Company ID (0x0211 = Telink)
 *
 * Format: BT_MESH_MODEL_OP_3(OP_BYTE, COMPANY_ID)
 * ============================================================================
 */

/* Fast Provision Vendor Company ID */
#define FAST_PROV_VENDOR_COMPANY_ID VENDOR_COMPANY_ID_TELINK

/* 0xC5: Reset Network
 * - Direction: Provisioner → Provisionee
 * - Purpose: Yêu cầu thiết bị reset network và mở default network
 * - Payload: Optional delay (uint16_t, milliseconds)
 * - Response: None (unacknowledged)
 */
#define VD_MESH_RESET_NETWORK                                                  \
  BT_MESH_MODEL_OP_3(0xC5, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xC6: Get MAC Address (ADDR_GET)
 * - Direction: Provisioner → Provisionee
 * - Purpose: Yêu cầu thiết bị gửi MAC address và unicast address hiện tại
 * - Payload: pid (uint16_t), ele_addr (uint16_t, optional)
 * - Response: VD_MESH_ADDR_GET_STS (0xC7)
 */
#define VD_MESH_ADDR_GET BT_MESH_MODEL_OP_3(0xC6, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xC7: MAC Address Response (ADDR_GET_STS)
 * - Direction: Provisionee → Provisioner
 * - Purpose: Trả về MAC address, PID và unicast address hiện tại
 * - Payload: mac[6], pid (uint16_t), default_addr (uint16_t)
 * - Triggered by: VD_MESH_ADDR_GET (0xC6)
 */
#define VD_MESH_ADDR_GET_STS                                                   \
  BT_MESH_MODEL_OP_3(0xC7, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xC8: Set Unicast Address (ADDR_SET)
 * - Direction: Provisioner → Provisionee
 * - Purpose: Gán unicast address mới cho thiết bị
 * - Payload: mac[6], ele_addr (uint16_t)
 * - Response: VD_MESH_ADDR_SET_STS (0xC9)
 */
#define VD_MESH_ADDR_SET BT_MESH_MODEL_OP_3(0xC8, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xC9: Set Address Response (ADDR_SET_STS)
 * - Direction: Provisionee → Provisioner
 * - Purpose: Xác nhận đã nhận và lưu unicast address
 * - Payload: None (empty)
 * - Triggered by: VD_MESH_ADDR_SET (0xC8)
 */
#define VD_MESH_ADDR_SET_STS                                                   \
  BT_MESH_MODEL_OP_3(0xC9, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xCA: Set Provision Data (PROV_DATA_SET)
 * - Direction: Provisioner → Provisionee
 * - Purpose: Gửi network keys (NetKey, AppKey) và thông tin network
 * - Payload: provison_net_info_str + mesh_appkey_set_t
 * - Response: None (unacknowledged)
 */
#define VD_MESH_PROV_DATA_SET                                                  \
  BT_MESH_MODEL_OP_3(0xCA, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xCB: Provision Confirm
 * - Direction: Provisioner → Provisionee
 * - Purpose: Xác nhận trước khi provision vào main network
 * - Payload: None (empty)
 * - Response: VD_MESH_PROV_CONFIRM_STS (0xCC)
 */
#define VD_MESH_PROV_CONFIRM                                                   \
  BT_MESH_MODEL_OP_3(0xCB, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xCC: Provision Confirm Response (PROV_CONFIRM_STS)
 * - Direction: Provisionee → Provisioner
 * - Purpose: Xác nhận đã sẵn sàng provision
 * - Payload: None (empty)
 * - Triggered by: VD_MESH_PROV_CONFIRM (0xCB)
 */
#define VD_MESH_PROV_CONFIRM_STS                                               \
  BT_MESH_MODEL_OP_3(0xCC, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xCD: Provision Complete
 * - Direction: Provisioner → Provisionee
 * - Purpose: Báo hiệu hoàn tất fast provision, thiết bị sẽ provision vào main
 * network
 * - Payload: Optional delay (uint16_t, milliseconds)
 * - Response: None (unacknowledged)
 */
#define VD_MESH_PROV_COMPLETE                                                  \
  BT_MESH_MODEL_OP_3(0xCD, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF0: Factory Reset (Config Node Set NoAck)
 * - Direction: Provisioner → Provisionee
 * - Purpose: Yêu cầu thiết bị factory reset và rời mạng
 * - Payload: None (empty)
 * - Response: None (unacknowledged)
 * - Note: This is an unacknowledged version of VD_CONFIG_NODE_SET_ACK
 */
#define VD_CONFIG_NODE_SET_NOACK                                               \
  BT_MESH_MODEL_OP_3(0xF0, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF1: Config Model Subscription Set
 * - Direction: Provisioner → Provisionee
 * - Purpose: Set subscription for models
 */
#define VD_CONFIG_MODEL_SUB_SET                                                \
  BT_MESH_MODEL_OP_3(0xF1, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF2: Config Model Subscription Status
 * - Direction: Provisionee → Provisioner
 * - Purpose: Status of subscription set
 */
#define VD_CONFIG_MODEL_SUB_STATUS                                             \
  BT_MESH_MODEL_OP_3(0xF2, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF3: Vendor Scene Request
 * - Direction: Provisioner → Provisionee
 * - Purpose: Request scene operation
 */
#define VD_SCENE_REQUEST BT_MESH_MODEL_OP_3(0xF3, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF4: Vendor Scene Request NoAck
 * - Direction: Provisioner → Provisionee
 * - Purpose: Request scene operation (Unacknowledged)
 */
#define VD_SCENE_REQUEST_NOACK                                                 \
  BT_MESH_MODEL_OP_3(0xF4, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF5: Vendor Scene Response
 * - Direction: Provisionee → Provisioner
 * - Purpose: Response to Scene Request
 */
#define VD_SCENE_RESPONSE BT_MESH_MODEL_OP_3(0xF5, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF6: Setup Execution Scene Set */
#define VD_SETUP_EXECUTION_SCENE_SET                                           \
  BT_MESH_MODEL_OP_3(0xF6, VENDOR_COMPANY_ID_TELINK)

/* 0xF7: Setup Execution Scene Status */
#define VD_SETUP_EXECUTION_SCENE_STATUS                                        \
  BT_MESH_MODEL_OP_3(0xF7, VENDOR_COMPANY_ID_TELINK)

/* 0xF8: Config Node Set Ack
 * - Direction: Provisioner → Provisionee
 * - Purpose: Configure node parameters with acknowledgement
 * - Payload: Variable
 * - Response: VD_CONFIG_NODE_STATUS (0xF9)
 */
#define VD_CONFIG_NODE_SET_ACK                                                 \
  BT_MESH_MODEL_OP_3(0xF8, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xF9: Config Node Status
 * - Direction: Provisionee → Provisioner
 * - Purpose: Response to VD_CONFIG_NODE_SET_ACK
 * - Payload: Variable (Status/Data)
 * - Triggered by: VD_CONFIG_NODE_SET_ACK (0xF8)
 */
#define VD_CONFIG_NODE_STATUS                                                  \
  BT_MESH_MODEL_OP_3(0xF9, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xDC: DC binding control locally */
#define VD_BINDING_CONTROL_LOCALLY                                             \
  BT_MESH_MODEL_OP_3(0xDC, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xDD: Dimming control set */
#define VD_DIMMING_CONTROL_SET                                                 \
  BT_MESH_MODEL_OP_3(0xDD, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xE0: Auto transition set */
#define VD_AUTO_TRANS_SET BT_MESH_MODEL_OP_3(0xE0, FAST_PROV_VENDOR_COMPANY_ID)
/* 0xE1: Auto transition set noack */
#define VD_AUTO_TRANS_SET_NOACK                                                \
  BT_MESH_MODEL_OP_3(0xE1, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xE2: Light option get */
#define VD_LIGHT_OPT_GET BT_MESH_MODEL_OP_3(0xE2, FAST_PROV_VENDOR_COMPANY_ID)
/* 0xE3: Light option status */
#define VD_LIGHT_OPT_STATUS                                                    \
  BT_MESH_MODEL_OP_3(0xE5, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xEE: Light state get */
#define VD_LIGHT_STATE_GET BT_MESH_MODEL_OP_3(0xEE, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xEF: Light state status */
#define VD_LIGHT_STATE_STATUS                                                  \
  BT_MESH_MODEL_OP_3(0xEF, FAST_PROV_VENDOR_COMPANY_ID)

/* 0xEF: Light state status */
#define VD_BINDING_CONTROL_LOCALLY                                             \
  BT_MESH_MODEL_OP_3(0xDC, FAST_PROV_VENDOR_COMPANY_ID)

/* ============================================================================
 * FAST PROVISION CONSTANTS
 * ============================================================================
 */

/* Default delays (milliseconds) */
#define FAST_PROV_RESET_DELAY_MS 1500    // Delay sau RESET_NETWORK
#define FAST_PROV_COMPLETE_DELAY_MS 2000 // Delay sau PROV_COMPLETE
#define FAST_PROV_PROC_INTERVAL_MS 100 // Interval cho state machine processing

/* Fast Provision Timeout */
#define FAST_PROVISION_TIMEOUT_MS (60 * 1000) // 60 seconds

/* ============================================================================
 * BLUETOOTH MESH CONFIGURATION MODEL OPCODES
 * ============================================================================
 */

/* 0x8049: Config Node Reset (SIG defined)
 * - Direction: Provisioner → Provisionee
 * - Purpose: Yêu cầu thiết bị reset và rời mạng
 * - Payload: None (empty)
 * - Response: Config Node Reset Status (0x4A80)
 */
#define MESH_MODEL_OP_CFG_NODE_RESET 0x8049

/* 0x4A80: Node Reset Status
 * - Direction: Provisionee → Provisioner
 * - Purpose: Báo cho provisioner biết thiết bị sẽ rời mạng (factory reset)
 * - Payload: None (empty)
 * - Response: None (unacknowledged)
 * - Note: Gửi trước khi factory reset để provisioner biết thiết bị sẽ rời mạng
 */
#define MESH_MODEL_OP_CFG_NODE_RESET_STATUS 0x4A80

/* ============================================================================
 * VENDOR MODEL IDS
 * ============================================================================
 */

/* Fast Provision Model ID - Vendor Model ID (phải = 0x0000 để tương thích với
 * provisioner) */
#define BT_MESH_MODEL_ID_VND_FAST_PROV 0x0000

/* Config Node Model ID - Vendor Model ID (chứa factory reset và các config
 * commands khác) */
#define BT_MESH_MODEL_ID_VND_CONFIG_NODE 0x0001

/* ============================================================================
 * PRODUCT ID (PID) - Tương thích với Telink
 * ============================================================================
 *
 * Product ID được dùng để identify device type trong quá trình provisioning
 * - Telink: embed PID vào composition data (MESH_PID_SEL)
 * - Nordic: embed PID vào UUID (bytes 2-3) để provisioner đọc được
 *
 * Giá trị PID tương thích với Telink version.h:
 * - LM_PC_BLE = 0x0890 (default)
 * - LM_TWO_IN_ONE_OUT_HP_MD = 0x0091
 * - LM_DOOR_FULL_SENSOR = 0x0883
 * - PID_LM_PIR_AC = 0x0700
 * ============================================================================
 */
#define PID_LUTO_CURTAIN_1_CHANNEL (0x0921)
#define PID_LUTO_CURTAIN_2_CHANNEL (0x0922)

#define PID_KNX_CURTAIN_1_CHANNEL (0x0BF2)
#define PID_KNX_CURTAIN_2_CHANNEL (0x0BF3)

#define LM_PID_MESH PID_KNX_CURTAIN_1_CHANNEL // PID BLE

/* Firmware Version: 1.0.0 */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#ifdef __cplusplus
}
#endif

#endif /* VENDOR_H__ */
