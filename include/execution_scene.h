/*
 * execution_scene.h
 *
 *  Created on: Jan 23, 2026
 *      Author: DungTran BK
 */

#ifndef EXECUTION_SCENE_H_
#define EXECUTION_SCENE_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
// #include "proj/tl_common.h"
// #include "periph/periph_led.h"
// #include "vendor/common/mesh_node.h"
#include <stdbool.h>
#include <stdint.h>

#include "app_device.h"
#include "led.h"
#include "utilities.h"

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

#define SCENE_ID_INVALID 0x0000
#define MAX_PAYLOAD_LEN 14
#define MAX_SCENE_DEST_ADR 5

enum ButtonKeyIndex_enum {
  EV_SW_ON = 5,
  EV_SW_OFF = 6,
  EV_BTN_MAX = 2,  // EV_SW_ON, EV_SW_OFF
  BTN_KEY_INVALID = 0xFF,
};

typedef struct {
  uint16_t dest_addr;
  uint8_t payload_len;
  uint8_t payload[MAX_PAYLOAD_LEN];
} __attribute__((packed)) control_infor_t;

typedef struct {
  uint16_t in_scene_id;
  uint8_t in_trans_t;
  uint8_t in_delay_t;
} __attribute__((packed)) scene_infor_t;

typedef struct {
  uint8_t btn_event;
  uint16_t scene_id;
  union {
    scene_infor_t scene_control[MAX_SCENE_DEST_ADR];
    control_infor_t control_infor[MAX_SCENE_DEST_ADR];
  };
  uint8_t trans_time;
} __attribute__((packed)) par_execution_scene_t;

typedef struct {
  uint8_t msg_type : 3;
  uint8_t msg_id : 5;
} __attribute__((packed)) msg_type_and_id_t;

typedef struct {
  uint8_t key_event : 4;
  uint8_t key_nbr : 4;
} __attribute__((packed)) setup_event_t;

typedef struct {
  uint8_t comparision : 4;  // comparision_operation
  uint8_t condition : 4;    // id_condition
} __attribute__((packed)) id_and_comparison_t;

typedef enum {
  COM_BIGGER = 0x00,
  COM_LESS = 0x01,
  COM_EQUAL = 0x02,
  COM_GREATER_OR_EQUAL = 0x03,
  COM_LESS_OR_EQUAL = 0x04,
  COM_RFU = 0x05,
} comparision_operation_enum;

typedef enum {
  CON_LIGHT_INTENSITY = 0x00,
  CON_TEMPERATURE = 0x01,
  CON_HUMIDITY = 0x02,
  CON_RFU = 0x04,
} id_condition_enum;

typedef enum {
  MSG_ADD = 0x00,
  MSG_DELETE = 0x01,
  MSG_DELETE_ALL = 0x02,
  MSG_GET = 0x03,
  MSG_RFU = 0x04
} msg_type_setup_execution_scene_enum;

typedef enum {
  MSG_ADD_RSP = 0x00,
  MSG_DELETE_RSP = 0x01,
  MSG_DELETE_ALL_RSP = 0x02,
  MSG_GET_RSP = 0x03,
  MSG_RSP_RFU = 0x04
} msg_type_setup_execution_scene_status_enum;

// Setup Execution Scene Set

typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  union {
    uint8_t event;
    setup_event_t setup_event_st;
  };
  uint16_t scene_id;
  union {
    uint8_t nums_dest;
    uint8_t nums_scene;
  };
  struct set_control_infor_t {
    uint16_t dest_addr;
    uint8_t payload_len;
    uint8_t payload[MAX_PAYLOAD_LEN];
  } set_control_infor[MAX_SCENE_DEST_ADR];
  uint8_t trans_time;
  struct add_condition_t {
    uint8_t logical;
    union {
      uint8_t condition_and_comparison;
      id_and_comparison_t id_and_comparision_st;
    };
    uint16_t compare_val;
    ;
  } add_condition;

} __attribute__((packed)) setup_execution_scene_set_t;

typedef struct {
  uint8_t msg_type;
  uint16_t scene_id;
  uint8_t tid;
  uint8_t trans_time;
  uint8_t delay_time;
} __attribute__((packed)) vendor_scene_recall_t;

enum {
  VENDOR_SCENE_NOACK = 0x00,
  VENDOR_SCENE_RECALL_NOACK = 0x01,
  VENDOR_SCENE_DEL_NOACK = 0x02
};

typedef struct {
  uint8_t msg_type;
  uint16_t opcode;
  uint8_t* par;
} __attribute__((packed)) control_by_op_t;

typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  uint8_t status_code;
  uint8_t event;
  uint16_t scene_id;
} __attribute__((packed)) execution_scene_add_rsp_str;

typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  uint8_t status_code;
  uint8_t event;
} __attribute__((packed)) execution_scene_del_rsp_str;

typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  uint8_t status_code;
} __attribute__((packed)) execution_scene_del_all_rsp_str;

typedef enum {
  EX_STATUS_SUCCESS = 0x00,
  EX_STATUS_FAIL = 0x01,
  EX_STATUS_NOT_SET_BEFORE = 0x02,
  END_EX_STATUS = 0x04
} vendor_execution_scene_status_enum;

// Header of get execution scene msg
typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  uint8_t status_code;
  uint8_t event;
  uint16_t scene_id;
  union {
    uint8_t nbr_of_dest;
    uint8_t nbr_of_scene;
  };
} __attribute__((packed)) header_execution_scene_get_rsp_t;

typedef struct {
  uint32_t send_last_t_ms;
  bool en_send;
  uint8_t btn_key;
  uint8_t send_ev_idx;
} __attribute__((packed)) send_exe_scene_delay_t;

typedef struct {
  union {
    uint8_t msg_type_and_id;
    msg_type_and_id_t msg_type_and_id_st;
  };
  uint8_t event;
  union {
    uint8_t event_inner; /* Renamed to avoid conflict */
    setup_event_t evt_st;
  };

} __attribute__((packed)) msg_get_execution_scene_t;

/* definitions removed as they are in utilities.h:
 * dim_ud_present_enum, control_direction_t, vd_up_down_control_t
 */

#define LM_SCENE_ID_INVALID 0xFFFF
#define INVALID_KEY_EVENT 0xFF

#define EXECUTION_SCENE_RETRY_TIME 0x1

extern LedColor_enum led_color[NUMBER_INPUT];

/******************************************************************************/
/*                             EXPORT FUNCTIONS                               */
/******************************************************************************/
void execution_scene_init(void);
void delete_all_execution_scene(void);
void execution_scene_active(uint8_t key_number, uint8_t key_event);
void execution_set_up_auto_send(void);
void execution_loop_task(void);

#endif /* EXECUTION_SCENE_H_ */
