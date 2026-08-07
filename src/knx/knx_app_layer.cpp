/**
 * @file knx_app_layer.cpp
 * @brief KNX Application Layer for Actuator - includes Factory Test logic
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "../../include/knx_adapter.h"

LOG_MODULE_REGISTER(knx_app, CONFIG_LOG_DEFAULT_LEVEL);

// ============================================================================
// KNX Factory Test
// ============================================================================

#define KNX_FACT_TEST_COUNT       5    // Number of test telegrams
#define KNX_FACT_PASS_THRESHOLD   3    // Minimum success count to PASS
#define KNX_FACT_CONFIRM_TIMEOUT  1500 // ms to wait for L_DATA_CON
#define KNX_FACT_DELAY_MIN_MS     1000 // Minimum random delay
#define KNX_FACT_DELAY_MAX_MS     2000 // Maximum random delay

typedef enum {
  KNX_FACT_IDLE,
  KNX_FACT_WAIT_DELAY,      // Waiting for random delay before sending
  KNX_FACT_WAIT_CONFIRM,    // Waiting for L_DATA_CON after sending
  KNX_FACT_DONE
} knx_fact_state_t;

static knx_fact_state_t s_state = KNX_FACT_IDLE;
static uint8_t  s_sent_count;
static uint8_t  s_success_count;
static uint32_t s_delay_end_time;
static uint32_t s_confirm_start_time;
static uint32_t s_tx_counter_before;         // General TX processed snapshot
static uint32_t s_tx_success_counter_before; // TX success snapshot (0x8B)
static uint32_t s_echo_counter_before; 
static uint32_t s_invalid_counter_before; // Invalid frame counter snapshot before send
static uint32_t s_unknown_counter_before; // Unknown control byte counter snapshot before send

/**
 * @brief Generate and send a random test telegram
 */
static bool knx_fact_send_test_telegram(uint8_t seq) {
  // Use a fixed Group Address for testing (31/7/255 = 0xFFFF)
  // this is the highest possible GA and safe for factory testing.
  const uint16_t test_ga = 0xFFFF;

  // Random payload: 2 bytes
  uint32_t rnd2 = sys_rand32_get();
  uint8_t payload[2];
  payload[0] = (uint8_t)(rnd2 & 0xFF);
  payload[1] = seq ^ (uint8_t)(rnd2 >> 8);

  LOG_INF("[KNX_FACT] TX #%d: GA=31/7/255 (0xFFFF) payload=[0x%02X,0x%02X]",
          seq + 1, payload[0], payload[1]);

  return knx_adapter_send_test_telegram(test_ga, payload, sizeof(payload));
}

/**
 * @brief Generate random delay between DELAY_MIN and DELAY_MAX
 */
static uint32_t knx_fact_random_delay(void) {
  uint32_t range = KNX_FACT_DELAY_MAX_MS - KNX_FACT_DELAY_MIN_MS;
  return KNX_FACT_DELAY_MIN_MS + (sys_rand32_get() % range);
}

/**
 * @brief Print final test result
 */
static void knx_fact_print_result(void) {
  LOG_INF("========================================");
  LOG_INF("[KNX_FACT] Factory Test COMPLETED");
  LOG_INF("[KNX_FACT] Result: %d/%d confirmed",
          s_success_count, KNX_FACT_TEST_COUNT);
  if (s_success_count >= KNX_FACT_PASS_THRESHOLD) {
    LOG_INF("[KNX_FACT] >>> PASS <<<");
  } else {
    LOG_ERR("[KNX_FACT] >>> FAIL <<<");
  }
  LOG_INF("========================================");
}

// ============================================================================
// Public API
// ============================================================================

