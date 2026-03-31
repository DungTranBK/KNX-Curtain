/**
 * @file knx_adapter.cpp
 * @brief KNX Adapter for Shutter Actuator
 * @details Implements the interface between KNX Stack and the shutter
 *          application. All shutter-specific actions are dispatched
 *          via extern "C" callbacks for the firmware rèm to implement.
 */

#include "../../include/knx_adapter.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#include "../../include/app_device.h"
#include "../../include/curtain.h"
#include "../../include/knx_mapping_config.h"
#include "../../include/knx_provision.h"
#include "../../include/led.h"
#include "knx/bau07B0.h"
#include "knx/device_object.h"
#include "knx/property.h"
#include "knx/security_interface_object.h"
#include "knx/table_object.h"
#include "knx_facade.h"
#include "nordic_platform.h"

#ifndef CONFIG_LOG_DEFAULT_LEVEL
#define CONFIG_LOG_DEFAULT_LEVEL 3
#endif

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
LOG_MODULE_REGISTER(knx_adapter, CONFIG_LOG_DEFAULT_LEVEL);

// ============================================================================
// 1. EXTERN CALLBACKS (firmware rèm phải implement)
// ============================================================================

extern "C" {
extern void app_knx_shutter_move(bool going_down);
extern void app_knx_shutter_stop(void);
extern void app_knx_shutter_set_position(uint8_t percent);
extern int app_get_curtain_current_position(uint8_t curtain_idx,
                                            uint8_t* position);
}

// ============================================================================
// 2. GLOBALS
// ============================================================================

static NordicPlatform* knx_platform = nullptr;
static KnxFacade<NordicPlatform, Bau07B0>* knx = nullptr;

static bool initialized = false;

static struct k_work_delayable knx_work;
static struct k_work_delayable knx_prog_led_blink_work;
static struct k_work_delayable knx_prog_timeout_work;

K_MUTEX_DEFINE(knx_stack_mutex);

static knx_shutter_config_t shutter_config;

// ============================================================================
// 3. ZBUS CHANNELS
// ============================================================================

ZBUS_CHAN_DEFINE(chan_knx_rx, struct knx_byte_msg, nullptr, nullptr,
                 ZBUS_OBSERVERS(knx_rx_listener), ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(chan_knx_tx, struct knx_byte_msg, nullptr, nullptr,
                 ZBUS_OBSERVERS(app_tx_listener), ZBUS_MSG_INIT(0));

static void app_tx_listener_cb(const struct zbus_channel* chan) {
  if (&chan_knx_tx == chan && knx) {
    const struct knx_byte_msg* msg =
        (const struct knx_byte_msg*)zbus_chan_const_msg(chan);
    LOG_DBG("TX Request from App: 0x%02X", msg->value);
  }
}
ZBUS_LISTENER_DEFINE(app_tx_listener, app_tx_listener_cb);

static void knx_rx_listener_cb(const struct zbus_channel* chan) {
  if (&chan_knx_rx == chan) {
    k_work_reschedule(&knx_work, K_NO_WAIT);
  }
}
ZBUS_LISTENER_DEFINE(knx_rx_listener, knx_rx_listener_cb);

// ============================================================================
// 4. INTERNAL HELPERS
// ============================================================================

static void rx_trigger_callback(void) {
  k_work_reschedule(&knx_work, K_NO_WAIT);
}

/**
 * @brief Called when ETS unloads tables (before re-downloading).
 *        Clears learned scene positions from NVS so new ETS values take effect.
 */
static void knx_on_tables_unload(void) {
  LOG_INF(">>> Tables unloaded (ETS download): clearing learned scenes");
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    char key[32];
    snprintf(key, sizeof(key), "app/scene/%d", i);
    settings_delete(key);
  }
}

/**
 * @brief Load all parameters from KNX stack into shutter_config struct
 */
