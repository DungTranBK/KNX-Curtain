# Đánh Giá Tương Thích: KNX_Shutter POC ↔ KNX-Curtain

## 1. Kết Luận Tổng Quan

| Tiêu chí | Đánh giá | Ghi chú |
|---|---|---|
| **Tương thích tổng thể** | ✅ **CAO** | POC thiết kế theo pattern extern callback, dễ ghép |
| **Xung đột code** | ✅ **KHÔNG** | POC hoàn toàn tách biệt, không overlap file nào |
| **Bridge API** | ✅ **SẴN SÀNG** | `app.c` đã có bridge functions, chỉ thiếu implement extern callbacks |
| **Position format** | ⚠️ **CẦN CHUYỂN ĐỔI** | POC: 0–100%, Curtain: 0x00–0xFF |
| **Effort ước tính** | **~1–2 ngày** | Chủ yếu copy + bridge + build fix |

---

## 2. Chi Tiết Từng Khía Cạnh

### 2.1 ✅ Tương Thích Kiến Trúc — **Hoàn Hảo**

POC được thiết kế đúng theo pattern **extern callback interface**:

```
KNX Bus → knx_adapter.cpp → extern "C" callbacks → firmware rèm
```

KNX-Curtain đã có sẵn bridge functions trong `app.c`:

```
app_handle_control_curtain_from_knx(idx, cmd_id, position)
app_handle_curtain_update_level(idx, position)        ← TODO
app_get_curtain_current_position(idx, *position)
knx_ets_has_been_configured()                         ← TODO
```

**Mapping trực tiếp:**

| POC extern callback | → | KNX-Curtain bridge API |
|---|---|---|
| `app_knx_shutter_move(going_down)` | → | `app_handle_control_curtain_from_knx(0, STOP/RUN, pos)` |
| `app_knx_shutter_stop()` | → | `app_handle_control_curtain_from_knx(0, STOP, 0)` |
| `app_knx_shutter_set_position(pct)` | → | `app_handle_control_curtain_from_knx(0, RUN, pos)` |

### 2.2 ⚠️ Position Format — **Cần Chuyển Đổi**

Đây là khác biệt quan trọng nhất:

| | KNX_Shutter POC | KNX-Curtain |
|---|---|---|
| **Range** | 0–100 (%) | 0x00–0xFF (0–255) |
| **Fully open** | 0 | 0x00 |
| **Fully closed** | 100 | 0xFF |
| **Convention** | KNX DPT 5.001 | Telink position char |

**Công thức chuyển đổi:**
```c
// KNX (0-100%) → Curtain (0x00-0xFF)
uint8_t knx_to_curtain_pos(uint8_t knx_pct) {
    return (uint8_t)((uint16_t)knx_pct * 255 / 100);
}

// Curtain (0x00-0xFF) → KNX (0-100%)
uint8_t curtain_to_knx_pos(uint8_t curtain_pos) {
    return (uint8_t)((uint16_t)curtain_pos * 100 / 255);
}
```

### 2.3 ✅ Blind Type Mapping — **Trùng Hoàn Toàn**

| POC `knx_blind_type_t` | Curtain `CurtainType_enum` |
|---|---|
| `BLIND_TYPE_CUSTOM (0)` | — (custom, không map) |
| `BLIND_TYPE_HOZ_DZ3W (1)` | `HOZ_DZ3W (0)` |
| `BLIND_TYPE_HOZ_DZ4W (2)` | `HOZ_DZ4W (1)` |
| `BLIND_TYPE_HOZ_DT99 (3)` | `HOZ_DT99 (2)` |
| `BLIND_TYPE_HOZ_DZ3WP (4)` | `HOZ_DZ3WP (3)` |
| `BLIND_TYPE_VER_220 (5)` | `VER_220V (4)` |

> ⚠️ **Lưu ý:** Giá trị enum KHÔNG giống nhau (POC bắt đầu từ 1, Curtain từ 0). Cần conversion khi apply blind type từ ETS.

### 2.4 ✅ UART Overlay — **Đã Sẵn Sàng**

Cả 2 project dùng cùng UART00 config:
- **TX**: P2.7 (hoặc P2.8 tuỳ rev)
- **RX**: P2.8 (hoặc P2.7 tuỳ rev)
- Không cần thay đổi overlay.

