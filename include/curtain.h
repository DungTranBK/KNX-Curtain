/*
 * curtain.h
 *
 *  Created on: Feb 17, 2021
 *      Author: DungTranBK
 */

#ifndef CURTAIN_H_
#define CURTAIN_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include "app_device.h"
#include "utilities.h"
#include "vendor_model.h"
#include <stdbool.h>
#include <stdint.h>

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

#define PAR_LEN_MSG_SET_LED_INTENSITY 0x1
#define PAR_LEN_MSG_LOCK_CURTAIN 0x1
#define PAR_LEN_MSG_SET_CURTAIN_TYPE 0x2

#define PAR_LEN_MSG_SET_LIMIT_TIME 0x4

#define CMD_MSG_SW_CONFIG_OPSET VD_CONFIG_ALL_SWITCH_OPT

#define MC_OPT_RESPONSE_LEN_MAX 64

typedef struct {
  uint8_t opt_id;
  uint8_t data[MC_OPT_RESPONSE_LEN_MAX];
} mc_opt_response_t;

typedef struct {
  uint8_t led_intensity_op;
  uint8_t led_intensity;
  uint8_t lock_device_op;
  uint16_t lock_device;
  uint8_t glass_type_op;
  uint8_t glass_type;
  uint8_t curtain_type_op;
  uint8_t curtain_type[2];
  uint8_t auto_lock_cfg_op;
  uint8_t auto_lock_cfg;
} mc_opt_response_all_t;

typedef struct {
  uint8_t btn_id;
  union {
    uint8_t btn_lock;
    uint16_t group_lock;
  };
} rsp_lock_dev_t;

typedef struct {
  bool en;
  uint16_t src_adr;
  uint8_t len;
  uint8_t par[6];
} rsp_mc_opt_to_gw_t;

typedef struct {
  uint8_t idx;
  uint8_t mask_cmd;
  uint32_t time_t;
} incomming_cfg_msg_t;

typedef struct {
  bool en;
  uint32_t send_last_t;
  uint8_t op_index;
  uint8_t btn_idx;
  bool en_fast;
  uint32_t delay_st;
  bool en_delay;
  bool send_join;
} cz_auto_send_t;

enum CurtainType_enum {
  HOZ_DZ3W = 0,
  HOZ_DZ4W = 1,
  HOZ_DT99 = 2,
  HOZ_DZ3WP = 3,
  VER_220V = 4,
  CZ_TYPE_UNKNOWN,
};
typedef uint8_t CurtainType_enum;

#define CZ_TYPE_DEFAULT HOZ_DZ4W

typedef struct {
  uint32_t limit_time_ms[ELE_CNT];
  uint8_t curtain_type[ELE_CNT];
  uint8_t rsv[8];
} curtain_config_t;

typedef struct {
  uint8_t endpoint;
  uint8_t cz_type;
} curtain_type_set_t;

typedef struct {
  uint8_t endpoint;
} curtain_type_get_t;

typedef struct {
  uint8_t op;
  uint8_t endpoint;
  uint8_t cz_type;
} curtain_type_rsp_t;

typedef struct {
  uint32_t limit_time;
} limit_time_set_t;

typedef struct {
  uint8_t op;
  uint8_t mode;
  uint32_t limit_time_ms;
} __attribute__((packed)) limit_time_rsp_t;

enum { BIT_LED_INTENSITY, BIT_LOCK_DEV, BIT_GLASS_TYPE, BIT_RELAY_TYPE };

enum {
  GET_LIMIT_TIME = 0,
  CHANGE_TO_LEARN_MODE = 1,
  EXIT_LEARN_MODE = 2,
  SET_LIMIT_TIME = 3
};

#define CONFIG_ALL_MASK 0x0F

enum { LIGHT_LOW = 0, LIGHT_HIGH = 1, INTENSITY_UNKNOWN };

enum {
  CURTAIN_CMD_IDLE = 0,
  CURTAIN_CMD_STOP = 1,
  CURTAIN_CMD_OPEN = 2,
  CURTAIN_CMD_CLOSE = 3,
};
typedef uint8_t CurtainCmd_enum;

enum {
  RUNNING_NORMAL = 0,
  LEARN_LIMIT_TIME = 1,
};
typedef uint8_t CurtainMode_enum;

enum {
  CURTAIN_STATE_IDLE = 0,
  CURTAIN_STATE_STOP = 1,
  CURTAIN_STATE_OPEN = 2,
  CURTAIN_STATE_CLOSE = 3,
  CURTAIN_STATE_WAIT_NEXT_CMD = 4,
};
typedef uint8_t CurtainState_enum;

enum {
  CURTAIN_CONTROL_ID_STOP = 1,
  CURTAIN_CONTROL_ID_RUN = 2,
};
typedef uint8_t CurtainControlId_enum;

enum {
  CURTAIN_0 = 0,
  CURTAIN_1 = 1,
};
typedef uint8_t CurtainNumber_enum;

