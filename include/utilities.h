#ifndef UTILITIES_H__
#define UTILITIES_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define MAX_U32 0xFFFFFFFF
#define MAX_U16 0xFFFF
#define MAX_U8 0xFF

#define HI_U16(x) (uint8_t)(x >> 8)
#define LO_U16(x) (uint8_t)(x)

static inline uint32_t clock_time_ms(void) { return (uint32_t)k_uptime_get(); }

static inline bool clock_time_exceed_ms(uint32_t ref, uint32_t span_ms) {
  return ((uint32_t)k_uptime_get() - ref) > span_ms;
}

static inline uint32_t clock_time_get_elapsed_time(uint32_t start_time) {
  return (uint32_t)k_uptime_get() - start_time;
}

/** Convert seconds to milliseconds (32-bit safe) */
#define s_to_ms(s) ((uint32_t)(s) * 1000U)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define foreach(i, n) for (int i = 0; i < (n); ++i)
#define foreach_range(i, s, e) for (int i = (s); i < (e); ++i)
#define foreach_arr(i, arr) for (int i = 0; i < ARRAY_SIZE(arr); ++i)

#ifndef BOOL
#define BOOL unsigned char
#endif

#ifndef PROV_SUCCESS
#define PROV_SUCCESS 0x1
#define PROV_FAIL 0x0
#endif

#ifndef PROV_STEP_SUCCESS
#define PROV_STEP_SUCCESS 0x1
#define PROV_STEP_FAIL 0x0
#endif

#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE 1
#endif

enum ButtonState_enum {
  _RELEASE = 0,
  HOLD_3S = 1,
  HOLD_5S = 2,
  HOLD_10S = 3,

  PRESS_TWO_TIME = 4,
  PRESS_THREE_TIME = 5, // just use by MC
  PRESS_FOUR_TIME = 6,
  PRESS_FIVE_TIME = 7,
  PRESS_TEN_TIME = 8, // just use by MC

  HOLD_15S = 9,
  HOLD_7S = 10,
  HOLD_9S = 11,
  HOLD_12S = 12,

  PRESS_SIX_TIME = 13,
  PRESS_EIGHT_TIME = 14,  // just use by MC
  HOLD_50MS = 15,         // just use by MC
  HOLD_500MS = 16,        // just use by MC
  PRESS_TWELVE_TIME = 17, // just use by MC

  PRESS_ONE_TIME = 18,

  RL_AFTER_PRESS = 0xFB,

  HOLD_2S = 0xFC,
  HOLD_4S = 0xFD,

  STATE_UNKNOWN = 0xFE,
  SHORT_HOLD = 0xFD,

  START_PRESS = 0xFE,
  NO_PRESS = 0xFF
};
typedef uint8_t ButtonState_enum;

#define TIMER_10MS 10
#define TIMER_20MS 20
#define TIMER_25MS 25
#define TIMER_50MS 50
#define TIMER_100MS 100
#define TIMER_200MS 200
#define TIMER_300MS 300
#define TIMER_500MS 500

#define TIMER_1S 1000
#define TIMER_1S2 1200
#define TIMER_1S5 1500
#define TIMER_2S 2000
#define TIMER_3S 3000
#define TIMER_5S 5000
#define TIMER_6S 6000
#define TIMER_7S 7000
#define TIMER_9S 9000
#define TIMER_10S 10000
#define TIMER_15S 15000
#define TIMER_20S 20000
#define TIMER_30S 30000
#define TIMER_1Min 60000
#define TIMER_70S 70000
#define TIMER_5Min 300000
#define TIMER_15Min 900000

#ifndef BT_MESH_MODEL_ID_SCENE_SRV
#define BT_MESH_MODEL_ID_SCENE_SRV 0x1203
#endif

