# BLE_NORDIC SDK - Adding New Models Guide

## 📋 Tổng Quan

BLE_NORDIC SDK được thiết kế để **dễ dàng thêm model mới** nhờ vào hàm `bind_all_models_with_appkey()` tự động bind tất cả models. Bạn chỉ cần:

1. **Định nghĩa model** trong `elements[]` array
2. **Implement handlers** cho model (nếu cần)
3. **Không cần sửa code bind** - tự động bind khi provision

---

## 🎯 Các Loại Model Có Thể Thêm

### 1. **SIG Models (Bluetooth Mesh Standard Models)**
- Generic OnOff, Generic Level, Generic Power OnOff, etc.
- Light Lightness, Light CTL, Light HSL, etc.
- Sensor, Time, Scene, etc.

### 2. **Vendor Models (Custom Models)**
- Fast Provision model (đã có)
- Custom vendor models với Company ID riêng

---

## 📝 Cách Thêm Model Mới

### **Bước 1: Thêm Model vào `elements[]` Array**

Mở file `src/main.c`, tìm đến dòng 362-374:

```c
static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(
        0,  // Element index
        BT_MESH_MODEL_LIST(
            // SIG Models
            BT_MESH_MODEL_CFG_SRV,
            BT_MESH_MODEL_CFG_CLI(&cfg_cli),
            BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
            BT_MESH_MODEL_HEALTH_CLI(&health_cli),
            BT_MESH_MODEL_ONOFF_SRV(&onoff_srv),
            BT_MESH_MODEL_ONOFF_CLI(&onoff_cli),
            
            // ✅ THÊM MODEL MỚI Ở ĐÂY
            // Ví dụ: Light Lightness Server
            BT_MESH_MODEL_LIGHTNESS_SRV(&lightness_srv),
            // Ví dụ: Sensor Server
            BT_MESH_MODEL_SENSOR_SRV(&sensor_srv),
        ),
        vnd_models  // Vendor models
    ),
};
```

### **Bước 2: Khai Báo Model Instance và Handlers**

Thêm vào phần **GLOBAL VARIABLES** (sau dòng 65):

```c
/* Light Lightness Server model instance */
static struct bt_mesh_lightness_srv lightness_srv = 
    BT_MESH_LIGHTNESS_SRV_INIT(&lightness_handlers);

/* Light Lightness handlers */
static struct bt_mesh_lightness_srv_handlers lightness_handlers = {
    .light_set = lightness_set_handler,
    .light_get = lightness_get_handler,
};

/* Handler functions */
static void lightness_set_handler(struct bt_mesh_lightness_srv *srv,
                                  struct bt_mesh_msg_ctx *ctx,
                                  const struct bt_mesh_lightness_set *set,
                                  struct bt_mesh_lightness_status *rsp)
{
    // Implement logic here
    LOG_INF("Lightness set to %d", set->lvl);
}

static void lightness_get_handler(struct bt_mesh_lightness_srv *srv,
                                  struct bt_mesh_msg_ctx *ctx,
                                  struct bt_mesh_lightness_status *rsp)
{
    // Implement logic here
    rsp->current = 0x8000;  // 50% brightness
}
```

### **Bước 3: Thêm Vendor Model (Nếu Cần)**

#### 3.1. Định nghĩa Opcodes trong `include/vendor.h`:

```c
/* Custom Vendor Model Opcodes */
#define VD_MESH_CUSTOM_OP_1    BT_MESH_MODEL_OP_3(0xD0, VENDOR_COMPANY_ID_TELINK)
#define VD_MESH_CUSTOM_OP_2    BT_MESH_MODEL_OP_3(0xD1, VENDOR_COMPANY_ID_TELINK)

/* Custom Vendor Model ID */
#define BT_MESH_MODEL_ID_VND_CUSTOM  0x0001
```

#### 3.2. Định nghĩa Model Macro trong `include/fast_provision.h` (hoặc tạo file mới):

```c
#define BT_MESH_MODEL_CUSTOM_SRV(_custom_ctx) \
    BT_MESH_MODEL_VND_CB(VENDOR_COMPANY_ID_TELINK, \
                         BT_MESH_MODEL_ID_VND_CUSTOM, \
                         custom_op, \
                         NULL, \
                         BT_MESH_MODEL_USER_DATA(custom_par_t, _custom_ctx), \
                         &custom_cb)
```

#### 3.3. Thêm vào `vnd_models[]` array (dòng 357-359):

```c
static const struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_FAST_PROV_SRV(&fast_prov_ctx),
    BT_MESH_MODEL_CUSTOM_SRV(&custom_ctx),  // ✅ THÊM MODEL MỚI
};
```

#### 3.4. Implement Handlers trong file riêng hoặc `main.c`:

```c
/* Custom model handlers */
static int handle_custom_op_1(const struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf)
{
    LOG_INF("Received custom opcode 1");
    // Process message
    return 0;
}

/* Opcode handlers array */
static const struct bt_mesh_model_op custom_op[] = {
    { VD_MESH_CUSTOM_OP_1, BT_MESH_LEN_EXACT(0), handle_custom_op_1 },
    { VD_MESH_CUSTOM_OP_2, BT_MESH_LEN_EXACT(4), handle_custom_op_2 },
    BT_MESH_MODEL_OP_END,
};
```

---

## ✅ Tự Động Bind AppKey

**QUAN TRỌNG:** Bạn **KHÔNG CẦN** sửa code bind AppKey! 

