#include "../../include/app.h"
#include "../../include/at24c02.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "../../include/app_device.h"
#include "../../include/button.h"
#include "../../include/curtain.h"
#include "../../include/fact.h"
#include "../../include/fast_provision.h"
#include "../../include/led.h"
#include "../../include/led_ev.h"
#include "../../include/mesh_node.h"
#include "../../include/network.h"
#include "../../include/pre_update.h"
#include "../../include/relay.h"
#include "../../include/scene.h"
#include "../../include/timestamp.h"
#include "../../include/utilities.h"
#include "../../include/vendor_model.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define APP_SUPER_LOOP_STACK_SIZE 2048
#define APP_SUPER_LOOP_PRIORITY 7

/* I2C device for AT24C02 EEPROM */
#define I2C_DEV_NODE DT_NODELABEL(i2c_gpio)
#define AT24C02_TEST_INTERVAL_MS 5000

/**
 * @brief Handle sending sensor data to Bluetooth Mesh
 */

/* Button parameter state tracking (ported from Telink app_btn_par_t) */
typedef struct {
  uint8_t evt;
  uint32_t change_st_start_t;
  bool is_hold_500ms;
} app_btn_par_t;

static app_btn_par_t app_btn_par[NUMBER_BUTTON];

const uint8_t button_config_arr[] = {CONFIG_BTN_ID_BLUETOOTH,
                                     CONFIG_BTN_ID_KNX};

typedef enum {
  STEP_IDLE,
  STEP_HOLD_10S,         // wait release
  STEP_HOLD_10S_RELEASE, // wait press one time
  STEP_WAIT_REBOOT_AFTER_RESET,
  STEP_INVALID,
} StepReset_Enum;

#define CONFIRM_RESET_TIMEOUT_MS TIMER_5S
#define RESET_NETWORK_TIMEOUT_MS TIMER_10S

typedef struct {
  bool is_active;
  u8 step;
  u32 change_step_last_t;
  u32 is_active_st_time;
} para_btn_reset_t;

static para_btn_reset_t para_btn_reset = {
    .is_active = false,
    .step = STEP_IDLE,
    .change_step_last_t = MAX_U32,
    .is_active_st_time = 0,
};

/**
 * @brief Check if button is a configuration button
 */
static bool is_config_button(uint8_t button_id) {
  return (button_id == CONFIG_BTN_ID_BLUETOOTH ||
          button_id == CONFIG_BTN_ID_KNX);
}

/**
 * @func    update_reset_by_btn_step
 * @brief   None
 * @param
 * @retval  None
 */
static void update_reset_by_btn_step(u8 step) {
  if (step < STEP_INVALID) {
    para_btn_reset.step = step;
    para_btn_reset.change_step_last_t = clock_time_ms();
  }
}

/**
 * @func   reset_timeout_handle
 * @brief
 * @param
 * @retval None
 */
static void reset_timeout_handle(void) {
  para_btn_reset.step = STEP_IDLE;
  para_btn_reset.is_active = false;
  led_refresh(0xFFFF);
}

/**
 * @func   button_check_clean_reset_param
 * @brief
 * @param
 * @retval None
 */
static void button_check_clean_reset_param(void) {
  if (para_btn_reset.step == STEP_WAIT_REBOOT_AFTER_RESET) {
    if (clock_time_exceed_ms(para_btn_reset.is_active_st_time,
                             RESET_NETWORK_TIMEOUT_MS)) {
      reset_timeout_handle();
      LOG_INF("\nev_handle_check_clean_reset_param OK");
    }
    return;
  }
  if (para_btn_reset.is_active == true) {
    if (clock_time_exceed_ms(para_btn_reset.is_active_st_time,
                             CONFIRM_RESET_TIMEOUT_MS)) {
      reset_timeout_handle();
      LOG_INF("\nev_handle_check_clean_reset_param OK");
    }
  }
  if (para_btn_reset.step == STEP_HOLD_10S) {
    if (clock_time_exceed_ms(para_btn_reset.change_step_last_t, TIMER_2S)) {
      reset_timeout_handle();
    }
  }
}

/**
 * @brief Button event handler - ported from Telink app_handle_button_state
 * @param button_id Button index (0..N)
 * @param evt Button event type
 */
