#ifndef KEY_REPORT_H_
#define KEY_REPORT_H_

#include "execution_scene.h"
#include "utilities.h"
#include <stdbool.h>
#include <stdint.h>


#define VD_RC_KEY_REPORT 0xC0

typedef struct {
  uint8_t code;
  uint8_t rsv[7];
} vd_rc_key_report_t;

typedef uint8_t ButtonKey_enum;

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/
void key_report_send(uint8_t idx, uint8_t key_code, bool en_execution);
uint16_t get_ele_addr_base_btn_idx(uint8_t idx);

#endif /* KEY_REPORT_H_ */