Hàm `bind_all_models_with_appkey()` tự động:
- ✅ Bind tất cả **SIG models** trong tất cả elements
- ✅ Bind tất cả **vendor models** trong tất cả elements
- ✅ Chạy khi Fast Provision hoàn thành
- ✅ Chạy khi Normal Provision nhận AppKey Add message

**Code bind tự động quét tất cả models trong composition data:**

```c
void bind_all_models_with_appkey(uint16_t app_idx)
{
    const struct bt_mesh_comp *comp = bt_mesh_comp_get();
    if (comp) {
        for (int elem_idx = 0; elem_idx < comp->elem_count; elem_idx++) {
            const struct bt_mesh_elem *elem = &comp->elem[elem_idx];
            
            // Tự động bind tất cả SIG models
            for (int i = 0; i < elem->model_count; i++) {
                // ... bind logic
            }
            
            // Tự động bind tất cả vendor models
            for (int i = 0; i < elem->vnd_model_count; i++) {
                // ... bind logic
            }
        }
    }
}
```

---

## 📊 So Sánh với Telink

| Khía Cạnh | Telink | Nordic (Code Hiện Tại) |
|-----------|--------|------------------------|
| **Thêm SIG Model** | Thêm vào `md_sig[]` array | Thêm vào `BT_MESH_MODEL_LIST()` |
| **Thêm Vendor Model** | Thêm vào `md_vendor[]` array | Thêm vào `vnd_models[]` array |
| **Bind AppKey** | Gọi `appkey_bind_all()` | **Tự động** qua `bind_all_models_with_appkey()` |
| **Cần Sửa Code Bind?** | ❌ Không (tự động) | ❌ **Không** (tự động) |
| **Độ Dễ Dàng** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## 🎯 Ví Dụ Thực Tế: Thêm Light Lightness Model

### **1. Thêm Include (nếu chưa có):**

```c
#include <bluetooth/mesh/models.h>  // Đã có sẵn
```

### **2. Khai Báo Model Instance:**

```c
/* Light Lightness Server */
static struct bt_mesh_lightness_srv lightness_srv = 
    BT_MESH_LIGHTNESS_SRV_INIT(&lightness_handlers);

static struct bt_mesh_lightness_srv_handlers lightness_handlers = {
    .light_set = lightness_set_handler,
    .light_get = lightness_get_handler,
};
```

### **3. Implement Handlers:**

```c
static void lightness_set_handler(struct bt_mesh_lightness_srv *srv,
                                  struct bt_mesh_msg_ctx *ctx,
                                  const struct bt_mesh_lightness_set *set,
                                  struct bt_mesh_lightness_status *rsp)
{
    LOG_INF("Lightness set: level=%d, transition_time=%d", 
            set->lvl, set->transition ? set->transition->time : 0);
    
    // Update hardware
    // ...
    
    // Update response
    rsp->current = set->lvl;
    rsp->target = set->lvl;
}

static void lightness_get_handler(struct bt_mesh_lightness_srv *srv,
                                  struct bt_mesh_msg_ctx *ctx,
                                  struct bt_mesh_lightness_status *rsp)
{
    // Read current state from hardware
    rsp->current = 0x8000;  // 50%
    rsp->target = 0x8000;
}
```

### **4. Thêm vào `elements[]`:**

```c
BT_MESH_MODEL_LIST(
    // ... existing models ...
    BT_MESH_MODEL_LIGHTNESS_SRV(&lightness_srv),  // ✅ THÊM DÒNG NÀY
),
```

### **5. Xong! Model sẽ tự động được bind khi provision.**

---

## ⚠️ Lưu Ý Quan Trọng

### **1. Model Phải Có Keys Array**
- Mỗi model phải có `keys[]` array với `keys_cnt > 0`
- Nordic SDK tự động cấp phát, không cần lo lắng

### **2. Element Address**
- Models trong cùng element có cùng element address
- Element 0 = primary address
- Element 1 = primary address + 1, v.v.

### **3. Composition Data**
- Khi thêm model, composition data tự động cập nhật
- Provisioner sẽ nhận được composition data mới khi scan

### **4. AppKey Binding**
- ✅ **Tự động** - không cần sửa code
- ✅ Chạy sau khi add AppKey thành công
- ✅ Bind tất cả models trong tất cả elements

---

## 🚀 Kết Luận

### **Độ Dễ Dàng: ⭐⭐⭐⭐⭐ (Rất Dễ)**

**Lý do:**
1. ✅ **Tự động bind** - không cần sửa code bind
2. ✅ **Cấu trúc rõ ràng** - chỉ cần thêm vào `elements[]` array
3. ✅ **Tương thích Telink** - logic bind giống Telink `appkey_bind_all()`
4. ✅ **Hỗ trợ cả SIG và Vendor models**
5. ✅ **Không cần thay đổi code hiện có**

**Chỉ cần:**
- Thêm model vào `elements[]` hoặc `vnd_models[]`
- Implement handlers (nếu cần)
- **Xong!** Model sẽ tự động được bind khi provision

---

## 📚 Tài Liệu Tham Khảo

- Nordic nRF Connect SDK Mesh Models: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/libraries/bluetooth_services/mesh/mesh_models.html
- Bluetooth Mesh Specification: https://www.bluetooth.com/specifications/specs/core-specification/
- Telink Mesh SDK: `sig_mesh_sdk_v3.1.0__LM_RADAR_ENTRANCE_BLE/vendor/common/`

