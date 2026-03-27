/**
 * @file network.c
 * @brief Network Information Management Implementation
 *
 * @details
 * This file manages and provides information about the device's main network:
 * - Application Key (AppKey) index bounded to models
 * - Network Key (NetKey) index of primary subnet
 * - Unicast address of device
 * - Provisioner address
 *
 * Main functionalities:
 * - Cache AppKey index to avoid repeated searching
 * - Automatically restore AppKey index from models when needed
 * - Provide network information to other modules (mesh_message_handler,
 * fast_provision)
 *
 * @note
 * - Only manages main network information, does not manage default network
 * - AppKey index is cached to optimize performance
 * - Automatically searches for AppKey from models if cache is empty
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================
 */
#include "../../include/network.h"

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/cfg.h>  // For bt_mesh_subnet_del()
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "../../include/default_network.h"  // For default_network_is_present(), del_tmp_keys()
#include "../../include/execution_scene.h"  // For delete_all_execution_scene()
#include "../../include/fast_provision.h"   // For is_main_network_provisioned()
#include "../../include/led.h"              // For led_blink_no_queue()
#include "../../include/led_ev.h"
#include "../../include/mesh_node.h"  // For send_node_reset_status()
#include "../../include/scene.h"
#include "mesh/access.h"  // For bt_mesh_comp_get()
#include "mesh/app_keys.h"  // For bt_mesh_app_keys_get(), bt_mesh_app_key_exists(), bt_mesh_app_key_del()
#include "mesh/foundation.h"  // For bt_mesh_primary_addr()
#include "mesh/net.h"  // For bt_mesh_subnet_next(), bt_mesh_net_loopback_clear()

