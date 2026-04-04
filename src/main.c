/**
 * @file main.c
 * @brief Bluetooth Mesh Relay Node - Main Application Entry Point
 *
 * @details
 * Main application file for Bluetooth Mesh Relay Node. Responsibilities:
 * - Hardware initialization (LEDs, buttons)
 * - Bluetooth and Mesh stack initialization
 * - Button event handling
 * - LED state management and attention blink
 *
 * Key features:
 * - Relay messages between nodes in mesh network
 * - Generic OnOff Server/Client models for LED control
 * - Fast Provision support (Quick Provision)
 * - Health model for fault reporting via attention LED
 *
 * @note
 * - Mesh logic (models, handlers) is in mesh_message_handler.c
 * - Network info (keys, addresses) is in network.c
 * - Fast Provision logic is in fast_provision.c
 */
/* ============================================================================
 * INCLUDES
 * ============================================================================
 */

/* Zephyr RTOS headers */
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

/* Bluetooth Mesh headers */
#include <bluetooth/mesh/models.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>

/* Nordic DK headers */
#include <dk_buttons_and_leds.h>

/* Project headers */
#include "../include/app.h"
#include "../include/app_device.h"
#include "../include/button.h"
#include "../include/curtain.h"
#include "../include/default_network.h"
#include "../include/execution_scene.h"
#include "../include/fact.h"
#include "../include/fast_provision.h"
#include "../include/knx_adapter.h"
#include "../include/led.h"
#include "../include/led_ev.h"
#include "../include/mesh_node.h"
#include "../include/net_message.h"
#include "../include/normal_provision.h"
#include "../include/pre_update.h"
#include "../include/relay.h"
#include "../include/sw_binding.h"
#include "../include/watchdog_process.h"

