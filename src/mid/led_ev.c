/*
 * led_ev.c
 *
 *  Ported from Telink SDK
 */

#include "../../include/led_ev.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../../include/app_device.h"
#include "../../include/fast_provision.h" // For STATE_DEV_...
#include "../../include/led.h"
#include "../../include/network.h"

LOG_MODULE_REGISTER(led_ev, CONFIG_LOG_DEFAULT_LEVEL);

static typETS_is_configured pvETS_is_configured = NULL;

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/

/*
 * Register callback to get ETS configured status
 */
void led_ev_callback_register(typETS_is_configured cb) {
  if (cb != NULL) {
    pvETS_is_configured = cb;
  }
}
/*
 * Mock OTA check until implemented
 * If ota_mcu.h exists, we should include it.
 */
static bool ota_mcu_new_firmware_is_avalable(void) { return false; }

/*
 * Renamed function to avoid conflict if the original led.c
 * still has send_led_evt_to_mcu (even if empty or stub).
 * The user wants separate file porting.
 */
void led_ev_handle(uint8_t led_evt, uint16_t mask) {
  LOG_INF("+++led_ev_handle: %d", led_evt);
  switch (led_evt) {
 case LED_POWER_ON:  {
      bool is_configured = knx_is_configured();
      bool ble_provisioned = (get_provision_state() == STATE_DEV_PROVED);

      if (!ble_provisioned && !is_configured) {
        /* Both unconfigured -> Blink Red */
        led_blink(CONFIG_LED_MASK_BLUETOOTH, CMD_BLINK_RED, 3,
                  LAST_STATE_REFRESH_LED, 300);
      } else {
        /* At least one configured */
        if (ble_provisioned) {
          /* Blink Blue for BLE */
          led_blink(CONFIG_LED_MASK_BLUETOOTH, CMD_BLINK_BLUE, 3,
                    LAST_STATE_REFRESH_LED, 300);
        }
        if (is_configured) {
          /* Blink Pink for KNX */
          led_blink(CONFIG_LED_MASK_KNX, CMD_BLINK_PINK, 3,
                    LAST_STATE_REFRESH_LED, 300);
        }
      }
      LOG_INF("LED_POWER_ON");
      break;
    }

  case LED_OTA_FAIL: {
    if (!ota_mcu_new_firmware_is_avalable()) {
      led_blink(0xFFFF, CMD_BLINK_RED, 6, LAST_STATE_REFRESH_LED, 250);
      k_sleep(K_MSEC(500));
    }
    break;
  }

  case LED_OTA_SUCESS: {
    led_blink(0xFFFF, CMD_BLINK_BLUE, 6, LAST_STATE_REFRESH_LED, 250);
    k_sleep(K_MSEC(500));
    LOG_INF("LED_OTA_SUCESS");
    break;
  }

  case LED_FAIL_ADD_APPKEY:
  case LED_PROVISION_FAIL: {
    led_blink(0xFFFF, CMD_BLINK_RED, 3, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_PROVISION_FAIL");
    break;
  }

  case LED_SUC_ADD_APPKEY: {
    led_blink(mask, CMD_BLINK_BLUE, 3, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_SUC_ADD_APPKEY");
    break;
  }

  case LED_PROVISION_SUCCESS: {
    led_blink(mask, CMD_BLINK_BLUE, 3, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_PROVISION_SUCCESS");
    break;
  }

  case LED_CMD_BINDING_ENABLE:
    led_blink(mask, CMD_BLINK_BLUE, 2, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_CMD_BINDING_ENABLE");
    break;

  case LED_CMD_BINDING_DISABLE:
    led_blink(mask, CMD_BLINK_RED, 2, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_CMD_BINDING_DISABLE");
    break;

  case LED_CMD_BINDING_FAIL:
    led_blink(mask, CMD_BLINK_RED, 1, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_CMD_BINDING_FAIL");
    break;

  case LED_CMD_SET_SUBSCRIPTION:
    led_blink(mask, CMD_BLINK_BLUE, 2, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_CMD_SET_SUBSCRIPTION");
    break;

  case LED_CMD_DEL_SUBSCRIPTION:
    led_blink(mask, CMD_BLINK_RED, 2, LAST_STATE_REFRESH_LED, 300);
    LOG_INF("LED_CMD_DEL_SUBSCRIPTION");
    break;

  case LED_CMD_SET_SCENE:
    led_blink(mask, CMD_BLINK_BLUE, 2, LAST_STATE_REFRESH_LED, 200);
    LOG_INF("LED_CMD_SET_SCENE");
    break;

  case LED_CMD_DEL_SCENE:
    led_blink(mask, CMD_BLINK_RED, 2, LAST_STATE_REFRESH_LED, 200);
    LOG_INF("LED_CMD_DEL_SCENE");
    break;

  case LED_OTA_BLOCK_TRANFER:
    led_blink(mask, CMD_BLINK_PINK, 1, LAST_STATE_REFRESH_LED, 500);
    LOG_INF("LED_OTA_BLOCK_TRANFER");
    break;

  case LED_DIMMING_UP:
    led_blink(mask, CMD_BLINK_BLUE, 2, LAST_STATE_REFRESH_LED, 100);
    LOG_INF("LED_DIMMING_UP");
    break;

  case LED_DIMMING_DOWN_LIMIT:
    led_blink(mask, CMD_BLINK_RED, 2, LAST_STATE_REFRESH_LED, 100);
    LOG_INF("LED_DIMMING_DOWN_LIMIT");
    break;

  case LED_NOTIFY_RESET_BLUETOOTH_MESH:
    led_blink(mask, CMD_BLINK_PINK, 2, LAST_STATE_REFRESH_LED, 100);
    LOG_INF("LED_NOTIFY_RESET_BLUETOOTH_MESH");
    break;

  default:
    LOG_INF("Unhandled LED Event: %d", led_evt);
    break;
  }
}
