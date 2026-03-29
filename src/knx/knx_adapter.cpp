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
#include "../../include/knx_mapping_config.h"
#include "../../include/knx_provision.h"
#include "../../include/led.h"
#include "knx/bau07B0.h"
#include "knx/device_object.h"
#include "knx/property.h"
#include "knx/security_interface_object.h"
#include "knx_facade.h"
#include "nordic_platform.h"

#ifndef CONFIG_LOG_DEFAULT_LEVEL
#define CONFIG_LOG_DEFAULT_LEVEL 3
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(knx_adapter, CONFIG_LOG_DEFAULT_LEVEL);

// ============================================================================
// 1. EXTERN CALLBACKS (firmware rèm phải implement)
// ============================================================================

extern "C" {
extern void app_knx_shutter_move(bool going_down);
extern void app_knx_shutter_stop(void);
extern void app_knx_shutter_set_position(uint8_t percent);
}

// ============================================================================
// 2. GLOBALS
// ============================================================================

static NordicPlatform* knx_platform = nullptr;
static KnxFacade<NordicPlatform, Bau07B0>* knx = nullptr;

static bool initialized = false;

static struct k_work_delayable knx_work;
static struct k_work_delayable knx_prog_led_blink_work;

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
 * @brief Load all parameters from KNX stack into shutter_config struct
 */
static void load_shutter_params(void) {
  // --- Basic parameters ---
  shutter_config.blind_type =
      (knx_blind_type_t)knx->paramByte(PARAM_BLIND_TYPE);
  shutter_config.time_mode =
      (knx_time_mode_t)knx->paramByte(PARAM_TIME_SAME_DIFF);

  // TimeOpen: 2 bytes, big-endian
  shutter_config.time_open_sec = knx->paramWord(PARAM_TIME_OPEN);

  // TimeClose: depends on time_mode
  if (shutter_config.time_mode == TIME_MODE_INDEPENDENT) {
    shutter_config.time_close_sec = knx->paramWord(PARAM_TIME_CLOSE);
  } else {
    shutter_config.time_close_sec = shutter_config.time_open_sec;
  }

  // --- Enable flags (bit-packed at offset 6) ---
  uint8_t enable_byte = knx->paramByte(PARAM_ENABLE_FLAGS);
  shutter_config.enable_position = (enable_byte >> PARAM_ENABLE_POS_BIT) & 1;
  shutter_config.enable_status = (enable_byte >> PARAM_ENABLE_STATUS_BIT) & 1;
  shutter_config.enable_scene = (enable_byte >> PARAM_ENABLE_SCENE_BIT) & 1;

  // --- Relay count (Custom mode only) ---
  shutter_config.relay_count = knx->paramByte(PARAM_RELAY_COUNT);

  // --- Scene assignments (10 scenes, 2 bytes each) ---
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    uint8_t byte0 = knx->paramByte(PARAM_SCENE_ACTIVE_NUM(i));
    shutter_config.scenes[i].active = (byte0 & SCENE_ACTIVE_MASK) != 0;
    shutter_config.scenes[i].num = byte0 & SCENE_NUM_MASK;
    shutter_config.scenes[i].pos = knx->paramByte(PARAM_SCENE_POS(i));
  }

  // --- Log configuration ---
  LOG_INF("Shutter Config: BlindType=%d, TimeOpen=%u, TimeClose=%u",
          shutter_config.blind_type, shutter_config.time_open_sec,
          shutter_config.time_close_sec);
  LOG_INF("  EnablePos=%d, EnableStatus=%d, EnableScene=%d",
          shutter_config.enable_position, shutter_config.enable_status,
          shutter_config.enable_scene);

  if (shutter_config.enable_scene) {
    for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
      if (shutter_config.scenes[i].active) {
        LOG_INF("  Scene %c: Num=%u, Pos=%u%%", 'A' + i,
                shutter_config.scenes[i].num, shutter_config.scenes[i].pos);
      }
    }
  }
}

/**
 * @brief Process incoming scene command (DPT 18.001)
 * @details Scene positions are pre-configured in ETS. On recall, we look up
 *          the matching scene and call set_position() with the configured %.
 *          Store commands are ignored (ETS manages scene positions).
 * @param raw_value Raw byte from GO6 SCENE
 */
static void process_scene_command(uint8_t raw_value) {
  if (DPT18_IS_STORE(raw_value)) {
    LOG_DBG("Scene STORE ignored (ETS-managed)");
    return;
  }

  uint8_t scene_num = DPT18_SCENE_NUM(raw_value);
  LOG_INF("Scene RECALL: num=%u", scene_num);

  // Search through configured scenes for matching number
  for (uint8_t i = 0; i < KNX_MAX_SCENES; i++) {
    if (!shutter_config.scenes[i].active) continue;
    if (shutter_config.scenes[i].num != scene_num) continue;

    uint8_t target_pos = shutter_config.scenes[i].pos;
    LOG_INF("  Scene %c: set_position(%u%%)", 'A' + i, target_pos);
    app_knx_shutter_set_position(target_pos);
    return;
  }

  LOG_DBG("Scene num=%u not configured, ignoring", scene_num);
}