static void load_shutter_params(void) {
  // --- Basic parameters ---
  shutter_config.motor_type =
      (knx_blind_type_t)knx->paramByte(PARAM_MOTOR_TYPE);

  // TravelTime: 2 bytes, big-endian
  shutter_config.travel_time_sec = knx->paramWord(PARAM_TRAVEL_TIME);

  // --- Enable flags ---
  // IMPORTANT: paramBit(offset, bitOffset) uses (7-bitOffset) internally,
  // matching ETS XML BitOffset convention (0=MSB, 7=LSB).
  // EnableScene:      XML Offset=6, BitOffset=2
  // EnableSceneStore: XML Offset=7, BitOffset=0
  uint8_t raw_byte6 = knx->paramByte(PARAM_ENABLE_FLAGS);
  uint8_t raw_byte7 = knx->paramByte(PARAM_ENABLE_SCENE_STORE_BYTE);
  LOG_INF("DEBUG: raw Byte[6]=0x%02X, raw Byte[7]=0x%02X", raw_byte6,
          raw_byte7);

  shutter_config.enable_scene =
      knx->paramBit(PARAM_ENABLE_FLAGS, PARAM_ENABLE_SCENE_BIT);
  shutter_config.enable_scene_store =
      knx->paramBit(PARAM_ENABLE_SCENE_STORE_BYTE, PARAM_ENABLE_SCENE_STORE_BIT);

  // --- Scene assignments (10 scenes, 2 bytes each) ---
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    uint8_t byte0 = knx->paramByte(PARAM_SCENE_ACTIVE_NUM(i));
    shutter_config.scenes[i].active = (byte0 != SCENE_NOT_ACTIVE_VAL);
    shutter_config.scenes[i].num =
        (byte0 != SCENE_NOT_ACTIVE_VAL) ? SCENE_VAL_TO_NUM(byte0) : 0;
    shutter_config.scenes[i].pos = knx->paramByte(PARAM_SCENE_POS(i));
  }

  // --- OVERRIDE WITH LEARNED SCENES FROM NVS ---
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    if (!shutter_config.scenes[i].active) continue;

    char key[32];
    snprintf(key, sizeof(key), "app/scene/%d", i);
    uint8_t saved_pos;
    ssize_t len = settings_load_one(key, &saved_pos, 1);
    if (len == 1) {
      LOG_INF("  Scene %c: overriding pos %u%% -> %u%% (from NVS)", 'A' + i,
              shutter_config.scenes[i].pos, saved_pos);
      shutter_config.scenes[i].pos = saved_pos;
    }
  }

  // --- Log configuration ---
  LOG_INF("Shutter Config: MotorType=%d, TravelTime=%u",
          shutter_config.motor_type, shutter_config.travel_time_sec);
  LOG_INF("  EnableScene=%d, EnableSceneStore=%d", shutter_config.enable_scene,
          shutter_config.enable_scene_store);

  if (shutter_config.enable_scene) {
    for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
      if (shutter_config.scenes[i].active) {
        LOG_INF("  Scene %c: Num=%u, Pos=%u%%", 'A' + i,
                shutter_config.scenes[i].num, shutter_config.scenes[i].pos);
      }
    }
  }

  // --- SYNC TO CURTAIN APP ---
  // If configured, overwrite BLE/Flash settings with KNX priority
  if (knx->configured()) {
    // Map KNX Enums (1-based) to App Enums (0-based)
    uint8_t app_type = (shutter_config.motor_type > 0)
                           ? (uint8_t)(shutter_config.motor_type - 1)
                           : 0;
    // Map Time (Seconds to Milliseconds)
    uint32_t limit_ms = (uint32_t)shutter_config.travel_time_sec * 1000;

    if (limit_ms == 0) limit_ms = 20000;  // Default 20s if not set

    curtain_set_opt(0, app_type, limit_ms);
  }
}

/**
 * @brief Process incoming scene command (DPT 18.001)
 * @param raw_value Raw byte from GO6 SCENE
 */
