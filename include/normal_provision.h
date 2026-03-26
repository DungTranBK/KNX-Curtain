/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Bluetooth Mesh provisioning handler for Nordic DKs.
 * @defgroup bt_mesh_dk_prov Bluetooth Mesh provisioning handler for Nordic DKs
 * @{
 */

#ifndef BT_MESH_DK_PROV_H__
#define BT_MESH_DK_PROV_H__

#include <zephyr/bluetooth/mesh.h>

#include "stdint.h"


#ifdef __cplusplus
extern "C" {
#endif

struct bt_mesh_dk_prov_callbacks {
  void (*prov_complete)(uint16_t net_idx, uint16_t src);
};
/** @brief Initialize the provisioning handler.
 *
 * @return The provisioning properties to pass to @em bt_mesh_init().
 */
const struct bt_mesh_prov* bt_mesh_dk_prov_custom_init(
    struct bt_mesh_dk_prov_callbacks* cb);

/** @brief Initialize Normal Provisioning (PB-ADV)
 *
 * Enable PB-ADV provisioning bearer để cho phép thiết bị gia nhập mạng
 * qua standard Bluetooth Mesh provisioning process.
 *
 * Hàm này cho phép cả normal provision và fast provision cùng hoạt động
 * khi thiết bị chưa có main network. Người dùng có thể chọn 1 trong 2 cách để
 * gia nhập.
 *
 * @note
 * - Gọi hàm này sau khi bt_mesh_init() đã được gọi
 * - Có thể gọi cùng lúc với fast_provision_init() để cho phép cả 2 chế độ
 * - Unprovision beacon sẽ chỉ được gửi khi hàm này được gọi
 *
 * @return 0 nếu thành công, error code nếu thất bại
 */
int normal_provision_init(void);

uint8_t* get_device_uuid(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_MESH_DK_PROV_H__ */

/** @} */
