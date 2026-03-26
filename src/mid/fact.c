/*
 * fact.c
 *
 *  Ported from Telink SDK for Nordic Zephyr
 */

#include "../../include/fact.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "../../include/app_device.h"
#include "../../include/network.h"
#include "../../include/utilities.h"
#include "../../include/vendor.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(fact, CONFIG_LOG_DEFAULT_LEVEL);

/* ============================================================================
 * DEFINITIONS
 * ============================================================================
 */

#define FACT_RETRY_TIME_CNT 2
#define FACT_TOTAL_TIME_LENGTH TIMER_70S
#define FACT_TEST_MESSAGE_CNT 5
#define FACT_PUBLISH_TIME_LEN (FACT_TOTAL_TIME_LENGTH / FACT_TEST_MESSAGE_CNT)

// Condition for PASS: Received >= (Total - 3) responses.
// Telink: (x >= (5 - 3)) ? 1 : 0 -> >= 2 responses (Echoed back)
#define FACT_IS_PASS(x) ((x >= (FACT_TEST_MESSAGE_CNT - 3)) ? 1 : 0)

/* ============================================================================
 * PRIVATE DATA
 * ============================================================================
 */

typedef struct {
  uint8_t enable;
  uint32_t st_time;
  uint16_t delay_time;
} fact_push_msg_delay_t;

static fact_push_msg_delay_t fact_push_msg_delay = {.enable = 0};
static fact_par_t fact_par = {.enable = 0, .done = 0};
static fact_test_rf_msg_t fact_rf_msg;

static struct k_work_delayable fact_work;

// External variable from network.c or main.c
extern uint8_t tbl_mac[6];

/* ============================================================================
 * PROTOTYPES
 * ============================================================================
 */
static void fact_process_work(struct k_work* work) {
  fact_main_function();
  if (fact_par.enable) {
    k_work_reschedule(&fact_work, K_MSEC(100));  // check interval
  }
}

/* ============================================================================
 * FUNCTIONS
 * ============================================================================
 */

void fact_init(void) { k_work_init_delayable(&fact_work, fact_process_work); }

uint8_t fact_is_activate(void) { return fact_par.enable; }

uint8_t fact_set(uint8_t act) {
  if (act) {
    fact_par.enable = 1;
    fact_par.done = 0;
    fact_par.ac_st_time = (uint32_t)k_uptime_get();
    fact_par.publish_cnt = 0;
    fact_par.response_cnt = 0;
    fact_par.pub_cnt_before = 0;
    fact_par.active_idx = 0xFF;  // MAX_U8

    // RF Power setting and TTL would go here if supported
    // rf_set_power_level_index(FACT_POWER_INDEX);
    LOG_INF("FACT Mode Enabled");

    // Schedule work immediately
    k_work_reschedule(&fact_work, K_NO_WAIT);
  } else {
    fact_par.enable = 0;
    // Restore RF Power and TTL if changed
    LOG_INF("FACT Mode Disabled");
  }
  return fact_par.enable;
}

int fact_handle_config_response(uint8_t* par, int par_len, uint16_t src_addr,
                                uint16_t dst_addr) {
  // Check if message is for this node
  uint16_t primary_addr = network_get_unicast_address();

  if (dst_addr == primary_addr) {
#ifdef FACT_DBG_EN
    LOG_INF("fact_handle_config_response");
#endif
    if (par_len < sizeof(fact_test_rf_msg_t) -
                      1) {  // op is already handled? check caller
      // par usually points to OP + DATA. Here structure is OP(1) + CNT(1) +
      // MAC(2) Adjust pointer based on how it's passed. Assuming par points to
      // Payload (OP included or not?) Typically vendor handler passes pointer
      // to OP.
    }

    // Mapping struct to pointer. par[0] is OP?
    // Telink: (fact_test_rf_msg_t*)&par[1] -> suggests par[0] was OP.
    // Let's assume par[0] is OP.

    fact_test_rf_msg_t* p_response = (fact_test_rf_msg_t*)par;

    // Verify if MAC matches local MAC (Echo check)
    // Note: fact_test_rf_msg_t has MAC[2] but we verify against tbl_mac[6]?
    // Telink code loops sizeof(p_response->MAC) which is 2.
    // So it only checks first 2 bytes? Or last 2?
    // "memcpy(fact_rf_msg.MAC, tbl_mac, sizeof(fact_rf_msg.MAC));" -> Copies
    // first 2 bytes of tbl_mac? Yes, array size is 2.

    bool match_flag = true;
    for (int i = 0; i < 2; i++) {
      if (p_response->MAC[i] != tbl_mac[i]) {
        match_flag = false;
        break;
      }
    }

    if (match_flag) {
      // Logic: verify count to avoid duplicate counting of same msg
      if (p_response->cnt == fact_par.pub_cnt_before) {
        if (fact_par.active_idx != p_response->cnt) {
          fact_par.response_cnt++;
          fact_par.active_idx = p_response->cnt;
          LOG_INF("Index match, pass condition. Cnt: %d",
                  fact_par.response_cnt);
        }
      }
    } else {
      LOG_WRN("MAC not match");
    }
  } else {
    return -1;
  }
  return 0;
}