LOG_MODULE_REGISTER(lumi_debug_main_c, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================
 */

/**
 * @brief Flag to enable/disable attention blink
 *
 * @details
 * - true: Enable attention blink
 * - false: Disable attention blink
 *
 * Set by Health model callbacks in mesh_message_handler.c
 */
bool attention;

/**
 * @brief Current LED state
 *
 * @details
 * - true: LED ON
 * - false: LED OFF
 *
 * Synchronized with LED hardware and OnOff model state
 */
bool is_led_on;

#if EN_DEBUG_NETWORK_INFO
static struct k_work_delayable main_periodic_work;
#endif

/* Global MAC Address Table (ref in fact.c) */
uint8_t tbl_mac[6] = {0};

/* ============================================================================
 * STATIC FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Log TX Power configuration
 *
 * @details
 * Print TX power configured in prj.conf
 */
static void log_tx_power(void) {
#if defined(CONFIG_BT_CTLR_TX_PWR_PLUS_8)
  LOG_INF("TX Power: +8 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_7)
  LOG_INF("TX Power: +7 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_6)
  LOG_INF("TX Power: +6 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_5)
  LOG_INF("TX Power: +5 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_4)
  LOG_INF("TX Power: +4 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_3)
  LOG_INF("TX Power: +3 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_2)
  LOG_INF("TX Power: +2 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_PLUS_1)
  LOG_INF("TX Power: +1 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_0)
  LOG_INF("TX Power: 0 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_4)
  LOG_INF("TX Power: -4 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_8)
  LOG_INF("TX Power: -8 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_12)
  LOG_INF("TX Power: -12 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_16)
  LOG_INF("TX Power: -16 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_20)
  LOG_INF("TX Power: -20 dBm");
#elif defined(CONFIG_BT_CTLR_TX_PWR_MINUS_40)
  LOG_INF("TX Power: -40 dBm");
#else
  LOG_INF("TX Power: Default (not specified)");
#endif
}

/* ============================================================================
 * INITIALIZATION FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Initialize hardware (LEDs and buttons)
 *
 * @return 0 on success, error code on failure
 *
 * @details
 * Initialize necessary hardware:
 * - LEDs: For status indication and attention blink
 * - Buttons: To receive user input
 *
 * @note
 * If initialization fails, the function will log error and return error code.
 * Application will continue running but some features might not work.
 */
static int initialize_hardware(void) {
  /* Initialize MAC Address from Hardware Info */
  hwinfo_get_device_id(tbl_mac, sizeof(tbl_mac));
  LOG_HEXDUMP_INF(tbl_mac, sizeof(tbl_mac), "Device MAC Address:");

  LOG_INF("Hardware initialized successfully");
  return 0;
}

/**
 * @brief Initialize Mesh stack
 *
 * @return 0 on success, error code on failure
 *
 * @details
 * Initialize entire Mesh stack including:
 * - Mesh models (OnOff, Health, Config, Fast Provision)
 * - Mesh composition
 * - Message handlers
 * - Config Client and Fast Provision context
 *
 * @note
 * All mesh initialization logic is handled in mesh_message_handler.c.
 * This function is just a wrapper to call mesh_initialize().
 */
static int initialize_mesh_stack(void) { return mesh_initialize(); }

/**
 * @brief Callback when Bluetooth stack is ready
 *
 * @param err Error code (0 = success)
 *
 * @details
 * This function is called by Bluetooth stack when initialization is complete.
 * Initialization order:
 * 1. Hardware (LEDs, buttons)
 * 2. Mesh stack (models, composition, handlers)
 * 3. Load settings and restore AppKey binding (if any)
 * 4. Initialize Fast Provision
 *
 * @note Side effects:
 * - Initialize hardware (LEDs, buttons)
 * - Initialize Mesh stack
 * - Load persistent settings from flash
 * - Initialize Fast Provision state machine
 *
 * @note
 * If error occurs at any step, function will log error and return.
 * Application will continue running but some features might not work.
 */
static void bt_ready(int err) {
  if (err) {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return;
  }

  LOG_INF("Bluetooth initialized");

  /* Log TX Power configuration */
  log_tx_power();

  err = initialize_mesh_stack();
  if (err) {
    LOG_ERR("Mesh stack initialization failed");
    return;
  }

  /* Step 3: Restore AppKey binding from settings */
  if (IS_ENABLED(CONFIG_SETTINGS)) {
    restore_appkey_binding_after_settings_load();
    /* Wait a bit to ensure settings have been processed by mesh stack */
    /* (restore_appkey_binding_after_settings_load() already has 1000ms delay
     * inside) */
    k_sleep(K_MSEC(500));

    /* Save MAC to settings after settings are ready */
    fast_provision_save_mac_after_settings_ready();
  }
  fast_provision_init();
  normal_provision_init();
  default_network_monitor_init();

  err = initialize_hardware();
  if (err) {
    LOG_ERR("Hardware initialization failed");
    return;
  }

  /* Init curtain */
  curtain_init();

  /* Initialize KNX adapter */
  knx_adapter_init();

  /* Initialize relay module */
  err = relay_init();
  if (err) {
    LOG_ERR("Relay initialization failed (err %d)", err);
  }

  /* Init button */
  button_init(button_handle_btn_event);

  /* Initialize LED library */
  err = led_init();
  led_refresh_callback_init(app_handle_refresh_led);
  if (err) {
    LOG_ERR("Initializing LED library failed (err %d)", err);
    return;
  }
  led_ev_handle(LED_POWER_ON, CONFIG_LED_MASK_BLUETOOTH | CONFIG_LED_MASK_KNX);
  led_refresh(0xFFFF);

  /* Init ecxecution scene */
  execution_scene_init();

  /* Init net message */
  net_msg_init();

  /* Pre-Update */
  pre_update_init();

  /* Binding */
  binding_init();

  /* App initial */
  app_init();

  // Delay power on
  k_sleep(K_MSEC(500));

#if EN_DEBUG_NETWORK_INFO
  /* Start periodic tasks */
  k_work_reschedule(&main_periodic_work, K_MSEC(1000));
#endif
}

#if EN_DEBUG_NETWORK_INFO
/*
 * @brief Main periodic handler
 */
static void main_periodic_handler(struct k_work *work) {
  k_work_reschedule(&main_periodic_work, K_MSEC(1000)); // 1s interval
  default_network_print_info();                         // Optional
}
#endif

/* ============================================================================
 * MAIN FUNCTION
 * ============================================================================
 */

/**
 * @brief Main entry point
 *
 * @return 0 on success
 *
 * @details
 * Application entry point:
 * 1. Initialize Watchdog
 * 2. Initialize NVS early
 * 3. Enable Bluetooth stack (triggers bt_ready callback)
 * 4. Init attention blink work
 * 5. Init main periodic work
 *
 * @note
 * Non-blocking. After return, runs in Zephyr main thread/idle loop.
 */

/**
 * @brief Log reset/reboot reason at startup
 */
static void log_reset_reason(void) {
  uint32_t cause = 0;
  int rc = hwinfo_get_reset_cause(&cause);
  if (rc != 0) {
    LOG_WRN("Could not read reset cause (err %d)", rc);
    return;
  }

  LOG_INF("========================================");
  LOG_INF("  RESET CAUSE: 0x%08X", cause);

  if (cause == 0) {
    LOG_INF("  -> Power-On Reset (cold boot)");
  } else {
    if (cause & RESET_PIN)
      LOG_INF("  -> RESET PIN");
    if (cause & RESET_SOFTWARE)
      LOG_INF("  -> SOFTWARE RESET (sys_reboot)");
    if (cause & RESET_BROWNOUT)
      LOG_INF("  -> BROWNOUT (low voltage)");
    if (cause & RESET_POR)
      LOG_INF("  -> POWER-ON RESET");
    if (cause & RESET_WATCHDOG)
      LOG_INF("  -> WATCHDOG TIMEOUT !!!");
    if (cause & RESET_DEBUG)
      LOG_INF("  -> DEBUG (debugger reset)");
    if (cause & RESET_SECURITY)
      LOG_INF("  -> SECURITY VIOLATION");
    if (cause & RESET_LOW_POWER_WAKE)
      LOG_INF("  -> LOW POWER WAKE");
    if (cause & RESET_CPU_LOCKUP)
      LOG_INF("  -> CPU LOCKUP (HardFault) !!!");
    if (cause & RESET_PARITY)
      LOG_INF("  -> PARITY ERROR");
    if (cause & RESET_HARDWARE)
      LOG_INF("  -> HARDWARE");
    if (cause & RESET_USER)
      LOG_INF("  -> USER RESET");
    if (cause & RESET_TEMPERATURE)
      LOG_INF("  -> TEMPERATURE");
  }

  LOG_INF("========================================");

  /* Clear the cause so next boot shows fresh info */
  hwinfo_clear_reset_cause();
}

int main(void) {
  int err;

  LOG_INF("=== Bluetooth Mesh Relay Node Starting ===");

  /* Log reset reason FIRST */
  // log_reset_reason();

  /* Initialize watchdog early to protect system */
  err = watchdog_process_init();
  if (err) {
    LOG_ERR("Watchdog init failed (err %d)", err);
    /* Continue even if watchdog init fails */
  }

  /* Initialize NVS early to ensure MAC address storage availability */
  fast_provision_nvs_init_early();

  /* Enable Bluetooth stack - bt_ready callback will be called when ready */
  err = bt_enable(bt_ready);
  if (err) {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return err;
  }

  LOG_INF("Main initialization complete, waiting for Bluetooth ready...");

#if EN_DEBUG_NETWORK_INFO
  /* Initialize main periodic work */
  k_work_init_delayable(&main_periodic_work, main_periodic_handler);
#endif

  return 0;
}