static void process_scene_command(uint8_t raw_value) {
  uint8_t scene_num = DPT18_SCENE_NUM(raw_value);
  bool is_store = DPT18_IS_STORE(raw_value);

  LOG_INF(">>> SCENE CMD: raw=0x%02X, scene_num=%u, is_store=%d",
          raw_value, scene_num, is_store);

  // Debug: dump all configured scenes for comparison
  for (uint8_t d = 0; d < KNX_MAX_SCENES; d++) {
    if (shutter_config.scenes[d].active) {
      LOG_INF("    Slot %c: active=1, num=%u, pos=%u%%",
              'A' + d, shutter_config.scenes[d].num,
              shutter_config.scenes[d].pos);
    }
  }

  if (is_store) {
    if (!shutter_config.enable_scene_store) {
      LOG_WRN("Scene STORE ignored (SLME disabled)");
      return;
    }

    // Find configured scene slot matching this number
    for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
      if (!shutter_config.scenes[i].active) continue;
      if (shutter_config.scenes[i].num != scene_num) continue;

      // Get current position from app
      uint8_t current_pos = 0;
      if (app_get_curtain_current_position(0, &current_pos) == 0) {
        // Convert 0-255 back to 0-100%
        uint8_t knx_pos = (uint8_t)((uint16_t)current_pos * 100 / 255);
        shutter_config.scenes[i].pos = knx_pos;

        // Save to NVS
        char key[32];
        snprintf(key, sizeof(key), "app/scene/%d", i);
        settings_save_one(key, &knx_pos, 1);

        LOG_INF("Scene %c STORED: num=%u, new_pos=%u%%", 'A' + i, scene_num,
                knx_pos);
      } else {
        LOG_ERR("Scene STORE: failed to get current position!");
      }
      return;
    }
    LOG_WRN("Scene STORE: num=%u not configured in any slot, ignoring",
            scene_num);
    return;
  }

  // --- RECALL ---
  LOG_INF("Scene RECALL: searching for num=%u", scene_num);

  // Search through configured scenes for matching number
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    if (!shutter_config.scenes[i].active) continue;
    if (shutter_config.scenes[i].num != scene_num) {
      LOG_DBG("  Slot %c: num=%u != %u, skip", 'A' + i,
              shutter_config.scenes[i].num, scene_num);
      continue;
    }

    uint8_t target_pos = shutter_config.scenes[i].pos;
    LOG_INF("  >>> MATCH Slot %c: set_position(%u%%)", 'A' + i, target_pos);
    app_knx_shutter_set_position(target_pos);
    return;
  }

  LOG_WRN("Scene RECALL: num=%u NOT FOUND in any slot!", scene_num);
}

// ============================================================================
// 5. WORK HANDLER (Polling & Sync)
// ============================================================================

static void knx_work_handler(struct k_work* work) {
  if (!knx_platform || !knx) return;

  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  knx->loop();

  if (knx->configured()) {
    static uint32_t last_log = 0;
    if (k_uptime_get_32() - last_log > 10000) {
      last_log = k_uptime_get_32();
      LOG_INF("KNX Heartbeat: Conf=%d, IA=0x%04x, EnableScene=%d, GO6_flag=%d",
              knx->configured(), knx->individualAddress(),
              shutter_config.enable_scene,
              (int)knx->getGroupObject(GO_SH_SCENE).commFlag());
    }

    // Debug: log if GO3 is updated even if flags are not checked yet
    if (knx->getGroupObject(GO_SH_SAPBP).commFlag() == ComFlag::Updated) {
      LOG_INF("DEBUG: GO_SH_SAPBP (3) UPDATED! current flag=%d",
              (int)knx->getGroupObject(GO_SH_SAPBP).commFlag());
    }

    // --- GO1: MUD (Move Up/Down) ---
    if (knx->getGroupObject(GO_SH_MUD).commFlag() == ComFlag::Updated) {
      bool going_down = (bool)knx->getGroupObject(GO_SH_MUD).value();
      knx->getGroupObject(GO_SH_MUD).commFlag(ComFlag::Ok);
      LOG_INF("KNX -> MUD: %s", going_down ? "DOWN/CLOSE" : "UP/OPEN");
      app_knx_shutter_move(going_down);
    }

    // --- GO2: STOP ---
    if (knx->getGroupObject(GO_SH_STOP).commFlag() == ComFlag::Updated) {
      knx->getGroupObject(GO_SH_STOP).commFlag(ComFlag::Ok);
      LOG_INF("KNX -> STOP");
      app_knx_shutter_stop();
    }

    // --- GO3: SAPBP (Set Absolute Position %) ---
    if (knx->getGroupObject(GO_SH_SAPBP).commFlag() == ComFlag::Updated) {
      uint8_t pct = (uint8_t)knx->getGroupObject(GO_SH_SAPBP).value();
      knx->getGroupObject(GO_SH_SAPBP).commFlag(ComFlag::Ok);
      LOG_INF("KNX -> Position: %u%%", pct);
      app_knx_shutter_set_position(pct);
    }

    // --- GO6: SCENE (DPT 18.001) ---
    // IMPORTANT: Must use valueRef() to get raw byte with Store bit (bit 7).
    // value() decodes DPT 18.001 and returns only scene number (bits 0-5),
    // stripping the Store/Learn flag!
    {
      ComFlag scene_flag = knx->getGroupObject(GO_SH_SCENE).commFlag();
      if (scene_flag == ComFlag::Updated) {
        uint8_t* raw_ptr = knx->getGroupObject(GO_SH_SCENE).valueRef();
        uint8_t scene_val = raw_ptr ? raw_ptr[0] : 0;
        knx->getGroupObject(GO_SH_SCENE).commFlag(ComFlag::Ok);
        LOG_INF("*** GO6 SCENE UPDATED: raw=0x%02X, enable_scene=%d",
                scene_val, shutter_config.enable_scene);
        if (shutter_config.enable_scene) {
          process_scene_command(scene_val);
        } else {
          LOG_WRN("GO6 received but enable_scene=FALSE, ignoring!");
        }
      }
    }
  }
  k_mutex_unlock(&knx_stack_mutex);

  knx_platform->checkRxIdle();

  if (knx_platform->isRxActive()) {
    k_work_reschedule(&knx_work, K_USEC(500));
  } else {
    k_work_reschedule(&knx_work, K_MSEC(50));
  }
}