// 2IN_2OUT
#define VD_CONFIG_SW_SET_MAIN_PARAMS 0x10
#define VD_CONFIG_SW_SET_MAP_UNMAP_INPUT 0x11
#define VD_CONFIG_ALL_SWITCH_OPT 0x20
#define VD_CONFIG_SWITCH_MODE_OPT 0x21
#define VD_CONFIG_LED_INTENSITY_OPT 0x22
#define VD_CONFIG_LOCK_ALL_SWITCH_OPT 0x23
#define VD_CONFIG_GLASS_TYPE_OPT 0x24
#define VD_CONFIG_RELAY_TYPE_OPT 0x25
#define VD_CONFIG_CURTAIN_TYPE_OPT 0x26
#define VD_CONFIG_DIMMER_TYPE_OPT 0x27
#define VD_CONFIG_LOCK_BIT_SW_OPT 0x28
#define VD_CONFIG_GET_VERSION_MCU 0x29
#define VD_CONFIG_ON_POWER_UP_STATE 0x2A
#define VD_CONFIG_AUTO_LOCK_OPT 0x2B
#define VD_CONFIG_GROUP_ASSOCIATION 0x30
#define VD_EN_ON_OFF_GROUP_DEFAULT 0x31
#define VD_DIMMING_CONFIGURATION 0x32
#define VD_CONFIG_CURTAIN_LIMIT_TIME 0x40
#define VD_CONFIG_NETWORK_MODE 0x50
#define VD_CONFIG_LED_MASTER_CONTROL 0xF0
#define VD_CONFIG_TIMESTAMP 0xAC
#define VD_QUERY_TIMESTAMP 0x2E
#define VD_AUTO_LED_BRIGHTNESS 0xAD
#define VD_FACT_TEST_RF 0xFF
#define VD_OTA_BLE_MESH_SETUP 0xAF
#define VD_GET_DEVICE_STATE 0xB0
#define VD_TS_LOCK_SCHEDULE 0xC8
#define VD_OTA_MCU_SETUP 0xAE
#define VD_AUTO_LOCK_TOUCH_TYPE 0x36
#define VD_CONFIG_BACKLIGHT_BRIGHTNESS 0x53

/* Switch config opcodes used by sw_config.c */
#define VD_MAP_INPUT_OUTPUT_OPT 0x2C
#define VD_LINK_UNLINK_V2_OPT 0x2D
#define VD_CONFIG_SCENE_SWITCH_MODE 0x2E
#define VD_DEVICE_INFORMATION 0x2F

/* SIG Generic OnPowerUp opcodes */
#define G_ON_POWER_UP_GET 0x8211
#define G_ON_POWER_UP_SET 0x8213
#define G_ON_POWER_UP_SET_NOACK 0x8214
#define G_ON_POWER_UP_STATUS 0x8212

#define VD_CONFIG_SENSOR_FAST_REPORT 0xC2

#define CMD_MSG_SW_CONFIG_OPSET VD_CONFIG_ALL_SWITCH_OPT
#define PAR_LEN_MSG_SET_SW_MODE 0x2
#define PAR_LEN_MSG_SET_LED_INTENSITY 0x1
#define PAR_LEN_MSG_LOCK_SWITCH 0x1
#define PAR_LEN_MSG_LOCK_BIT_SWITCH 0x2
#define PAR_LEN_MSG_SET_TIMESTAMP 0x4
#define PAR_LEN_MSG_SET_ON_PW_UP_STATE 0x02

typedef struct {
  uint8_t evt;
  uint32_t update_st_t;
  BOOL hold_500ms_flag;
} btn_evt_t;

typedef struct {
  BOOL en;
  uint32_t start_time;
  uint16_t time_len;
} send_brightness_delay_t;

enum { B_START_PRESS, B_HOLD_500MS, B_RELEASE, B_ST_UNKNOWN };

enum { TYPE_CTL_LIGHTNESS = 0, TYPE_CTL_ON_OFF = 1, TYPE_RSV };

enum dim_ud_present_enum { UD_UP, UD_DOWN, UD_UNKNOWN };