### 2.5 ✅ Build System — **Không Xung Đột**

KNX-Curtain `CMakeLists.txt` hiện không có gì liên quan KNX. Chỉ cần **thêm** các block sau:
- KNX compile definitions
- Include directories cho `knx_stack/src` và `src/knx`
- Source files: `knx_adapter.cpp`, `knx_app_layer.cpp`, `knx_provision.c`
- KNX stack core (glob)

### 2.6 ✅ Include Files — **Không Xung Đột**

KNX-Curtain hiện KHÔNG có file nào trong `include/` trùng tên với POC:

| POC files cần copy | Trùng? |
|---|---|
| `knx_adapter.h` | ❌ Chưa có |
| `knx_mapping_config.h` | ❌ Chưa có |
| `knx_provision.h` (từ Actuator) | ❌ Chưa có |

### 2.7 ✅ ZBUS — **Sẵn Sàng**

POC dùng ZBUS cho inter-module communication:
- `chan_knx_rx`: KNX → App
- `chan_knx_tx`: App → KNX

KNX-Curtain chưa dùng ZBUS cho KNX → không xung đột.

---

## 3. Các Điểm Cần Xử Lý Khi Merge

### 3.1 Implement Extern Callbacks (trong `app.c`)

```c
// Implement 3 extern callbacks mà POC yêu cầu
void app_knx_shutter_move(bool going_down) {
    uint8_t pos = going_down ? 0xFF : 0x00;  // Full close / Full open
    app_handle_control_curtain_from_knx(0, CURTAIN_CONTROL_ID_RUN, pos);
}

void app_knx_shutter_stop(void) {
    app_handle_control_curtain_from_knx(0, CURTAIN_CONTROL_ID_STOP, 0);
}

void app_knx_shutter_set_position(uint8_t percent) {
    uint8_t curtain_pos = (uint8_t)((uint16_t)percent * 255 / 100);
    app_handle_control_curtain_from_knx(0, CURTAIN_CONTROL_ID_RUN, curtain_pos);
}
```

### 3.2 Implement Reverse Path (Curtain → KNX)

`app_handle_curtain_update_level()` hiện là TODO. Cần gọi:
```c
void app_handle_curtain_update_level(uint8_t curtain_idx,
                                     uint8_t current_position) {
    uint8_t knx_pct = (uint8_t)((uint16_t)current_position * 100 / 255);
    knx_send_position_status(knx_pct);
}
```

### 3.3 Implement `knx_ets_has_been_configured()`

```c
bool knx_ets_has_been_configured(void) {
    return knx_is_configured();  // delegate to adapter
}
```

### 3.4 Add `knx_adapter_init()` vào `main.c`

Trong `bt_ready()`, sau `curtain_init()`:
```c
knx_adapter_init();
```

### 3.5 Copy `knx_provision` từ Actuator

POC không có provisioning. Cần copy:
- `include/knx_provision.h`
- `src/knx/knx_provision.c`

Và integrate vào `knx_adapter_init()` để load FDSK/serial từ flash.

---

## 4. Risk Assessment

| Risk | Level | Mitigation |
|---|---|---|
| Position conversion rounding | **Thấp** | `255/100` và `100/255` — sai max 1 unit |
| Blind type enum mismatch | **Trung bình** | Thêm conversion function, test kỹ với từng type |
| Memory (RAM/Flash) | **Thấp** | KNX stack ~40KB flash, KNX-Curtain còn dư nhiều |
| UART conflict | **Rất thấp** | UART00 đã cấu hình, chỉ 1 consumer (KNX stack) |
| Boot timing | **Thấp** | POC đã fix boot order (NVS→readMemory→UART) |
| Scene + BLE Mesh scene conflict | **Trung bình** | KNX scenes và BLE scenes hoạt động independent, cần test không deadlock |

---

## 5. Tóm Tắt Effort

| Task | Effort |
|---|---|
| Copy knx_stack/ | 5 phút |
| Copy src/knx/ + headers | 10 phút |
| Update CMakeLists.txt | 15 phút |
| Implement bridge callbacks | 30 phút |
| Implement reverse path | 15 phút |
| Update main.c | 5 phút |
| Build fix + test | 1–2 giờ |
| **Tổng** | **~3–4 giờ** |