extern "C" {

void knx_app_init(void) { LOG_INF("KNX App Layer Initialized"); }

void knx_fact_start_test(void) {
  if (s_state != KNX_FACT_IDLE && s_state != KNX_FACT_DONE) {
    LOG_WRN("[KNX_FACT] Test already running, ignoring start");
    return;
  }

  // Exit programming mode if currently active
  if (knx_get_prog_mode()) {
    knx_set_prog_mode(false);
    LOG_INF("[KNX_FACT] Exited KNX Programming Mode for test");
  }

  // Check if TPUART is connected to bus
  if (!knx_adapter_is_tpuart_connected()) {
    LOG_ERR("[KNX_FACT] TPUART NOT connected to KNX bus! Test will FAIL.");
  }

  s_sent_count = 0;
  s_success_count = 0;
  knx_adapter_set_factory_test_mode(true);
  s_state = KNX_FACT_WAIT_DELAY;

  // First telegram: short initial delay (500ms-2s)
  uint32_t initial_delay = 500 + (sys_rand32_get() % 1500);
  s_delay_end_time = k_uptime_get_32() + initial_delay;

  LOG_INF("========================================");
  LOG_INF("[KNX_FACT] Factory Test STARTED");
  LOG_INF("[KNX_FACT] TPUART connected: %s",
          knx_adapter_is_tpuart_connected() ? "YES" : "NO");
  LOG_INF("[KNX_FACT] Will send %d telegrams, pass threshold=%d/%d",
          KNX_FACT_TEST_COUNT, KNX_FACT_PASS_THRESHOLD, KNX_FACT_TEST_COUNT);
  LOG_INF("[KNX_FACT] Random delay: %d-%d ms per telegram",
          KNX_FACT_DELAY_MIN_MS, KNX_FACT_DELAY_MAX_MS);
  LOG_INF("========================================");
}

void knx_fact_process(void) {
  if (s_state == KNX_FACT_IDLE || s_state == KNX_FACT_DONE) {
    return;
  }

  uint32_t now = k_uptime_get_32();

  switch (s_state) {
    case KNX_FACT_WAIT_DELAY:
      if (now >= s_delay_end_time) {
        // Snapshot all counters BEFORE sending
        s_tx_counter_before = knx_adapter_get_tx_processed_count();
        s_tx_success_counter_before = knx_adapter_get_tx_success_count();
        s_echo_counter_before = knx_adapter_get_echo_count();
        s_invalid_counter_before = knx_adapter_get_invalid_count();
        s_unknown_counter_before = knx_adapter_get_unknown_count();

        // Send test telegram
        bool send_ok = knx_fact_send_test_telegram(s_sent_count);
        s_sent_count++;

        if (send_ok) {
          s_confirm_start_time = now;
          s_state = KNX_FACT_WAIT_CONFIRM;
        } else {
          LOG_WRN("[KNX_FACT] TX #%d: send FAILED (not connected/queue full)",
                  s_sent_count);
          if (s_sent_count >= KNX_FACT_TEST_COUNT) {
            s_state = KNX_FACT_DONE;
            knx_fact_print_result();
          } else {
            s_delay_end_time = now + knx_fact_random_delay();
          }
        }
      }
      break;

    case KNX_FACT_WAIT_CONFIRM: {
      // Check if TPUART has processed the frame by comparing counters.
      // getTxProcessedFrameCounter() increments when processTxFrameComplete()
      // is called (after L_DATA_CON received from TPUART chip).
      uint32_t tx_success_now = knx_adapter_get_tx_success_count();
      uint32_t echo_now = knx_adapter_get_echo_count();
      uint32_t invalid_now = knx_adapter_get_invalid_count();
      uint32_t unknown_now = knx_adapter_get_unknown_count();
      uint32_t elapsed = now - s_confirm_start_time;

      if ((tx_success_now > s_tx_success_counter_before) && (echo_now > s_echo_counter_before)) {
        // BOTH TPUART confirmation (0x8B Success) AND physical bus Echo received
        // Now check if the bus was "clean" during this transmission
        if (invalid_now == s_invalid_counter_before && unknown_now == s_unknown_counter_before) {
          s_success_count++;
          LOG_INF("[KNX_FACT] TX #%d: VERIFIED (LDataSuccess + Echo) in %dms (total succ=%d/%d)",
                  s_sent_count, elapsed, s_success_count, s_sent_count);
        } else {
          LOG_ERR("[KNX_FACT] TX #%d: FAILED - DIRTY BUS! (Garbage detected: invalid=%d, unknown=%d)",
                  s_sent_count, invalid_now - s_invalid_counter_before, unknown_now - s_unknown_counter_before);
        }

        if (s_sent_count >= KNX_FACT_TEST_COUNT) {
          s_state = KNX_FACT_DONE;
          knx_adapter_set_factory_test_mode(false);
          knx_fact_print_result();
        } else {
          s_delay_end_time = now + knx_fact_random_delay();
          s_state = KNX_FACT_WAIT_DELAY;
        }
      } else if (elapsed >= KNX_FACT_CONFIRM_TIMEOUT) {
        // Timeout: Either L_DATA_CON Success (0x8B) or Echo was missing
        if (tx_success_now > s_tx_success_counter_before) {
          LOG_ERR("[KNX_FACT] TX #%d: FAILED - L_DATA_CON Success OK but NO ECHO!", s_sent_count);
        } else {
          // Identify if it's 0x0B (Failure) vs pure Timeout
          uint32_t tx_proc_now = knx_adapter_get_tx_processed_count();
          if (tx_proc_now > s_tx_counter_before) {
            LOG_ERR("[KNX_FACT] TX #%d: FAILED - L_DATA_CON Failure (0x0B) received (Bus power issue?)", s_sent_count);
          } else {
            LOG_ERR("[KNX_FACT] TX #%d: FAILED - TIMEOUT (No response from TPUART)", s_sent_count);
          }
        }

        if (s_sent_count >= KNX_FACT_TEST_COUNT) {
          s_state = KNX_FACT_DONE;
          knx_adapter_set_factory_test_mode(false);
          knx_fact_print_result();
        } else {
          s_delay_end_time = now + knx_fact_random_delay();
          s_state = KNX_FACT_WAIT_DELAY;
        }
      }
      // else: still waiting, do nothing
      break;
    }

    default:
      break;
  }
}

bool knx_fact_get_result(void) {
  if (s_state != KNX_FACT_DONE) {
    return false;
  }
  return (s_success_count >= KNX_FACT_PASS_THRESHOLD);
}

}  // extern "C"
