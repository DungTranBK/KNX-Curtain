#ifndef VENDOR_MODEL_H__
#define VENDOR_MODEL_H__

#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#include "vendor.h"

//------------------vendor --------------------
extern bool vd_config_model_sub_set_flag;
extern bool vd_scene_request_flag;

enum VendorSceneRequest_enum {
  VD_SCENE_STORE = 0x00,
  VD_SCENE_RECALL = 0x01,
  VD_SCENE_DEL = 0x02,
  VD_SCENE_GET = 0x03,
  VD_SCENE_REG_GET = 0x04,
};
typedef uint8_t VendorSceneRequest_t;

enum VendorSceneRequestNoAck_enum {
  VD_SCENE_STORE_NOACK = 0x00,
  VD_SCENE_RECALL_NOACK = 0x01,
  VD_SCENE_DEL_NOACK = 0x02,
};
typedef uint8_t VendorSceneRequestNoAck_t;

enum VendorSceneResponse_enum {
  VD_SCENE_STATUS = 0x00,
  VD_SCENE_REG_STATUS = 0x01,
};
typedef uint8_t VendorSceneResponse_t;

#define MC_OPT_RESPONSE_LEN_MAX 64
#define SCENE_CNT_MAX 16

/* Structs already added */
#include "scene.h"

typedef struct {
  uint8_t msg_type;
  uint8_t st;
  uint16_t current_id;
  uint16_t id[SCENE_CNT_MAX];
} vd_scene_reg_status_t;

/* scene_data_t and model_scene_t moved to scene.h */
/* scene_proc_t defined in scene.c */

typedef struct {
  uint8_t msg_type;
  uint8_t st;
  uint16_t current_id;
  uint16_t target_id;
  uint8_t remain_t;
} vd_scene_status_t;

/* Externs moved to scene.h */

typedef struct {
  uint8_t code;
  uint8_t data;
} vd_config_node_t;

typedef struct {
  uint8_t ttl;
} vendor_config_ttl_set_t;

#define VD_CONFIG_NODE_RST 0x00
#define VD_CONFIG_SET_TTL 0x01
#define VD_CONFIG_GET_TTL 0x02

/* 0xF8 */
#define CONFIG_NODE_SET 0x00
#define CONFIG_NODE_GET 0x01
#define CONFIG_NODE_QUERY 0x02

// bind locally

typedef struct {
  uint16_t pid;
  uint8_t f_ctt;
  uint8_t state;
  uint16_t lightness;
  uint16_t temp;
  uint16_t hue;
  uint16_t sat;
  uint8_t action;
} light_state_st_t;

typedef struct {
  uint8_t sub_type;
  uint16_t ele_adr;
  uint16_t sub_adr;
  union {
    uint8_t sig_model[2];
    uint8_t vd_model[4];
    uint8_t *model;
  };
} vd_config_model_sub_set_t;

typedef struct {
  uint16_t ele_adr;
  uint16_t sub_adr;
  union {
    uint8_t vd_model[4];
    uint8_t sig_model[2];
    uint8_t *model;
  };
} vd_config_model_sub_add_del_and_over_write_t;

typedef struct {
  uint16_t ele_adr;
  union {
    uint8_t sig_model[2];
    uint8_t vd_model[4];
    uint8_t *model;
  };
} vd_config_model_sub_del_all_t;

typedef struct {
  uint16_t ele_adr;
  uint8_t sig_model[2];
} vd_config_model_sub_get_t;

typedef struct {
  uint8_t status;
  uint16_t sub_adr;
} vd_config_model_sub_status;

enum vd_config_model_sub_type_enum {
  VD_CFG_MODEL_SUB_ADD = 0,
  VD_CFG_MODEL_SUB_DEL = 1,
  VD_CFG_MODEL_SUB_OVERWRITE = 2,
  VD_CFG_MODEL_SUB_DEL_ALL = 3,
  VD_CFG_MODEL_SUB_GET = 4,
  VD_CFG_MODEL_SUB_STATUS = 5,
  VD_CFG_MODEL_SUB_UNKNOWN
};
typedef uint8_t vd_config_model_sub_type_t;

typedef struct {
  uint8_t msg_type;
  uint8_t code;
  uint8_t data[10];
} vd_config_node_ack_t;

typedef struct {
  uint8_t id_led;
  uint16_t led_mask;
  uint8_t state;
  uint8_t blink_times;
  uint8_t last_state;
} led_master_control_t;

typedef struct {
  bool flag;
  uint32_t delay_start_t_ms;
  uint32_t delay_t_ms;
} vd_send_on_power_up_delay_t;

enum { CMD_CODE_MCU, CMD_CODE_BLE_MESH, CMD_CODE_UNKNOWN };

/**
 * @brief Send VD_CONFIG_NODE_STATUS (0xF9)
 *
 * @param dst_addr Destination address (0 for publish)
 * @param src_addr Source element address
 * @param data     Payload data
 * @param len      Payload length
 * @return 0 on success
 */
int vendor_model_send_config_status(uint16_t src_addr, uint16_t dst_addr,
                                    uint8_t *data, size_t len);

/* Status Codes (Telink Compatible) */
#define ST_SUCCESS 0x00
#define ST_INVALID_ADR 0x01
#define ST_INVALID_MODEL 0x02
#define ST_FEATURE_NOT_SUP 0x03
#define ST_CAN_NOT_SET 0x04
#define ST_UNSPEC_ERR 0x05

/* Address Constants */
#define ADR_GROUP_START_POINT 0xC000

/* Expoesed Operations */
typedef struct {
  uint16_t adr_src;
  uint16_t adr_dst;
  uint8_t *model;
  uint8_t model_idx;
  uint8_t *op;
} mesh_cb_fun_par_t; // Common Mesh Callback Parameter

extern const struct bt_mesh_model_op config_node_op[];

int mesh_tx_cmd_rsp(uint32_t opcode, uint8_t *par, uint32_t len,
                    uint16_t adr_src, uint16_t adr_dst, uint8_t *uuid,
                    void *model);

/* Sensor Constants and Structs */

/* Callback function type for vendor handle */
typedef int (*vendor_handle_func_t)(uint8_t *par, int par_len,
                                    mesh_cb_fun_par_t *cb_par);

void vendor_handle_func_callback_init(vendor_handle_func_t func);

/* Sensor Report Function */
int mesh_tx_sensor_st_rsp(uint16_t ele_adr, uint16_t dst_adr, uint16_t op_rsp,
                          uint16_t prop_id, uint8_t *raw_value, uint8_t len);

void scene_reg_response_delay_init(uint8_t idx, uint16_t dst_adr, uint8_t st);
void vendor_model_proc(void);

#endif /* VENDOR_MODEL_H__ */