// ============================================================================
// 5. WORK HANDLER (Polling & Sync)
// ============================================================================

static void knx_work_handler(struct k_work* work) {
  if (!knx_platform || !knx) return;

  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  knx->loop();

  if (knx->configured()) {
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
    if (shutter_config.enable_position) {
      if (knx->getGroupObject(GO_SH_SAPBP).commFlag() == ComFlag::Updated) {
        uint8_t pct = (uint8_t)knx->getGroupObject(GO_SH_SAPBP).value();
        knx->getGroupObject(GO_SH_SAPBP).commFlag(ComFlag::Ok);
        LOG_INF("KNX -> Position: %u%%", pct);
        app_knx_shutter_set_position(pct);
      }
    }

    // --- GO6: SCENE (DPT 18.001) ---
    if (shutter_config.enable_scene) {
      if (knx->getGroupObject(GO_SH_SCENE).commFlag() == ComFlag::Updated) {
        uint8_t scene_val = (uint8_t)knx->getGroupObject(GO_SH_SCENE).value();
        knx->getGroupObject(GO_SH_SCENE).commFlag(ComFlag::Ok);
        process_scene_command(scene_val);
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

// ============================================================================
// 6. PUBLIC API
// ============================================================================

extern "C" {

int knx_adapter_init(void) {
  LOG_INF("Initializing KNX Shutter Adapter...");

  k_work_init_delayable(&knx_work, knx_work_handler);
  k_work_init_delayable(&knx_prog_led_blink_work,
                        knx_prog_led_blink_work_handler);

  knx = new KnxFacade<NordicPlatform, Bau07B0>();
  if (!knx) return -ENOMEM;

  knx_platform = &knx->platform();

  // =========================================================================
  // BOOT ORDER (KNX Standard Requirement):
  // Load ALL config & sequence numbers from NVS FIRST, THEN enable UART.
  // Enabling UART before readMemory() risks processing incoming KNX frames
  // with seq=0 → security failure / replay protection reject.
  // =========================================================================

  // --- Provisioning: Read FDSK + Serial from flash partition ---
  knx_provision_data_t prov;
  if (knx_provision_read(&prov)) {
    LOG_INF("Provisioning data FOUND");
    knx->bauNumber(knx_provision_get_bau_number(&prov));
    SecurityInterfaceObject::setFDSK(prov.fdsk);
  } else {
    LOG_WRN("No provisioning data — using hardcoded defaults (dev mode)");
    knx->bauNumber(KNX_BAU_NUMBER_DEFAULT);
    // FDSK uses hardcoded fallback in SecurityInterfaceObject
    // (security_interface_object.cpp)
  }

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
    LOG_INF("Device configured. Loading Shutter Parameters...");
    load_shutter_params();

    // Setup DPTs for Group Objects
    knx->getGroupObject(GO_SH_MUD).dataPointType(DPT_UpDown);
    knx->getGroupObject(GO_SH_STOP).dataPointType(DPT_Trigger);
    if (shutter_config.enable_position) {
      knx->getGroupObject(GO_SH_SAPBP).dataPointType(DPT_Scaling);
    }
    if (shutter_config.enable_status) {
      knx->getGroupObject(GO_SH_IMUD).dataPointType(DPT_UpDown);
      knx->getGroupObject(GO_SH_CAPBP).dataPointType(DPT_Scaling);
    }
    if (shutter_config.enable_scene) {
      knx->getGroupObject(GO_SH_SCENE).dataPointType(DPT_SceneControl);
    }
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
  } else {
    k_work_cancel_delayable(&knx_prog_led_blink_work);
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
  } else {
    k_work_cancel_delayable(&knx_prog_led_blink_work);
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
  LOG_INF("BlindType: %d", shutter_config.blind_type);
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
  if (!knx || !initialized || !shutter_config.enable_status) return;

  k_mutex_lock(&knx_stack_mutex, K_FOREVER);
  if (knx->configured()) {
    knx->getGroupObject(GO_SH_IMUD).value(going_down);
    LOG_INF("Direction -> KNX: %s", going_down ? "DOWN" : "UP");
  }
  k_mutex_unlock(&knx_stack_mutex);
}

void knx_send_position_status(uint8_t percent) {
  if (!knx || !initialized || !shutter_config.enable_status) return;

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
