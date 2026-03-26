/*
 * dimming.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTranBK
 *  Ported to Nordic: Jan 13, 2026
 */

#ifndef DIMMING_H_
#define DIMMING_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "app_device.h"


/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

enum dimming_on_type_enum {
  ON_RESTORE,
  ON_SET_UP_VALUE,
  ON_INVALID,
};
typedef uint8_t dimming_on_type_enum;

enum dimming_en_enum { DIMMING_DISABLE, DIMMING_ENABLE, DIMMING_EN_INVALID };

#define DIMMING_LIGHTNESS_MIN 0x2628 /* s16 is -23000 */
#define DIMMING_LIGHTNESS_MAX 0xFFFF

#define DIMMING_TRANS_T_MIN 0x0A
#define DIMMING_TRANS_T_DEFAULT 0x32

enum dimming_type_enum {
  DIM_TYPE_LIGHTNESS = 0,
  DIM_TYPE_TEMPERATURE = 1,
  DIM_TYPE_HUE = 2,
  DIM_TYPE_UNKNOWN,
};
typedef uint8_t dimming_type_enum;

typedef struct {
  uint8_t on_type;
  uint16_t on_value;
  uint8_t tran_t;
  uint8_t dim_type;
} dim_config_set_t;

extern dim_config_set_t dimming_config[NUMBER_INPUT];
extern uint8_t control_binding_locally_flag[NUMBER_INPUT];
extern btn_evt_t touch_btn_before_st[NUMBER_INPUT];
/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/

void dimming_init(void);
void dimming_active(uint8_t idx, uint8_t dir, uint16_t dst_addr);
int dimming_configuration_set(uint8_t idx, uint8_t* par, uint8_t par_len);
int dimming_configuration_get(uint8_t idx);
bool dimming_mod_lightness(uint8_t idx, uint16_t* lightness);
void dimming_handle_btn_st(uint8_t idx, uint8_t evt);

#endif /* DIMMING_H_ */
