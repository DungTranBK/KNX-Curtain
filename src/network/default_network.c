
/**
 * @file default_network.c
 * @brief Default Network Management - Telink Compatible
 */
#include "../../include/default_network.h"

#include <errno.h> // For EBUSY
#include <network.h>
#include <string.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/cfg.h> // For bt_mesh_subnet_del()
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "../../include/app_device.h"
#include "../../include/fact.h"
#include "../../include/fast_provision.h"
#include "../../include/vendor.h"
#include "mesh/access.h"
#include "mesh/crypto.h" // For bt_mesh_s1() to check crypto ready (includes keys.h)
#include "mesh/foundation.h"
#include "mesh/mesh.h" // For bt_mesh_start(), bt_mesh_scan_disable(), bt_mesh_suspend()
#include "mesh/net.h" // For bt_mesh_subnet_next(), bt_mesh_net_settings_commit(), bt_mesh.flags
#include "mesh/rpl.h"
#include "mesh/settings.h" // For bt_mesh_model_settings_commit()

#define ENABLE_DEFAULT_NETWORK_LOG 1

#ifdef ENABLE_DEFAULT_NETWORK_LOG
LOG_MODULE_REGISTER(default_network, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(default_network, LOG_LEVEL_NONE);
#endif

/* Default Network Keys - Synced with Telink SDK */
const uint8_t default_net_key[DEFAULT_NET_KEY_SIZE] = {
    0x34, 0xD0, 0x2F, 0x83, 0x0B, 0x5E, 0x76, 0xB0,
    0x39, 0xE1, 0x35, 0x21, 0xDE, 0x73, 0x1A, 0x48};

const uint8_t default_app_key[DEFAULT_APP_KEY_SIZE] = {
    0x15, 0xF7, 0x41, 0xCB, 0xB7, 0xA0, 0x74, 0xC4,
    0xC6, 0x8E, 0x1D, 0xE2, 0xC3, 0xB6, 0x5D, 0xF9};

const uint8_t default_dev_key[DEFAULT_DEV_KEY_SIZE] = {
    0x9d, 0x6d, 0xd0, 0xe9, 0x6e, 0xb2, 0x5d, 0xc1,
    0x9a, 0x40, 0xed, 0x99, 0x14, 0xf8, 0xf0, 0x3f};

/* Forward declarations */
static void default_network_bind_appkey(void);
static void default_network_start(void);
static void default_network_set_temp_element_addr(void);
static bool is_mesh_stack_ready(void);
static uint8_t count_subnets(void);
static bool is_crypto_ready_for_key_import(void);

/* Default Network State - Similar to Telink */
typedef struct {
  bool enabled;
  uint32_t start_time_s;
  uint16_t time_len_s;
} default_network_state_t;

static default_network_state_t m_default_nw = {.enabled = false};
static bool set_tmp_keys_flag = false;
static bool del_tmp_keys_flag =
    true; /* Default true because default network is not present initially */
static bool is_enabling =
    false; /* Flag to avoid duplicate call when enabling */
static uint32_t provision_complete_time =
    0; /* Time when provisioned to delay closing default network */
static uint32_t last_enable_attempt_time =
    0; /* Time of last enable attempt to avoid retrying too fast */
static uint32_t mesh_init_time =
    0; /* Time when mesh stack initialized to delay enabling default network */
static uint32_t boot_time = 0; /* Boot time */
static bool default_network_configured_after_reboot =
    false; /* Flag to configure only once after reboot */

/* Delay after power on before enabling default network */
#define DEFAULT_NETWORK_ENABLE_DELAY_MS                                        \
  (2000) /* 2 seconds delay after power on */

/* Monitor work - Print default network status every 30 seconds */
static struct k_work_delayable monitor_work;

/**
 * @brief Check if default network is present
 */
bool default_network_is_present(void) {
  /* Check if subnet 0x0001 exists */
  const struct bt_mesh_subnet *subnet =
      bt_mesh_subnet_get(DEFAULT_NETWORK_SUBNET_INDEX);
  if (!subnet) {
    return false;
  }

  /* IMPORTANT: Check if keys are valid
   * If subnet exists but keys are invalid, subnet has been disabled
   * According to SDK subnet.c:bt_mesh_net_cred_find(), SDK checks keys[j].valid
   * before using subnet for encrypt/decrypt
   */
  if (!bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX)) {
    return false; /* Subnet deleted during check */
  }

  bool has_valid_keys = false;
  for (int i = 0; i < ARRAY_SIZE(subnet->keys); i++) {
    if (subnet->keys[i].valid) {
      has_valid_keys = true;
      break;
    }
  }

  if (!has_valid_keys) {
    return false; /* Subnet exists but keys invalid (disabled) */
  }

  /* Check if AppKey 0x0001 exists and belongs to subnet 0x0001
   * This identifies default network because:
   * - Default network has subnet index 0x0001 and AppKey index 0x0001
   * - Main network usually has subnet index 0x0000 and different AppKey index
   */
  if (!bt_mesh_app_key_exists(DEFAULT_APPKEY_INDEX)) {
    return false;
  }

  /* Check if AppKey belongs to subnet 0x0001
   * In Zephyr/NCS, bt_mesh_app_key_get() can be used to get AppKey info
   * But simpler: if subnet 0x0001 exists and AppKey 0x0001 exists,
   * and we added them together in default_network_enable(),
   * then it is the default network
   */
  return true;
}