void button_handle_btn_event(uint8_t button_id, uint8_t evt) {
  if (button_id >= NUMBER_BUTTON) {
    return;
  }
  uint8_t idx = button_id;
  /* Handle NO_PRESS - check for timeout on hold_15s */
  if (evt == NO_PRESS) {
    goto UPDATE_BTN_PAR;
  }
  if (idx == CONFIG_BUTTON_IDX) {
    switch (evt) {
    case START_PRESS:
      if (led_get_blink_led_flag() == BLINK_IDLE) {
        led_off_all();
      }
      k_sleep(K_MSEC(100));
      led_refresh(BACKUP_MASK);
      break;

    case PRESS_ONE_TIME:
      if (para_btn_reset.step != STEP_HOLD_10S_RELEASE) {
        // TODO, KNX enable/disable configuration mode
        break;
      }
      if (para_btn_reset.is_active == true) {
        if (!clock_time_exceed_ms(para_btn_reset.is_active_st_time,
                                  CONFIRM_RESET_TIMEOUT_MS)) {
          // TODO, KNX clear ETS data
          LOG_INF("\n KNX CLEAR ETS DATA");
        } else {
          button_check_clean_reset_param();
          LOG_INF("\n Confirm reset KNX timeout");
        }
        led_enable_queue();
      }
      break;

    case PRESS_TWO_TIME:
      if (para_btn_reset.step != STEP_HOLD_10S_RELEASE) {
        network_enable_provisioning_with_timeout();
        break;
      }
      if (para_btn_reset.is_active == true) {
        if (!clock_time_exceed_ms(para_btn_reset.is_active_st_time,
                                  CONFIRM_RESET_TIMEOUT_MS)) {
          factory_reset_and_reboot();
          LOG_INF("\n BLE mesh OUT_NETWORK");
        } else {
          button_check_clean_reset_param();
          LOG_INF("\n Confirm reset Bluetooth timeout");
        }
        led_enable_queue();
      }
      break;

    case HOLD_10S:
      led_disable_current_command_and_queue_with_timeout(
          CONFIRM_RESET_TIMEOUT_MS);
      // led_off_all();
      led_blink_no_queue(CONFIG_LED_MASK, 2, LAST_STATE_REFRESH_LED,
                         LED_COLOR_BLUE, 200);
      update_reset_by_btn_step(STEP_HOLD_10S);
      break;

    case _RELEASE:
    case RL_AFTER_PRESS:
      if (para_btn_reset.step == STEP_HOLD_10S) {
        if (!clock_time_exceed_ms(para_btn_reset.change_step_last_t,
                                  TIMER_2S)) {
          update_reset_by_btn_step(STEP_HOLD_10S_RELEASE);
          para_btn_reset.is_active = true;
          para_btn_reset.is_active_st_time = clock_time_ms();
          if (led_get_blink_led_flag() == BLINK_IDLE) {
            led_refresh(CONFIG_LED_MASK);
          }
          LOG_INF("\n STEP_HOLD_10S_RELEASE");
        }
      }
      break;
    }
  }
UPDATE_BTN_PAR:
  app_btn_par[idx].evt = evt;
  app_btn_par[idx].change_st_start_t = clock_time_ms();
}

/* ============================================================================
 * Export funtions for KNX
 * ============================================================================
 */

/**
 * @brief Dispatch received serial frames to appropriate handlers.
 */
void app_serial_dispatch(uint8_t *par, size_t par_len) {
  // TODO, KNX handle incoming message from serial port
}

/*
 * @func    app_handle_control_relay_from_knx
 * @brief   Handle control relay from KNX, must be called by KNX driver
 * @param   model_index: Model index
 * @param   status: Status
 * @retval  None
 */
int app_handle_control_curtain_from_knx(uint8_t curtain_idx,
                                        CurtainControlId_enum cmd_id,
                                        uint8_t position) {
  if (curtain_idx >= ELE_CNT) {
    return -1;
  }
  net_msg_cb_par_t cb_par;
  cb_par.model_idx = curtain_idx;
  cb_par.adr_src = GATEWAY_UNICAST_ADDR;
  cb_par.adr_dst = network_get_unicast_address() + curtain_idx;

  vd_cmd_g_level_set_t level_set;
  memset(&level_set, 0, sizeof(level_set));
  level_set.level = position;
  level_set.type = cmd_id;
  return handle_mesh_cmd_sig_g_level_set((u8 *)&level_set,
                                         sizeof(vd_cmd_g_level_set_t), &cb_par);
}

/**
 * @brief Handle relay state change - notify all streams (Mesh + KNX)
 * @param model_idx Model index
 * @param status Status
 */
void app_handle_curtain_update_level(uint8_t curtain_idx,
                                     uint8_t current_position) {
  // TODO, KNX send state change to KNX bus
  LOG_INF("KNX: app_handle_curtain_update_level: curtain_idx=%d, "
          "current_position=%d",
          curtain_idx, current_position);
}

/**
 * @brief Get relay status
 * @param model_idx Model index
 * @param status Pointer to store status
 * @retval 0 on success, -1 on failure
 */