enum {
  EX_ST_START_OPEN = 0,
  EX_ST_OPENNING = 1,
  EX_ST_OPENNED = 2,
  EX_ST_START_CLOSE = 3,
  EX_ST_CLOSING = 4,
  EX_ST_CLOSED = 5,
  EX_ST_STOP = 6,
  EX_ST_STOPED = 7,
  EX_ST_UNKNOWN = 8,
};
typedef uint8_t ExternCutainState_enum;

enum {
  MIN_STEP = 0,
  SEND_STEP_1 = 1,
  SEND_STEP_2 = 2,
  SEND_STEP_3 = 3,
  SEND_STEP_4 = 4,
  SEND_STEP_5 = 5,
  SEND_STEP_6 = 6,
  SEND_STEP_7 = 7,
  MAX_STEP = 8,
  SEND_STEP_NULL = 9,
};
typedef uint8_t CurtainSendStep_enum;

typedef struct {
  /*-------------Curtain Mode And State------------*/
  CurtainMode_enum curtainMode;
  CurtainState_enum curtainState;
  /*-------------Curtain Command-------------------*/
  CurtainState_enum currentCmd;
  CurtainState_enum bufferCmd;
  uint8_t cmdStep;
  uint32_t startDelayTimeForCmdStep;
  /*-------------Curtain Parameter----------------*/
  uint32_t curtainLimitTime;
  uint32_t currentPosition;
  uint32_t startPosition;
  uint32_t curtainRunningStartTime;
  uint32_t destinationPositionToGoTo;
  uint32_t tempDestinationPositionToGoTo;
#ifdef HOLD_FOR_TOUCH
  uint8_t enableControlCurtain;
#endif
  CurtainSendStep_enum sendStep;
  uint8_t enableCalculatorCurtainPosition;
  ExternCutainState_enum extended_state;
} Curtain_str;

typedef struct {
  uint8_t devNo;
  uint8_t calipCommandId;
  uint32_t limitTime;
} CalipCurtainLimitTime_str;

enum {
  _MIN_POSITION = 0x00,
  _MAX_POSITION = 0xFF,
};
typedef uint8_t PositionPerChar_enum;

enum {
  CURRENT_CMD_HANDLE_NOT_DONE = 0,
  CURRENT_CMD_HANDLE_DONE = 1,
  CURRENT_CMD_HANDLE_ERROR = 2,
};
typedef uint8_t CurrentCmdHandler_enum;

enum {
  OFF = 0,
  ON = 1,
};

#define LIMIT_TIME_DEFAULT_MS TIMER_20S

#define DESTINATION_NULL 0xFFFFFFFF
#define CURTAIN_CORRECTION_ERROR_VAL 0x02u
#define CZ_EXTENDED_STATE 1

#define KEY_FLASH 0x00

typedef struct {
  uint8_t present_level;
  uint8_t target_level;
  uint8_t state;
} curtain_level_t;

enum {
  ST_START_OPEN = 0,
  ST_OPENNING = 1,
  ST_OPENNED = 2,
  ST_START_CLOSE = 3,
  ST_CLOSING = 4,
  ST_CLOSED = 5,
  ST_STOP = 6,
  ST_STOPED = 7,
  ST_UNKNOWN = 8,
};
typedef uint8_t CutainState_enum;

#define DEFAULT_PULL_DELAY 300 // ms

typedef void (*typeCurtain_updateCurtainCurrentPosition)(uint8_t idx,
                                                         uint8_t pos);

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/
void curtain_config_auto_send_task(void);
void curtain_setup_enable_send_config_delay(bool en, bool join_flag);

void curtain_set_level(uint8_t idx, uint8_t pos);
uint8_t curtain_get_target_position(uint8_t idx);
uint8_t curtain_get_curret_level(uint8_t CT_No);
void curtain_get_position_through_uart(uint8_t idx);
void curtain_update_present_level(uint8_t idx, uint8_t level);
void curtain_response_st_to_gateway(uint8_t idx);
void curtain_config_init(void);
void curtain_config_set_params(uint8_t type, int val);
int curtain_cfg_set_mcu_opt(int idx, uint8_t *par, int par_len, uint8_t cmd);
int curtain_cfg_get_mcu_opt(int idx, uint8_t *par, int par_len, uint8_t cmd);
int curtain_config_handle_response_from_mcu(uint8_t *par, int par_len);
void curtain_reset_all_device_config(void);

int cz_config_enable_auto_send(void);

void curtain_handle_refresh_led(uint16_t mask);
void curtain_handle_option_button_state(uint8_t st);

void curtain_control_curtain_by_app(CurtainNumber_enum CT_No,
                                 CurtainControlId_enum CT_CmdId,
                                 uint8_t positionPerChar);

void curtain_init(void);
void curtain_proc(void);

int curtain_cfg_handle_set_message(int model_idx, uint8_t *par, int par_len,
                                   uint8_t cmd, bool notify_led_en);
int curtain_cfg_handle_get_message(int model_idx, uint8_t *par, int par_len,
                                   uint8_t cmd);

void curtain_callback_init(typeCurtain_updateCurtainCurrentPosition func);
#endif /* CURTAIN_H_ */