/**
 * @brief Process default network state
 */
void proc_default_network(void) {
  /* Skip all network management while factory test is active */
  if (fact_is_activate()) {
    return;
  }

  bool is_provisioned = network_is_main_network_provisioned();
  bool should_close = false;

  LOG_DBG("proc_default_network: provisioned=%s, default_network_present=%s",
          is_provisioned ? "YES" : "NO",
          default_network_is_present() ? "YES" : "NO");

#if EN_PROVISIONING_TOGGLE
  /* Top-Level: Delete restored default network on boot (if Toggle ON) */
  if (default_network_is_present() &&
      !default_network_configured_after_reboot) {
    LOG_INF("Default network found on boot (Restored). Cleaning up...");
    del_tmp_keys();
    return;
  }
#endif

  /* Check timeout if set */
  if (m_default_nw.enabled && m_default_nw.time_len_s > 0) {
    uint32_t current_time = k_uptime_get() / 1000;
    uint32_t elapsed = current_time - m_default_nw.start_time_s;

    if (elapsed >= m_default_nw.time_len_s) {
      /* Timeout expired */
      should_close = true;
      m_default_nw.enabled = false;
      LOG_INF("Default network timeout expired, closing");
    }
  }

  if (should_close) {
    /* Close default network due to timeout
     * IMPORTANT: Only close if not currently enabling
     * and mesh stack is ready
     */
    if (!is_enabling) {
      del_tmp_keys();
    } else {
      LOG_DBG("Skipping default network close (currently enabling)");
    }
  } else if (!is_provisioned) {
    /* Not provisioned -> Open default network after delay (if not open) */
    bool is_present = default_network_is_present();

    if (!is_present) {
#if EN_PROVISIONING_TOGGLE
      /* Auto-enable disabled by toggle feature */
      /* Logic: If network is missing, do NOT auto-create. User must toggle ON
       * using button. */
      return;
#else
      /* Enable default network after boot delay (wait for crypto) */
      uint32_t current_time = k_uptime_get();

      /* Get boot time first time (when mesh init) */
      if (mesh_init_time == 0) {
        mesh_init_time = current_time;
        boot_time = current_time;
        LOG_DBG(
            "Mesh init detected, will enable default network after %u ms delay",
            DEFAULT_NETWORK_ENABLE_DELAY_MS);
      }

      /* Check if 2 seconds passed after power on */
      uint32_t elapsed = current_time - boot_time;
      if (elapsed < DEFAULT_NETWORK_ENABLE_DELAY_MS) {
        /* Not 2 seconds yet - wait */
        LOG_DBG("Waiting for boot delay (%u ms remaining)...",
                DEFAULT_NETWORK_ENABLE_DELAY_MS - elapsed);
        return;
      }

      /* Check crypto ready before enable */
      if (!is_crypto_ready_for_key_import()) {
        /* Crypto not ready - wait
         * proc_default_network() is called periodically, will retry
         */
        LOG_DBG("Crypto not ready yet, waiting for next proc_default_network() "
                "call");
        return;
      }

      /* 2 seconds passed and crypto ready - enable default network */
      LOG_INF("Enabling default network (boot delay: %u ms)", elapsed);
      set_tmp_keys(true);

      /* IMPORTANT: Set flag after enable to avoid recall
       * If subnet created successfully, next default_network_is_present() will
       * return true and else branch will check this flag to avoid re-configure
       */
      if (default_network_is_present()) {
        default_network_configured_after_reboot = true;
      }
#endif
    } else {
      /* Default network restored: Configure element address and keys if needed
       */
      if (default_network_configured_after_reboot) {
        /* Already configured, do nothing more */
        return;
      }

      uint32_t current_time = k_uptime_get();

      /* Get boot time first time (when mesh init) */
      if (mesh_init_time == 0) {
        mesh_init_time = current_time;
        boot_time = current_time;
        LOG_DBG(
            "Default network already present, will configure after %u ms delay",
            DEFAULT_NETWORK_ENABLE_DELAY_MS);
      }

      /* Check if 2 seconds passed after power on */
      uint32_t elapsed = current_time - boot_time;
      if (elapsed < DEFAULT_NETWORK_ENABLE_DELAY_MS) {
        /* Not 2 seconds yet - wait */
        LOG_DBG("Waiting for boot delay (%u ms remaining)...",
                DEFAULT_NETWORK_ENABLE_DELAY_MS - elapsed);
        return;
      }

      /* Check crypto ready before configure */
      if (!is_crypto_ready_for_key_import()) {
        /* Crypto not ready - wait */
        LOG_DBG("Crypto not ready yet, waiting for next proc_default_network() "
                "call");
        return;
      }

      /* 2 seconds passed and crypto ready - configure default network */
      LOG_INF("Configuring default network (boot delay: %u ms)", elapsed);
      set_tmp_keys(true);
      default_network_configured_after_reboot = true; /* Mark configured */
    }
  } else {
    /* Provisioned:
     * - If m_default_nw.enabled = true: Keep default network open (opened
     * manual via C5) -> Only check timeout, don't close
     * - If m_default_nw.enabled = false: Close default network (not needed
     * anymore)
     *
     * IMPORTANT: Wait a bit after provisioned for mesh stack to stabilize
     * before closing
     */
    bool is_present = default_network_is_present();

    if (m_default_nw.enabled) {
      /* Default network opened manual via C5 - KEEP OPEN
       * Only check timeout (handled above), don't close
       */
      if (!is_present && !is_enabling) {
        /* Default network should be open but is not
         * Only reopen if not currently enabling (avoid race
         * condition) and mesh stack ready
         */
        if (is_mesh_stack_ready()) {
          set_tmp_keys(true);
        }
      }
      /* If is_present = true or is_enabling = true, keep as is */
    } else {
      /* Not opened manual - close if open
       * IMPORTANT: Wait at least 5 seconds after provisioned for mesh stack to
       * stabilize completely. Avoid closing immediately after provisioned as it
       * may cause hang
       */
      if (is_present) {
        uint32_t current_time = k_uptime_get();

        /* First time detect provisioned, save time */
        if (provision_complete_time == 0) {
          provision_complete_time = current_time;
          LOG_INF("Provisioned detected, will close default network after 5 "
                  "seconds");
          return; /* Wait next time */
        }

        /* Check if 5 seconds passed */
        uint32_t elapsed = current_time - provision_complete_time;
        if (elapsed >= 5000) {
          LOG_INF("Closing default network after provisioning (waited %u ms)",
                  elapsed);
          /* Reset provision_complete_time BEFORE calling del_tmp_keys()
           * to avoid reset again if del_tmp_keys() fail or return immediately
           * and avoid infinite loop
           */
          provision_complete_time = 0;
          del_tmp_keys();
        } else {
          /* Not 5 seconds yet, log progress */
          LOG_DBG("Waiting to close default network (%u ms / 5000 ms)",
                  elapsed);
        }
      } else {
        /* Default network closed, reset timer */
        provision_complete_time = 0;
      }
    }
  }
}

