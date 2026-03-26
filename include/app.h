#ifndef APP_H
#define APP_H

#include <stddef.h>
#include <stdint.h>

void app_serial_dispatch(uint8_t *data, size_t data_len);

/**
 * @brief Button event handler - called by button_proc() when GPIO button event
 * occurs
 * @param button_id Button index (0 = option button, 1..N = touch buttons)
 * @param evt Button event type (START_PRESS, HOLD_500MS, PRESS_ONE_TIME, etc.)
 */
void button_handle_btn_event(uint8_t button_id, uint8_t evt);
void app_handle_refresh_led(uint16_t mask);

#endif /* APP_H */
