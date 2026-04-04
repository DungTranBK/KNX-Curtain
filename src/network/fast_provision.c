/**
 * @file fast_provision.c
 * @brief Fast Provision Implementation - Telink Compatible
 * @details Fast Provision implementation allows unprovisioned devices
 *          to receive provisioning commands via Default Network
 *
 * Compatible with Telink SDK vendor/common/fast_provision_model.c
 */
#include "../../include/fast_provision.h"

#include <errno.h> // For ENOENT
#include <string.h>
#include <string.h>                // For strcmp, memcpy
#include <zephyr/bluetooth/addr.h> // For bt_addr_le_t, BT_ADDR_LE_RANDOM
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/cfg.h> // For bt_mesh_subnet_exists()
#include <zephyr/bluetooth/mesh/cfg.h> // For bt_mesh_subnet_del()
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h> // For settings_save_one, settings_load_subtree
#include <zephyr/sys/byteorder.h> // For sys_get_be32()

#include "../../include/app_device.h"
#include "../../include/default_network.h"
#include "../../include/led_ev.h"
#include "../../include/vendor_model.h"
#include "mesh/access.h"
#include "mesh/app_keys.h"   // For bt_mesh_app_key_exists()
#include "mesh/foundation.h" // For bt_mesh_primary_addr()
#include "mesh/keys.h"       // For bt_mesh_key_import(), bt_mesh_key_destroy()
#include "mesh/mesh.h"       // For bt_mesh.flags, bt_mesh.dev_key
#include "mesh/net.h" // For bt_mesh_net_create(), bt_mesh_net_settings_commit()
#include "mesh/settings.h" // For bt_mesh_settings_store_pending()
#include "mesh_node.h"
#include "network.h"
#include "vendor.h"

LOG_MODULE_REGISTER(fast_provision,
                    LOG_LEVEL_NONE); // Changed to INF to see all logs

/* Fast Provision Context - Global for handlers to access */
fast_prov_par_t *g_fast_prov = NULL;

/* Address randomization flag - module-level so it can be reset on retry */
static bool default_addr_random = false;

/* Provisioning lock: prevent toggling OFF for 10s after ADDR_SET received */
#define FAST_PROV_LOCK_DURATION_MS 10000
static int64_t prov_lock_start_tick = 0;

/* Deferred MAC response - Telink compatible random delay (0-5000ms)
 * When receiving ADDR_GET, do NOT send response immediately.
 * Instead, save context and send after random delay to avoid RF collision
 * when multiple devices respond simultaneously.
 * Telink: get_mac_time, delay_time, cb_para
 */
static int64_t get_mac_time = 0;      /* Timestamp when ADDR_GET received */
static uint32_t get_mac_delay_ms = 0; /* Random delay in ms (0-5000) */
static const struct bt_mesh_model *get_mac_saved_model = NULL;
static uint16_t get_mac_saved_dst = 0; /* Provisioner address (to reply to) */

/* Forward declaration for deferred MAC response */
static void send_deferred_get_mac_response(void);

/* ============================================================================
 * Main Network Status Check
 * ============================================================================
 */

/* Device MAC Address Header (Telink compatible) - Used to create device key */
/* Device key = MAC[6] + mac_header[10] = 16 bytes */
static const uint8_t mac_header[] = {0x04, 0x95, 0xF1, 0x48, 0x15,
                                     0xC7, 0x53, 0x7B, 0xC9, 0x0F};

/* Product ID - uses LM_PID_MESH from vendor.h */
/* No need to define here anymore, use from vendor.h */

/* Device key generated from MAC + mac_header - used for MAIN NETWORK */
static uint8_t device_key_from_mac[16] = {0};

/**
 * @brief Set device key to default value
 *
 * @details
 * Device key = MAC[6] + mac_header[10] = 16 bytes (Telink compatible)
 *
 * IMPORTANT:
 * - This device key is used for MAIN NETWORK when provisioning
 * - NOT used for default network (default network uses tmp_nk, tmp_ak only)
 * - Device key is generated from device MAC address + fixed header
 * - This device key is used in bt_mesh_provision() to join main network
 */
/* Static MAC address to ensure all models use the same MAC */
static uint8_t device_mac_static[6] = {0};
static bool device_mac_initialized = false;

/* Settings key for MAC address storage (Telink compatible) */
/* Use Settings API instead of direct NVS to ensure persistence */
#define MAC_ADDR_SETTINGS_KEY                                                  \
  "fast_prov/mac" /* Settings key for MAC address                              \
                   */

/* Settings handler for MAC address - use Settings API instead of direct NVS */
static struct settings_handler mac_settings_handler;
static bool mac_settings_registered = false;

/**
 * @brief Settings handler: Get MAC address
 */
static int mac_settings_get(const char *name, char *val, int val_len_max) {
  if (strcmp(name, "mac") == 0) {
    if (val_len_max < 6) {
      return -EINVAL;
    }
    memcpy(val, device_mac_static, 6);
    return 6;
  }
  return -ENOENT;
}

/**
 * @brief Settings handler: Set MAC address (called when settings_load())
 */
static int mac_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg) {
  if (strcmp(name, "mac") == 0) {
    if (len != 6) {
      return -EINVAL;
    }
    ssize_t ret = read_cb(cb_arg, device_mac_static, 6);
    if (ret == 6) {
      device_mac_initialized = true;
      LOG_DBG("MAC loaded from settings: %02X:%02X:%02X:%02X:%02X:%02X",
              device_mac_static[0], device_mac_static[1], device_mac_static[2],
              device_mac_static[3], device_mac_static[4], device_mac_static[5]);
      /* Update to g_fast_prov if available */
      if (g_fast_prov) {
        memcpy(g_fast_prov->mac_addr_info.mac, device_mac_static, 6);
      }
      return 0;
    }
    return ret < 0 ? ret : -EINVAL;
  }
  return -ENOENT;
}

/**
 * @brief Settings handler: Commit (after load is done)
 */
static int mac_settings_commit(void) {
  /* MAC has been loaded in h_set, no need to do anything else */
  return 0;
}

/**
 * @brief Register Settings handler for MAC address
 */
static void register_mac_settings_handler(void) {
  if (mac_settings_registered) {
    return;
  }

  mac_settings_handler.name = "fast_prov";
  mac_settings_handler.h_get = mac_settings_get;
  mac_settings_handler.h_set = mac_settings_set;
  mac_settings_handler.h_commit = mac_settings_commit;
  mac_settings_handler.h_export = NULL;

  int err = settings_register(&mac_settings_handler);
  if (err == 0) {
    mac_settings_registered = true;
    LOG_INF("MAC settings handler registered");
  } else {
    LOG_ERR("Failed to register MAC settings handler: %d", err);
  }
}

/* Forward declarations for static functions */
static bool read_mac_from_settings(uint8_t *mac);
static bool write_mac_to_settings(const uint8_t *mac);

/* ============================================================================
 * HELPER FUNCTIONS - Single Source of Truth for MAC Generation
 * ============================================================================
 */

/**
 * @brief Generate MAC address from hardware ID (DETERMINISTIC)
 *
 * @param mac Output buffer (6 bytes)
 * @return true if successful
 *
 * @details
 * ALWAYS generates the SAME MAC from hardware ID (no random).
 * This is the SINGLE SOURCE OF TRUTH for MAC generation.
 *
 * Algorithm:
 * 1. Get Nordic hardware ID (unique per chip)
 * 2. Use first 6 bytes as MAC (or pad if shorter)
 * 3. Set format bits (locally administered, static random)
 * 4. Validate not all zeros
 */
static bool generate_mac_from_hardware_id(uint8_t *mac) {
  if (!mac) {
    return false;
  }

  /* Get hardware ID (unique per chip) */
  uint8_t hw_id[16] = {0};
  size_t hw_id_len = hwinfo_get_device_id(hw_id, sizeof(hw_id));

  if (hw_id_len > 0) {
    /* Use hardware ID as base for MAC */
    if (hw_id_len >= 6) {
      memcpy(mac, hw_id, 6);
    } else {
      /* Pad with zeros if HW ID is shorter */
      memcpy(mac, hw_id, hw_id_len);
      memset(mac + hw_id_len, 0, 6 - hw_id_len);
    }
  } else {
    /* Fallback: use fixed MAC if no hardware ID */
    memset(mac, 0, 6);
    mac[0] = 0x02;
    mac[1] = 0x01;
    LOG_WRN("No hardware ID available, using fallback MAC");
  }

  /* Set MAC format bits */
  mac[0] = (mac[0] & 0xFC) | 0x02; /* Locally administered, unicast */
  mac[5] = (mac[5] & 0x3F) | 0xC0; /* Static random address */

  /* Validate not all zeros */
  bool is_all_zero = true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      is_all_zero = false;
      break;
    }
  }
  if (is_all_zero) {
    mac[0] = 0x02;
    mac[1] = 0x01;
    mac[5] = 0xC0;
  }

  return true;
}

/**
 * @brief Initialize device key and temporary address from MAC
 *
 * @details
 * MUST be called after MAC is set in g_fast_prov->mac_addr_info.mac
 *
 * Creates:
 * - device_key_from_mac[16] = MAC[6] + mac_header[10]
 * - Temporary address for default network (from MAC[0:1])
 */
static void initialize_device_key_from_mac(void) {
  if (!g_fast_prov) {
    return;
  }

  /* Create device key from MAC */
  memcpy(device_key_from_mac, g_fast_prov->mac_addr_info.mac, 6);
  memcpy(device_key_from_mac + 6, mac_header, sizeof(mac_header));

  /* Create temporary address (Telink compatible) */
  uint16_t tmp_addr = (g_fast_prov->mac_addr_info.mac[0] +
                       (g_fast_prov->mac_addr_info.mac[1] << 8)) &
                      0x7FFF;
  if (tmp_addr == 0) {
    tmp_addr = 1;
  }
  g_fast_prov->mac_addr_info.default_addr = tmp_addr;
  g_fast_prov->mac_addr_info.addr = tmp_addr;

  LOG_DBG("Device key and temp addr initialized from MAC");
}

