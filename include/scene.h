#ifndef SCENE_H__
#define SCENE_H__

#include <stdint.h>
#include <zephyr/bluetooth/mesh.h>

#include "app_device.h"

#define SCENE_CNT_MAX 16

typedef struct {
  uint16_t id;
  uint16_t lightness_s16; // Use 16-bit to match OnOff/Lightness
  uint16_t level;
} scene_data_t;

typedef struct {
  scene_data_t data[NUMBER_INPUT][SCENE_CNT_MAX];
} model_scene_t;

/* scene_proc_t moved to vendor_model.h or unused */

/* Externs */
extern const struct bt_mesh_model_op my_scene_op[];
extern struct bt_mesh_model_pub my_scene_pub;
extern model_scene_t my_model_scene;

typedef uint8_t (*typeScene_handleGetTargetLevel)(uint8_t);
typedef void (*typeScene_setUpSceneResponseDelay)(uint8_t model_idx,
                                                  uint16_t dst_adr, uint8_t st);
typedef void (*typeScene_handleSetLevel)(uint8_t idx, uint8_t level);

/* API - with address params for response, ack controls whether to send response
 */
int my_scene_recall(uint8_t elem_idx, uint16_t scene_id, uint16_t addr_src,
                    uint16_t addr_dst, bool ack);
int my_scene_store_handler(uint8_t elem_idx, uint16_t scene_id,
                           uint16_t addr_src, uint16_t addr_dst, bool ack);
int my_scene_delete_handler(uint8_t elem_idx, uint16_t scene_id,
                            uint16_t addr_src, uint16_t addr_dst, bool ack);
uint16_t scene_get_current(uint8_t idx);
int mesh_tx_cmd_scene_reg_st(uint8_t idx, uint16_t ele_adr, uint16_t dst_adr,
                             uint8_t st);
int mesh_tx_cmd_scene_st(uint8_t idx, uint16_t ele_adr, uint16_t dst_adr,
                         uint8_t st);
void delete_all_scene(void);

void scene_callback_init(
    typeScene_handleGetTargetLevel handleGetTargetLevel,
    typeScene_setUpSceneResponseDelay setUpSceneResponseDelay,
    typeScene_handleSetLevel handleSetLevel);

#endif /* SCENE_H__ */
