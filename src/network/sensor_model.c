
#include "../../include/sensor_model.h"

#include <bluetooth/mesh/sensor.h>
#include <bluetooth/mesh/sensor_srv.h>
#include <bluetooth/mesh/sensor_types.h>
#include <stddef.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "../../include/sensor.h"
#include "../../include/vendor_model.h"

LOG_MODULE_DECLARE(vendor_model);

/*
 * Custom Sensor Definitions
 * Matching app.c requirements: 1-byte raw data, specific scaling.
 */

/* Shared format for 1-byte data */
static const struct bt_mesh_sensor_format format_u8 = {
    .size = 1,
    .cb = NULL,
};

/* Shared format for 2-byte data */
static const struct bt_mesh_sensor_format format_u16 = {
    .size = 2,
    .cb = NULL,
};

static const struct bt_mesh_sensor_channel channel_u16[] = {
    {.format = &format_u16},
};

static const struct bt_mesh_sensor_channel channel_u8[] = {
    {.format = &format_u8},
};

static const struct bt_mesh_sensor_type type_app_temp = {
    .id = TEMP_PROPERTY_ID,
    .channel_count = 1,
    .channels = channel_u16,
};

static const struct bt_mesh_sensor_type type_app_humi = {
    .id = HUMI_PROPERTY_ID,
    .channel_count = 1,
    .channels = channel_u8,
};

/* Helper to send response and suppress default behavior */
static int send_manual_rsp(struct bt_mesh_msg_ctx* ctx, uint16_t prop_id,
                           uint8_t* val, uint8_t len) {
  mesh_tx_sensor_st_rsp(ctx->recv_dst, ctx->addr, SENSOR_STATUS, prop_id, val,
                        len);
  return -1; /* Suppress default response */
}

/* Sensor Get Handlers */
static int temp_get(struct bt_mesh_sensor_srv* srv,
                    struct bt_mesh_sensor* sensor, struct bt_mesh_msg_ctx* ctx,
                    struct bt_mesh_sensor_value* rsp) {
  /* x10 resolution (0.1 deg C) -> uint16_t */
  uint16_t val = sensor_get_temp();
  uint8_t val_bytes[2];
  sys_put_le16(val, val_bytes);
  return send_manual_rsp(ctx, TEMP_PROPERTY_ID, val_bytes, 2);
}

static int humi_get(struct bt_mesh_sensor_srv* srv,
                    struct bt_mesh_sensor* sensor, struct bt_mesh_msg_ctx* ctx,
                    struct bt_mesh_sensor_value* rsp) {
  /* Humidity is already 1% resolution */
  uint8_t val = sensor_get_humi();
  return send_manual_rsp(ctx, HUMI_PROPERTY_ID, &val, 1);
}

/* Sensor Descriptors */
static struct bt_mesh_sensor temp_sensor = {
    .type = &type_app_temp,
    .get = temp_get,
};

static struct bt_mesh_sensor humi_sensor = {
    .type = &type_app_humi,
    .get = humi_get,
};

/* Sensor Server Instances */
static struct bt_mesh_sensor* temp_sensor_ptrs[] = {
    &temp_sensor,
};

static struct bt_mesh_sensor* humi_sensor_ptrs[] = {
    &humi_sensor,
};

struct bt_mesh_sensor_srv sensor_srv_temp =
    BT_MESH_SENSOR_SRV_INIT(temp_sensor_ptrs, ARRAY_SIZE(temp_sensor_ptrs));

struct bt_mesh_sensor_srv sensor_srv_humi =
    BT_MESH_SENSOR_SRV_INIT(humi_sensor_ptrs, ARRAY_SIZE(humi_sensor_ptrs));
