# Kiến Trúc KNX-Curtain (BLE Mesh)

> Project: `/home/iot/Lumi/KNX_Product/Nordic/KNX-Curtain`
> Platform: nRF54L15 / Zephyr RTOS / BLE Mesh

## 1. Tổng Quan

KNX-Curtain là firmware điều khiển rèm trên nền BLE Mesh (Zephyr). Hiện tại **chỉ hỗ trợ BLE Mesh**, chưa tích hợp KNX bus. Tuy nhiên, code đã được chuẩn bị sẵn **bridge API** cho KNX ở tầng `app.c`.

### Sơ Đồ Layer

```
┌─────────────────────────────────────────────────┐
│                   main.c                        │
│  (init BLE stack, hardware, modules)            │
├─────────────────────────────────────────────────┤
│                   app.c                         │
│  (button logic, bridge API cho KNX, LED mgmt)   │
├──────────────────┬──────────────────────────────┤
│   curtain.c      │       net_message.c          │
│  (motor control  │  (BLE Mesh on/off/level msg) │
│   position       │                              │
│   NVS persist)   │                              │
├──────────────────┼──────────────────────────────┤
│   relay.c        │    vendor_model.c            │
│  (latching GPIO  │  (vendor opcode, config,     │
│   pulse timer)   │   scene, binding, provision) │
├──────────────────┴──────────────────────────────┤
│  Network Layer: mesh_node.c, network.c,         │
│  fast_provision.c, normal_provision.c           │
├─────────────────────────────────────────────────┤
│  Support: led.c, button.c, watchdog_process.c,  │
│  execution_scene.c, sw_binding.c, at24c02.c     │
└─────────────────────────────────────────────────┘
```

## 2. Module Chính

### 2.1 `curtain.c` — Điều Khiển Rèm (1689 lines)

Core logic điều khiển motor rèm:

| Tính năng | Chi tiết |
|---|---|
| **Curtain Types** | `HOZ_DZ3W`, `HOZ_DZ4W`, `HOZ_DT99`, `HOZ_DZ3WP`, `VER_220V` |
| **Position** | 0x00–0xFF (0=fully open, 0xFF=fully closed) |
| **State Machine** | IDLE → OPEN/CLOSE → STOP, với buffer command |
| **NVS Persistence** | Zephyr Settings API (`curtain/pos`, `curtain/opt`) |
| **Limit Time** | Configurable per-channel, learn mode support |
| **Callback** | `curtain_callback_init(func)` — notify vị trí hiện tại |

**Key API:**
```c
void curtain_control_curtain_by_app(CurtainNumber_enum CT_No,
                                     CurtainControlId_enum CT_CmdId,
                                     uint8_t positionPerChar);
void curtain_set_level(uint8_t idx, uint8_t pos);
uint8_t curtain_get_target_position(uint8_t idx);
uint8_t curtain_get_curret_level(uint8_t CT_No);
void curtain_update_present_level(uint8_t idx, uint8_t level);
```

**Enums quan trọng:**
```c
typedef enum {
  CURTAIN_CONTROL_ID_STOP = 1,
  CURTAIN_CONTROL_ID_RUN  = 2,
} CurtainControlId_enum;

typedef enum {
  CURTAIN_CMD_IDLE  = 0,
  CURTAIN_CMD_STOP  = 1,
  CURTAIN_CMD_OPEN  = 2,
  CURTAIN_CMD_CLOSE = 3,
} CurtainCmd_enum;
```

### 2.2 `relay.c` — GPIO Relay Driver (528 lines)

Điều khiển latching relay qua GPIO pulse:
- **2 relays**: latching ON/OFF pins per channel
- **Pulse timer**: 10ms on-time → reset signal
- **State machine**: IDLE → BUSY → IDLE
- **NVS**: lưu relay target state

### 2.3 `app.c` — Application Bridge (428 lines)

Đây là tầng kết nối giữa inputs (BLE Mesh, KNX, button) và curtain.c:

**Bridge API đã chuẩn bị cho KNX:**
```c
// KNX → Curtain (đã implement logic)
int app_handle_control_curtain_from_knx(uint8_t curtain_idx,
                                        CurtainControlId_enum cmd_id,
                                        uint8_t position);

// Curtain → KNX (chỉ có TODO log)
void app_handle_curtain_update_level(uint8_t curtain_idx,
                                     uint8_t current_position);

// Query
int app_get_curtain_current_position(uint8_t curtain_idx,
                                     uint8_t *current_position);

// ETS check (trả về false, chưa implement)
bool knx_ets_has_been_configured(void);
```

**Button Config:**
- `CONFIG_BTN_ID_BLUETOOTH` — nút Bluetooth
- `CONFIG_BTN_ID_KNX` — nút KNX (đã khai báo nhưng chưa có KNX logic)

### 2.4 `net_message.c` — BLE Mesh Message Handler (453 lines)

- Handle SIG Generic Level SET/GET
- Callback dispatch tới curtain: `pvMessage_handle_control_curtain_by_app`
- Status publish to gateway
- Scene execution, binding group notification

### 2.5 `vendor_model.c` — Vendor Opcode Handler (1271 lines)

- VD_CONFIG_NODE opcodes (SET/GET/STATUS)
- Device option, light state, lock device
- Curtain type, limit time config
- TX gate (semaphore-based)

### 2.6 `main.c` — Entry Point (358 lines)

Boot sequence:
1. `watchdog_process_init()`
2. `fast_provision_nvs_init_early()`
3. `bt_enable(bt_ready)` → callback:
   - `mesh_initialize()`
   - `settings_load()` + `restore_appkey_binding`
   - `fast_provision_init()`, `normal_provision_init()`
   - `curtain_init()`, `relay_init()`, `button_init()`
   - `led_init()`, `execution_scene_init()`, `net_msg_init()`
   - `pre_update_init()`, `binding_init()`

**Lưu ý:** Chưa có `knx_adapter_init()` trong boot sequence.

## 3. Build System

### CMakeLists.txt
- BLE Mesh only, không có KNX definitions
- Sources: `main.c`, `app/*.c`, `mid/*.c`, `network/*.c`, `driver/at24c02.c`
- Thiếu: `src/knx/`, `knx_stack/`, KNX compile definitions

### Kconfig
- `KNX_ACTUATOR_4_RELAY` (tên sai, nên rename `KNX_CURTAIN`)
- `WATCHDOG_PROCESS_TIMEOUT_MS` default 12000ms

### Overlay
- `uart00`: TX=P2.7, RX=P2.8 (đã cấu hình, sẵn sàng cho KNX TPUART)
- `uart20`: Console (P1.04 TX, P1.03 RX)
- I2C, SPI, PWM cho LED, button, EEPROM

## 4. Dữ Liệu Vị Trí

| Thuộc tính | Giá trị |
|---|---|
| **Range** | 0x00 – 0xFF (Telink convention) |
| **0x00** | Fully open |
| **0xFF** | Fully closed |
| **NVS** | `curtain/pos` |
| **Internal type** | `uint8_t` (PositionPerChar) |