// Periodic LED blink while in Programming Mode
static void knx_prog_led_blink_work_handler(struct k_work* work) {
  if (!knx || !knx->progMode()) return;
  led_blink_color(1 << CONFIG_LED_IDX_KNX, LED_COLOR_PINK, 1,
                  LAST_STATE_REFRESH_LED, 300);
  k_work_schedule(&knx_prog_led_blink_work, K_SECONDS(5));
}

static void knx_prog_timeout_work_handler(struct k_work* work) {
  LOG_INF("KNX Programming Mode timeout (5 minutes) reached. Exiting...");
  knx_set_prog_mode(false);
}

// ============================================================================
// 6. PUBLIC API
// ============================================================================

extern "C" {

int knx_adapter_init(void) {
  LOG_INF("Initializing KNX Shutter Adapter...");

  k_work_init_delayable(&knx_work, knx_work_handler);
  k_work_init_delayable(&knx_prog_led_blink_work,
                        knx_prog_led_blink_work_handler);
  k_work_init_delayable(&knx_prog_timeout_work,
                        knx_prog_timeout_work_handler);

  // --- Step 0: Read Provisioning Data (MUST BE BEFORE NEW KNXFACADE) ---
  // This ensures SecurityInterfaceObject constructor sees the correct FDSK
  // and initializes PID_TOOL_KEY property correctly (mirroring hardcode).
  knx_provision_data_t prov;
  bool has_provision = knx_provision_read(&prov);
  if (has_provision) {
    LOG_INF("Provisioning data FOUND at boot - Setting FDSK");
    SecurityInterfaceObject::setFDSK(prov.fdsk);
  } else {
    LOG_WRN("No provisioning data found - using hardcoded defaults");
  }

  knx = new KnxFacade<NordicPlatform, Bau07B0>();
  if (!knx) return -ENOMEM;

  if (has_provision) {
    knx->bauNumber(knx_provision_get_bau_number(&prov));
  } else {
    knx->bauNumber(KNX_BAU_NUMBER_DEFAULT);
  }

  knx_platform = &knx->platform();

  // Explicitly log the active FDSK for verification as requested by user
  LOG_INF("FINAL ACTIVE FDSK for boot:");
  LOG_HEXDUMP_INF(SecurityInterfaceObject::fdsk(), 16, "Active FDSK:");

  // --- Metadata Setup (from knx_mapping_config.h) ---
  knx->manufacturerId(KNX_MANUFACTURER_ID);

  const uint8_t hwType[] = KNX_HARDWARE_TYPE;
  knx->hardwareType(hwType);
  knx->version(KNX_HARDWARE_VERSION);

  const uint8_t order[10] = KNX_ORDER_NUMBER;
  knx->orderNumber(order);

  // PID 13 & 66
  {
    const uint8_t progVersion[5] = KNX_PID13_PROG_VERSION;
    uint8_t count = 1;
    knx->bau().parameters().writeProperty(
        (PropertyID)13, 1, const_cast<uint8_t*>(progVersion), count);
    if (count == 0) LOG_WRN("PID 13 write failed");
  }
  {
    const uint8_t appProgId[3] = KNX_PID66_APP_PROG_ID;
    uint8_t count = 1;
    knx->bau().parameters().writeProperty(
        (PropertyID)66, 1, const_cast<uint8_t*>(appProgId), count);
    if (count == 0) LOG_WRN("PID 66 write failed");
  }

  // Register beforeRestartCallback to handle Master Reset (Erase all and
  // reboot)
  knx->bau().beforeRestartCallback(knx_wipe_config);

  // Register callback to clear learned scenes when ETS unloads tables
  TableObject::beforeTablesUnloadCallback(knx_on_tables_unload);

  // Step 1: Load NVS memory (EEPROM buffer: config, keys, group objects)
  LOG_INF("[BOOT] Step 1/3 - Loading KNX config from NVS...");
  knx_platform->getNonVolatileMemoryStart();

  // Step 2: readMemory() restores: IA, assoc table, group keys, seq numbers
  //    _sequenceNumber and _sequenceNumberToolAccess are loaded HERE.
  LOG_INF(
      "[BOOT] Step 2/3 - Restoring KNX memory (seq numbers, keys, config)...");
  knx->readMemory();
  knx->start();

  // Step 3: Enable UART LAST - only after ALL config & seq numbers are loaded.
  // This is the correct KNX boot sequence:
  //   readMemory() done → seq restored → UART RX enabled → ready for bus
  //   frames.
  LOG_INF("[BOOT] Step 3/3 - Enabling UART (KNX bus ready to receive)...");
  knx_platform->setRxTriggerCallback(rx_trigger_callback);
  knx_platform->setupUart();

  if (knx->configured()) {
    LOG_INF("KNX Status: [CONFIGURED] - Loading Shutter Parameters...");
    load_shutter_params();

    // --- Detailed Configuration log ---
    const char* motor_str = "Unknown";
    switch (shutter_config.motor_type) {
      case 1: motor_str = "HOZ_DZ3W"; break;
      case 2: motor_str = "HOZ_DZ4W"; break;
      case 3: motor_str = "HOZ_DT99"; break;
      case 4: motor_str = "ROLLER_DZ3W"; break;
      case 5: motor_str = "ROLLER_DZ4W"; break;
    }

    LOG_INF("===== KNX SHUTTER CONFIGURATION =====");
    LOG_INF("  Curtain Type      : %u (%s)", shutter_config.motor_type, motor_str);
    LOG_INF("  Travel Time       : %u seconds", shutter_config.travel_time_sec);
    LOG_INF("  Scenes Handling   : %s", shutter_config.enable_scene ? "ENABLED" : "DISABLED");
    LOG_INF("  Scene Storage     : %s", shutter_config.enable_scene_store ? "ENABLED (Learning OK)" : "DISABLED (Read-only)");
    LOG_INF("======================================");

    // Setup DPTs for Group Objects
    knx->getGroupObject(GO_SH_MUD).dataPointType(DPT_UpDown);
    knx->getGroupObject(GO_SH_STOP).dataPointType(DPT_Trigger);
    knx->getGroupObject(GO_SH_SAPBP).dataPointType(DPT_Scaling);
    knx->getGroupObject(GO_SH_IMUD).dataPointType(DPT_UpDown);
    knx->getGroupObject(GO_SH_CAPBP).dataPointType(DPT_Scaling);
    if (shutter_config.enable_scene) {
      knx->getGroupObject(GO_SH_SCENE).dataPointType(DPT_SceneControl);
    }
  } else {
    LOG_WRN("KNX Status: [NOT CONFIGURED] - Waiting for ETS download.");
  }

  k_work_reschedule(&knx_work, K_MSEC(100));
  initialized = true;
  LOG_INF("KNX Shutter Adapter Initialized Successfully");
  return 0;
}

bool knx_adapter_is_initialized(void) { return initialized; }

bool knx_is_configured(void) {
  if (!knx) return false;
  return knx->configured();
}

bool knx_toggle_prog_mode(void) {
  if (!knx) return false;
  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  bool new_mode = !knx->progMode();
  knx->progMode(new_mode);

  if (new_mode) {
    led_blink_color(1 << CONFIG_LED_IDX_KNX, LED_COLOR_PINK, 1,
                    LAST_STATE_REFRESH_LED, 300);
    k_work_schedule(&knx_prog_led_blink_work, K_SECONDS(5));
    k_work_schedule(&knx_prog_timeout_work, K_MINUTES(5));
  } else {
    k_work_cancel_delayable(&knx_prog_led_blink_work);
    k_work_cancel_delayable(&knx_prog_timeout_work);
    led_blink_color(1 << CONFIG_LED_IDX_KNX, LED_COLOR_RED, 1,
                    LAST_STATE_REFRESH_LED, 300);
  }

  k_mutex_unlock(&knx_stack_mutex);
  LOG_INF("KNX Programming Mode: %s", new_mode ? "ON" : "OFF");
  return new_mode;
}

void knx_set_prog_mode(bool enable) {
  if (!knx) return;
  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  bool current_mode = knx->progMode();
  if (current_mode == enable) {
    k_mutex_unlock(&knx_stack_mutex);
    return;
  }

  knx->progMode(enable);

  if (enable) {
    led_blink_color(1 << CONFIG_LED_IDX_KNX, LED_COLOR_PINK, 1,
                    LAST_STATE_REFRESH_LED, 300);
    k_work_schedule(&knx_prog_led_blink_work, K_SECONDS(5));
    k_work_schedule(&knx_prog_timeout_work, K_MINUTES(5));
  } else {
    k_work_cancel_delayable(&knx_prog_led_blink_work);
    k_work_cancel_delayable(&knx_prog_timeout_work);
    led_blink_color(1 << CONFIG_LED_IDX_KNX, LED_COLOR_RED, 1,
                    LAST_STATE_REFRESH_LED, 300);
  }

  k_mutex_unlock(&knx_stack_mutex);
}

bool knx_get_prog_mode(void) {
  if (!knx) return false;
  return knx->progMode();
}

void knx_log_device_info(void) {
  if (!knx) return;
  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  uint16_t ia = knx->individualAddress();
  LOG_INF("Individual Address: %d.%d.%d", (ia >> 12) & 0x0F, (ia >> 8) & 0x0F,
          ia & 0xFF);
  LOG_INF("Configured: %s", knx->configured() ? "YES" : "NO");
  LOG_INF("MotorType: %d", shutter_config.motor_type);
  k_mutex_unlock(&knx_stack_mutex);
}

void knx_wipe_config(void) {
  LOG_WRN("!!! KNX FACTORY RESET REQUESTED !!!");

  k_work_cancel_delayable(&knx_work);
  k_msleep(100);
  initialized = false;

  size_t size = knx_platform->getNonVolatileMemorySize();
  uint8_t* eeprom = knx_platform->getEepromBuffer(size);
  if (eeprom) {
    memset(eeprom, 0xFF, size);
    knx_platform->commitNonVolatileMemory();
  }

  sys_reboot(0);
}

void knx_send_direction_feedback(bool going_down) {
  if (!knx || !initialized) return;

  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  if (knx->configured()) {
    knx->getGroupObject(GO_SH_IMUD).value(going_down);
    LOG_INF("Direction -> KNX: %s", going_down ? "DOWN" : "UP");
  }
  k_mutex_unlock(&knx_stack_mutex);
}

void knx_send_position_status(uint8_t percent) {
  if (!knx || !initialized) return;

  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  if (knx->configured()) {
    knx->getGroupObject(GO_SH_CAPBP).value(percent);
    LOG_INF("Position -> KNX: %u%%", percent);
  }
  k_mutex_unlock(&knx_stack_mutex);
}

const knx_shutter_config_t* knx_get_shutter_config(void) {
  if (!initialized || !knx || !knx->configured()) return nullptr;
  return &shutter_config;
}

}  // extern "C"