enum control_direction_t {
  DIR_UP,
  DIR_DOWN,
  DIR_UP_SMOOTH,
  DIR_DOWN_SMOOTH,
  DIR_STOP,
  DIR_UP_DOWN_SMOOTH,
  DIR_UP_SMOOTH_LIMIT,
  DIR_DOWN_SMOOTH_LIMIT,
  DIR_RSV
};

enum src_control_enum {
  SRC_DEVICE,
  SRC_APP,
  SRC_BINDING,
  SRC_UNKNOWN,
};
typedef uint8_t src_control_enum;

typedef struct {
  uint16_t model_id;
  uint8_t dir;
  uint16_t step;
  uint8_t trans_t_ms;
} __attribute__((packed)) vd_up_down_control_t;

typedef struct {
  uint8_t onoff;
  uint8_t tid;
  uint8_t transit_t;
  uint8_t delay;
} __attribute__((packed)) mesh_cmd_g_onoff_set_t;

typedef struct {
  uint16_t lightness;
  uint8_t tid;
  uint8_t transit_t;
  uint8_t delay;
} __attribute__((packed)) mesh_cmd_lightness_set_t;

typedef struct {
  uint8_t type;
  uint8_t level;
  uint8_t tid;
  uint8_t transit_t;
  uint8_t delay;
} vd_cmd_g_level_set_t;

#define LEVEL_MIN (-32767)
#define LEVEL_MAX (32767)
#define LIGHTNESS_MIN (1) // can not set 0
#define LIGHTNESS_MAX (0xFFFF)
#define CTL_TEMP_MIN (0x0320) // 800
#define CTL_TEMP_MAX (0x4E20) // 20000
#define CTL_D_UV_MIN (-32768)
#define CTL_D_UV_MAX (32767)
#define HSL_HUE_MIN (0)
#define HSL_HUE_MAX (0xFFFF)
#define HSL_SAT_MIN (0)
#define HSL_SAT_MAX (0xFFFF)
#define XYL_X_MIN (0)
#define XYL_X_MAX (0xFFFF)
#define XYL_Y_MIN (0)
#define XYL_Y_MAX (0xFFFF)

#define LEVEL_OFF (-32768)
#define LUM_OFF (0)

#define SIG_MD_LIGHTNESS_S 0x1300
#define SIG_MD_LIGHT_CTL_TEMP_S 0x1306
#define SIG_MD_LIGHT_HSL_HUE_S 0x1307

/* SIG opcode*/
#define G_ONOFF_GET 0x8201
#define G_ONOFF_SET 0x8202
#define G_ONOFF_SET_NOACK 0x8203
#define G_ONOFF_STATUS 0x8204

/* Generic OnOff state values (Telink mesh compat) */
#define G_OFF 0
#define G_ON 1
#define G_ONOFF_RSV 0x02
#define SUB_UNKNOWN 0xFF

#define LIGHT_HSL_SET 0x8276
#define LIGHT_HSL_SET_NOACK 0x8277
#define LIGHT_CTL_SET 0x825E
#define LIGHT_CTL_SET_NOACK 0x825F
#define LIGHT_CTL_TEMP_SET 0x8264
#define LIGHT_CTL_TEMP_SET_NOACK 0x8265
#define LIGHTNESS_SET 0x824C
#define LIGHTNESS_SET_NOACK 0x824D
#define G_LEVEL_SET 0x8206
#define G_LEVEL_SET_NOACK 0x8207
#define G_LEVEL_STATUS 0x8208

#ifndef START_VENDOR_OPCODE
#define START_VENDOR_OPCODE 0xC0
#endif
#define STATUS_NONE (0xffffffff)

#define DEFAULT_TRANS_TIME_FOR_DIMMING (0x0A)

static inline uint16_t s16_to_u16(int16_t val) { return (val + 32768); }

static inline int16_t u16_to_s16(uint16_t val) { return (val - 32768); }
#endif /* UTILITIES_H__ */