/**
 * @brief Count number of current subnets
 *
 * @return Number of subnets
 */
static uint8_t count_subnets(void) {
  uint8_t count = 0;
  struct bt_mesh_subnet *subnet = bt_mesh_subnet_next(NULL);

  while (subnet) {
    count++;
    subnet = bt_mesh_subnet_next(subnet);
  }

  return count;
}

/**
 * @brief Check if mesh stack is ready
 */
static bool is_mesh_stack_ready(void) {
  /* Check if mesh composition exists - mesh stack initialized */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    return false; /* Mesh stack not initialized */
  }

  /* Check if any subnet */
  uint8_t subnet_count = count_subnets();

  if (subnet_count > 0) {
    /* Existing subnet -> check BT_MESH_VALID flag */
    if (!atomic_test_bit(bt_mesh.flags, BT_MESH_VALID)) {
      return false;
    }
  }
  /* No subnet -> mesh stack still ready to add new subnet */

  return true;
}

/**
 * @brief Check if crypto is ready for key import
 */
static bool is_crypto_ready_for_key_import(void) {
  uint8_t random_bytes[16];
  int ret;

  /* Try generating random to check crypto hardware ready
   * psa_generate_random() doesn't need key, only needs crypto hardware ready
   * If successful, crypto hardware accelerator is ready for key import
   */
  ret = bt_rand(random_bytes, sizeof(random_bytes));
  if (ret != 0) {
    /* Crypto not ready or error */
    LOG_DBG("Crypto not ready (random generation failed: %d)", ret);
    return false;
  }

  /* Random generation successful -> crypto hardware ready */
  return true;
}

/**
 * @brief Enable network with given keys (add keys, bind models, start mesh)
 * @param net_key  16-byte network key
 * @param app_key  16-byte application key
 */
int default_network_enable_with_keys(const uint8_t *net_key,
                                     const uint8_t *app_key) {
  uint8_t status;

  /* Check stack and crypto */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    LOG_ERR("Mesh stack not initialized");
    return -EAGAIN;
  }

  if (!is_crypto_ready_for_key_import()) {
    LOG_DBG("Crypto not ready for key import");
    return -EAGAIN;
  }

  /* Add NetKey */
  status = bt_mesh_subnet_add(DEFAULT_NETWORK_SUBNET_INDEX, net_key);
  if (status == STATUS_INSUFF_RESOURCES) {
    LOG_ERR("No room for subnet");
    return -ENOMEM;
  } else if (status == STATUS_UNSPECIFIED) {
    LOG_ERR("Failed to import net key (status: 0x%02x)", status);
    return -EIO;
  } else if (status != STATUS_SUCCESS && status != STATUS_IDX_ALREADY_STORED) {
    LOG_ERR("Failed to add net key, status: 0x%02x", status);
    return -EINVAL;
  }

  /* Add AppKey */
  status = bt_mesh_app_key_add(DEFAULT_APPKEY_INDEX,
                               DEFAULT_NETWORK_SUBNET_INDEX, app_key);
  if (status == STATUS_INSUFF_RESOURCES) {
    LOG_ERR("No room for app key");
    bt_mesh_subnet_del(DEFAULT_NETWORK_SUBNET_INDEX);
    return -ENOMEM;
  } else if (status == STATUS_INVALID_NETKEY) {
    LOG_ERR("Subnet 0x%04X not found for app key",
            DEFAULT_NETWORK_SUBNET_INDEX);
    return -EINVAL;
  } else if (status == STATUS_CANNOT_SET) {
    LOG_ERR("Failed to import app key (status: 0x%02x)", status);
    bt_mesh_subnet_del(DEFAULT_NETWORK_SUBNET_INDEX);
    return -EIO;
  } else if (status != STATUS_SUCCESS && status != STATUS_IDX_ALREADY_STORED) {
    LOG_ERR("Failed to add app key, status: 0x%02x", status);
    bt_mesh_subnet_del(DEFAULT_NETWORK_SUBNET_INDEX);
    return -EINVAL;
  }

  /* Bind keys, configure address, and start mesh */
  default_network_bind_appkey();

  if (bt_mesh_is_main_network_provisioned() == false) {
    default_network_set_temp_element_addr();
  }

  default_network_start();

  LOG_INF("Network enabled (subnet: 0x%04X, element: 0x%04X)",
          DEFAULT_NETWORK_SUBNET_INDEX,
          (comp && comp->elem_count > 0 && comp->elem[0].rt)
              ? comp->elem[0].rt->addr
              : 0);
  return 0;
}

