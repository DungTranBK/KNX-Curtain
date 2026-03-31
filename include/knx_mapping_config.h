/**
 * @file knx_mapping_config.h
 * @brief KNX Mapping Configuration for Shutter Actuator
 * @details Defines Group Object indices, Parameter offsets, enums and structs
 *          based on M-0085_A-0001-0C-89CC.xml product definition.
 */

#ifndef KNX_MAPPING_CONFIG_H__
#define KNX_MAPPING_CONFIG_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 1. APPLICATION METADATA (khớp với XML - giống nhau mọi thiết bị cùng loại)
// ============================================================================
#define KNX_MANUFACTURER_ID 0x0085
#define KNX_BAU_NUMBER_DEFAULT 0x00000000  // Mặc định là 0 (phải nạp từ flash)
#define KNX_HARDWARE_TYPE {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define KNX_HARDWARE_VERSION 0x01
#define KNX_ORDER_NUMBER {'S', 'H', '0', '0', '1', ' ', ' ', ' ', ' ', ' '}
#define KNX_APP_NUMBER 0x0001
#define KNX_APP_VERSION 0x01  // ApplicationVersion="1" (Matched to XML)

// PID 13: [MID_H, MID_L, AppNum_H, AppNum_L, AppVer]
#define KNX_PID13_PROG_VERSION                                                 \
  {(uint8_t)(KNX_MANUFACTURER_ID >> 8), (uint8_t)(KNX_MANUFACTURER_ID & 0xFF), \
   (uint8_t)(KNX_APP_NUMBER >> 8), (uint8_t)(KNX_APP_NUMBER & 0xFF),           \
   KNX_APP_VERSION}
// PID 66: [AppNum_H, AppNum_L, AppVer]
#define KNX_PID66_APP_PROG_ID                                        \
  {(uint8_t)(KNX_APP_NUMBER >> 8), (uint8_t)(KNX_APP_NUMBER & 0xFF), \
   KNX_APP_VERSION}

#define KNX_PARAMETER_SIZE 28
#define KNX_MAX_KO_NUMBER 6
#define KNX_MAX_SCENES 10

// ============================================================================
// 2. GROUP OBJECT MAPPING (Number= in XML, 1-based)
// ============================================================================
#define GO_SH_MUD 1    // Move Up/Down       (1 bit, Write)
#define GO_SH_STOP 2   // Stop               (1 bit, Write)
#define GO_SH_SAPBP 3  // Set Position %     (1 byte, Write)
#define GO_SH_CAPBP 4  // Current Position %  (1 byte, Transmit)
#define GO_SH_IMUD 5   // Direction Feedback  (1 bit, Transmit)
#define GO_SH_SCENE 6  // Scene Control       (1 byte, Write)

// ============================================================================
// 3. PARAMETER MAPPING (Offsets in RS-04-00000 segment, 28 bytes total)
// ============================================================================
//
// Memory Map:
// Memory Map:
//   Offset 0       : MotorType       (1 byte,  enum 1-5)
//   Offset 1       : UNUSED
//   Offset 2-3     : TravelTime      (2 bytes, uint16 BE, 1-300 sec)
//   Offset 4-5     : UNUSED
//   Offset 6 bit2  : EnableScene     (1 bit)
//   Offset 7 bit0  : EnableSceneStore (1 bit)
//   Offset 8-27    : Scene A-J       (10 × 2 bytes each)
//     Each scene:
//       Byte0        : Scene Number (1-64, 0=Inactive)
//       Byte1        : Target Position % (0-100)

// --- Basic Parameters ---
#define PARAM_MOTOR_TYPE 0
#define PARAM_TRAVEL_TIME 2   // 2 bytes (uint16 BE)
#define PARAM_ENABLE_FLAGS 6  // Bit-packed enable flags
#define PARAM_ENABLE_SCENE_STORE_BYTE 7
#define PARAM_ENABLE_SCENE_STORE_BIT 0

// Enable flag bit positions within offset 6
#define PARAM_ENABLE_SCENE_BIT 2

// --- Scene Parameters (10 scenes, 2 bytes each) ---
#define PARAM_SCENE_BASE 8
#define PARAM_SCENE_STRIDE 2

// Scene byte 0: Scene Number (1-64). 0 = not active.
// Scene byte 1: TargetPos (0-100)
#define PARAM_SCENE_ACTIVE_NUM(i) \
  (PARAM_SCENE_BASE + ((i) * PARAM_SCENE_STRIDE))
#define PARAM_SCENE_POS(i) (PARAM_SCENE_BASE + ((i) * PARAM_SCENE_STRIDE) + 1)

// Scene number mapping: Bus Scene Number = PARAM_VAL - 1
#define SCENE_NOT_ACTIVE_VAL 0
#define SCENE_VAL_TO_NUM(val) ((val) - 1)

// ============================================================================
// 4. ENUMS
// ============================================================================

typedef enum {
  BLIND_TYPE_CUSTOM = 0,     // Lumi Custom Motor Script
  BLIND_TYPE_HOZ_DZ3W = 1,   // Standard 3-wire Motor
  BLIND_TYPE_HOZ_DZ4W = 2,   // Standard 4-wire Motor
  BLIND_TYPE_HOZ_DT99 = 3,   // AutoHome Intelligent
  BLIND_TYPE_HOZ_DZ3WP = 4,  // Pulse-driven 3-wire
  BLIND_TYPE_VER_220 = 5     // Direct 220V Roll-up
} knx_blind_type_t;

typedef enum {
  TIME_MODE_SYNC = 0,        // Synchronized Open & Close
  TIME_MODE_INDEPENDENT = 1  // Independent Open / Close
} knx_time_mode_t;

// ============================================================================
// 5. RUNTIME STRUCTURES
// ============================================================================

typedef struct {
  bool active;  // Scene enabled (từ ETS)
  uint8_t num;  // Scene number (0-63, khớp DPT 18.001)
  uint8_t pos;  // Target position % (0-100)
} knx_scene_config_t;

typedef struct {
  // --- Motor config ---
  knx_blind_type_t motor_type;
  uint16_t travel_time_sec;  // 1-300

  // --- Enable flags ---
  bool enable_scene;
  bool enable_scene_store;

  // --- Scene assignments ---
  knx_scene_config_t scenes[KNX_MAX_SCENES];
} knx_shutter_config_t;

// ============================================================================
// 6. DPT 18.001 HELPERS (Scene Control)
// ============================================================================
#define DPT18_LEARN_BIT 0x80       // bit 7 = learn/store
#define DPT18_SCENE_NUM_MASK 0x3F  // bits 0-5 = scene number (0-63)

#define DPT18_IS_STORE(val) (((val) & DPT18_LEARN_BIT) != 0)
#define DPT18_SCENE_NUM(val) ((val) & DPT18_SCENE_NUM_MASK)

#ifdef __cplusplus
}
#endif

#endif /* KNX_MAPPING_CONFIG_H__ */
