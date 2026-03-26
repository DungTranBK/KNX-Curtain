/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "../../include/normal_provision.h"

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/main.h>  // For bt_mesh_is_main_network_provisioned()
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#include "../../include/app_device.h"
#include "../../include/network.h"
#include "../../include/vendor.h"  // For LM_PID_MESH
#include "mesh/access.h"
LOG_MODULE_REGISTER(dk_bt_mesh_prov_custom, CONFIG_BT_MESH_DK_PROV_LOG_LEVEL);

static void prov_reset(void) {
  LOG_INF("Node Reset triggered from Mesh Stack");
  network_handle_node_reset();
}

static uint8_t dev_uuid[16];

static struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    /* Advertise No OOB: no output/input actions or sizes */
    .output_actions = 0,
    .output_number = NULL,
    .output_string = NULL,
    .input = NULL,
    .input_complete = NULL, /* OOB disabled */
    .reset = prov_reset,
};

const struct bt_mesh_prov* bt_mesh_dk_prov_custom_init(
    struct bt_mesh_dk_prov_callbacks* cb) {
  /* Generate an RFC-4122 version 4 compliant UUID.
   * Format:
   *
   * 0                   1                   2                   3
   * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          time_low                             |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |       time_mid                |         time_hi_and_version   |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |clk_seq_hi_res |  clk_seq_low  |         node (0-1)            |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                         node (2-5)                            |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * Where the 4 most significant bits of time_hi_and_version shall be
   * 0b0010 and the 2 most significant bits of clk_seq_hi_res shall be
   * 0b10. The remaining fields have no required values, and are fetched
   * from the HW info device ID. The fields are encoded in big endian
   * format.
   *
   * https://tools.ietf.org/html/rfc4122
   */
  size_t id_len = hwinfo_get_device_id(dev_uuid, sizeof(dev_uuid));

  if (!IS_ENABLED(CONFIG_BT_MESH_DK_LEGACY_UUID_GEN)) {
    /* Fill rest of buffer with inverted device ID if shorter than UUID size */
    for (size_t i = id_len; i < sizeof(dev_uuid); i++) {
      dev_uuid[i] = dev_uuid[i % id_len] ^ 0xff;
    }
  }

  /* Embed PID (Bytes 2-3) - as per vendor.h comment */
  /* PID is placed at bytes 2-3 for Provisioner to easily identify device type
   */
  dev_uuid[2] = (LM_PID_MESH >> 8) & 0xFF;
  dev_uuid[3] = LM_PID_MESH & 0xFF;

  /* Embed Firmware Version (Bytes 10-12) */
  /* Version 1.0.0 -> 01 00 00 */
  dev_uuid[10] = FW_VERSION_MAJOR;
  dev_uuid[11] = FW_VERSION_MINOR;
  dev_uuid[12] = FW_VERSION_PATCH;

  LOG_INF("Device UUID generated with PID: 0x%04X, Version: %d.%d.%d",
          LM_PID_MESH, FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
  LOG_HEXDUMP_INF(dev_uuid, 16, "Device UUID:");

  if (cb->prov_complete != NULL) {
    prov.complete = cb->prov_complete;
  } else {
    prov.complete =
        NULL; /* No default callback - mesh_message_handler provides one */
  }

  return &prov;
}

/*
 * Get device UUID
 */
uint8_t* get_device_uuid(void) { return dev_uuid; }

/**
 * @brief Initialize Normal Provisioning (PB-ADV)
 *
 * @details
 * This function enables PB-ADV provisioning bearer to allow device to join
 * network via standard Bluetooth Mesh provisioning process.
 *
 * The function will:
 * - Check if device already has main network
 * - If no main network, enable PB-ADV provisioning
 * - Allow both normal provision and fast provision to work
 * - User can choose one of the two methods to join network
 *
 * @note
 * - Call this function after bt_mesh_init() has been called
 * - Can be called simultaneously with fast_provision_init() to allow both modes
 * - Unprovision beacon will only be sent when this function is called
 *
 * @return 0 on success, error code on failure
 */
int normal_provision_init(void) {
  int err;

  /* Check if device already has main network */
  if (network_is_main_network_provisioned()) {
    LOG_INF(
        "Device already provisioned into main network, skipping normal "
        "provision init");
    return 0; /* Main network exists, no need to enable normal provisioning */
  }

  /* Check if mesh stack is initialized */
  const struct bt_mesh_comp* comp = bt_mesh_comp_get();
  if (!comp) {
    LOG_ERR("Mesh stack not initialized, cannot enable normal provisioning");
    return -EAGAIN; /* Mesh stack not ready */
  }

  /* Enable PB-ADV provisioning bearer */
#if EN_PROVISIONING_TOGGLE
  /* If manual toggle is enabled, DO NOT enable provisioning automatically on
   * boot. */
  /* User must press button to enable. */
  LOG_INF(
      "Provisioning toggle enabled - waiting for user input (default: "
      "DISABLED)");
  return 0;
#else
  err = bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
#endif
  if (err) {
    if (err == -EALREADY) {
      /* Already enabled (might be called multiple times) - not an error */
      LOG_INF("PB-ADV provisioning already enabled");
      return 0;
    } else {
      LOG_ERR("Failed to enable PB-ADV provisioning (err %d)", err);
      return err;
    }
  }

  LOG_INF(
      "Normal provisioning (PB-ADV) enabled - device can join via standard "
      "provisioning");
  LOG_INF("Both normal provision and fast provision are now available");

  /* Allow blink on provision complete */
  network_allow_provision_blink(true);

  return 0;
}
