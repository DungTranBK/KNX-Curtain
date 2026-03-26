/*
 * relay.c
 *
 * Ported from Telink ONE_WIRE_SWITCH relay.c to Nordic nRF Connect SDK
 * (Zephyr). Supports: 2-wire power, latching relay only.
 *
 * Key differences from Telink:
 * - Uses k_timer instead of timer1_hw for pulse control (10ms)
 * - Uses Zephyr Settings (NVS) for flash persistence instead of flash_user
 * - Uses Zephyr GPIO API with DeviceTree instead of direct register access
 * - Removed: zero-crossing, power type detection, normal relay, switch modes
 *
 * Author: DungTranBK
 */

#include "../../include/relay.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#include "../../include/app_device.h"
#include "../../include/utilities.h"

LOG_MODULE_REGISTER(relay, CONFIG_LOG_DEFAULT_LEVEL);

/******************************************************************************/
/*                              PRIVATE DATA                                  */
/******************************************************************************/

/**
 * GPIO pin configuration for each relay (on_pin + off_pin).
 */
typedef struct {
  const struct gpio_dt_spec on_pin;
  const struct gpio_dt_spec off_pin;
} relay_pin_t;

/**
 * Active relay channel tracking (ported from Telink relay_active_t).
 */
typedef struct {
  uint8_t channel; /* Active relay index, or NO_RL_CHN_ACTIVE */
  uint8_t state;   /* Target state for active relay */
  uint8_t step;    /* Step in pulse sequence: 0=drive, 1=wait timer */
} relay_active_t;

/**
 * Delayed store context for flash persistence.
 */
typedef struct {
  bool enable;
  uint32_t start_time;
} relay_store_delay_t;

/* GPIO pin definitions from DeviceTree */
static const relay_pin_t relay_pins[RELAY_COUNT] = {
    {
        .on_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay0), on_gpios),
        .off_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay0), off_gpios),
    },
    {
        .on_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay1), on_gpios),
        .off_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay1), off_gpios),
    },
    {
        .on_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay2), on_gpios),
        .off_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay2), off_gpios),
    },
    {
        .on_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay3), on_gpios),
        .off_pin = GPIO_DT_SPEC_GET(DT_ALIAS(relay3), off_gpios),
    },
};

/* State machine */
static uint8_t relay_module_st = RELAY_IDLE;

/* Active channel tracking */
static relay_active_t active_channel = {
    .channel = NO_RL_CHN_ACTIVE,
    .state = 0xFF,
    .step = 0,
};

typedef struct {
  u16 hold_t_on[RELAY_COUNT];
  u16 hold_t_off[RELAY_COUNT];
  u16 RLS_TARGET;
  u16 RLS_PRESENT;
  u16 RLS_SET;
  u16 RLS_ACTUAL;
  u8 relay_module_st;
  bool done_on_off_control_flag;
  u16 half_period_of_grid;
  u32 md_busy_st_time;
  u32 control_relay_st_time;
  u32 check_power_type_st_time;
  u32 wait_zero_point_st_time;
  bool control_when_timeout;
  u16 isr_cnt;
  src_control_enum src_control[RELAY_COUNT];
} relay_para_t;

static relay_para_t relay_para;

/* Power-on delay to prevent voltage sag */
static uint32_t power_on_delay_interval_ms = 0;
static uint32_t power_on_delay_st_time = 0;
static bool power_on_delay_done = false;

/* Flash store delay */
static relay_store_delay_t store_delay = {.enable = false};

/* Callback */
static relay_state_change_cb_t state_change_cb = NULL;
static relay_handle_update_control_message_params
    update_control_message_params_cb = NULL;

/* k_timer for pulse control (replaces Telink timer1_hw) */
static struct k_timer relay_pulse_timer;

/******************************************************************************/
/*                        PRIVATE FUNCTIONS DECLARATION                       */
/******************************************************************************/
static void relay_latching_on(uint8_t idx);
static void relay_latching_off(uint8_t idx);
static void relay_reset_control_signal(uint8_t idx);
static void relay_handle_store_delay(void);
static void relay_setup_store_delay(void);

/******************************************************************************/
/*                  SETTINGS (FLASH PERSISTENCE) HANDLER                      */
/******************************************************************************/

