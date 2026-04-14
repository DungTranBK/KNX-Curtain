/*
 * relay.h
 *
 * Relay control module for latching relays (ported from Telink).
 * Supports: 2-wire power, latching relay only.
 * Uses k_timer for precise pulse timing (10ms).
 *
 * Author: DungTranBK
 */

#ifndef RELAY_H_
#define RELAY_H_

#include <stdbool.h>
#include <stdint.h>

#include "./utilities.h"

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

#define RELAY_COUNT 4

/**
 * @brief Relay module status
 */
enum {
  RELAY_IDLE = 0,
  RELAY_BUSY = 1,
};

/**
 * @brief Relay control completion status
 */
enum {
  CONTROL_IN_PROCESSING = 0,
  CONTROL_SUCCESS = 1,
};

/**
 * @enum relay_state_t
 * @brief Relay states (matches Telink G_ON/G_OFF).
 */
typedef enum {
  RELAY_STATE_OFF = 0,
  RELAY_STATE_ON = 1,
} relay_state_t;

/**
 * @brief No relay channel is active
 */
#define NO_RL_CHN_ACTIVE 0xFF

/**
 * @brief Pulse duration for latching relay in milliseconds.
 */
#define RELAY_PULSE_DURATION_MS 20

/**
 * @brief Minimum interval between consecutive relay controls (ms).
 *        Prevents overloading the power supply for 2-wire (one-wire) power.
 *        Matches Telink CONTROL_RL_INTERVAL_MS = 500ms.
 */
#define RELAY_CONTROL_INTERVAL_MS 50
#define RELAY_CONTROL_INTERVAL_MS_IN_FACT 500

/**
 * @brief Delay before storing relay state to flash (ms).
 *        Matches Telink STORE_RELAY_STATE_TIME_LEN_MS = 3000ms.
 */
#define RELAY_STORE_DELAY_MS 3000

/**
 * @brief Timeout for relay module busy state (ms).
 *        If relay stays BUSY for longer, system reboots.
 *        Matches Telink MODULE_BUSY_TIME_OUT_MS = 10000ms.
 */
#define RELAY_BUSY_TIMEOUT_MS 10000

/**
 * @brief Bitmask for valid relay channels.
 */
#define RELAY_BACKUP_MASK ((1 << RELAY_COUNT) - 1)

/**
 * @brief Callback type for relay state change notification.
 * @param idx Relay index (0 to RELAY_COUNT-1).
 * @param state New state (RELAY_STATE_ON or RELAY_STATE_OFF).
 */
typedef void (*relay_state_change_cb_t)(uint8_t idx, uint8_t state);

typedef void (*relay_handle_update_control_message_params)(int idx,
                                                           uint16_t src,
                                                           uint16_t dst,
                                                           uint16_t opcode);

/******************************************************************************/
/*                             EXPORT FUNCTIONS                               */
/******************************************************************************/

/**
 * @brief Register a callback for relay state changes.
 * @param cb Callback function.
 */
void relay_callback_init(relay_state_change_cb_t cb,
                         relay_handle_update_control_message_params cb_1);

/**
 * @brief Initialize the relay module (GPIO, timers, restore state).
 * @return 0 on success, negative error code on failure.
 */
int relay_init(void);

/**
 * @brief Main relay processing loop. Must be called periodically (e.g. from
 * main loop). Processes the state machine: compares target vs present bitmask,
 *        drives relays sequentially, handles pulse timing.
 * @return RELAY_IDLE or RELAY_BUSY.
 */
uint8_t relay_proc(void);

/**
 * @brief Set the target state of a specific relay.
 *        The actual relay control happens in relay_proc().
 * @param idx Relay index (0 to RELAY_COUNT-1).
 * @param state Target state (RELAY_STATE_ON or RELAY_STATE_OFF).
 * @param store If true, schedule storing state to flash.
 */
void relay_set_target_state(uint8_t idx, uint8_t state, src_control_enum src,
                            uint16_t dst_addr, bool store);

/**
 * @brief Get the current target state bitmask for all relays.
 * @return Bitmask of target states.
 */
uint16_t relay_get_target_state(void);

/**
 * @brief Get the target state for a specific relay.
 * @param idx Relay index (0 to RELAY_COUNT-1).
 * @return RELAY_STATE_ON or RELAY_STATE_OFF.
 */
uint8_t relay_get_target_state_by_index(uint8_t idx);

/**
 * @brief Get the current present state bitmask for all relays.
 * @return Bitmask of present (actual) states.
 */
uint16_t relay_get_present_state(void);

/**
 * @brief Restore target state from flash.
 *        Called during init or after factory reset.
 */
void relay_restore_target_state(void);

void relay_control_directly(uint8_t idx, uint8_t st);

#endif /* RELAY_H_ */
