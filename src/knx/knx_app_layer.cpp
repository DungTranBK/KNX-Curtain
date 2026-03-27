/**
 * @file knx_app_layer.cpp
 * @brief KNX Application Layer for Shutter Actuator
 * @details Placeholder - all shutter logic is handled in knx_adapter.cpp
 *          via extern "C" callbacks to the firmware rèm.
 *          Future: Move scene or complex logic here if needed.
 */

#include "../../include/knx_adapter.h"
#include "../../include/knx_mapping_config.h"

// Currently empty - all KNX -> App dispatching is done in knx_adapter.cpp
// Firmware rèm implements: app_knx_shutter_move(), app_knx_shutter_stop(),
//   app_knx_shutter_set_position(), app_knx_shutter_get_position()
