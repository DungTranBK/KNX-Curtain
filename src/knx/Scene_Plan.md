# KNX Scene Control - Shutter Actuator (PoC)

## 1. Overview
The Shutter Actuator PoC supports 10 Scene Slots (A–J). This document defines the implementation strategy for both **Recall** (gọi cảnh) and **Store** (học/lưu cảnh) using DPT 18.001 (Scene Control).

## 2. Parameter Mapping
Based on the XML definition (`M-0085_O-6`), the scene parameters are mapped at **Offsets 8–27** in the application memory segment (RS-04-00000).

- **Slot A–J** (10 slots, 2 bytes each):
  - **Byte 0**: Scene Number (Full byte). 
    - `0`: Not active.
    - `1–64`: Corresponding to "Scene No. 1–64" in ETS.
    - **Note**: The value sent on the KNX bus is `Byte0 - 1`.
  - **Byte 1**: Stored Position (0–100%).

- **Global Config**:
  - **Offset 6 bit 2**: `EnableScene` (Cho phép điều khiển cảnh).
  - **Offset 7 bit 0**: `EnableSceneStore` (Cho phép học cảnh - SLME).

## 3. Implementation Logic

### 3.1 Scene Recall (Recall)
When a Telegram is received on **GO6** with `bit 7 = 0`:
1. Extract `scene_num = raw & 0x3F` (0-63).
2. Search through slots A–J (0–9) for a match where:
   - `shutter_config.scenes[i].active == true`
   - `shutter_config.scenes[i].num == scene_num`
3. If found, call `app_knx_shutter_set_position(shutter_config.scenes[i].pos)`.

### 3.2 Scene Store (Learn)
When a Telegram is received on **GO6** with `bit 7 = 1`:
1. Check if `shutter_config.enable_scene_store` (SLME) is enabled. If not, ignore.
2. Extract `scene_num = raw & 0x3F`.
3. Search for a matching **configured** slot (same as Recall).
4. If found:
   - Get current curtain position: `uint8_t current_pos = curtain_get_curret_level(0)`. (Range 0–255)
   - Convert to KNX percentage: `knx_pct = (uint16_t)current_pos * 100 / 255`.
   - Update `shutter_config.scenes[i].pos = knx_pct`.
   - Update RAM mirror.
   - Persist to NVS via Zephyr **Settings API**.

## 4. NVS Persistence
Learned scene positions will be stored using the Zephyr `settings` subsystem to survive power cycles.
- **Settings Path**: `app/sceneX/pos` (where X is 0–9).
- **Initialization**: On boot, after reading parameters from the KNX stack, the system will load any stored scene positions from `settings` to override the initial ETS values.

## 5. Implementation Steps
1. [ ] Fix `knx_mapping_config.h` to match full-byte scene mapping.
2. [ ] Update `load_shutter_params()` in `knx_adapter.cpp`.
3. [ ] Implement `process_scene_command()` with Store logic in `knx_adapter.cpp`.
4. [ ] Implement NVS Save/Load helpers.
