/**
 * @file knx_adapter.h
 * @brief KNX Adapter Public API for Shutter Actuator
 */

#ifndef KNX_ADAPTER_H__
#define KNX_ADAPTER_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include "knx_mapping_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ZBUS Message for App <-> Adapter communication
// ============================================================================
struct knx_byte_msg {
  uint8_t value;
  uint64_t timestamp;
};
ZBUS_CHAN_DECLARE(chan_knx_rx);
ZBUS_CHAN_DECLARE(chan_knx_tx);

struct zbus_observer;
extern const struct zbus_observer knx_rx_listener;

// ============================================================================
// KNX Adapter Lifecycle
// ============================================================================

/** @brief Initialize KNX Adapter (UART, Stack, Work Queue) */
int knx_adapter_init(void);

/** @brief Check if adapter is initialized */
bool knx_adapter_is_initialized(void);

/** @brief Check if ETS has been configured */
bool knx_is_configured(void);

/** @brief Report current curtain position to KNX bus (0-255) */
void knx_adapter_report_curtain_pos(uint8_t raw_val);

// ============================================================================
// KNX Factory Test API
// ============================================================================

/** @brief Start the KNX bus communication test */
void knx_fact_start_test(void);

/** @brief Process the test state machine (call from work loop) */
void knx_fact_process(void);

/** @brief Get the final test result (true = PASS, false = FAIL) */
bool knx_fact_get_result(void);

/** @brief Send a raw test telegram bypass mapping logic */
bool knx_adapter_send_test_telegram(uint16_t dest_ga, uint8_t* payload,
                                    uint8_t payload_len);

/** @brief Check if TPUART chip is physically connected */
bool knx_adapter_is_tpuart_connected(void);

/** @brief Get the number of TX frames that have been processed (confirmed) by TPUART */
uint32_t knx_adapter_get_tx_processed_count(void);

/** @brief Get the number of TX frames successfully confirmed (0x8B) by TPUART */
uint32_t knx_adapter_get_tx_success_count(void);

/** @brief Enable/Disable strict factory test mode in the stack */
void knx_adapter_set_factory_test_mode(bool enable);

/** @brief Get the number of strictly validated echo frames */
uint32_t knx_adapter_get_echo_count(void);

/** @brief Get the number of invalid frames (checksum/parity error) */
uint32_t knx_adapter_get_invalid_count(void);

/** @brief Get the number of unknown control bytes (garbage on bus) */
uint32_t knx_adapter_get_unknown_count(void);

// ============================================================================
// Programming Mode
// ============================================================================

/** @brief Toggle Programming Mode, returns new state */
bool knx_toggle_prog_mode(void);

/** @brief Set Programming Mode */
void knx_set_prog_mode(bool enable);

/** @brief Get Programming Mode state */
bool knx_get_prog_mode(void);

// ============================================================================
// Device Info & Reset
// ============================================================================

/** @brief Log device info to console */
void knx_log_device_info(void);

/** @brief Wipe all KNX Configuration (Full Factory Reset) */
void knx_wipe_config(void);

// ============================================================================
// Shutter Status Feedback (Firmware rèm → KNX Bus)
// ============================================================================

/**
 * @brief Send movement direction feedback to KNX bus (GO4 - IMUD)
 * @param going_down true = closing/down, false = opening/up
 */
void knx_send_direction_feedback(bool going_down);

/**
 * @brief Send current position status to KNX bus (GO5 - CAPBP)
 * @param percent Current position 0-100% (0=fully open, 100=fully closed)
 */
void knx_send_position_status(uint8_t percent);

// ============================================================================
// Configuration Access
// ============================================================================

/**
 * @brief Get shutter configuration loaded from ETS parameters
 * @return Pointer to read-only config struct, NULL if not configured
 */
const knx_shutter_config_t* knx_get_shutter_config(void);

#ifdef __cplusplus
}
#endif

#endif /* KNX_ADAPTER_H__ */