/**
 * @brief Enable default network (add keys, bind models, start mesh)
 */
static int default_network_enable(void) {
  return default_network_enable_with_keys(default_net_key, default_app_key);
}

/**
 * @brief Set temporary keys (default network keys)
 */
void set_tmp_keys(bool enable) {
  if (enable) {
    /* Check if default network is present (by actual subnet checks) */
    bool is_present = default_network_is_present();

    /* If default network is present and configured, no need to do anything */
    if (is_present && default_network_configured_after_reboot) {
      set_tmp_keys_flag = true;
      del_tmp_keys_flag = false;
      is_enabling = false;
      last_enable_attempt_time = 0; /* Reset retry timer */
      return;
    }

    if (is_present) {
      /* Default network restored: Ensure configured (set address, bind keys) */
      LOG_DBG("Default network already present, but will ensure it's fully "
              "configured");
    }

    /* Set flags */
    set_tmp_keys_flag = true;
    del_tmp_keys_flag = false;

    /* IMPORTANT: Call default_network_enable() directly
     * Crypto has been checked ready in proc_default_network()
     * If still fail, it is real error, no retry
     */
    if (!is_enabling) {
      is_enabling = true;
      set_tmp_keys_flag = true;
      del_tmp_keys_flag = false;

      LOG_DBG("Calling default_network_enable()");
      int ret = default_network_enable();

      is_enabling = false;

      if (ret == 0) {
        /* Success */
        /* IMPORTANT: Set flag after enable success to avoid recall
         * If subnet created successfully, next default_network_is_present()
         * will return true and else branch in proc_default_network() will check
         * this flag to avoid re-configure
         */
        if (default_network_is_present()) {
          default_network_configured_after_reboot = true;
        }

        /* IMPORTANT: Restart Fast Provision state machine + work queue
         * When provisioning mode is toggled OFF then ON, the state machine
         * may be stuck in an intermediate state. Reset it to allow
         * re-provisioning without power cycle.
         */
        fast_provision_restart();
      } else {
        /* Error - log and do not retry */
        LOG_ERR("Failed to enable default network: %d", ret);
        set_tmp_keys_flag = false;
      }
    } else {
      LOG_DBG("Already enabling, skipping");
    }

  } else {
    /* Disable default network */
    is_enabling = false;
    del_tmp_keys();
  }
}

/**
 * @brief Bind AppKey to all models (SIG and vendor)
 */
static void default_network_bind_appkey(void) {
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (!comp) {
    return;
  }

  LOG_INF("Binding default AppKey to all models");

  for (int elem_idx = 0; elem_idx < comp->elem_count; elem_idx++) {
    const struct bt_mesh_elem *elem = &comp->elem[elem_idx];

    /* Bind AppKey to Fast Provision vendor model in default network */
    /* Support both new model ID (FAST_PROV_SRV) and old model ID (FAST_PROV) */
    for (int i = 0; i < elem->vnd_model_count; i++) {
      struct bt_mesh_model *model =
          (struct bt_mesh_model *)&elem->vnd_models[i];
      if (model->vnd.company == FAST_PROV_VENDOR_COMPANY_ID &&
          (model->vnd.id == BT_MESH_MODEL_ID_VND_FAST_PROV_SRV ||
           model->vnd.id == BT_MESH_MODEL_ID_VND_FAST_PROV)) {
        if (!bt_mesh_model_has_key(model, DEFAULT_APPKEY_INDEX)) {
          for (int j = 0; j < model->keys_cnt; j++) {
            if (model->keys[j] == BT_MESH_KEY_UNUSED) {
              model->keys[j] = DEFAULT_APPKEY_INDEX;
              break;
            }
          }
        }
      }
    }
  }
}

/* (Unbind-all helper removed; no longer used) */

/**
 * @brief Set temporary element address from MAC address (Telink compatible)
 */
