/*
 * led_ev.h
 *
 *  Author: DungTranBK
 */

#ifndef LED_EV_H_
#define LED_EV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*                       EXPORT TYPE AND DEFINITION                           */
/******************************************************************************/

#include "led.h"

/* Legacy LED event definitions (for led_ev.c compatibility) */
typedef enum {
  LED_POWER_ON,
  LED_OTA_FAIL,
  LED_OTA_SUCESS,
  LED_PROVISION_FAIL,
  LED_PROVISION_SUCCESS,
  LED_SUC_ADD_APPKEY,
  LED_FAIL_ADD_APPKEY,
  LED_CMD_SET_SUBSCRIPTION,
  LED_CMD_DEL_SUBSCRIPTION,
  LED_CMD_SET_SCENE,
  LED_CMD_DEL_SCENE,
  LED_CMD_BINDING_ENABLE,
  LED_CMD_BINDING_DISABLE,
  LED_CMD_BINDING_FAIL,
  LED_OTA_BLOCK_TRANFER,
  LED_DIMMING_UP,
  LED_DIMMING_DOWN_LIMIT,
  LED_NOTIFY_RESET_BLUETOOTH_MESH,
  END_LED_EVT
} led_evt_t;

typedef bool (*typETS_is_configured)(void);

/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/
void led_ev_handle(uint8_t led_evt, uint16_t mask);
void led_ev_callback_register(typETS_is_configured cb);

#ifdef __cplusplus
}
#endif

#endif /* LED_EV_H_ */