static int relay_settings_set(const char *name, size_t len,
                              settings_read_cb read_cb, void *cb_arg) {
  if (!strcmp(name, "target")) {
    if (len != sizeof(relay_para.RLS_TARGET)) {
      return -EINVAL;
    }
    int rc =
        read_cb(cb_arg, &relay_para.RLS_TARGET, sizeof(relay_para.RLS_TARGET));
    if (rc < 0) {
      return rc;
    }
    LOG_INF("Restored relay target state: 0x%04X", relay_para.RLS_TARGET);
    return 0;
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(relay, "relay", NULL, relay_settings_set, NULL,
                               NULL);

/******************************************************************************/
/*                          TIMER HANDLER (ISR CONTEXT)                       */
/******************************************************************************/

static struct k_work relay_pulse_work;

/**
 * @brief Work handler - ends the relay pulse from the system workqueue.
 *        This safely calls gpio_pin_set_dt outside of the ISR context.
 */
static void relay_pulse_work_handler(struct k_work *work) {
  uint8_t idx = active_channel.channel;

  if (relay_para.relay_module_st != RELAY_BUSY) {
    return;
  }

  if (idx < RELAY_COUNT) {
    if (active_channel.step == 1) {
      /* Step 1: Turn off control signal (end pulse) */
      LOG_DBG("Relay %d: Pulse ended (OFF signal)", idx);
      relay_reset_control_signal(idx);
      relay_para.done_on_off_control_flag = CONTROL_SUCCESS;
      relay_para.relay_module_st = RELAY_IDLE;
    }
  } else {
    /* Invalid channel - reset all */
    for (int i = 0; i < RELAY_COUNT; i++) {
      relay_reset_control_signal(i);
    }
    relay_para.done_on_off_control_flag = CONTROL_SUCCESS;
    relay_para.relay_module_st = RELAY_IDLE;
  }
}

/**
 * @brief Timer expiry handler - schedules the end of the relay pulse.
 *        Equivalent to Telink's relay_handle_control_when_timer_match().
 *        Runs in ISR context for precise timing, delegates to k_work.
 */
static void relay_pulse_timer_handler(struct k_timer *timer) {
  k_work_submit(&relay_pulse_work);
}

/******************************************************************************/
/*                        GPIO CONTROL FUNCTIONS                              */
/******************************************************************************/

/**
 * @brief Turn ON a latching relay (set on_pin HIGH, off_pin LOW).
 *        Ported from Telink relay_latching_on().
 */
static void relay_latching_on(uint8_t idx) {
  if (idx < RELAY_COUNT) {
    int ret;
    ret = gpio_pin_set_dt(&relay_pins[idx].off_pin, 0);
    if (ret) {
      LOG_ERR("Relay %d: OFF pin set 0 failed: %d", idx, ret);
    }
    ret = gpio_pin_set_dt(&relay_pins[idx].on_pin, 1);
    if (ret) {
      LOG_ERR("Relay %d: ON pin set 1 failed: %d", idx, ret);
    }
    if (!ret) {
      LOG_DBG("Relay %d: Latching ON OK", idx);
    }
  }
}

/**
 * @brief Turn OFF a latching relay (set off_pin HIGH, on_pin LOW).
 *        Ported from Telink relay_latching_off().
 */
static void relay_latching_off(uint8_t idx) {
  if (idx < RELAY_COUNT) {
    int ret;
    ret = gpio_pin_set_dt(&relay_pins[idx].on_pin, 0);
    if (ret) {
      LOG_ERR("Relay %d: ON pin set 0 failed: %d", idx, ret);
    }
    ret = gpio_pin_set_dt(&relay_pins[idx].off_pin, 1);
    if (ret) {
      LOG_ERR("Relay %d: OFF pin set 1 failed: %d", idx, ret);
    }
    if (!ret) {
      LOG_DBG("Relay %d: Latching OFF OK", idx);
    }
  }
}

/**
 * @brief Reset both control signals to LOW (end of pulse).
 *        Ported from Telink relay_reset_control_signal().
 */
static void relay_reset_control_signal(uint8_t idx) {
  if (idx < RELAY_COUNT) {
    int r1 = gpio_pin_set_dt(&relay_pins[idx].on_pin, 0);
    int r2 = gpio_pin_set_dt(&relay_pins[idx].off_pin, 0);
    if (r1 || r2) {
      LOG_ERR("Relay %d: reset signal failed: on=%d off=%d", idx, r1, r2);
    }
  }
}

/******************************************************************************/
/*                      FLASH STORE DELAY FUNCTIONS                           */
/******************************************************************************/

/**
 * @brief Store relay target state to NVS.
 *        Ported from Telink relay_store_target_state().
 */
static void relay_store_target_state(void) {
  int rc = settings_save_one("relay/target", &relay_para.RLS_TARGET,
                             sizeof(relay_para.RLS_TARGET));
  if (rc) {
    LOG_ERR("Failed to store relay state: %d", rc);
  } else {
    LOG_INF("Stored relay target state: 0x%04X", relay_para.RLS_TARGET);
  }
}

/**
 * @brief Handle delayed store - called from relay_proc().
 *        Ported from Telink relay_handle_store_delay().
 */
static void relay_handle_store_delay(void) {
  if (store_delay.enable) {
    uint32_t now = (uint32_t)k_uptime_get();
    if ((now - store_delay.start_time) > RELAY_STORE_DELAY_MS) {
      relay_store_target_state();
      store_delay.enable = false;
      LOG_DBG("Store delay completed");
    }
  }
}

/**
 * @brief Setup a delayed store.
 *        Ported from Telink relay_setup_store_state_delay().
 */
static void relay_setup_store_delay(void) {
  store_delay.enable = true;
  store_delay.start_time = (uint32_t)k_uptime_get();
}

/******************************************************************************/
/*                          EXPORT FUNCTIONS                                  */
/******************************************************************************/

/**
 * @brief Initialize the relay module.
 *        Ported from Telink relay_init().
 */
int relay_init(void) {
  int ret;

  for (int i = 0; i < RELAY_COUNT; i++) {
    /* Check if GPIO controllers are ready */
    if (!device_is_ready(relay_pins[i].on_pin.port) ||
        !device_is_ready(relay_pins[i].off_pin.port)) {
      LOG_ERR("Relay %d GPIO device not ready", i);
      return -ENODEV;
    }

    /* Configure ON pin */
    ret = gpio_pin_configure_dt(&relay_pins[i].on_pin, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
      LOG_ERR("Relay %d: Failed to configure ON pin: %d", i, ret);
      return ret;
    }

    /* Configure OFF pin */
    ret = gpio_pin_configure_dt(&relay_pins[i].off_pin, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
      LOG_ERR("Relay %d: Failed to configure OFF pin: %d", i, ret);
      return ret;
    }
  }

  /* Initialize pulse timer */
  k_timer_init(&relay_pulse_timer, relay_pulse_timer_handler, NULL);
  k_work_init(&relay_pulse_work, relay_pulse_work_handler);

  /* Initialize Power-on delay */
  power_on_delay_done = false;
  power_on_delay_st_time = (uint32_t)k_uptime_get();
  /* Random delay between 3000 ms and 10000 ms */
  power_on_delay_interval_ms = 3000 + (sys_rand32_get() % 7000);

  /* Initialize state machine */
  relay_para.relay_module_st = RELAY_IDLE;
  relay_para.done_on_off_control_flag = CONTROL_IN_PROCESSING;
  relay_para.control_relay_st_time = (uint32_t)k_uptime_get();
  relay_para.md_busy_st_time = (uint32_t)k_uptime_get();

  relay_para.RLS_TARGET = relay_para.RLS_TARGET = 0;

  LOG_INF("Relay module initialized (%d channels, latching, 2-wire)",
          RELAY_COUNT);
  return 0;
}

/**
 * @brief Main relay processing loop.
 *        Ported from Telink relay_proc() (ONE_WIRE_SWITCH version).
 *
 *        State machine:
 *        1. IDLE + target != present → find first different channel
 *        2. Drive relay (latching on/off) + start pulse timer
 *        3. BUSY → wait for timer to complete
 *        4. Timer fires → reset control signal → IDLE
 *        5. Repeat for next channel
 */
uint8_t relay_proc(void) {
  /* Random power-on relay control delay to prevent power sag */
  if (!power_on_delay_done) {
    if (((uint32_t)k_uptime_get() - power_on_delay_st_time) <
        power_on_delay_interval_ms) {
      return RELAY_IDLE; /* Block relay switching during power-on delay */
    } else {
      power_on_delay_done = true;
      LOG_INF("Power-on relay delay finished (%u ms)",
              power_on_delay_interval_ms);
    }
  }

  /* Rate-limit relay control for 2-wire power supply (one at a time) */
  uint32_t now = (uint32_t)k_uptime_get();
  if ((now - relay_para.control_relay_st_time) < RELAY_CONTROL_INTERVAL_MS) {
    if (relay_para.relay_module_st == RELAY_BUSY) {
      /* Already controlling, let it finish */
    } else if (relay_para.RLS_TARGET != relay_para.RLS_PRESENT) {
      /* Need to control but too soon, wait */
      relay_para.md_busy_st_time =
          now; /* Reset watchdog since we are just waiting */
      return RELAY_BUSY;
    }
  }

  /* Busy timeout safety check */
  if (relay_para.relay_module_st != RELAY_BUSY) {
    relay_para.md_busy_st_time = now;
  } else {
    if ((now - relay_para.md_busy_st_time) > RELAY_BUSY_TIMEOUT_MS) {
      LOG_ERR("Relay module busy timeout! Rebooting...");
      sys_reboot(SYS_REBOOT_COLD);
    }
  }

  if (relay_para.relay_module_st == RELAY_IDLE) {
    /* Process completed control */
    if (relay_para.done_on_off_control_flag == CONTROL_SUCCESS) {
      if (active_channel.channel != NO_RL_CHN_ACTIVE) {
        /* Update present state bitmask */
        if (active_channel.state == RELAY_STATE_ON) {
          relay_para.RLS_PRESENT |= (uint16_t)(1 << active_channel.channel);
        } else {
          relay_para.RLS_PRESENT &= (uint16_t)(~(1 << active_channel.channel));
        }
        relay_para.RLS_PRESENT &= RELAY_BACKUP_MASK;
        active_channel.channel = NO_RL_CHN_ACTIVE;
      }
      relay_para.done_on_off_control_flag = CONTROL_IN_PROCESSING;
    }

    /* Check if any relay needs to change */
    if (relay_para.RLS_TARGET != relay_para.RLS_PRESENT) {
      for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (((relay_para.RLS_TARGET >> i) & 0x01) !=
            ((relay_para.RLS_PRESENT >> i) & 0x01)) {
          /* Found a relay that needs to change */
          active_channel.channel = i;
          active_channel.step = 0;
          active_channel.state = (relay_para.RLS_TARGET >> i) & 0x01;

          /* Drive the relay immediately (for 2-wire, no zero-crossing needed)
           */
          if (active_channel.state == RELAY_STATE_ON) {
            relay_latching_on(i);
          } else {
            relay_latching_off(i);
          }

          /* Start pulse timer to end control signal after 10ms */
          active_channel.step = 1;
          k_timer_start(&relay_pulse_timer, K_MSEC(RELAY_PULSE_DURATION_MS),
                        K_NO_WAIT);

          relay_para.relay_module_st = RELAY_BUSY;
          relay_para.control_relay_st_time = now;
          break;
        }
      }
    }
  }
  return relay_para.relay_module_st;
}

/**
 * @brief Set the target state of a specific relay.
 *        Ported from Telink relay_set_target_state().
 */
void relay_set_target_state(uint8_t idx, uint8_t state, src_control_enum src,
                            uint16_t dst_addr, bool store) {
  if (idx >= RELAY_COUNT) {
    return;
  }
  if (state == G_ON) {
    relay_para.RLS_SET |= (uint16_t)(1 << idx);
  } else {
    relay_para.RLS_SET &= (uint16_t)(~(1 << idx));
  }
  relay_para.RLS_SET &= BACKUP_MASK_RL;
  // Compare
  if (state != ((relay_para.RLS_TARGET >> idx) & 0x01)) {
    if (store == true) {
      relay_setup_store_delay();
    }
  }
  // Update source control RL
  relay_para.src_control[idx] = src;
  if (src == SRC_DEVICE) {
    if (update_control_message_params_cb != NULL) {
      update_control_message_params_cb(idx, dst_addr, dst_addr, G_ONOFF_SET);
    }
  }
}

/**
 * @brief Get the target state bitmask.
 *        Ported from Telink relay_get_target_state().
 */
uint16_t relay_get_target_state(void) { return relay_para.RLS_TARGET; }

/**
 * @brief Get the target state for a specific relay index.
 */
uint8_t relay_get_target_state_by_index(uint8_t idx) {
  if (idx >= RELAY_COUNT) {
    return RELAY_STATE_OFF;
  }
  return (relay_para.RLS_TARGET >> idx) & 0x01;
}

/**
 * @brief Get the present state bitmask.
 *        Ported from Telink relay_get_present_state().
 */
uint16_t relay_get_present_state(void) { return relay_para.RLS_PRESENT; }

/**
 * @func    relay_control_directly
 * @brief
 * @param
 * @retval  None
 */
void relay_control_directly(uint8_t idx, uint8_t st) {
  if (st == G_ON) {
    relay_para.RLS_TARGET |= (uint16_t)(1 << idx);
    relay_para.RLS_PRESENT &= (uint16_t)(~(1 << idx));
  } else {
    relay_para.RLS_TARGET &= (uint16_t)(~(1 << idx));
    relay_para.RLS_PRESENT |= (uint16_t)(1 << idx);
  }
  relay_para.RLS_TARGET &= BACKUP_MASK_RL;
}