static void default_network_set_temp_element_addr(void) {
  /* Get fast_provision context if available */
  extern fast_prov_par_t *g_fast_prov;

  if (!g_fast_prov) {
    LOG_WRN("g_fast_prov is NULL, cannot set temporary address");
    return;
  }

  LOG_INF("\n--- Setting temporary element address ---");

  /* IMPORTANT: Reuse temporary address calculated in
   * mesh_device_key_set_default() (like Telink: only calculate once in
   * mesh_flash_retrieve())
   *
   * Telink calculates ele_adr_primary once in mesh_flash_retrieve():
   *   ele_adr_primary = (tbl_mac[0] + (tbl_mac[1] << 8)) & (~(BIT(15)))
   *
   * In Nordic, temporary address already calculated in
   * mesh_device_key_set_default() with same Telink formula, so just reuse
   * here.
   */
  uint16_t tmp_addr;
  if (g_fast_prov->mac_addr_info.default_addr == 0) {
    /* If temporary address not set (should not happen), calculate with Telink
     * formula */
    tmp_addr = (g_fast_prov->mac_addr_info.mac[0] +
                (g_fast_prov->mac_addr_info.mac[1] << 8)) &
               0x7FFF;
    if (tmp_addr == 0) {
      tmp_addr = 1;
    }
    g_fast_prov->mac_addr_info.default_addr = tmp_addr;
    g_fast_prov->mac_addr_info.addr = tmp_addr;
    LOG_INF("Temporary address calculated (fallback): 0x%04X (from MAC[0:1] = "
            "%02x:%02x)",
            tmp_addr, g_fast_prov->mac_addr_info.mac[0],
            g_fast_prov->mac_addr_info.mac[1]);
  } else {
    /* Reuse temporary address calculated in mesh_device_key_set_default() */
    tmp_addr = g_fast_prov->mac_addr_info.default_addr;
  }

  /* Set element addresses for all elements */
  const struct bt_mesh_comp *comp = bt_mesh_comp_get();
  if (comp && comp->elem_count > 0) {
    const struct bt_mesh_elem *elem = &comp->elem[0];
    if (elem && elem->rt) {
      elem->rt->addr = tmp_addr;
      LOG_INF("Element[0] address set to: 0x%04X", tmp_addr);

      for (int i = 1; i < comp->elem_count; i++) {
        const struct bt_mesh_elem *e = &comp->elem[i];
        if (e && e->rt) {
          e->rt->addr = tmp_addr + i;
          LOG_INF("Element[%d] address set to: 0x%04X", i, tmp_addr + i);
        }
      }
    } else {
      LOG_WRN("Element[0] or rt is NULL, cannot set address");
    }
  } else {
    LOG_WRN("Mesh composition not available, cannot set element addresses");
  }
}

/**
 * @brief Start mesh networking after default network setup
 */
static void default_network_start(void) {
  /* Check if subnet exists (similar to settings.c:mesh_commit line 102) */
  if (!bt_mesh_subnet_next(NULL)) {
    LOG_WRN("No subnet found, cannot start mesh stack");
    return;
  }

  /* If BT_MESH_VALID is set, mesh stack already started */
  if (atomic_test_bit(bt_mesh.flags, BT_MESH_VALID)) {
    LOG_DBG("BT_MESH_VALID already set, mesh stack already started");
    return;
  }

  /* Commit settings (similar to settings.c:mesh_commit lines 107-108) */
  bt_mesh_net_settings_commit();
  bt_mesh_model_settings_commit();

  /* Set BT_MESH_VALID flag (similar to settings.c:mesh_commit line 110) */
  atomic_set_bit(bt_mesh.flags, BT_MESH_VALID);

  /* Start mesh stack (similar to settings.c:mesh_commit line 112) */
  int ret = bt_mesh_start();
  if (ret != 0) {
    LOG_ERR("Failed to start mesh stack: %d", ret);
    atomic_clear_bit(bt_mesh.flags, BT_MESH_VALID);
  } else {
    LOG_DBG("Mesh stack started successfully");
  }
}

/**
 * @brief Delete temporary keys (Disable default network)
 */
