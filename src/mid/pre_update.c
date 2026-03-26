/*
 * pre_update.c
 *
 *  Ported from Telink SDK
 */

#include "../../include/pre_update.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "../../include/app_device.h"
#include "../../include/fast_provision.h"  // For STATE_DEV_PROVED
#include "../../include/net_message.h"
#include "../../include/network.h"  // For get_provision_state, network_mode_actived
#include "../../include/timestamp.h"

LOG_MODULE_REGISTER(pre_update, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/

/**
 * @brief Helper function to check if the time interval has been exceeded.
 */
static bool clock_time_exceed_s(uint32_t last_s, uint32_t interval_s) {
  uint32_t now_s = k_uptime_get() / 1000;
  return ((now_s - last_s) >= interval_s);
}

static uint32_t clock_time_s(void) { return k_uptime_get() / 1000; }

static update_status_periodically_t update_status[NUMBER_INPUT];
static bool is_initialized = false;

/**
 * @brief Reset the pre-update time after join.
 */
void pre_update_reset_time_after_join(void) {
  for (int i = 0; i < NUMBER_INPUT; i++) {
    update_status[i].last_t_s = clock_time_s();
    uint32_t rand_val = sys_rand32_get();
    if (get_provision_state() == STATE_DEV_PROVED) {
      update_status[i].interval_t_s =
          MESH_PERIODIC_PUBLISH_TIME_PW_ON +
          (rand_val % MESH_PERIODIC_PUBLISH_RANDOM_TIME_PW_ON);
    }
    LOG_INF("---Pre-update channel %d: interval %u s after join", i,
            update_status[i].interval_t_s);
  }
  is_initialized = true;
}

/**
 * @brief Initialize the pre-update module.
 */
void pre_update_init(void) {
  for (int i = 0; i < NUMBER_INPUT; i++) {
    update_status[i].last_t_s = clock_time_s();
    uint32_t rand_val = sys_rand32_get();
    if (get_provision_state() == STATE_DEV_PROVED) {
      update_status[i].interval_t_s =
          MESH_PERIODIC_PUBLISH_TIME_PW_ON +
          (rand_val % MESH_PERIODIC_PUBLISH_RANDOM_TIME_PW_ON);
    } else {
      /* After provision: 1500 + rand % 600 */
      update_status[i].interval_t_s =
          MESH_PERIODIC_PUBLISH_TIME +
          (rand_val % MESH_PERIODIC_PUBLISH_RANDOM_TIME);
    }
    LOG_INF("Pre-update channel %d: interval %u s", i,
            update_status[i].interval_t_s);
  }
  is_initialized = true;
}

/**
 * @brief Process the pre-update module.
 */
void pre_update_proc(void) {
  if (!is_initialized) {
    return;
  }
  for (int i = 0; i < NUMBER_INPUT; i++) {
    if (clock_time_exceed_s(update_status[i].last_t_s,
                            update_status[i].interval_t_s)) {
      /* Add 50ms delay to prevent immediate congestion across multiple reports
       */
      net_message_publish_status_delay(i, 50);

      LOG_INF("pre_update_proc: update channel %d: interval %u s", i,
              update_status[i].interval_t_s);

      update_status[i].last_t_s = clock_time_s();
      uint32_t rand_val = sys_rand32_get();
      update_status[i].interval_t_s =
          MESH_PERIODIC_PUBLISH_TIME +
          (rand_val % MESH_PERIODIC_PUBLISH_RANDOM_TIME);
      if (i == 0) {
        if (timestamp_device_is_have_time() == false) {
          timestamp_send_query_time(network_get_unicast_address());
        }
      }
    }
  }
}