/*
 * timestamp.c
 *
 * Ported from Telink SDK to Nordic nRF Connect SDK
 */

#include "../../include/timestamp.h"

#include <time.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../../include/app_device.h"
#include "../../include/mesh_node.h"
#include "../../include/network.h"
#include "../../include/utilities.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(timestamp, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

#define PAR_LEN_SET_TIMESTAMP 4
#define TIMESTAMP_CHECK_INTERVAL_MS 1000

typedef struct {
  time_t current_time;
  time_t previous_time;
  uint32_t time_tick_ms;
  bool is_set_flag;
  uint32_t set_st_time_s;
} time_par_t;

static time_par_t time_par;

/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/

/**
 * @func   clock_time_exceed_s
 * @brief  Helper to check if time interval exceeded in seconds
 */
static bool clock_time_exceed_s(uint32_t last_s, uint32_t interval_s) {
  uint32_t now_s = k_uptime_get_32() / 1000;
  return (now_s - last_s) >= interval_s;
}

static uint32_t clock_time_s(void) { return k_uptime_get_32() / 1000; }

/**
 * @func   timestamp_get_second_in_day
 */
uint32_t timestamp_get_second_in_day(void) {
  if (!time_par.is_set_flag) {
    if (!clock_time_exceed_s(time_par.set_st_time_s,
                             SET_TIMESTAMP_MAX_INTERVAL_S)) {
      struct tm tm_st;
      gmtime_r(&time_par.current_time, &tm_st);
      return (uint32_t)((tm_st.tm_hour * 60 + tm_st.tm_min) * 60 +
                        tm_st.tm_sec);
    }
  }
  return INVALID_SEC;
}

/**
 * @func   timestamp_get_minute_in_day
 */
uint16_t timestamp_get_minute_in_day(void) {
  if (time_par.is_set_flag) {
    if (!clock_time_exceed_s(time_par.set_st_time_s,
                             SET_TIMESTAMP_MAX_INTERVAL_S)) {
      struct tm tm_st;
      gmtime_r(&time_par.current_time, &tm_st);
      return (uint16_t)(tm_st.tm_hour * 60 + tm_st.tm_min);
    }
  }
  return INVALID_MINUTE;
}

/**
 * @func   timestamp_get_week_day
 */
uint8_t timestamp_get_week_day(void) {
  if (time_par.is_set_flag) {
    if (!clock_time_exceed_s(time_par.set_st_time_s,
                             SET_TIMESTAMP_MAX_INTERVAL_S)) {
      struct tm tm_st;
      gmtime_r(&time_par.current_time, &tm_st);
      return (uint8_t)(tm_st.tm_wday);
    }
  }
  return INVALID_WEEK_DAY;
}

/**
 * @func   timestamp_device_is_have_time
 */
bool timestamp_device_is_have_time(void) { return time_par.is_set_flag; }

/**
 * @func   timestamp_send_query_time
 */
void timestamp_send_query_time(uint16_t src) {
  // Send response
  timestamp_query_t timestamp_query;
  timestamp_query.cmd = VD_QUERY_TIMESTAMP;
  timestamp_query.dst_adr = BT_MESH_ADDR_ALL_NODES;  // 0xFFFF

  /* Telink used GATEWAY_UNICAST_ADDR for query, matching here */
  mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (uint8_t*)&timestamp_query,
                  sizeof(timestamp_query), src, 0x0001, NULL, NULL);
}

/**
 * @func   timestamp_proc
 */
void timestamp_proc(void) {
  if (!time_par.is_set_flag) {
    return;
  }

  uint32_t clock_tmp = k_uptime_get_32();
  uint32_t t_delta = clock_tmp - time_par.time_tick_ms;

  if (t_delta >= TIMESTAMP_CHECK_INTERVAL_MS) {
    uint32_t interval_cnt = t_delta / TIMESTAMP_CHECK_INTERVAL_MS;
    time_par.current_time += interval_cnt;
    time_par.time_tick_ms += interval_cnt * TIMESTAMP_CHECK_INTERVAL_MS;

#if 0
    /* Log thời gian 1s 1 lần */
    struct tm tm_st;
    gmtime_r(&time_par.current_time, &tm_st);
    
    // Mảng tên thứ cho dễ đọc
    static const char* wday_name[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    
    LOG_INF("Current Time: %s %02d/%02d/%04d %02d:%02d:%02d",
            wday_name[tm_st.tm_wday % 7],
            tm_st.tm_mday,
            tm_st.tm_mon + 1, /* tm_mon is 0-11 */
            tm_st.tm_year + 1900, /* tm_year is years since 1900 */
            tm_st.tm_hour,
            tm_st.tm_min,
            tm_st.tm_sec);
#endif
  }

  if (time_par.previous_time != time_par.current_time) {
    time_par.previous_time = time_par.current_time;
  }
}

/**
 * @func   timestamp_get
 */
void timestamp_get(uint16_t adr_dst, uint16_t adr_src) {
  // Send response
  timestamp_rsp_t timestamp_rsp;
  timestamp_rsp.cmd = VD_CONFIG_TIMESTAMP;
  timestamp_rsp.value = time_par.current_time;

  mesh_tx_cmd_rsp(VD_CONFIG_NODE_STATUS, (uint8_t*)&timestamp_rsp,
                  sizeof(timestamp_rsp), adr_dst, adr_src, NULL, NULL);
}

/**
 * @func   timestamp_set
 */
int timestamp_set(uint8_t model_idx, uint8_t* par, uint8_t par_len) {
  if (par_len == PAR_LEN_SET_TIMESTAMP) {
    if (model_idx == 0) {
      time_par.is_set_flag = true;
      time_par.current_time = 0;
      for (int i = 0; i < sizeof(time_t) && i < 4; i++) {
        time_par.current_time |= ((time_t)par[i]) << (8 * i);
      }
      time_par.time_tick_ms = k_uptime_get_32();
      time_par.set_st_time_s = clock_time_s();

      LOG_INF("Timestamp set: %u", (uint32_t)time_par.current_time);
      return 0;
    }
  }
  return -1;
}