void del_tmp_keys(void) {
  uint8_t status;
  bool subnet_exists;

  LOG_INF("========== del_tmp_keys: START ==========");
  LOG_INF("del_tmp_keys_flag: %s", del_tmp_keys_flag ? "true" : "false");

  /* IMPORTANT: Check actual subnet instead of relying on flag
   * Flag might be incorrect due to race condition or reset
   * Check actual subnet to ensure accuracy
   */
  subnet_exists = bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX);
  if (!subnet_exists && del_tmp_keys_flag) {
    /* Subnet not exists and flag set -> already closed */
    LOG_INF("Already closed (subnet not exists and flag set), returning");
    return; /* Already closed */
  }

  /* If subnet not exists but flag not set -> reset flag and return */
  if (!subnet_exists) {
    del_tmp_keys_flag = true;
    set_tmp_keys_flag = false;
    LOG_INF("Subnet not exists, resetting flags - DONE");
    return;
  }

  /* If flag set but subnet still exists -> reset flag and continue closing
   * Could be race condition or close not completed
   */
  if (del_tmp_keys_flag) {
    LOG_WRN("Flag set but subnet still exists, resetting flag and closing");
    del_tmp_keys_flag = false; /* Reset to allow closing */
  }

  LOG_INF("Closing default network (subnet 0x%04X)",
          DEFAULT_NETWORK_SUBNET_INDEX);
  LOG_INF("Subnet exists check: %s", subnet_exists ? "YES" : "NO");

  del_tmp_keys_flag = true;
  set_tmp_keys_flag = false;
  LOG_INF("Flags set: del_tmp_keys_flag=true, set_tmp_keys_flag=false");

  /* Step 0: Stop Fast Provision work queue to avoid accessing deleted keys
   * IMPORTANT: Must stop before deleting subnet
   */
  // LOG_INF("Step 0: Stopping Fast Provision...");
  fast_provision_stop();
  // LOG_INF("Step 0: Fast Provision stopped");

  /* Step 1: Delete AppKey BEFORE deleting subnet (optional but recommended -
   * per ChatGPT example) According to SDK app_keys.c:bt_mesh_app_key_del(), it
   * will:
   * - Delete AppKey and unbind all models
   * - Schedule settings delete if CONFIG_BT_SETTINGS
   */
  LOG_INF("Step 1: Deleting AppKey 0x%04X (net_idx 0x%04X)...",
          DEFAULT_APPKEY_INDEX, DEFAULT_NETWORK_SUBNET_INDEX);
  status =
      bt_mesh_app_key_del(DEFAULT_APPKEY_INDEX, DEFAULT_NETWORK_SUBNET_INDEX);
  LOG_INF("Step 1: bt_mesh_app_key_del returned: 0x%02x", status);
  if (status != STATUS_SUCCESS && status != STATUS_INVALID_BINDING) {
    LOG_INF("Step 1: AppKey delete returned 0x%02x (ignore if unknown)",
            status);
  } else {
    LOG_INF("Step 1: AppKey 0x%04X deleted successfully", DEFAULT_APPKEY_INDEX);
  }

  /* Step 2: Delete subnet completely using SDK function (per ChatGPT example)
   * According to SDK subnet.c:bt_mesh_subnet_del() and subnet_del():
   * - Update settings (delete) if CONFIG_BT_SETTINGS
   * - Destroy keys
   * - Clear loopback buffers
   * - Call subnet_evt with BT_MESH_KEY_DELETED -> app_keys callback
   * automatically deletes AppKeys
   * - Memset subnet to 0
   *
   * When re-enabling, bt_mesh_subnet_add() will create new subnet with same
   * net_idx
   */
  LOG_INF("Step 2: Deleting subnet 0x%04X...", DEFAULT_NETWORK_SUBNET_INDEX);
  status = bt_mesh_subnet_del(DEFAULT_NETWORK_SUBNET_INDEX);
  LOG_INF("Step 2: bt_mesh_subnet_del returned: 0x%02x", status);

  if (status != STATUS_SUCCESS && status != STATUS_INVALID_NETKEY) {
    LOG_ERR("Step 2: bt_mesh_subnet_del FAILED: 0x%02x", status);
    /* Reset flags to allow retry */
    del_tmp_keys_flag = false;
    LOG_INF("========== del_tmp_keys: FAILED ==========");
    return;
  }

  /* Verify subnet is deleted */
  subnet_exists = bt_mesh_subnet_exists(DEFAULT_NETWORK_SUBNET_INDEX);
  LOG_INF("Step 3: Verify subnet deleted - exists: %s",
          subnet_exists ? "YES (ERROR!)" : "NO (OK)");

  // LOG_INF("Default mesh subnet 0x%03x deleted successfully",
  // DEFAULT_NETWORK_SUBNET_INDEX);

  LOG_INF("========== del_tmp_keys: SUCCESS ==========");

  /* Clear RPL */
  bt_mesh_rpl_clear();
}

/**
 * @brief Manually set default network state
 */
void set_default_network_manual(bool enable, uint16_t time_s) {
  LOG_INF("set_default_network_manual: enable=%s, time_s=%u",
          enable ? "true" : "false", time_s);
  if (enable) {
    LOG_INF("\n ---1");
    /* IMPORTANT: Set enabled = true BEFORE opening default network
     * so proc_default_network() does not close immediately after opening
     */
    uint16_t timeout = (time_s > 0) ? time_s : DEFAULT_NETWORK_DEFAULT_TIME_S;
    m_default_nw.enabled = true;
    m_default_nw.start_time_s = k_uptime_get() / 1000;
    m_default_nw.time_len_s = timeout;

    bool already_enabled = default_network_is_present();

    /* Open default network if not open */
    if (!already_enabled) {
      LOG_INF("Opening default network (timeout=%u s)", timeout);
      set_tmp_keys(true);
    }

  } else {
    LOG_INF("\n ---2");
    /* Close default network immediately */
    m_default_nw.enabled = false;
    m_default_nw.time_len_s = 0;
    del_tmp_keys();
  }
}

/**
 * @brief Monitor default network status
 */
static void monitor_default_network_status(void) {
  bool is_present = default_network_is_present();
  bool is_provisioned = network_is_main_network_provisioned();
  uint32_t current_time = k_uptime_get() / 1000;

  LOG_INF("Default Network: %s | Main Network: %s",
          is_present ? "OPEN" : "CLOSED",
          is_provisioned ? "PROVISIONED" : "NOT_PROVISIONED");

  if (is_present && m_default_nw.enabled && m_default_nw.time_len_s > 0) {
    uint32_t elapsed = current_time - m_default_nw.start_time_s;
    uint32_t remaining = (elapsed < m_default_nw.time_len_s)
                             ? (m_default_nw.time_len_s - elapsed)
                             : 0;
    if (remaining > 0) {
      LOG_INF("  Timeout remaining: %u seconds", remaining);
    }
  }
}