LOG_MODULE_REGISTER(network, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * DELAYED WORK FOR FACTORY RESET
 * ============================================================================
 */

/* Delayed work to perform mesh reset and reboot after sending Node Reset Status
 */
static struct k_work_delayable factory_reset_work;

/* Delayed work to perform reboot after stack reset (callback from mesh stack)
 */
static struct k_work_delayable post_reset_work;

/* Work handler: Perform reboot after stack reset */
static void post_reset_work_handler(struct k_work* work) {
  LOG_INF("Post Reset Work: Rebooting device...");

  // 1. Reset network info cache
  network_reset_info();
  delete_all_execution_scene();
  delete_all_scene();

  // 2. Reboot system (cold reboot to ensure all state is reset)
  k_sleep(K_MSEC(1000));
  sys_reboot(SYS_REBOOT_COLD);
}

/* ============================================================================
 * PROVISIONING TOGGLE (5 MINUTES TIMEOUT, 5S LED BLINK)
 * ============================================================================
 */

#if EN_PROVISIONING_TOGGLE

#define PROVISIONING_TIMEOUT_MIN 5
#define PROVISIONING_LED_BLINK_INTERVAL_SEC 5

static bool provisioning_mode_active = false;
static struct k_work_delayable provisioning_timeout_work;
static struct k_work_delayable provisioning_led_blink_work;
static struct k_work_delayable provisioning_enable_delayed_work;
static struct k_work_delayable provisioning_stop_keys_delayed_work;

/* Forward declarations */
static void provisioning_led_blink_work_handler(struct k_work* work);
static void provisioning_enable_delayed_work_handler(struct k_work* work);
static void provisioning_stop_keys_delayed_work_handler(struct k_work* work);
static void provisioning_stop(void);

/* Stop all provisioning works and reset state */
static void provisioning_stop(void) {
  if (!provisioning_mode_active) {
    return;
  }

  /* Block toggle OFF if fast provision is actively in progress */
  if (fast_provision_is_busy()) {
    LOG_INF("Provisioning stop blocked: fast provision in progress");
    return;
  }

  LOG_INF("Provisioning mode disabling...");
  provisioning_mode_active = false;

  /* 1. Blink Red to notify user that mode exited */
  led_blink_color(1 << CONFIG_LED_IDX_BLUETOOTH, LED_COLOR_RED, 1,
                  LAST_STATE_REFRESH_LED, 300);

  /* 2. Cancel existing tasks */
  k_work_cancel_delayable(&provisioning_timeout_work);
  k_work_cancel_delayable(&provisioning_led_blink_work);

  /* 3. Disable Provisioning bearers */
  bt_mesh_prov_disable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

  /* 4. Wait 200ms via Workqueue to cleanup Keys (non-blocking) */
  k_work_init_delayable(&provisioning_stop_keys_delayed_work,
                        provisioning_stop_keys_delayed_work_handler);
  k_work_schedule(&provisioning_stop_keys_delayed_work, K_MSEC(200));
}

/* Helper Handler: Cleanup Keys after GATT is fully disabled */
static void provisioning_stop_keys_delayed_work_handler(struct k_work* work) {
  LOG_INF("Provisioning cleanup: Disabling Default Network keys.");
  set_tmp_keys(false);
}

/* Work handler: Disable provisioning after timeout */
static void provisioning_timeout_work_handler(struct k_work* work) {
  LOG_INF("Provisioning timeout (%d min). Disabling.",
          PROVISIONING_TIMEOUT_MIN);
  provisioning_stop();
}

/* Work handler: Periodic LED blink every 5 seconds */
static void provisioning_led_blink_work_handler(struct k_work* work) {
  if (!provisioning_mode_active) {
    return;
  }
  /* Blink pink once */
  led_blink_color(1 << CONFIG_LED_IDX_BLUETOOTH, LED_COLOR_BLUE, 1,
                  LAST_STATE_REFRESH_LED, 300);
  /* Reschedule for next blink */
  k_work_schedule(&provisioning_led_blink_work,
                  K_SECONDS(PROVISIONING_LED_BLINK_INTERVAL_SEC));
}

/* Helper Handler: Enable Provisioning after Keys have stabilized for 2s */
static void provisioning_enable_delayed_work_handler(struct k_work* work) {
  int err = bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

  if (err && err != -EALREADY) {
    LOG_ERR("Failed to enable provisioning (delayed): %d", err);
    set_tmp_keys(false);
    provisioning_mode_active = false;
    return;
  }

  provisioning_mode_active = true;
  LOG_INF("Provisioning ENABLED successfully.");

  /* Start pink LED blink cycle (5s interval) and set 5-minute timeout */
  k_work_init_delayable(&provisioning_led_blink_work,
                        provisioning_led_blink_work_handler);
  k_work_schedule(&provisioning_led_blink_work,
                  K_SECONDS(PROVISIONING_LED_BLINK_INTERVAL_SEC));

  k_work_init_delayable(&provisioning_timeout_work,
                        provisioning_timeout_work_handler);
  k_work_schedule(&provisioning_timeout_work,
                  K_MINUTES(PROVISIONING_TIMEOUT_MIN));
}

/**
 * @brief Toggle network joining mode
 *
 * @details
 * - If device is already provisioned, ignore
 * - If mode is active, disable it
 * - If mode is not active, enable it with:
 *   - Blink pink LED immediately (300ms)
 *   - Then blink pink every 5s
 *   - Auto-disable after 5 minutes
 */
void network_enable_provisioning_with_timeout(void) {
  /* If already provisioned to main network, ignore */
  if (network_is_main_network_provisioned()) {
    LOG_INF("Device already provisioned. Ignoring button press.");
    return;
  }

  /* Toggle: if active, then disable */
  if (provisioning_mode_active) {
    provisioning_stop();
    return;
  }

  /* Step 1: Blink pink LED immediately (Visual feedback) */
  led_blink_color(1 << CONFIG_LED_IDX_BLUETOOTH, LED_COLOR_BLUE, 1,
                  LAST_STATE_REFRESH_LED, 300);

  /* Step 2: Enable Default Network for Fast Provision */
  set_tmp_keys(true);

  /* Step 3: Schedule 2s delay to load Keys stably, then enable GATT
   * (non-blocking) */
  k_work_init_delayable(&provisioning_enable_delayed_work,
                        provisioning_enable_delayed_work_handler);
  k_work_schedule(&provisioning_enable_delayed_work, K_MSEC(2000));

  LOG_INF("Provisioning enable initiated. GATT start scheduled in 2s.");
}

/* Stop mode quietly (only cancel timers/works, no network/LED changes) */
void provisioning_stop_quietly(void) {
  if (!provisioning_mode_active) {
    return;
  }
  LOG_INF("Provisioning mode stopped quietly.");
  provisioning_mode_active = false;
  k_work_cancel_delayable(&provisioning_timeout_work);
  k_work_cancel_delayable(&provisioning_led_blink_work);
}
#endif /* EN_PROVISIONING_TOGGLE */

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/**
 * @brief Default provisioner address
 *
 * @details
 * Default unicast address of provisioner in main network.
 * Can be dynamically updated when receiving messages from provisioner.
 */
#define NETWORK_PROVISIONER_ADDRESS 0x0001

/* ============================================================================
 * PRIVATE VARIABLES
 * ============================================================================
 */

/**
 * @brief Cached Application Key index
 *
 * @details
 * - 0xFFFF: No cache (not bound or not found)
 * - Not 0xFFFF: AppKey index bounded to models
 *
 * @note
 * This cache is updated when:
 * - AppKey is bounded to models (call network_set_appkey_index())
 * - AppKey is found from models (in network_get_appkey_index())
 */
static uint16_t cached_appkey_index = 0xFFFF;

/* ============================================================================
 * PRIVATE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Check if AppKey belongs to main network (subnet 0x0000)
 *
 * @param app_idx Application Key index to check
 *
 * @return true if AppKey belongs to main network, false if not
 *
 * @details
 * Check if AppKey belongs to primary subnet (main network).
 * Exclude Default Network AppKey (subnet 0x0001).
 */
static bool is_appkey_from_main_network(uint16_t app_idx) {
  /* Get main network AppKeys list (subnet 0x0000) */
  uint16_t app_keys[16] = {0}; /* Max 16 AppKeys */
  ssize_t count = bt_mesh_app_keys_get(BT_MESH_NET_PRIMARY, app_keys,
                                       ARRAY_SIZE(app_keys), 0);

  if (count < 0) {
    /* Cannot get list -> assume AppKey belongs to main network */
    return true;
  }

  /* Check if AppKey is in the list */
  for (ssize_t i = 0; i < count; i++) {
    if (app_keys[i] == app_idx) {
      return true; /* AppKey belongs to main network */
    }
  }

  /* AppKey not in main network list -> belongs to default network */
  return false;
}

/**
 * @brief Find AppKey index bounded to models (only Main Network AppKey)
 *
 * @return AppKey index if found, 0xFFFF if not found
 *
 * @details
 * This functions scans all models (SIG and vendor) in all elements
 * to find the first AppKey index that is bounded AND belongs to main network.
 *
 * Process:
 * 1. Check if device is provisioned
 * 2. Get composition data
 * 3. Scan all models in all elements
 * 4. Check keys of each model
 * 5. Check if AppKey belongs to main network (exclude default network AppKey)
 * 6. Return first valid AppKey index found
 *
 * @note
 * - Only return Main Network AppKey (subnet 0x0000)
 * - Exclude Default Network AppKey (subnet 0x0001)
 * - This function only finds AppKey index, does not cache. To cache, call
 * network_set_appkey_index().
 */
static uint16_t find_bound_appkey_index_internal(void) {
  /* Get composition data */
  const struct bt_mesh_comp* comp = bt_mesh_comp_get();
  if (!comp) {
    return 0xFFFF;
  }

  /* Scan all elements */
  for (int elem_idx = 0; elem_idx < comp->elem_count; elem_idx++) {
    const struct bt_mesh_elem* elem = &comp->elem[elem_idx];

    /* Check standard models */
    for (int i = 0; i < elem->model_count; i++) {
      struct bt_mesh_model* model = (struct bt_mesh_model*)&elem->models[i];
      for (int j = 0; j < model->keys_cnt; j++) {
        if (model->keys[j] != BT_MESH_KEY_UNUSED) {
          uint16_t app_idx = model->keys[j];
          /* Check if AppKey exists and belongs to main network */
          if (bt_mesh_app_key_exists(app_idx) &&
              is_appkey_from_main_network(app_idx)) {
            return app_idx; /* Found Main Network AppKey */
          }
        }
      }
    }

    /* Check vendor models */
    for (int i = 0; i < elem->vnd_model_count; i++) {
      struct bt_mesh_model* model = (struct bt_mesh_model*)&elem->vnd_models[i];
      for (int j = 0; j < model->keys_cnt; j++) {
        if (model->keys[j] != BT_MESH_KEY_UNUSED) {
          uint16_t app_idx = model->keys[j];
          /* Check if AppKey exists and belongs to main network */
          if (bt_mesh_app_key_exists(app_idx) &&
              is_appkey_from_main_network(app_idx)) {
            return app_idx; /* Found Main Network AppKey */
          }
        }
      }
    }
  }

  return 0xFFFF;
}

/* ============================================================================
 * PUBLIC FUNCTIONS - Network Information Getters
 * ============================================================================
 */

uint16_t network_get_netkey_index(void) {
  /* Main network always uses primary subnet (0x0000) */
  return BT_MESH_NET_PRIMARY;
}

uint16_t network_get_unicast_address(void) {
  /* Get device primary address */
  uint16_t addr = bt_mesh_primary_addr();

  /* Check for valid address */
  if (addr == BT_MESH_ADDR_UNASSIGNED) {
    return 0xFFFF;
  }

  return addr;
}

uint16_t network_get_provisioner_address(void) {
  return NETWORK_PROVISIONER_ADDRESS;
}

bool network_is_main_network_provisioned(void) {
  return bt_mesh_is_main_network_provisioned();
}

uint8_t get_provision_state(void) {
  if (network_is_main_network_provisioned()) {
    return STATE_DEV_PROVED;
  }
  /* TODO: Logic to detect if we are currently "PROVING"
   * For now, return UNPROV or PROVED based on main network status.
   */
  return STATE_DEV_UNPROV;
}

/**
 * @brief Get Main Network Application Key index
 *
 * @return AppKey index if found, 0xFFFF if not found
 *
 * @details
 * This function returns AppKey index that is bounded to models in main
 * network.
 */
uint16_t network_get_appkey_index(void) {
  /* If cached, check if cache is valid (belongs to main network) */
  if (cached_appkey_index != 0xFFFF) {
    /* Check if cache belongs to main network */
    if (is_appkey_from_main_network(cached_appkey_index)) {
      return cached_appkey_index; /* Cache hit, return immediately */
    } else {
      /* Cache invalid (belongs to default network) -> clear cache and
       * re-search
       */
      cached_appkey_index = 0xFFFF;
    }
  }

  /* Find from models and cache */
  uint16_t app_idx = find_bound_appkey_index_internal();
  if (app_idx != 0xFFFF) {
    /* Ensure AppKey belongs to main network before caching */
    if (is_appkey_from_main_network(app_idx)) {
      cached_appkey_index = app_idx;
    } else {
      return 0xFFFF; /* Do not cache Default Network AppKey */
    }
  }

  return app_idx;
}

/* ============================================================================
 * PUBLIC FUNCTIONS - Network Information Setters
 * ============================================================================
 */

/**
 * @brief Set bounded Application Key index
 *
 * @param app_idx Application Key index
 *
 * @details
 * Cache AppKey index for later use. This function is called when:
 * - AppKey is bounded to models (in mesh_message_handler.c)
 * - AppKey is added to mesh stack (in fast_provision.c)
 */
void network_set_appkey_index(uint16_t app_idx) {
  if (app_idx != 0xFFFF) {
    /* Only cache Main Network AppKey */
    if (is_appkey_from_main_network(app_idx)) {
      cached_appkey_index = app_idx;
    }
  }
}

/**
 * @brief Update provisioner address if needed
 *
 * @param addr New address from message
 *
 * @details
 * This functions is called when receiving message from provisioner to update
 * address.
 *
 * @note
 * Currently only logs, dynamic update not implemented because provisioner
 * address is usually fixed (0x0001). Can be extended later if needed.
 */
void network_update_provisioner_address(uint16_t addr) {
  /* Check valid address (unicast address) */
  if (addr != BT_MESH_ADDR_UNASSIGNED && (addr & 0xC000) == 0x0000) {
    /* TODO: Implement dynamic update if needed */
  }
}

/**
 * @brief Reset network info (when device reset)
 *
 * @details
 * Clear AppKey index cache and reset to initial state.
 * This function is called when:
 * - Device is reset (in main.c)
 * - Mesh configuration is cleared
 *
 * @note
 * After reset, network_get_appkey_index() will automatically re-search from
 * models.
 */
void network_reset_info(void) { cached_appkey_index = 0xFFFF; }

/**
 * @brief Factory Reset - Fully reset and reboot chip
 *
 * @details
 * Fully reset mesh stack and clear settings, device will be unprovisioned
 * after reboot.
 */
void factory_reset_and_reboot(void) {
  LOG_INF("Factory Reset: Leaving main network");
  if (network_is_main_network_provisioned()) {
    LOG_INF("Sending Node Reset Status to provisioner...");
    send_node_reset_status();
  } else {
    LOG_INF("Device not provisioned, scheduling factory reset immediately...");
  }
  /* If node is not provisioned, bt_mesh_reset() returns early without
   * calling the prov->reset callback. Directly trigger the reset handler
   * so the device still reboots properly.
   */
  if (!network_is_main_network_provisioned()) {
    network_handle_node_reset();
  }
  LOG_INF("Calling bt_mesh_reset()...");
  bt_mesh_reset();
}

/**
 * @brief Handle Node Reset from Stack Callback
 *
 * @details
 * This function is called by bt_mesh_prov.reset callback when the stack
 * processes a Node Reset command.
 * The stack has already cleared the network state, so we only need to:
 * 1. Blink LED
 * 2. Clear application data
 * 3. Reboot
 */
void network_handle_node_reset(void) {
  LOG_INF("Network Node Reset Triggered (from Stack Callback)");
  // Notify led
  led_ev_handle(LED_NOTIFY_RESET_BLUETOOTH_MESH, CONFIG_LED_MASK_BLUETOOTH);
  k_work_init_delayable(&post_reset_work, post_reset_work_handler);
  k_work_schedule(&post_reset_work, K_SECONDS(2));
}

/* ============================================================================
 * PROVISIONING BLINK CONTROL
 * ============================================================================
 */

/* Global flag to control provisioning LED blink */
static bool g_provisioning_blink_enabled = false;

void network_allow_provision_blink(bool allow) {
  g_provisioning_blink_enabled = allow;
  LOG_DBG("Provisioning blink allowed: %d", allow);
}

bool network_can_blink_provision(void) { return g_provisioning_blink_enabled; }