int app_get_curtain_current_position(uint8_t curtain_idx,
                                     uint8_t *current_position) {
  if (curtain_idx >= RELAY_COUNT || current_position == NULL) {
    return -1;
  }
  *current_position = curtain_get_curret_level(curtain_idx);
  return 0;
}

/**
 * @brief app_handle_set_switch_config
 * @param model_idx Model index : 0x00 - 0xFF
 * @param par Pointer to store status
 * @param par_len Parameter length
 * @param cmd Command
 *   1. VD_CONFIG_CURTAIN_TYPE_OPT (cmd: 0x26)
        - Endpoint (1 byte): 0x01 - 0xFF
        - Curtain Type (2 bytes):   HOZ_DZ3W = 0
                                    HOZ_DZ4W = 1
                                    HOZ_DT99 = 2
                                    HOZ_DZ3WP = 3
                                    VER_220V = 4
     2. VD_CONFIG_CURTAIN_LIMIT_TIME (cmd: 0x40)
        - Curtain Limit time (4 bytes): 0x00000000 - 0x012c (1s - 300s)
          NOTE: MSB first (same as uint16_t, uint32_t...)
 * @retval 0 on success, -1 on failure
 */
int app_handle_set_curtain_config(int model_idx, uint8_t *par, int par_len,
                                  uint8_t cmd) {
  if (cmd == VD_CONFIG_CURTAIN_TYPE_OPT ||
      cmd == VD_CONFIG_CURTAIN_LIMIT_TIME) {
    return curtain_cfg_handle_set_message(model_idx, par, par_len, cmd, false);
  }
  return -1;
}

/*
 Used to handle LED display in standby state:
- Red LED: Not configured to Bluetooth network or KNX ETS not configured
- Red LED: Configured to Bluetooth network or KNX ETS configured
*/
bool knx_ets_has_been_configured(void) {
  // TODO, KNX check ETS has been configured
  return false;
}

/* ============================================================================
 * End Export funtions for KNX
 * ============================================================================
 */
/*
 * @func    app_handle_refresh_led
 * @brief   Refresh LED state based on provisioning status
 * @param   mask: LED mask to refresh
 * @retval  None
 */
void app_handle_refresh_led(uint16_t mask) {
  // Confirm reset case
  if (para_btn_reset.step == STEP_HOLD_10S ||
      para_btn_reset.step == STEP_HOLD_10S_RELEASE ||
      para_btn_reset.step == STEP_WAIT_REBOOT_AFTER_RESET) {
    foreach (i, NUMBER_BUTTON) {
      if (is_config_button(i)) {
        if (para_btn_reset.step == STEP_HOLD_10S_RELEASE ||
            para_btn_reset.step == STEP_WAIT_REBOOT_AFTER_RESET) {
          led_set_color(i, LED_COLOR_PINK);
        }
      } else {
        led_off(i);
      }
    }
    return;
  }
  /* Normal case */
  for (int i = 0; i < NUMBER_LED; i++) {
    if ((mask >> i) & 1) {
      if (STATE_DEV_PROVED == get_provision_state() ||
          knx_ets_has_been_configured()) {
        led_set_color(i, LED_COLOR_BLUE);
      } else {
        led_set_color(i, LED_COLOR_RED);
      }
    }
  }
}

static void app_super_loop(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  LOG_INF("Super loop started");

  led_ev_callback_register(knx_ets_has_been_configured);

  scene_callback_init(curtain_get_target_position,
                      scene_reg_response_delay_init, curtain_set_level);
  curtain_callback_init(app_handle_curtain_update_level);

  uint8_t loop_cnt = 0;
  while (1) {
    /* Yield for 2ms */
    k_msleep(2);

    /* Curtain runs every 2ms for precise position tracking */
    curtain_proc();

    /* Other tasks run every 10ms (every 5th iteration) */
    if (++loop_cnt >= 5) {
      loop_cnt = 0;

      /* Process button GPIO scanning */
      button_proc();

      /* Handle LED Events (blink, animation) */
      led_handle_event_function();

      /*Check clean reset param*/
      button_check_clean_reset_param();

      /* Relay */
      relay_proc();

      /* Pre-update for Mesh state synchronization */
      pre_update_proc();

      /* Timestamp processing */
      timestamp_proc();

      /* Vendor model */
      vendor_model_proc();
    }
  }
}

/* Define and start the thread automatically */
K_THREAD_DEFINE(app_super_loop_tid, APP_SUPER_LOOP_STACK_SIZE, app_super_loop,
                NULL, NULL, NULL, APP_SUPER_LOOP_PRIORITY, 0, 0);