/**
 * @brief Work handler for monitoring default network
 *
 * @param work Work item pointer
 */
static void monitor_work_handler(struct k_work *work) {
  monitor_default_network_status();

  /* Reschedule after 30 seconds */
  k_work_reschedule(&monitor_work, K_SECONDS(30));
}

/**
 * @brief Initialize default network monitoring
 */
void default_network_monitor_init(void) {
  /* Init boot time - time of power on */
  boot_time = k_uptime_get();
  mesh_init_time = 0; /* Reset so proc_default_network() can set again */

  /* Init monitor work */
  k_work_init_delayable(&monitor_work, monitor_work_handler);
  k_work_reschedule(&monitor_work, K_SECONDS(30));
}

#include "mesh/app_keys.h"

#if 0
/* GLOBAL counter for file (avoid undeclared error) */
static int g_subnet_count = 0;

/* Callback function for Subnet */
static void net_info_cb(struct bt_mesh_subnet *sub) {
  g_subnet_count++;
  LOG_INF("--- Subnet #%d ---", g_subnet_count);
  LOG_INF("  NetKey Index: 0x%04X", sub->net_idx);

  /* Print key info */
  if (sub->keys[0].valid) {
    LOG_INF("  Key[0]: Valid (NetID: %02x%02x...)", sub->keys[0].net_id[0],
            sub->keys[0].net_id[1]);
  }
}

/**
 * @brief Print info of all networks (Subnets) and AppKeys
 *        Public API Safe Version
 */
void default_network_print_info(void) {
  LOG_INF("=== Network Information List ===");

  /* Reset counter */
  g_subnet_count = 0;

  /* Print subnets using foreach */
  bt_mesh_subnet_foreach(net_info_cb);

  if (g_subnet_count == 0) {
    LOG_INF("No subnets found.");
  }

  LOG_INF("--- AppKeys List (Scanning Index 0-20) ---");
  bool app_key_found = false;

  /* Scan first 20 indices (commonly used) via Public API */
  for (uint16_t i = 0; i <= 20; i++) {
    if (bt_mesh_app_key_exists(i)) {
      LOG_INF("  AppKey Index: 0x%04X - EXISTS", i);
      app_key_found = true;
    }
  }

  if (!app_key_found) {
    LOG_INF("  No AppKeys found in range 0-20.");
  }

  LOG_INF("==============================");
}
#endif

#if 1

#include <psa/crypto.h>

#include "mesh/app_keys.h"

/* ================================================================= */
/* 1. STRUCT & GLOBALS DEFINITIONS (MUST BE FIRST)                   */
/* ================================================================= */

static int g_subnet_count = 0;

/* --- INTERNAL SDK STRUCTURES (Replicated) --- */
struct bt_mesh_app_cred_dummy {
  uint8_t id;
  struct bt_mesh_key val;
};

struct bt_mesh_app_key_dummy {
  uint16_t net_idx;
  uint16_t app_idx;
  bool updated;
  struct bt_mesh_app_cred_dummy keys[2];
};

/* --- EXTERN HACK FUNCTION --- */
extern void *bt_mesh_app_key_get_internal_ptr(uint16_t app_idx);

/* ================================================================= */
/* 2. HELPER FUNCTIONS (Define before use to fix implicit errors)    */
/* ================================================================= */

static void print_key_material(const char *name, const uint8_t *key_val) {
  LOG_HEXDUMP_INF(key_val, 16, name);
}

static void try_print_psa_key(const char *name, struct bt_mesh_key *key) {
  psa_key_id_t *p_id = (psa_key_id_t *)key;
  psa_key_id_t key_id = *p_id;

  uint8_t raw_key[32];
  size_t out_len = 0;
  psa_status_t status =
      psa_export_key(key_id, raw_key, sizeof(raw_key), &out_len);

  if (status == PSA_SUCCESS) {
    LOG_INF("  %s (PSA SUCCESS):", name);
    LOG_HEXDUMP_INF(raw_key, out_len, "    Decrypted Raw Key");
  } else {
    /* PSA_ERROR_NOT_PERMITTED (-134) usually */
    LOG_INF("  %s (PSA Failed %d): Handle 0x%08x", name, status, key_id);
  }
}

/* Callback for AppKey Iteration (Helper) */
static void print_app_key_details(uint16_t app_idx) {
  LOG_INF("  AppKey Index: 0x%04X - EXISTS", app_idx);

  /* --- HACK: GET INTERNAL STRUCT --- */
  struct bt_mesh_app_key_dummy *key =
      (struct bt_mesh_app_key_dummy *)bt_mesh_app_key_get_internal_ptr(app_idx);

  if (key) {
    LOG_INF("    Bound to NetKey: 0x%04X", key->net_idx);
    /* Note: key->keys[0].val is struct bt_mesh_key, containing psa_id */
    try_print_psa_key("    Raw AppKey", &key->keys[0].val);

    if (key->updated) {
      try_print_psa_key("    Raw AppKey(New)", &key->keys[1].val);
    }
  } else {
    LOG_INF("    (Struct ptr null - HACK failed)");
  }
}

