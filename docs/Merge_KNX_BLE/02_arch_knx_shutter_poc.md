# Kiến Trúc KNX_Shutter POC (KNX Only)

> Project: `/home/iot/Lumi/KNX_Product/KNX_Product_POC/Lumi_KNX_Product/KNX_Shutter/code`
> Platform: nRF54L15 / Zephyr RTOS / KNX TP-UART

## 1. Tổng Quan

KNX_Shutter POC là firmware shutter actuator dạng **KNX-only** (không có BLE Mesh). Dev riêng để kiểm chứng KNX stack cho thiết bị rèm, cần merge vào KNX-Curtain để tạo firmware dual-stack (KNX + BLE).

### Sơ Đồ Layer

```
┌─────────────────────────────────────────────────┐
│              knx_adapter.cpp (436 lines)        │
│  (KNX stack lifecycle, GO polling, scene,       │
│   direction/position feedback, prog mode)       │
├─────────────────────────────────────────────────┤
│              knx_mapping_config.h (159 lines)   │
│  (Metadata, GO mapping, param offsets,          │
│   enums, runtime structs, scene helpers)        │
├─────────────────────────────────────────────────┤
│              knx_adapter.h (99 lines)           │
│  (Public API, ZBUS channels)                    │
├─────────────────────────────────────────────────┤
│              knx_stack/ (186 items)             │
│  (KNX TP-UART stack, nordic_platform,           │
│   knx_facade, bau07B0, security)               │
└─────────────────────────────────────────────────┘
         │                    ▲
         │ extern "C"         │ feedback API
         ▼                    │
┌─────────────────────────────────────────────────┐
│  Firmware rèm phải implement:                   │
│  • app_knx_shutter_move(bool going_down)        │
│  • app_knx_shutter_stop()                       │
│  • app_knx_shutter_set_position(uint8_t %)      │
└─────────────────────────────────────────────────┘
```

## 2. Module Chính

### 2.1 `knx_adapter.cpp` — Core Adapter (436 lines)

#### Boot Sequence (đúng thứ tự bảo mật KNX):
```
Step 1/3: getNonVolatileMemoryStart()  → Load NVS
Step 2/3: readMemory() + start()       → Restore seq numbers, keys
Step 3/3: setupUart()                  → Enable KNX bus RX
```

#### Group Object Polling (`knx_work_handler`):
| GO | Name | DPT | Direction | Action |
|---|---|---|---|---|
| GO1 | MUD (Move Up/Down) | DPT_UpDown (1 bit) | Write | `app_knx_shutter_move(going_down)` |
| GO2 | STOP | DPT_Trigger (1 bit) | Write | `app_knx_shutter_stop()` |
| GO3 | SAPBP (Set Position %) | DPT_Scaling (1 byte) | Write | `app_knx_shutter_set_position(pct)` |
| GO4 | IMUD (Direction Feedback) | DPT_UpDown (1 bit) | Transmit | `knx_send_direction_feedback()` |
| GO5 | CAPBP (Current Position %) | DPT_Scaling (1 byte) | Transmit | `knx_send_position_status()` |
| GO6 | SCENE | DPT_SceneControl (1 byte) | Write | `process_scene_command()` |

#### Scene Support (DPT 18.001):
- 10 scenes (A–J), mỗi scene 2 bytes (active+num, target position)
- Recall: lookup scene num → `set_position(target_pos)`
- Store: ignored (ETS-managed)

#### Enable Flags (bit-packed, offset 6):
- `enable_position`: cho phép GO3 (SAPBP)
- `enable_status`: cho phép GO4+GO5 (feedback)
- `enable_scene`: cho phép GO6

### 2.2 `knx_mapping_config.h` — Configuration (159 lines)

#### Application Metadata:
```c
KNX_MANUFACTURER_ID  = 0x0085
KNX_APP_NUMBER       = 0x0001
KNX_APP_VERSION      = 0x0C  (v12)
KNX_PARAMETER_SIZE   = 28
KNX_MAX_KO_NUMBER    = 6
KNX_MAX_SCENES       = 10
```

#### Parameter Memory Map (28 bytes):
```
Offset  Field           Size    Description
------  -----           ----    -----------
0       BlindType       1B      enum (0=Custom..5=VER_220)
1       TimeSameDiff    1B      0=Sync, 1=Independent
2-3     TimeOpen        2B      uint16 BE, 1-300 sec
4-5     TimeClose       2B      uint16 BE, 1-300 sec
6       EnableFlags     1B      bit0=Pos, bit1=Status, bit2=Scene
7       RelayCount      1B      2 or 3 (Custom mode only)
8-27    Scene A-J       20B     10 × 2B (active+num, target_pos)
```

#### Blind Type Enum:
| Value | Type | Description |
|---|---|---|
| 0 | `BLIND_TYPE_CUSTOM` | Lumi Custom Motor Script |
| 1 | `BLIND_TYPE_HOZ_DZ3W` | Standard 3-wire |
| 2 | `BLIND_TYPE_HOZ_DZ4W` | Standard 4-wire |
| 3 | `BLIND_TYPE_HOZ_DT99` | AutoHome Intelligent |
| 4 | `BLIND_TYPE_HOZ_DZ3WP` | Pulse-driven 3-wire |
| 5 | `BLIND_TYPE_VER_220` | Direct 220V Roll-up |

### 2.3 `knx_adapter.h` — Public API

```c
// Lifecycle
int  knx_adapter_init(void);
bool knx_adapter_is_initialized(void);
bool knx_is_configured(void);

// Programming Mode
bool knx_toggle_prog_mode(void);
void knx_set_prog_mode(bool enable);
bool knx_get_prog_mode(void);

// Info & Reset
void knx_log_device_info(void);
void knx_wipe_config(void);

// KNX Bus Feedback (firmware → bus)
void knx_send_direction_feedback(bool going_down);
void knx_send_position_status(uint8_t percent);

// Config Access
const knx_shutter_config_t* knx_get_shutter_config(void);

// ZBUS
ZBUS_CHAN_DECLARE(chan_knx_rx);
ZBUS_CHAN_DECLARE(chan_knx_tx);
```

### 2.4 `knx_app_layer.cpp` — Stub (19 lines)

Minimal stub — chỉ có `knx_app_init()` log message.

### 2.5 `knx_stack/` — KNX Core Stack (186 items)

| Component | Description |
|---|---|
| `nordic_platform.cpp` | Zephyr NVS + UART driver cho KNX TP-UART |
| `knx_facade.cpp` | Template facade pattern (KnxFacade<Platform, BAU>) |
| `knx/*.cpp` | Full KNX stack: data link, transport, application layer, security, group objects |
| `bau07B0` | BAU type cho System B (TP devices) |

## 3. Extern Callbacks

POC **không** có firmware rèm thực tế — nó declare 3 extern callbacks mà firmware rèm phải implement:

```c
extern "C" {
  void app_knx_shutter_move(bool going_down);
  void app_knx_shutter_stop(void);
  void app_knx_shutter_set_position(uint8_t percent);
}
```

**Đây là "interface contract" giữa POC và firmware rèm.**

## 4. Dữ Liệu Vị Trí

| Thuộc tính | Giá trị |
|---|---|
| **Range** | 0–100 (KNX DPT 5.001 convention, %) |
| **0** | Fully open |
| **100** | Fully closed |
| **DPT** | DPT_Scaling (0-100%) |

> ⚠️ **Khác so với KNX-Curtain** sử dụng 0x00–0xFF (Telink convention).