void fact_update_result_to_mcu(void) {
  test_rf_result_t test_rf_result;

  if (FACT_IS_PASS(fact_par.response_cnt)) {
    test_rf_result.result = FACT_PASS;
    LOG_INF("FACT_PASS");
  } else {
    test_rf_result.result = FACT_FAIL;
    LOG_INF("FACT_FAIL");
  }
  test_rf_result.rssi_value = 0;

  // TODO
}

static void fact_push_message_delay(void) {
  if (fact_push_msg_delay.enable == 1) {
    if (clock_time_exceed_ms(fact_push_msg_delay.st_time,
                             fact_push_msg_delay.delay_time)) {
      fact_par.pub_cnt_before = fact_par.publish_cnt;

      fact_rf_msg.op = VD_FACT_TEST_RF;
      fact_rf_msg.cnt = fact_par.publish_cnt;
      // Copy only first 2 bytes of MAC
      memcpy(fact_rf_msg.MAC, tbl_mac, sizeof(fact_rf_msg.MAC));

      uint16_t ele_adr_primary = network_get_unicast_address();

      for (int i = 0; i < FACT_RETRY_TIME_CNT; i++) {
        mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (uint8_t*)&fact_rf_msg,
                        sizeof(fact_test_rf_msg_t), ele_adr_primary,
                        GATEWAY_UNICAST_ADDR, 0, 0);
      }

      LOG_INF("PUBLISH TEST MESSAGE: %d, Delay: %d", fact_par.publish_cnt,
              fact_push_msg_delay.delay_time);
      fact_push_msg_delay.enable = 0;
    }
  }
}

uint8_t fact_main_function(void) {
  if (fact_par.enable) {
    if (fact_par.done) {
      if (clock_time_exceed_ms(fact_par.ac_st_time, TIMER_5Min)) {
        fact_set(0);
      }
      return fact_par.enable;
    }

    // Push message delay
    if (!FACT_IS_PASS(fact_par.response_cnt)) {
      fact_push_message_delay();
    }

    // Timeout
    if (clock_time_exceed_ms(fact_par.ac_st_time, FACT_TOTAL_TIME_LENGTH)) {
      fact_par.done = 1;
      fact_update_result_to_mcu();

      LOG_INF("Fact Finished. Response: %d", fact_par.response_cnt);
    } else {
      uint32_t next_step_time = (fact_par.publish_cnt * FACT_PUBLISH_TIME_LEN);
      if (clock_time_exceed_ms(fact_par.ac_st_time, next_step_time)) {
        if (fact_par.publish_cnt <= FACT_TEST_MESSAGE_CNT) {
          // Publish test message
          fact_push_msg_delay.st_time = (uint32_t)k_uptime_get();

          // Random delay logic
          uint32_t rand_val = sys_rand32_get();
          fact_push_msg_delay.delay_time =
              rand_val % (FACT_PUBLISH_TIME_LEN - TIMER_500MS);

          fact_push_msg_delay.enable = 1;
        }
        fact_par.publish_cnt++;
        LOG_INF("*** STEP PUB ***");
      }
    }
  }
  return fact_par.enable;
}