/* Callback function for Subnet */
static void net_info_cb(struct bt_mesh_subnet *sub) {
  g_subnet_count++;
  LOG_INF("--- Subnet #%d ---", g_subnet_count);
  LOG_INF("  NetKey Index: 0x%04X", sub->net_idx);
  /* Key Refresh Phase & NodeID State */
  LOG_INF("  KR Phase: %d, NodeID: %d", sub->kr_phase, sub->node_id);

  if (sub->keys[0].valid) {
    LOG_INF("  Key[0] (Current): NetID %02x%02x...", sub->keys[0].net_id[0],
            sub->keys[0].net_id[1]);
    try_print_psa_key("    Raw NetKey", &sub->keys[0].net);
  }
  if (sub->keys[1].valid) {
    LOG_INF("  Key[1] (Update): NetID %02x%02x...", sub->keys[1].net_id[0],
            sub->keys[1].net_id[1]);
    try_print_psa_key("    Raw NetKey(New)", &sub->keys[1].net);
  }
}

/* ================================================================= */
/* 3. MAIN PRINT FUNCTION                                            */
/* ================================================================= */

/**
 * @brief Print info of all networks (Subnets) and AppKeys
 */
void default_network_print_info(void) {
  LOG_INF("=== Network Information (Deep Debug) ===");

  /* 1. Node Address */
  uint16_t primary_addr = bt_mesh_primary_addr();
  LOG_INF("  Node Primary Address: 0x%04X %s", primary_addr,
          (primary_addr == BT_MESH_ADDR_UNASSIGNED) ? "(Unassigned)" : "");

  /* 3. Subnets */
  g_subnet_count = 0;
  bt_mesh_subnet_foreach(net_info_cb);
  if (g_subnet_count == 0)
    LOG_INF("No subnets found.");

  /* 4. AppKeys */
  LOG_INF("--- AppKeys List (Scanning 0-20) ---");
  bool app_key_found = false;

  /* Manual Loop for commonly used indices */
  for (uint16_t i = 0; i <= 20; i++) {
    if (bt_mesh_app_key_exists(i)) {
      app_key_found = true;
      print_app_key_details(i);
    }
  }

  if (!app_key_found)
    LOG_INF("  No AppKeys found in range 0-20.");

  LOG_INF("========================================");
}

#else
#include <psa/crypto.h>

/* Helper to print raw bytes */
static void print_key_material(const char *name, const uint8_t *key_val) {
  LOG_HEXDUMP_INF(key_val, 16, name);
}

/* Helper to try exporting PSA Key */
static void try_print_psa_key(const char *name, struct bt_mesh_key *key) {
  /* Hack: Assume struct bt_mesh_key in SDK 3.0.1 contains psa_key_id_t at start
   */
  /* We cast struct pointer to psa_key_id_t* to get ID */
  psa_key_id_t *p_id = (psa_key_id_t *)key;
  psa_key_id_t key_id = *p_id;

  uint8_t raw_key[32];
  size_t out_len = 0;
  psa_status_t status;

  /* Call PSA Crypto export API */
  status = psa_export_key(key_id, raw_key, sizeof(raw_key), &out_len);

  if (status == PSA_SUCCESS) {
    LOG_INF("  %s (PSA Export SUCCESS):", name);
    LOG_HEXDUMP_INF(raw_key, out_len, "    Decrypted Raw Key");
  } else {
    LOG_INF("  %s (PSA Export Failed): Status %d", name, status);
    /* If status = -134 (PSA_ERROR_NOT_PERMITTED), means HW blocks export */
    LOG_INF("    Key Handle ID: 0x%08x", key_id);
  }
}

/* GLOBAL static counter */
static int g_subnet_count = 0;

/* Callback function for Subnet */
static void net_info_cb(struct bt_mesh_subnet *sub) {
  g_subnet_count++;
  LOG_INF("--- Subnet #%d ---", g_subnet_count);
  LOG_INF("  NetKey Index: 0x%04X", sub->net_idx);

  /* Print key info */
  if (sub->keys[0].valid) {
    LOG_INF("  Key[0]: Valid (NetID: %02x%02x...)", sub->keys[0].net_id[0],
            sub->keys[0].net_id[1]);

    /* Try decrypt/export raw key */
    try_print_psa_key("Key[0]", &sub->keys[0].net);
  }
}

/**
 * @brief Print info of all networks (Subnets) and AppKeys
 */
void default_network_print_info(void) {
  LOG_INF("=== Network Information List ===");

  /* Print default key (Hardcoded) for reference */
  LOG_INF("--- Known Hardcoded Keys (Reference) ---");
  print_key_material("Default NetKey:", default_net_key);
  print_key_material("Default AppKey:", default_app_key);

  /* Reset counter */
  g_subnet_count = 0;

  /* Print Subnets using foreach */
  bt_mesh_subnet_foreach(net_info_cb);

  if (g_subnet_count == 0) {
    LOG_INF("No subnets found.");
  }

  LOG_INF("--- AppKeys List (Scanning Index 0-20) ---");
  bool app_key_found = false;

  for (uint16_t i = 0; i <= 20; i++) {
    if (bt_mesh_app_key_exists(i)) {
      LOG_INF("  AppKey Index: 0x%04X - EXISTS", i);
      app_key_found = true;
    }
  }

  if (!app_key_found) {
    LOG_INF("  No AppKeys found in range 0-20.");
  }

  LOG_INF("==============================");
}
#endif