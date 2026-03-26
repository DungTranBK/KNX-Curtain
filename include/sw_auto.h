/*
 * sw_auto.h
 *
 * Auto ON/OFF timer module — ported from Telink to Nordic nRF Connect SDK.
 *
 * Implements timer-based automatic relay control:
 *   TOGGLE_AUTO_ON:  after being OFF for delay_time_s seconds → auto turn ON
 *   TOGGLE_AUTO_OFF: after being ON  for delay_time_s seconds → auto turn OFF
 *
 * Telink API mapping:
 *   clock_time_ms()         → utilities.h  (Zephyr k_uptime_get)
 *   clock_time_exceed_ms()  → utilities.h
 *   s_to_ms()               → s_to_ms() macro in utilities.h
 *   p_trans->present        → relay_get_target_state_by_index()
 *   G_ON / G_OFF            → RELAY_STATE_ON / RELAY_STATE_OFF
 *   pv_handle_control_device(idx, st, false) → callback(idx, st, false)
 *
 * Original Author: DungTranBK
 * Nordic Port:     DungTranBK
 */

#ifndef SW_AUTO_H_
#define SW_AUTO_H_

#include <stdbool.h>
#include <stdint.h>

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

/**
 * @brief Callback invoked when auto-timer fires to control a relay.
 *
 * @param model_idx  Element/relay index (0-based).
 * @param state      Target state: RELAY_STATE_ON or RELAY_STATE_OFF.
 * @param from_mesh  false for local auto-control (same as Telink's 'false').
 */
typedef void (*type_control_dev_callback_func)(uint16_t model_idx, bool state,
                                               bool from_mesh);

/******************************************************************************/
/*                             EXPORT FUNCTIONS                               */
/******************************************************************************/

/**
 * @brief Register callback to be invoked when auto-timer fires.
 * @param callbackFunc Callback function pointer (NULL-safe).
 */
void auto_callback_init(type_control_dev_callback_func callbackFunc);

/**
 * @brief (Re)start the auto-timer for element @p model_idx.
 *        Call whenever the relay state changes.
 * @param model_idx Element index (0-based, < ELE_CNT).
 * @param st        New relay state (RELAY_STATE_ON or RELAY_STATE_OFF).
 */
void auto_reset_time_trans(uint16_t model_idx, uint8_t st);

/**
 * @brief Periodic task — must be called from main super-loop (every ~10ms).
 *        Checks timers and fires callback when auto-on/off delay expires.
 */
void auto_proc(void);

#endif /* SW_AUTO_H_ */