/**
 * @brief Get or generate MAC address (MAIN ENTRY POINT)
 *
 * @param mac Output buffer (6 bytes)
 * @return true if successful
 *
 * @details
 * Priority order:
 * 1. Return from cache if already initialized
 * 2. Load from settings if available
 * 3. Generate from hardware ID
 * 4. Save to settings for next boot
 */
static bool get_or_generate_mac(uint8_t *mac) {
  if (!mac) {
    return false;
  }

  /* 1. Check cache */
  if (device_mac_initialized) {
    memcpy(mac, device_mac_static, 6);
    LOG_DBG("MAC from cache: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
            mac[2], mac[3], mac[4], mac[5]);
    return true;
  }

  /* 2. Try load from settings */
  if (read_mac_from_settings(mac)) {
    /* Update cache */
    memcpy(device_mac_static, mac, 6);
    device_mac_initialized = true;
    LOG_INF("MAC from settings: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
            mac[2], mac[3], mac[4], mac[5]);
    return true;
  }

  /* 3. Generate from hardware ID */
  if (!generate_mac_from_hardware_id(mac)) {
    LOG_ERR("Failed to generate MAC from hardware ID");
    return false;
  }

  /* Update cache */
  memcpy(device_mac_static, mac, 6);
  device_mac_initialized = true;

  /* 4. Save to settings (best effort) */
  if (write_mac_to_settings(mac)) {
    LOG_INF("MAC generated and saved: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    /* Expected: Settings may not be ready during early init */
    LOG_DBG("MAC generated, save deferred (settings not ready): "
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  return true;
}

/**
 * @brief Initialize Settings handler for MAC address (called from main before
 * enabling Bluetooth)
 *
 * @return 0 on success, error code on failure
 *
 * @details
 * This functions registers settings handler to save/read MAC address.
 * Settings API automatically manages NVS and ensures persistence.
 */
int fast_provision_nvs_init_early(void) {
  /* Register settings handler for MAC address */
  register_mac_settings_handler();
  LOG_DBG("MAC settings handler initialized");

  uint8_t mac[6];

  /* Get or generate MAC (handles caching, settings, generation) */
  if (get_or_generate_mac(mac)) {
    LOG_INF("MAC initialized early: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    LOG_WRN("MAC initialization deferred (will initialize later)");
  }

  return 0;
}

/**
 * @brief Read MAC address from Settings (flash storage) - Telink compatible
 *
 * @param mac Buffer to store MAC address (6 bytes)
 * @return true if read successfully and MAC is valid, false otherwise
 *
 * @details
 * Use Settings API to read MAC from flash (similar to Telink: read MAC from
 * flash at CFG_ADR_MAC) Settings API automatically manages NVS and ensures
 * persistence.
 */
static bool read_mac_from_settings(uint8_t *mac) {
  if (!mac) {
    return false;
  }

  /* Register settings handler if not registered */
  register_mac_settings_handler();

  /* Check if MAC has been loaded from settings (in h_set callback when
   * settings_load() runs) */
  if (device_mac_initialized) {
    /* MAC loaded from settings in h_set callback */
    memcpy(mac, device_mac_static, 6);
    LOG_DBG("MAC read from settings: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
  }

  /* If MAC not loaded, try reading directly from settings */
  /* NOTE: settings_load_one() only works after settings_load() has been
   * called */
  ssize_t len = settings_load_one(MAC_ADDR_SETTINGS_KEY, mac, 6);
  if (len == 6) {
    /* Check valid MAC (not all 0xFF like Telink) */
    uint32_t *pmac = (uint32_t *)mac;
    if (*pmac == 0xFFFFFFFF) {
      LOG_DBG(
          "MAC in settings is invalid (0xFFFFFFFF) - treating as not found");
      return false;
    }

    /* Check MAC is not all zeros */
    bool is_valid = false;
    for (int i = 0; i < 6; i++) {
      if (mac[i] != 0) {
        is_valid = true;
        break;
      }
    }

    if (is_valid) {
      LOG_DBG("MAC read from settings: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
              mac[1], mac[2], mac[3], mac[4], mac[5]);
      /* Update to device_mac_static for synchronization */
      memcpy(device_mac_static, mac, 6);
      device_mac_initialized = true;
      return true;
    }
  } else if (len < 0) {
    /* Error code: ENOENT = not found (normal if MAC not saved yet) */
    if (len != -ENOENT) {
      LOG_DBG("MAC not found in settings (error=%d)", len);
    }
  } else if (len == 0) {
    /* len=0 means settings_load() not called yet or MAC not saved */
    /* No warning log as this is normal during first boot */
    LOG_DBG("MAC not yet loaded from settings (settings_load() may not have "
            "been called yet)");
  } else {
    /* len > 0 but != 6: this should not happen */
    LOG_WRN("MAC in settings has wrong size (len=%d, expected 6)", len);
  }

  return false;
}

/**
 * @brief Write MAC address to Settings (flash storage) - Telink compatible
 *
 * @param mac MAC address to save (6 bytes)
 * @return true if successful, false otherwise
 *
 * @details
 * Use Settings API to write MAC to flash (similar to Telink: write MAC to flash
 * at CFG_ADR_MAC) Settings API automatically manages NVS and ensures
 * persistence. After writing, MAC will be saved permanently and read back in
 * subsequent boots.
 */
static bool write_mac_to_settings(const uint8_t *mac) {
  if (!mac) {
    return false;
  }

  /* Register settings handler if not registered */
  register_mac_settings_handler();

  /* Write MAC to settings */
  int err = settings_save_one(MAC_ADDR_SETTINGS_KEY, mac, 6);
  if (err == 0) {
    LOG_INF("MAC saved to settings: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
  } else {
    /* Expected during early init when settings subsystem not ready */
    LOG_DBG("MAC save deferred (err=%d, settings not ready)", err);
    return false;
  }
}

/**
 * @brief Get MAC address from fast provision (for SDK use)
 *
 * @param mac Buffer to store MAC address (6 bytes)
 * @return true if MAC exists, false otherwise
 *
 * @details
 * This function is used by SDK to get MAC from fast provision.
 * If MAC doesn't exist, it will be generated from hardware ID.
 *
 * EXPORT: Allow SDK (addr.c, id.c) to call this function
 */
bool fast_provision_get_mac_for_bt_id(uint8_t *mac) {
  if (!mac) {
    LOG_ERR("fast_provision_get_mac_for_bt_id: mac is NULL");
    return false;
  }

  LOG_DBG("fast_provision_get_mac_for_bt_id() called");

  /* Use shared MAC generation logic */
  return get_or_generate_mac(mac);
}

void mesh_device_key_set_default(void) {
  if (!g_fast_prov) {
    return;
  }

  uint8_t mac[6];

  /* Get or generate MAC (handles caching, settings, generation) */
  if (!get_or_generate_mac(mac)) {
    LOG_ERR("Failed to get MAC address");
    return;
  }

  /* Copy to fast provision context */
  memcpy(g_fast_prov->mac_addr_info.mac, mac, 6);
  initialize_device_key_from_mac();

  LOG_INF("MAC and device key initialized: %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Get device key for main network provisioning
 *
 * @return Pointer to device key (16 bytes), or NULL if not initialized
 */
static const uint8_t *get_device_key_for_main_network(void) {
  /* Check if device key has been generated */
  bool is_zero = true;
  for (int i = 0; i < 16; i++) {
    if (device_key_from_mac[i] != 0) {
      is_zero = false;
      break;
    }
  }

  if (is_zero) {
    LOG_WRN("Device key not initialized, using default_dev_key");
    return default_dev_key;
  }

  return device_key_from_mac;
}

/**
 * @brief Initialize Fast Provision values
 */
void mesh_fast_prov_val_init(void) {
  if (!g_fast_prov) {
    LOG_ERR("Fast provision context not initialized");
    return;
  }

  memset(g_fast_prov, 0, sizeof(*g_fast_prov));

  /* Check main network, not default network */
  /* bt_mesh_is_provisioned() returns true if any subnet exists (including
   * default network) */
  /* But we need to check main network specifically */
  bool main_network_provisioned = network_is_main_network_provisioned();

  if (main_network_provisioned) {
    g_fast_prov->not_need_prov = true;
    LOG_INF("Main network already provisioned, fast provision disabled");
  } else {
    /* No main network - allow fast provision */
    mesh_device_key_set_default();
    g_fast_prov->get_mac_en = true;
    g_fast_prov->cur_sts = FAST_PROV_IDLE;
    g_fast_prov->pid = LM_PID_MESH;
    LOG_INF("Fast provision enabled (main network not provisioned)");
  }
}

/**
 * @brief Set Fast Provision state
 */
int mesh_fast_prov_sts_set(enum fast_prov_state sts_set) {
  if (!g_fast_prov) {
    return -EINVAL;
  }

  if (sts_set == FAST_PROV_IDLE) {
    g_fast_prov->start_tick = 0;
  } else {
    g_fast_prov->start_tick = k_uptime_get();
  }

  if (sts_set != g_fast_prov->cur_sts) {
    g_fast_prov->last_sts = g_fast_prov->cur_sts;
    g_fast_prov->cur_sts = sts_set;
    LOG_INF("State: %d -> %d", g_fast_prov->last_sts, g_fast_prov->cur_sts);
  }

  return 0;
}

/**
 * @brief Get current Fast Provision state
 */
enum fast_prov_state mesh_fast_prov_sts_get(void) {
  if (!g_fast_prov) {
    return FAST_PROV_IDLE;
  }
  return g_fast_prov->cur_sts;
}

/**
 * @brief Get element count callback for PID
 */
uint8_t mesh_fast_prov_get_ele_cnt_callback(uint16_t pid) {
  /* Return the actual number of elements defined in the system */
  return ELE_CNT;
}

/**
 * @brief Check timeout and reset to IDLE if state is stuck too long
 *
 * @details
 * Check timeout for fast provision process:
 * - If start_tick != 0 and timeout exceeded -> reset to TIME_OUT state
 * - If state stuck at intermediate states (SET_ADDR, NET_INFO, CONFIRM) too
 * long without progress -> reset to IDLE to restart
 */
static void mesh_fast_provision_timeout(void) {
  if (!g_fast_prov) {
    return;
  }

  /* Check timeout if start_tick is set */
  if (g_fast_prov->start_tick != 0) {
    int64_t elapsed = k_uptime_get() - g_fast_prov->start_tick;
    if (elapsed >= FAST_PROVISION_TIMEOUT_MS) {
      LOG_WRN("Fast provision timeout after %lld ms (state: %d)", elapsed,
              g_fast_prov->cur_sts);
      mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
      g_fast_prov->start_tick = 0;
      return;
    }
  }

  /* Check if state stuck at intermediate states too long */
  /* If state is SET_ADDR, NET_INFO, or CONFIRM and no progress for 30
   * seconds */
  enum fast_prov_state cur_sts = g_fast_prov->cur_sts;
  if (cur_sts == FAST_PROV_SET_ADDR || cur_sts == FAST_PROV_NET_INFO ||
      cur_sts == FAST_PROV_CONFIRM) {
    /* If start_tick = 0, set it to start timeout count for this state */
    if (g_fast_prov->start_tick == 0) {
      g_fast_prov->start_tick = k_uptime_get();
      LOG_DBG("Starting timeout timer for state %d", cur_sts);
      return;
    }

    /* Check 30-second timeout for intermediate states */
    int64_t elapsed = k_uptime_get() - g_fast_prov->start_tick;
    if (elapsed >= 30000) { // 30 seconds timeout for intermediate states
      LOG_WRN("Fast provision state stuck at %d for %lld ms, resetting to IDLE",
              cur_sts, elapsed);
      mesh_fast_prov_sts_set(FAST_PROV_IDLE);
      g_fast_prov->start_tick = 0;
      g_fast_prov->rcv_op = 0;
    }
  } else if (cur_sts == FAST_PROV_IDLE) {
    /* If already back to IDLE, reset start_tick to stop checking timeout */
    if (g_fast_prov->start_tick != 0) {
      g_fast_prov->start_tick = 0;
    }
  }
}

/**
 * @brief Process Fast Provision state machine
 */
void mesh_fast_prov_proc(void) {
  if (!g_fast_prov) {
    return;
  }

  /* Check if device is busy or already provisioned
   * IMPORTANT: Check g_fast_prov->not_need_prov first to avoid race
   * condition If not_need_prov = true, device is provisioned and doesn't need
   * fast provision anymore
   */
  if (g_fast_prov->not_need_prov) {
    /* Device provisioned to main network, fast provision not needed */
    return;
  }

  /* Double-check: If provisioned to main network, disable fast provision
   */
  if (network_is_main_network_provisioned()) {
    g_fast_prov->not_need_prov = true;
    LOG_INF("Main network provisioned, disabling fast provision");
    return;
  }

  /* Check timeout */
  mesh_fast_provision_timeout();

  /* Process deferred GET_MAC response (Telink compatible random delay) */
  send_deferred_get_mac_response();

  /* Process state machine */
  switch (g_fast_prov->cur_sts) {
  case FAST_PROV_COMPLETE:
    if (g_fast_prov->pending && g_fast_prov->delay > 0) {
      int64_t elapsed = k_uptime_get() - g_fast_prov->start_tick;
      if (elapsed >= g_fast_prov->delay) {
        g_fast_prov->delay = 0;
        g_fast_prov->pending = false;

        /* Check if already provisioned to main network
         * If provisioned (possibly due to simultaneous normal provisioning),
         * just add AppKey and cleanup, no need to re-provision
         */
        if (network_is_main_network_provisioned()) {
          LOG_INF("Main network already provisioned (possibly via normal "
                  "provisioning), adding AppKey only");
          uint16_t app_idx =
              (g_fast_prov->net_info.appkey_set.net_app_idx[2] & 0x0F) |
              ((g_fast_prov->net_info.appkey_set.net_app_idx[1] & 0x0F) << 4);
          uint8_t status = bt_mesh_app_key_add(
              app_idx, g_fast_prov->net_info.pro_data.key_index,
              g_fast_prov->net_info.appkey_set.app_key);
          if (status == STATUS_SUCCESS || status == STATUS_IDX_ALREADY_STORED) {
            LOG_INF("AppKey 0x%04X added successfully", app_idx);
            /* Bind AppKey to all models */
            bind_all_models_with_appkey(app_idx);
          } else {
            LOG_ERR("Failed to add AppKey: 0x%02x", status);
          }
          /* Cleanup default network */
          del_tmp_keys();
          mesh_fast_prov_sts_set(FAST_PROV_IDLE);
          g_fast_prov->not_need_prov = true;
          return;
        }

        const uint8_t *dev_key = get_device_key_for_main_network();
        if (!dev_key) {
          LOG_ERR("Device key is NULL, cannot provision");
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        int ret;
        struct bt_mesh_key mesh_dev_key;
        struct bt_mesh_key mesh_net_key;

        ret = bt_mesh_key_import(BT_MESH_KEY_TYPE_DEV, dev_key, &mesh_dev_key);
        if (ret != 0) {
          LOG_ERR("Failed to import device key: %d", ret);
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        ret = bt_mesh_key_import(BT_MESH_KEY_TYPE_NET,
                                 g_fast_prov->net_info.pro_data.net_key,
                                 &mesh_net_key);
        if (ret != 0) {
          LOG_ERR("Failed to import network key: %d", ret);
          bt_mesh_key_destroy(&mesh_dev_key);
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        uint16_t net_idx = g_fast_prov->net_info.pro_data.key_index;
        /* IMPORTANT: IV index is stored as big-endian in buffer
         * Need to convert from big-endian bytes to uint32_t
         */
        uint32_t iv_index =
            sys_get_be32(g_fast_prov->net_info.pro_data.iv_index);
        uint8_t flags = g_fast_prov->net_info.pro_data.flags;

        ret = bt_mesh_net_create(net_idx, flags, &mesh_net_key, iv_index);
        if (ret != 0 && ret != -EALREADY) {
          LOG_ERR("Failed to create main network: %d", ret);
          bt_mesh_key_destroy(&mesh_net_key);
          bt_mesh_key_destroy(&mesh_dev_key);
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        /* IMPORTANT: If ret == -EALREADY, subnet already exists (possibly due
         * to normal provisioning) Check if provisioned, if yes then only need
         * to add AppKey
         */
        if (ret == -EALREADY) {
          LOG_INF("Network already exists (possibly via normal provisioning), "
                  "checking status");
          if (network_is_main_network_provisioned()) {
            LOG_INF("Main network already provisioned, adding AppKey only");
            /* Keys added to mesh stack, just destroy local copies */
            bt_mesh_key_destroy(&mesh_net_key);
            bt_mesh_key_destroy(&mesh_dev_key);
            /* Add AppKey and bind models */
            uint16_t app_idx =
                (g_fast_prov->net_info.appkey_set.net_app_idx[2] & 0x0F) |
                ((g_fast_prov->net_info.appkey_set.net_app_idx[1] & 0x0F) << 4);
            uint8_t status = bt_mesh_app_key_add(
                app_idx, net_idx, g_fast_prov->net_info.appkey_set.app_key);
            if (status == STATUS_SUCCESS ||
                status == STATUS_IDX_ALREADY_STORED) {
              bind_all_models_with_appkey(app_idx);
            }
            del_tmp_keys();
            mesh_fast_prov_sts_set(FAST_PROV_IDLE);
            g_fast_prov->not_need_prov = true;
            return;
          } else {
            /* Subnet exists but not properly provisioned - cleanup and
             * retry */
            LOG_WRN("Network exists but not properly provisioned, cleaning up");
            bt_mesh_subnet_del(net_idx);
            bt_mesh_key_destroy(&mesh_net_key);
            bt_mesh_key_destroy(&mesh_dev_key);
            mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
            return;
          }
        }

        memcpy(&bt_mesh.dev_key, &mesh_dev_key, sizeof(struct bt_mesh_key));
        bt_mesh_comp_provision(g_fast_prov->net_info.pro_data.unicast_address);
        bt_mesh_net_settings_commit();
        bt_mesh.seq = 0U;
        atomic_set_bit(bt_mesh.flags, BT_MESH_VALID);

        if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
          bt_mesh_net_store();
        }

        ret = bt_mesh_start();
        if (ret != 0) {
          LOG_ERR("Failed to start mesh stack: %d", ret);
          /* IMPORTANT: Cleanup created subnet because keys have been added to
           * mesh stack */
          bt_mesh_subnet_del(net_idx);
          /* Destroy local key copies */
          bt_mesh_key_destroy(&mesh_net_key);
          bt_mesh_key_destroy(&mesh_dev_key);
          atomic_clear_bit(bt_mesh.flags, BT_MESH_VALID);
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        /* Wait a bit for mesh stack to start up */
        k_sleep(K_MSEC(100));

        /* Verify successful provisioning */
        if (!network_is_main_network_provisioned()) {
          LOG_ERR("Provisioning failed - main network not provisioned after "
                  "bt_mesh_start()");
          /* IMPORTANT: Cleanup created subnet */
          bt_mesh_subnet_del(net_idx);
          /* Destroy local key copies */
          bt_mesh_key_destroy(&mesh_net_key);
          bt_mesh_key_destroy(&mesh_dev_key);
          atomic_clear_bit(bt_mesh.flags, BT_MESH_VALID);
          mesh_fast_prov_sts_set(FAST_PROV_TIME_OUT);
          return;
        }

        uint16_t app_idx =
            (g_fast_prov->net_info.appkey_set.net_app_idx[2] & 0x0F) |
            ((g_fast_prov->net_info.appkey_set.net_app_idx[1] & 0x0F) << 4);
        uint8_t status = bt_mesh_app_key_add(
            app_idx, g_fast_prov->net_info.pro_data.key_index,
            g_fast_prov->net_info.appkey_set.app_key);

        if (status == STATUS_SUCCESS || status == STATUS_IDX_ALREADY_STORED) {
          LOG_INF("AppKey 0x%04X added to mesh stack", app_idx);

          /* Wait a bit for AppKey to be added to storage */
          k_sleep(K_MSEC(50));

          /* Optimization: Use bind_all_models_with_appkey() to bind all
           * models This function will automatically:
           * - Bind all models with AppKey from fast provision
           * - Bind additional AppKey 0x0000 for Fast Provision model if
           * needed (for C5 messages)
           * - Save settings to flash
           * Same as normal provisioning - simple and consistent
           */
          bind_all_models_with_appkey(app_idx);
        } else {
          LOG_ERR("Failed to add AppKey 0x%04X: 0x%02x", app_idx, status);
          /* AppKey add failed but provisioning successful, do not cleanup
           * subnet. Can retry add AppKey later or provisioner will add it
           * again.
           */
        }

        /* Log network info after successful provisioning */
        LOG_INF("========================================");
        LOG_INF("=== MAIN NETWORK PROVISIONED ===");
        LOG_INF("========================================");
        LOG_INF("  Network Index: 0x%04X", net_idx);
        LOG_INF("  Unicast Address: 0x%04X",
                g_fast_prov->net_info.pro_data.unicast_address);
        LOG_INF("  IV Index: 0x%08X", iv_index);
        LOG_INF("  Flags: 0x%02X", flags);
        LOG_INF("  AppKey Index: 0x%04X", app_idx);
        LOG_HEXDUMP_INF(g_fast_prov->net_info.appkey_set.app_key, 16,
                        "  AppKey:");
        LOG_INF("  Primary Address: 0x%04X", bt_mesh_primary_addr());
        LOG_INF("  Provisioning Status: %s",
                network_is_main_network_provisioned() ? "PROVISIONED"
                                                      : "NOT PROVISIONED");
        LOG_INF(
            "  AppKey Bind Status: %s",
            (status == STATUS_SUCCESS || status == STATUS_IDX_ALREADY_STORED)
                ? "BOUND"
                : "FAILED");
        LOG_INF("========================================");

        /* Blink LED to indicate Fast Provision success */
        // led_ev_handle(LED_PROVISION_SUCCESS, CONFIG_LED_MASK_BLUETOOTH);

        /* IMPORTANT: Mark as provisioned to disable fast provision */
        g_fast_prov->not_need_prov = true;

#if EN_PROVISIONING_TOGGLE
        provisioning_stop_quietly();
#endif
        /* IMPORTANT: Disable normal provisioning when fast provision succeeds
         * to avoid conflict and ensure only one provisioning method active
         */
        int disable_ret = bt_mesh_prov_disable(BT_MESH_PROV_ADV);
        if (disable_ret == 0) {
          LOG_INF("Normal provisioning (PB-ADV) disabled after fast provision "
                  "success");
        } else if (disable_ret == -EALREADY) {
          /* Already disabled or not enabled - OK */
          LOG_DBG("Normal provisioning already disabled or not enabled");
        } else {
          LOG_WRN("Failed to disable normal provisioning: %d", disable_ret);
        }

        /* DO NOT call del_tmp_keys() here - let proc_default_network() handle
         * it with delay to avoid hang when mesh stack is not stable after
         * provisioned
         */
        mesh_fast_prov_sts_set(FAST_PROV_IDLE);
      }
    }
    break;

  case FAST_PROV_TIME_OUT:
    LOG_WRN("Fast provision timed out");
    del_tmp_keys();
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    break;

  case FAST_PROV_RESET_NETWORK:
    /* RESET_NETWORK state: Default network opened, waiting for ADDR_GET
     * Switch to IDLE to be ready for ADDR_GET
     */
    LOG_DBG("FAST_PROV_RESET_NETWORK: Default network opened, waiting for "
            "ADDR_GET");
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    break;

  default:
    break;
  }

  /* Process received opcodes - use if-else to avoid switch warning with 3-byte
   * opcodes */
  uint32_t rcv_op = g_fast_prov->rcv_op;

  /* RESET_NETWORK (C5) processed directly in handle_reset_network callback
   * No need to process in state machine anymore
   */
  if (rcv_op == VD_MESH_ADDR_GET) {
    /* Only process if in IDLE - if already in GET_ADDR then handler already
     * processed */
    if (g_fast_prov->cur_sts == FAST_PROV_IDLE) {
      LOG_INF("Received ADDR_GET command in state machine");
      mesh_fast_prov_sts_set(FAST_PROV_GET_ADDR);
    } else if (g_fast_prov->cur_sts == FAST_PROV_GET_ADDR) {
      /* Already in GET_ADDR, handler processed - do nothing more */
      LOG_DBG("ADDR_GET already processed by handler");
    }
  } else if (rcv_op == VD_MESH_ADDR_SET) {
    if (g_fast_prov->cur_sts == FAST_PROV_GET_ADDR) {
      LOG_INF("Received ADDR_SET command");
      mesh_fast_prov_sts_set(FAST_PROV_SET_ADDR);
    }
  } else if (rcv_op == VD_MESH_PROV_DATA_SET) {
    if (g_fast_prov->cur_sts == FAST_PROV_SET_ADDR) {
      LOG_INF("Received PROV_DATA_SET command in state machine");
      mesh_fast_prov_sts_set(FAST_PROV_NET_INFO);
      LOG_INF("  State changed to FAST_PROV_NET_INFO - Waiting for "
              "PROV_CONFIRM (CB)");
    } else {
      LOG_DBG("PROV_DATA_SET received but wrong state: %d (expected: %d)",
              g_fast_prov->cur_sts, FAST_PROV_SET_ADDR);
    }
  } else if (rcv_op == VD_MESH_PROV_CONFIRM) {
    if (g_fast_prov->cur_sts == FAST_PROV_NET_INFO) {
      LOG_INF("Received PROV_CONFIRM command in state machine");
      mesh_fast_prov_sts_set(FAST_PROV_CONFIRM);
      LOG_INF("  State changed to FAST_PROV_CONFIRM - Waiting for "
              "PROV_COMPLETE (CD)");
    } else {
      LOG_WRN("PROV_CONFIRM received but wrong state: %d (expected: %d)",
              g_fast_prov->cur_sts, FAST_PROV_NET_INFO);
    }
  } else if (rcv_op == VD_MESH_PROV_COMPLETE) {
    if (g_fast_prov->cur_sts == FAST_PROV_CONFIRM) {
      LOG_INF("Received PROV_COMPLETE command in state machine");
      g_fast_prov->pending = true;
      g_fast_prov->delay = 1000; // 1 seconds delay
      g_fast_prov->start_tick = k_uptime_get();
      mesh_fast_prov_sts_set(FAST_PROV_COMPLETE);
      LOG_INF("  State changed to FAST_PROV_COMPLETE - Will provision to main "
              "network after delay");
    } else {
      LOG_WRN("PROV_COMPLETE received but wrong state: %d (expected: %d)",
              g_fast_prov->cur_sts, FAST_PROV_CONFIRM);
    }
  }

  /* Clear received opcode after processing */
  g_fast_prov->rcv_op = 0;
}

/* ============================================================================
 * MESSAGE HANDLERS
 * ============================================================================
 */

/**
 * @brief Handler: VD_MESH_RESET_NETWORK (0xC5)
 *
 * @details
 * This handler receives RESET_NETWORK message from provisioner.
 * - Can receive from default network (when not provisioned)
 * - Can receive from main network (when already provisioned)
 * - Do not check not_need_prov because RESET_NETWORK needs to be processed even
 * when provisioned
 *
 * @note
 * Message hook is called BEFORE handler - this is normal mesh stack behavior.
 * Flow: Message -> Hook -> Routing -> Handler
 */
static int handle_reset_network(const struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */
  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  /* Log at beginning of function to identify call order */
  LOG_INF("=== RESET_NETWORK (0xC5) handler called ===");
  LOG_INF(
      "  Request from: src=0x%04x, dst=0x%04x, net_idx=0x%04x, app_idx=0x%04x",
      ctx->addr, ctx->recv_dst, ctx->net_idx, ctx->app_idx);
  LOG_INF("  Buffer length: %d bytes", buf->len);

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov;

  bool is_main_network = network_is_main_network_provisioned();
  /* RESET_NETWORK can be sent from main network after provisioned
   * Do not check not_need_prov because RESET_NETWORK needs to be processed even
   * when provisioned
   *
   * Note: If handler is called, it means model has been bound with AppKey
   * (mesh stack only routes message to handler if model bound with AppKey)
   */
  if (!is_main_network) {
    LOG_WRN("Device not provisioned, ignoring RESET_NETWORK");
    return 0;
  }

  LOG_INF("...Opening default network with timeout: %u seconds",
          DEFAULT_NETWORK_DEFAULT_TIME_S);
  set_default_network_manual(true, DEFAULT_NETWORK_DEFAULT_TIME_S);

  return 0;
}

/**
 * @brief Handler: VD_MESH_ADDR_GET (0xC6) - Request MAC address
 * @note Only Server model processes this request
 */
static int handle_addr_get(const struct bt_mesh_model *model,
                           struct bt_mesh_msg_ctx *ctx,
                           struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */
  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov;

  /* Only Server model (ID 0x0000) processes request messages */
  if (model->vnd.id != BT_MESH_MODEL_ID_VND_FAST_PROV_SRV) {
    LOG_DBG("ADDR_GET ignored by non-Server model");
    return 0;
  }

  /* Get element address - this is source address used when sending response
   */
  const struct bt_mesh_elem *elem = bt_mesh_model_elem(model);
  uint16_t ele_adr = elem ? elem->rt->addr : BT_MESH_ADDR_UNASSIGNED;
  if (ele_adr == BT_MESH_ADDR_UNASSIGNED) {
    if (fprov && fprov->mac_addr_info.default_addr != 0) {
      ele_adr = fprov->mac_addr_info.default_addr;
      LOG_DBG("Using temporary address from MAC: 0x%04x", ele_adr);
    } else {
      /* Fallback: if no temporary address, use 0x0000 */
      ele_adr = BT_MESH_ADDR_UNASSIGNED;
      LOG_DBG("No temporary address available, using 0x0000");
    }
  }

  /* IMPORTANT: fprov has been checked at start of function, no need to check
   * again */
  if (fprov->not_need_prov || !fprov->get_mac_en) {
    return 0;
  }

  if (fprov->cur_sts == FAST_PROV_SET_ADDR ||
      fprov->cur_sts == FAST_PROV_NET_INFO ||
      fprov->cur_sts == FAST_PROV_CONFIRM ||
      fprov->cur_sts == FAST_PROV_COMPLETE) {
    return 0;
  }

  if (fprov->cur_sts == FAST_PROV_GET_ADDR &&
      fprov->rcv_op == VD_MESH_ADDR_GET) {
    return 0;
  }

  mac_addr_get_t mac_get = {0};
  if (buf->len >= 2) {
    mac_get.pid = net_buf_simple_pull_le16(buf);
  }
  if (buf->len >= 2) {
    mac_get.ele_addr = net_buf_simple_pull_le16(buf);
  }

  if (mac_get.pid != LM_PID_MESH && mac_get.pid != 0xFFFF) {
    return 0;
  }

  /* HANDLE CONFLICT ADDRESS - IDENTICAL TO TELINK
   * Telink logic:
   * - If receive MAC_GET with ele_addr (possibly address conflict from
   * provisioner)
   * - Randomize address to avoid conflict: tmp_ele_addr =
   * mac_get.ele_addr+256+(u16)clock_time()%(0x8000-256-mac_get.ele_addr)
   * - Only random once (static flag default_addr_random)
   */
  /* default_addr_random is now module-level (can be reset by
   * mesh_fast_prov_reset_for_retry) */
  if (mac_get.ele_addr != 0 && !default_addr_random &&
      buf->len >= sizeof(mac_addr_get_t)) {
    /* IMPORTANT: Validate ele_addr to avoid division by zero and overflow */
    if (mac_get.ele_addr >= 0x8000 - 256) {
      LOG_WRN("Invalid ele_addr for conflict resolution: 0x%04x, skipping",
              mac_get.ele_addr);
    } else {
      /* Conflict exists - re-random address like Telink */
      default_addr_random = true;

      /* Telink formula: tmp_ele_addr =
       * mac_get.ele_addr+256+(u16)clock_time()%(0x8000-256-mac_get.ele_addr) */
      uint32_t clock_time =
          (uint32_t)k_uptime_get(); /* Use uptime instead of clock_time() */
      uint16_t divisor = 0x8000 - 256 - mac_get.ele_addr;
      uint16_t tmp_ele_addr =
          mac_get.ele_addr + 256 + (uint16_t)(clock_time % divisor);
      tmp_ele_addr &= 0x7FFF;

      /* Update temporary address */
      fprov->mac_addr_info.default_addr = tmp_ele_addr;
      fprov->mac_addr_info.addr = tmp_ele_addr;

      /* Update element address in mesh stack */
      const struct bt_mesh_comp *comp = bt_mesh_comp_get();
      if (comp && comp->elem_count > 0) {
        const struct bt_mesh_elem *elem = &comp->elem[0];
        if (elem && elem->rt) {
          elem->rt->addr = tmp_ele_addr;
          LOG_INF("Setting temporary element address: 0x%04X (to avoid "
                  "conflict, Telink compatible)",
                  tmp_ele_addr);
        }
      }
    }
  }

  /* Set rcv_op and state immediately to avoid reprocessing */
  fprov->rcv_op = (uint32_t)VD_MESH_ADDR_GET;
  if (fprov->cur_sts == FAST_PROV_IDLE) {
    mesh_fast_prov_sts_set(FAST_PROV_GET_ADDR);
  }

  /* DEFERRED RESPONSE - Telink compatible random delay (0-5000ms)
   * Save context and set random delay. Response will be sent from
   * mesh_fast_prov_proc() when delay expires.
   * This avoids RF collision when multiple devices respond to ADDR_GET.
   */
  get_mac_saved_model = model;
  get_mac_saved_dst = ctx->addr;
  get_mac_time = k_uptime_get();
  uint32_t random_val;
  sys_rand_get(&random_val, sizeof(random_val));
  get_mac_delay_ms = random_val % 5000; /* 0-5000ms like Telink */

  LOG_INF("ADDR_GET received, response deferred by %u ms", get_mac_delay_ms);

  /* Suppress unprovisioned beacon for 60s to prioritize Radio for Fast Prov */
  extern void bt_mesh_beacon_unprov_suppress(void);
  bt_mesh_beacon_unprov_suppress();

  /* Disable PB-GATT advertising to further reduce RF interference */
  bt_mesh_pb_gatt_srv_disable();

  return 0;
}

/**
 * @brief Send deferred GET_MAC response (called from mesh_fast_prov_proc)
 *
 * @details
 * Telink compatible: send MAC response after random delay (0-5000ms)
 * to avoid RF collision when multiple devices respond simultaneously.
 */
static void send_deferred_get_mac_response(void) {
  if (get_mac_time == 0 || !get_mac_saved_model || !g_fast_prov) {
    return;
  }

  /* Check if delay has elapsed */
  int64_t elapsed = k_uptime_get() - get_mac_time;
  if (elapsed < (int64_t)get_mac_delay_ms) {
    return; /* Not yet, wait more */
  }

  /* Clear deferred state */
  get_mac_time = 0;
  const struct bt_mesh_model *model = get_mac_saved_model;
  uint16_t dst_addr = get_mac_saved_dst;
  get_mac_saved_model = NULL;
  get_mac_saved_dst = 0;

  /* Prepare response */
  fast_prov_mac_st rsp = {0};
  memcpy(rsp.mac, g_fast_prov->mac_addr_info.mac, 6);
  rsp.pid = LM_PID_MESH;
  rsp.default_addr = g_fast_prov->mac_addr_info.default_addr;
  rsp.addr = rsp.default_addr;

  uint8_t payload[8];
  memcpy(payload, rsp.mac, 6);
  payload[6] = rsp.pid & 0xFF;
  payload[7] = (rsp.pid >> 8) & 0xFF;

  /* Use Queue mechanism instead of direct send to avoid "Advertiser is busy"
   * error */
  int ret = mesh_tx_cmd_rsp(VD_MESH_ADDR_GET_STS, payload, 8,
                            bt_mesh_model_elem(model)->rt->addr, dst_addr, NULL,
                            (void *)model);

  if (ret) {
    LOG_ERR("Failed to push MAC rsp to queue: %d", ret);
    if (g_fast_prov->cur_sts == FAST_PROV_GET_ADDR) {
      mesh_fast_prov_sts_set(FAST_PROV_IDLE);
      g_fast_prov->rcv_op = 0;
    }
  } else {
    LOG_INF("=== Queued GET_MAC response (after %u ms delay) ===",
            get_mac_delay_ms);
    LOG_INF("  dst=0x%04x, MAC=%02X:%02X:%02X:%02X:%02X:%02X, PID=0x%04x",
            dst_addr, rsp.mac[0], rsp.mac[1], rsp.mac[2], rsp.mac[3],
            rsp.mac[4], rsp.mac[5], rsp.pid);
  }
}

/**
 * @brief Handler: VD_MESH_ADDR_SET (0xC8) - Assign unicast address
 */
static int handle_addr_set(const struct bt_mesh_model *model,
                           struct bt_mesh_msg_ctx *ctx,
                           struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */

  LOG_INF("=========================\n ADDR_SET (0xC8) handler "
          "\n=========================");

  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov;

  if (fprov->not_need_prov || fprov->cur_sts != FAST_PROV_GET_ADDR) {
    return 0;
  }

  mac_addr_set_t addr_set = {0};
  if (buf->len >= 6) {
    memcpy(addr_set.mac, net_buf_simple_pull_mem(buf, 6), 6);
  }
  if (buf->len >= 2) {
    addr_set.ele_addr = net_buf_simple_pull_le16(buf);
  }

  /* IMPORTANT: Validate MAC address has been initialized */
  if (fprov->mac_addr_info.mac[0] == 0 && fprov->mac_addr_info.mac[1] == 0) {
    LOG_ERR("Fast provision context MAC not initialized");
    return -ENODEV;
  }

  if (memcmp(addr_set.mac, fprov->mac_addr_info.mac, 6) != 0) {
    return 0;
  }

  if (addr_set.ele_addr == 0x0000 || addr_set.ele_addr >= 0x8000) {
    LOG_ERR("Invalid unicast address: 0x%04x", addr_set.ele_addr);
    return -EINVAL;
  }

  /* Store assigned address */
  fprov->mac_addr_info.addr = addr_set.ele_addr;
  fprov->mac_addr_info.default_addr =
      addr_set.ele_addr; // Also update default_addr
  fprov->get_mac_en = false;
  fprov->rcv_op = (uint32_t)VD_MESH_ADDR_SET;

  /* IMPORTANT: Validate default network subnet exists before sending response
   */
  if (!bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX)) {
    LOG_ERR("Default network subnet not found");
    return -ENODEV;
  }

  uint8_t payload[8];
  memcpy(payload, fprov->mac_addr_info.mac, 6);
  payload[6] = fprov->pid & 0xFF;
  payload[7] = (fprov->pid >> 8) & 0xFF;

  /* Use Queue instead of calling Model Send directly */
  k_msleep(50);
  int ret = mesh_tx_cmd_rsp(VD_MESH_ADDR_SET_STS, payload, 8,
                            bt_mesh_model_elem(model)->rt->addr, ctx->addr,
                            NULL, (void *)model);

  if (ret) {
    LOG_ERR("Failed to push address assigmnent response to queue: %d", ret);
    /* If sending response fails, reset to IDLE to retry */
    fprov->rcv_op = 0;
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    return ret;
  } else {
    LOG_INF("=== Queued SET_NODE response ===");
    LOG_INF("  dst=0x%04x, MAC=%02X:%02X:%02X:%02X:%02X:%02X", ctx->addr,
            fprov->mac_addr_info.mac[0], fprov->mac_addr_info.mac[1],
            fprov->mac_addr_info.mac[2], fprov->mac_addr_info.mac[3],
            fprov->mac_addr_info.mac[4], fprov->mac_addr_info.mac[5]);
    LOG_INF("  assigned_addr=0x%04x, PID=0x%04x", fprov->mac_addr_info.addr,
            fprov->pid);
  }

  /* Switch state to FAST_PROV_SET_ADDR after processing */
  /* This state indicates ADDR_SET received and processed, ready for
   * PROV_DATA_SET
   */
  LOG_INF("ADDR_SET processed successfully, state: %d -> %d", fprov->cur_sts,
          FAST_PROV_SET_ADDR);
  mesh_fast_prov_sts_set(FAST_PROV_SET_ADDR);
  /* Clear rcv_op after processing to avoid reprocessing */
  fprov->rcv_op = 0;

  /* Lock provisioning toggle for 10s to protect active flow */
  prov_lock_start_tick = k_uptime_get();
  LOG_INF("Provisioning lock activated (10s)");

  return 0; /* Return 0 to indicate success */
}

/**
 * @brief Handler: VD_MESH_PROV_DATA_SET (0xCA) - Set provision data
 */
static int handle_prov_data_set(const struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */
  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov; // Set global pointer

  LOG_INF("=== PROV_DATA_SET (0xCA) handler ===");
  LOG_INF("  Request from: src=0x%04x, dst=0x%04x", ctx->addr, ctx->recv_dst);
  LOG_INF("  State: %d (expected: %d), buf_len: %d", fprov->cur_sts,
          FAST_PROV_SET_ADDR, buf->len);

  if (fprov->not_need_prov) {
    LOG_INF("Device already provisioned, ignoring PROV_DATA_SET");
    return 0;
  }

  if (fprov->cur_sts != FAST_PROV_SET_ADDR) {
    LOG_WRN("Wrong state for PROV_DATA_SET: %d (expected: %d)", fprov->cur_sts,
            FAST_PROV_SET_ADDR);

    /* If state is incorrect, reset to IDLE to restart */
    /* Do not process PROV_DATA_SET in wrong state as it may cause errors */
    LOG_WRN("Resetting to IDLE state due to wrong state, will retry from "
            "beginning");
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    g_fast_prov->start_tick = 0;
    g_fast_prov->rcv_op = 0;
    return 0;
  }

  LOG_INF("✓ PROV_DATA_SET received - State: %d -> %d", fprov->cur_sts,
          FAST_PROV_NET_INFO);

  LOG_INF("Buffer length: %d bytes", buf->len);
  LOG_HEXDUMP_INF(buf->data, buf->len, "Provision data:");

  /* Calculate normal payload length (Telink format) */
  uint8_t normal_len =
      sizeof(provison_net_info_str) + sizeof(mesh_appkey_set_t);
  /* provison_net_info_str: 16 (net_key) + 2 (key_index) + 1 (flags) + 4
   * (iv_index) + 2 (unicast_address) = 25 */
  /* mesh_appkey_set_t: 3 (net_app_idx) + 16 (app_key) = 19 */
  /* normal_len = 25 + 19 = 44 bytes */

  /* IMPORTANT: Validate buffer length before parsing */
  size_t original_len = buf->len;
  if (original_len < normal_len) {
    LOG_ERR("Invalid payload length: %d bytes (minimum %d bytes required)",
            original_len, normal_len);
    /* Reset state to retry */
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    fprov->rcv_op = 0;
    return -EINVAL;
  }

  bool en_fast_revert_network = false;

  /* Check if payload has extra byte for fast_revert_network flag */
  if (original_len == normal_len + 1) {
    LOG_INF("  Payload has fast_revert_network flag (45 bytes)");
  } else if (original_len == normal_len) {
    LOG_INF("  Normal payload length: %d bytes (no fast_revert flag)",
            normal_len);
  } else {
    LOG_WRN("  Unexpected payload length: %d bytes (expected %d or %d)",
            original_len, normal_len, normal_len + 1);
    /* Continue parsing if minimum data is sufficient */
  }

  /* Parse provision data - Telink format */
  memset(&fprov->net_info, 0, sizeof(fprov->net_info));

  /* Network info - Parse from buffer (Telink format) */
  if (buf->len >= sizeof(provison_net_info_str)) {
    memcpy(&fprov->net_info.pro_data, buf->data, sizeof(provison_net_info_str));
    net_buf_simple_pull_mem(buf, sizeof(provison_net_info_str));
  } else {
    LOG_ERR("Buffer too short for network info: %d bytes (need %d)", buf->len,
            sizeof(provison_net_info_str));
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    fprov->rcv_op = 0;
    return -EINVAL;
  }

  /* App key info - Parse from buffer (Telink format) */
  if (buf->len >= sizeof(mesh_appkey_set_t)) {
    memcpy(&fprov->net_info.appkey_set, buf->data, sizeof(mesh_appkey_set_t));
    net_buf_simple_pull_mem(buf, sizeof(mesh_appkey_set_t));
  } else {
    LOG_ERR("Buffer too short for app key info: %d bytes (need %d)", buf->len,
            sizeof(mesh_appkey_set_t));
    mesh_fast_prov_sts_set(FAST_PROV_IDLE);
    fprov->rcv_op = 0;
    return -EINVAL;
  }

  /* Check if payload has extra byte for fast_revert_network flag */
  if (buf->len == 1) {
    /* Extra byte at the end is the fast_revert_network flag */
    uint8_t flag_byte = net_buf_simple_pull_u8(buf);
    en_fast_revert_network = (flag_byte == 1) ? true : false;
    LOG_INF("  Fast revert network flag detected: %s (byte=0x%02x)",
            en_fast_revert_network ? "ENABLED" : "DISABLED", flag_byte);
    if (en_fast_revert_network) {
      LOG_INF("  ⚠ Will skip CB/CC and go directly to CD!");
    }
  }

  /* Use assigned address from ADDR_SET (Telink behavior) */
  fprov->net_info.pro_data.unicast_address =
      fprov->mac_addr_info.addr > 0 ? fprov->mac_addr_info.addr
                                    : fprov->mac_addr_info.default_addr;

  LOG_INF("Provision data received:");
  LOG_INF("  NetKey index: 0x%04x", fprov->net_info.pro_data.key_index);
  LOG_INF("  Flags: 0x%02x", fprov->net_info.pro_data.flags);
  LOG_INF("  Unicast addr: 0x%04x", fprov->net_info.pro_data.unicast_address);
  LOG_HEXDUMP_INF(fprov->net_info.pro_data.net_key, 16, "NetKey:");
  LOG_HEXDUMP_INF(fprov->net_info.appkey_set.app_key, 16, "AppKey:");

  /* Set received opcode */
  fprov->rcv_op = (uint32_t)VD_MESH_PROV_DATA_SET;

  /* If fast_revert_network is enabled, skip CB/CC and go directly to CD (Telink
   * behavior) */
  if (en_fast_revert_network) {
    LOG_INF("  === Fast revert network ENABLED - Skipping CB/CC ===");
    LOG_INF("  Setting rcv_op to PROV_COMPLETE and state to FAST_PROV_CONFIRM");
    fprov->rcv_op = (uint32_t)VD_MESH_PROV_COMPLETE;
    fprov->delay = 0;
    mesh_fast_prov_sts_set(
        FAST_PROV_CONFIRM); // Set state to CONFIRM (will process CD next)
    LOG_INF("  State: %d -> %d (will process CD in state machine)",
            FAST_PROV_NET_INFO, FAST_PROV_CONFIRM);
  } else {
    /* Normal flow: wait for CB */
    mesh_fast_prov_sts_set(FAST_PROV_NET_INFO);
    LOG_INF("  Normal flow: State set to FAST_PROV_NET_INFO, waiting for CB");
  }

  return 0;
}

/**
 * @brief Handler: VD_MESH_PROV_CONFIRM (0xCB)
 */
static int handle_prov_confirm(const struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */
  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov; // Set global pointer

  LOG_INF("=== PROV_CONFIRM (0xCB) handler ===");
  LOG_INF("  Request from: src=0x%04x, dst=0x%04x", ctx->addr, ctx->recv_dst);
  LOG_INF("  Current state: %d (expected: %d)", fprov->cur_sts,
          FAST_PROV_NET_INFO);

  if (fprov->not_need_prov) {
    LOG_DBG("Device already provisioned, ignoring PROV_CONFIRM");
    return 0;
  }

  if (fprov->cur_sts != FAST_PROV_NET_INFO) {
    LOG_WRN("Wrong state for PROV_CONFIRM: %d (expected: %d)", fprov->cur_sts,
            FAST_PROV_NET_INFO);
    return 0;
  }

  LOG_INF("✓ Provision confirmation received - State: %d -> %d", fprov->cur_sts,
          FAST_PROV_CONFIRM);

  fprov->rcv_op = (uint32_t)VD_MESH_PROV_CONFIRM;

  /* IMPORTANT: Validate default network subnet exists before sending response
   */
  if (!bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX)) {
    LOG_ERR("Default network subnet not found");
    return -ENODEV;
  }

  /* Gửi vào hàng đợi Zero-Drop */
  int ret = mesh_tx_cmd_rsp(VD_MESH_PROV_CONFIRM_STS, NULL, 0,
                            bt_mesh_model_elem(model)->rt->addr, ctx->addr,
                            NULL, (void *)model);

  if (ret) {
    LOG_ERR("Failed to push confirmation response to queue, err: %d", ret);
  } else {
    LOG_INF("Provision confirmation queued successfully");
  }

  return ret;
}

/**
 * @brief Handler: VD_MESH_PROV_COMPLETE (0xCD)
 */
static int handle_prov_complete(const struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct net_buf_simple *buf) {
  /* IMPORTANT: Validate input parameters */
  if (!model || !ctx || !buf) {
    LOG_ERR("Invalid parameters: model=%p, ctx=%p, buf=%p", model, ctx, buf);
    return -EINVAL;
  }

  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision context is NULL!");
    return -ENODEV;
  }

  g_fast_prov = fprov; // Set global pointer

  LOG_INF("=== PROV_COMPLETE (0xCD) handler ===");
  LOG_INF("  Request from: src=0x%04x, dst=0x%04x", ctx->addr, ctx->recv_dst);
  LOG_INF("  Current state: %d (expected: %d)", fprov->cur_sts,
          FAST_PROV_CONFIRM);

  if (fprov->cur_sts != FAST_PROV_CONFIRM) {
    LOG_WRN("Wrong state for PROV_COMPLETE: %d (expected: %d)", fprov->cur_sts,
            FAST_PROV_CONFIRM);
    return 0;
  }

  uint16_t delay_ms = 0;
  if (buf->len >= 2) {
    delay_ms = net_buf_simple_pull_le16(buf);
  }

  LOG_INF("✓ Provision complete received - delay: %d ms", delay_ms);
  LOG_INF("  State: %d -> %d (will provision to main network after delay)",
          fprov->cur_sts, FAST_PROV_COMPLETE);

  fprov->rcv_op = (uint32_t)VD_MESH_PROV_COMPLETE;
  fprov->delay = delay_ms > 0 ? delay_ms : 2000;
  fprov->pending = true;
  fprov->start_tick = k_uptime_get();

  /* IMPORTANT: Set state to COMPLETE to let state machine wait for delay
   * Do not call del_tmp_keys() here because default network is needed for state
   * machine operation. del_tmp_keys() will be called in state machine after
   * provisioning success
   */
  mesh_fast_prov_sts_set(FAST_PROV_COMPLETE);

  return 0;
}

/**
 * @brief Handler: VD_MESH_ADDR_GET_STS (0xC7) - MAC Address Response
 * @note Provisionee does not need to process this response, but handler needed
 * for compatibility
 */
static int handle_addr_get_rsp(const struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf) {
  // Provisionee does not need to process response messages
  // Handler only for compatibility with model operations structure
  LOG_DBG("Received ADDR_GET_STS response (provisionee ignores)");
  return 0;
}

/**
 * @brief Handler: VD_MESH_ADDR_SET_STS (0xC9) - Address Assignment Response
 * @note Provisionee does not need to process this response, but handler needed
 * for compatibility
 */
static int handle_addr_set_rsp(const struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf) {
  // Provisionee does not need to process response messages
  // This handler is only for compatibility with model operations structure
  LOG_DBG("Received ADDR_SET_STS response (provisionee ignores)");
  return 0;
}

/**
 * @brief Handler: VD_MESH_PROV_CONFIRM_STS (0xCC) - Provision Confirm Response
 * @note Provisionee does not need to process this response, but handler needed
 * for compatibility
 */
static int handle_prov_confirm_rsp(const struct bt_mesh_model *model,
                                   struct bt_mesh_msg_ctx *ctx,
                                   struct net_buf_simple *buf) {
  // Provisionee does not need to process response messages
  // This handler is only for compatibility with model operations structure
  LOG_DBG("Received PROV_CONFIRM_STS response (provisionee ignores)");
  return 0;
}

/* Model Operations - Compatible with provisioner */
const struct bt_mesh_model_op fast_prov_op[] = {
    {
        VD_MESH_RESET_NETWORK,
        BT_MESH_LEN_MIN(0),
        handle_reset_network,
    },
    {
        VD_MESH_ADDR_GET,
        BT_MESH_LEN_MIN(2),
        handle_addr_get,
    },
    {
        VD_MESH_ADDR_GET_STS,
        BT_MESH_LEN_MIN(6), // Minimum 6 bytes (MAC address), flexible length
        handle_addr_get_rsp,
    },
    {
        VD_MESH_ADDR_SET,
        BT_MESH_LEN_MIN(8),
        handle_addr_set,
    },
    {
        VD_MESH_ADDR_SET_STS,
        0,
        handle_addr_set_rsp,
    },
    {
        VD_MESH_PROV_DATA_SET,
        BT_MESH_LEN_MIN(16),
        handle_prov_data_set,
    },
    {
        VD_MESH_PROV_CONFIRM,
        BT_MESH_LEN_MIN(0),
        handle_prov_confirm,
    },
    {
        VD_MESH_PROV_CONFIRM_STS,
        0,
        handle_prov_confirm_rsp,
    },
    {
        VD_MESH_PROV_COMPLETE,
        BT_MESH_LEN_MIN(2),
        handle_prov_complete,
    },
    BT_MESH_MODEL_OP_END,
};

/* Model Callbacks */
static int fast_prov_init(const struct bt_mesh_model *model) {
  fast_prov_par_t *fprov = model->rt->user_data;
  if (!fprov) {
    LOG_ERR("Fast Provision model init failed: user_data is NULL");
    return -EINVAL;
  }

  memset(fprov, 0, sizeof(*fprov));
  g_fast_prov = fprov;

  /* Register settings handler if not registered (needed to load MAC) */
  register_mac_settings_handler();

  /* Telink sets ele_adr_primary from MAC when initializing system
   * (mesh_init_all) From Telink log:
   * - ele_adr_primary = 0x0230 set from MAC (30:82:D4:67:CD:AB)
   * - SDK routes messages with destination=0x0230 to models
   * - This shows Telink SDK sets element address for models right from start
   *
   * In Nordic/Zephyr/NCS:
   * - Calculate temporary address from MAC address (Telink compatible)
   * - Element address (elem->rt->addr) will be set later in
   * default_network_enable()
   * - After setting, SDK will route messages with destination = temporary
   * address to handler
   *
   * NOTE: mesh_device_key_set_default() will be called again in
   * fast_provision_init() after settings_load() is called, to ensure MAC
   * is loaded from settings.
   */
  /* Do not call mesh_device_key_set_default() here because settings_load() not
   * called yet */

  /* Log temporary address - Element address will be set later in
   * default_network_enable() */
  if (g_fast_prov && g_fast_prov->mac_addr_info.default_addr != 0) {
    LOG_INF("  Temporary address calculated from MAC: 0x%04x",
            g_fast_prov->mac_addr_info.default_addr);
    LOG_DBG("  Element address (elem->rt->addr) will be set in "
            "default_network_enable()");
    LOG_DBG("  After setting, SDK will route messages with destination=0x%04x "
            "to handler",
            g_fast_prov->mac_addr_info.default_addr);
  }

  /* Bind default AppKey - always bind to receive messages from default network
   * Fast Provision model needs to bind with DEFAULT_APPKEY_INDEX (0x0001) to
   * receive messages from default network (C6, C7, C8, CA, CB, CD)
   *
   * After joining main network, will bind additional AppKey 0x0000 to receive
   * C5 from main network Model has CONFIG_BT_MESH_MODEL_KEY_COUNT=2 slots to
   * bind both AppKeys
   */
  struct bt_mesh_model *m = (struct bt_mesh_model *)model;
  LOG_INF("Fast Provision model init: key slots=%d", m->keys_cnt);

  if (!bt_mesh_model_has_key(m, DEFAULT_APPKEY_INDEX)) {
    for (int i = 0; i < m->keys_cnt; i++) {
      if (m->keys[i] == BT_MESH_KEY_UNUSED) {
        m->keys[i] = DEFAULT_APPKEY_INDEX;
        LOG_INF("  ✓ Bound DEFAULT_APPKEY_INDEX (0x%04X) to slot %d (default "
                "network)",
                DEFAULT_APPKEY_INDEX, i);
        break;
      }
    }
  } else {
    LOG_INF(
        "  Fast Provision model already bound to DEFAULT_APPKEY_INDEX (0x%04X)",
        DEFAULT_APPKEY_INDEX);
  }

  /* Log bind status */
  LOG_INF("Fast Provision model bind status:");
  LOG_INF("    - Default network (AppKey 0x%04X): %s", DEFAULT_APPKEY_INDEX,
          bt_mesh_model_has_key(m, DEFAULT_APPKEY_INDEX) ? "BOUND"
                                                         : "NOT BOUND");
  LOG_INF(
      "    - Main network (AppKey 0x0000): %s (will bind after provisioning)",
      bt_mesh_model_has_key(m, 0x0000) ? "BOUND" : "NOT BOUND");

  if (g_fast_prov->mac_addr_info.default_addr != 0) {
    LOG_INF("  Temporary element address: 0x%04x (from MAC, Telink compatible)",
            g_fast_prov->mac_addr_info.default_addr);
  }
  return 0;
}

static void fast_prov_reset(const struct bt_mesh_model *model) {
  fast_prov_par_t *fprov = model->rt->user_data;
  (void)fprov;         // Suppress unused variable warning
  g_fast_prov = fprov; // Set global pointer

  /* Telink: ele_adr_primary set from MAC when system initializes
   * Reset temporary address from MAC (like Telink)
   */
  mesh_device_key_set_default(); // Set MAC and temporary address from MAC

  mesh_fast_prov_val_init();

  LOG_INF("Fast Provision model reset");
  if (g_fast_prov->mac_addr_info.default_addr != 0) {
    LOG_INF("  Temporary element address: 0x%04x (from MAC, Telink compatible)",
            g_fast_prov->mac_addr_info.default_addr);
  }
}

const struct bt_mesh_model_cb fast_prov_cb = {
    .init = fast_prov_init,
    .reset = fast_prov_reset,
};

/* ============================================================================
 * Public API Functions - Called from main.c
 * ============================================================================
 */

/* Work item for Fast Provision processing */
static struct k_work_delayable fast_prov_work;

/**
 * @brief Fast Provision work handler - handles periodic state machine
 */
static void fast_prov_work_handler(struct k_work *work) {
  /* Ensure context initialized from model */
  if (g_fast_prov == NULL) {
    const struct bt_mesh_comp *comp = bt_mesh_comp_get();
    if (comp && comp->elem_count > 0) {
      const struct bt_mesh_elem *elem = &comp->elem[0];
      const struct bt_mesh_model *fast_prov_mod = bt_mesh_model_find_vnd(
          elem, FAST_PROV_VENDOR_COMPANY_ID, BT_MESH_MODEL_ID_VND_FAST_PROV);
      if (fast_prov_mod && fast_prov_mod->rt->user_data) {
        g_fast_prov = fast_prov_mod->rt->user_data;
      }
    }
  }

  /* Process Fast Provision state machine */
  if (g_fast_prov) {
    mesh_fast_prov_proc();
  }

  /* Process default network timeout - LIKE TELINK while(1) */
  /* This function checks default network timeout and automatically closes when
   * expired */
  /* Related to set_default_network_manual() - when setting timeout, need to
   * call periodically to check */
  proc_default_network();

  /* Reschedule to run periodically */
  k_work_reschedule(&fast_prov_work, K_MSEC(FAST_PROV_PROC_INTERVAL_MS));
}

/**
 * @brief Bind AppKey to Fast Provision Server model if needed
 *
 * @param app_idx Application Key index to bind
 * @return 0 on success, negative error code on failure
 *
 * @details Bind AppKey for Server model to transmit/receive messages
 */
int fast_provision_bind_appkey(uint16_t app_idx) {
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp || comp->elem_count == 0) {
    return -ENODEV;
  }

  const struct bt_mesh_elem *elem = &comp->elem[0];
  int bound_count = 0;

  /* Bind Server model - Only Server model needed for provisionee */
  const struct bt_mesh_model *fast_prov_srv = bt_mesh_model_find_vnd(
      elem, FAST_PROV_VENDOR_COMPANY_ID, BT_MESH_MODEL_ID_VND_FAST_PROV_SRV);
  if (fast_prov_srv) {
    struct bt_mesh_model *m = (struct bt_mesh_model *)fast_prov_srv;
    if (!bt_mesh_model_has_key(m, app_idx)) {
      for (int i = 0; i < m->keys_cnt; i++) {
        if (m->keys[i] == BT_MESH_KEY_UNUSED) {
          m->keys[i] = app_idx;
          LOG_INF("Fast Provision Server model AppKey 0x%04x bound", app_idx);
          bound_count++;
          break;
        }
      }
    } else {
      bound_count++; /* Already bound */
    }
  }

  /* Fallback: if not found with new model ID, try old ID */
  if (bound_count == 0) {
    const struct bt_mesh_model *fast_prov_mod = bt_mesh_model_find_vnd(
        elem, FAST_PROV_VENDOR_COMPANY_ID, BT_MESH_MODEL_ID_VND_FAST_PROV);
    if (fast_prov_mod) {
      struct bt_mesh_model *m = (struct bt_mesh_model *)fast_prov_mod;
      if (!bt_mesh_model_has_key(m, app_idx)) {
        for (int i = 0; i < m->keys_cnt; i++) {
          if (m->keys[i] == BT_MESH_KEY_UNUSED) {
            m->keys[i] = app_idx;
            LOG_INF("Fast Provision model (legacy) AppKey 0x%04x bound",
                    app_idx);
            return 0;
          }
        }
      } else {
        return 0; /* Already bound */
      }
    }
  }

  if (bound_count > 0) {
    return 0; /* At least one model bound */
  }

  return -ENOENT; /* No models found */
}

/**
 * @brief Initialize Fast Provision module - All initialization logic
 *
 * @details
 * - Get context from model (initialized in bt_mesh_init)
 * - Check provisioning status
 * - Setup default network (if needed)
 * - Initialize Fast Provision values
 * - Bind AppKey for Fast Provision model
 * - Start work queue to process state machine
 *
 * @note Called after bt_mesh_init() in bt_ready()
 */
void fast_provision_init(void) {
  /* Step 1: Get context from model (initialized in bt_mesh_init) */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (comp && comp->elem_count > 0) {
    const struct bt_mesh_elem *elem = &comp->elem[0];

    /* Prioritize finding Server model */
    const struct bt_mesh_model *fast_prov_mod = bt_mesh_model_find_vnd(
        elem, FAST_PROV_VENDOR_COMPANY_ID, BT_MESH_MODEL_ID_VND_FAST_PROV_SRV);

    /* Fallback: find with old model ID */
    if (!fast_prov_mod) {
      fast_prov_mod = bt_mesh_model_find_vnd(elem, FAST_PROV_VENDOR_COMPANY_ID,
                                             BT_MESH_MODEL_ID_VND_FAST_PROV);
    }

    if (fast_prov_mod && fast_prov_mod->rt->user_data) {
      g_fast_prov = fast_prov_mod->rt->user_data;
      LOG_INF("Fast Provision context initialized from model");
    } else {
      LOG_ERR("Fast Provision model not found or user_data is NULL!");
      return;
    }
  } else {
    LOG_ERR("Failed to get mesh composition");
    return;
  }

  /* Step 2: Setup default network - LIKE TELINK */
  /* Note: proc_default_network() called periodically in main loop (while(1))
   * Here only called once to initialize. Then will be called periodically.
   * check_and_del_default_power_on() NOT called here (Telink does not have) */
  proc_default_network();

  /* Step 3: Initialize Fast Provision values */
  mesh_fast_prov_val_init();

  /* Step 4: Bind default AppKey if default network is setup */
  if (default_network_is_present()) {
    fast_provision_bind_appkey(DEFAULT_APPKEY_INDEX);
  }

  /* Step 5: Start work queue to process state machine */
  fast_provision_start();
}

/**
 * @brief Start Fast Provision processing work queue
 */
void fast_provision_start(void) {
  k_work_init_delayable(&fast_prov_work, fast_prov_work_handler);
  k_work_reschedule(&fast_prov_work, K_MSEC(FAST_PROV_PROC_INTERVAL_MS));

  /* Allow blink on provision complete */
  network_allow_provision_blink(true);

  LOG_INF("Fast Provision work queue started");
}

/**
 * @brief Save MAC address to settings after settings ready
 *
 * @details
 * This function should be called after settings_load() is called to ensure
 * MAC is saved to flash. If MAC not saved, will save immediately.
 *
 * @note Called from bt_ready() after settings_load() called
 */
void fast_provision_save_mac_after_settings_ready(void) {
  if (!device_mac_initialized) {
    LOG_DBG("MAC not initialized yet, skipping save");
    return;
  }

  /* Check if MAC is saved by reading back */
  uint8_t saved_mac[6] = {0};
  ssize_t len = settings_load_one(MAC_ADDR_SETTINGS_KEY, saved_mac, 6);

  if (len == 6) {
    /* MAC saved, check if different */
    if (memcmp(saved_mac, device_mac_static, 6) == 0) {
      LOG_DBG("MAC already saved to settings: %02X:%02X:%02X:%02X:%02X:%02X",
              saved_mac[0], saved_mac[1], saved_mac[2], saved_mac[3],
              saved_mac[4], saved_mac[5]);
      return;
    } else {
      LOG_INF("MAC in settings differs from current MAC, updating...");
    }
  }

  /* Save MAC to settings */
  int err = settings_save_one(MAC_ADDR_SETTINGS_KEY, device_mac_static, 6);
  if (err == 0) {
    LOG_DBG("MAC saved to settings after settings_load(): "
            "%02X:%02X:%02X:%02X:%02X:%02X",
            device_mac_static[0], device_mac_static[1], device_mac_static[2],
            device_mac_static[3], device_mac_static[4], device_mac_static[5]);
  } else {
    LOG_ERR("Failed to save MAC to settings after settings_load(): %d", err);
  }
}

/**
 * @brief Stop Fast Provision processing work queue
 */
void fast_provision_stop(void) {
  k_work_cancel_delayable(&fast_prov_work);
  LOG_DBG("Fast Provision work queue stopped");
}

/**
 * @brief Reset Fast Provision state machine for retry
 *
 * @details
 * Reset all state variables to allow re-provisioning without power cycle.
 * Called when provisioning mode is toggled OFF then ON again.
 *
 * Resets:
 * - State machine to IDLE
 * - get_mac_en to true (allow responding to ADDR_GET)
 * - rcv_op, pending, delay, start_tick
 * - default_addr_random flag (allow address conflict resolution again)
 * - Partial net_info data (discard incomplete provisioning data)
 * - NOT reset: mac_addr_info (MAC stays the same), pid
 */
static void mesh_fast_prov_reset_for_retry(void) {
  if (!g_fast_prov) {
    return;
  }

  /* Skip if already provisioned to main network */
  if (network_is_main_network_provisioned()) {
    g_fast_prov->not_need_prov = true;
    LOG_INF("Fast provision reset skipped: main network provisioned");
    return;
  }

  /* Reset state machine */
  g_fast_prov->cur_sts = FAST_PROV_IDLE;
  g_fast_prov->last_sts = FAST_PROV_IDLE;
  g_fast_prov->rcv_op = 0;
  g_fast_prov->start_tick = 0;
  g_fast_prov->pending = false;
  g_fast_prov->delay = 0;

  /* Re-enable MAC address responses */
  g_fast_prov->get_mac_en = true;
  g_fast_prov->not_need_prov = false;

  /* Reset address randomization flag */
  default_addr_random = false;

  /* Clear provisioning lock */
  prov_lock_start_tick = 0;

  /* Clear deferred MAC response */
  get_mac_time = 0;
  get_mac_saved_model = NULL;
  get_mac_saved_dst = 0;

  /* Clear incomplete provisioning data */
  memset(&g_fast_prov->net_info, 0, sizeof(g_fast_prov->net_info));

  /* Re-initialize device key and temp address from MAC */
  mesh_device_key_set_default();

  LOG_INF("Fast provision state reset for retry");
}

/**
 * @brief Restart Fast Provision (reset state + restart work queue)
 *
 * @details
 * Called when provisioning mode is re-enabled after being disabled.
 * Ensures device can re-join network without power cycle.
 *
 * IMPORTANT: Do NOT call fast_provision_start() here because it calls
 * k_work_init_delayable() which re-initializes the work item.
 * On nRF54L with TrustZone, re-initializing a previously cancelled
 * delayable work can cause SECURE FAULT (Attribution unit violation).
 * The work was already initialized in fast_provision_init() at boot -
 * we just need to reschedule it.
 *
 * Uses 500ms delay to let mesh stack stabilize after
 * default_network_enable() creates new subnet/AppKey.
 */
void fast_provision_restart(void) {
  /* Reset state machine */
  mesh_fast_prov_reset_for_retry();

  /* Restart work queue - just reschedule, do NOT re-init
   * The work was already initialized in fast_provision_init() at boot.
   * Use 500ms delay to let mesh stack stabilize after default_network_enable()
   */
  k_work_reschedule(&fast_prov_work, K_MSEC(500));

  /* Allow blink on provision complete */
  network_allow_provision_blink(true);

  LOG_INF("Fast provision restarted (state reset + work queue restarted)");
}

/**
 * @brief Check if fast provision is busy (should not be interrupted)
 *
 * @return true if ADDR_SET was received within the last 10 seconds
 *
 * @details
 * After receiving ADDR_SET (set node), the provisioning flow is actively
 * in progress. Prevent user from toggling OFF provisioning mode for 10s
 * to allow the flow to complete (SET_ADDR → NET_INFO → CONFIRM → COMPLETE).
 */
bool fast_provision_is_busy(void) {
  if (prov_lock_start_tick == 0) {
    return false;
  }

  int64_t elapsed = k_uptime_get() - prov_lock_start_tick;
  if (elapsed < FAST_PROV_LOCK_DURATION_MS) {
    LOG_INF("Fast provision busy (lock: %lld/%d ms)", elapsed,
            FAST_PROV_LOCK_DURATION_MS);
    return true;
  }

  /* Lock expired */
  prov_lock_start_tick = 0;
  return false;
}
