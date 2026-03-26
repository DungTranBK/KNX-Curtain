#include "../../include/key_report.h"

#include "../../include/app_device.h"  // For GATEWAY_UNICAST_ADDR
#include "../../include/execution_scene.h"
#include "../../include/network.h"       // For network_get_unicast_address
#include "../../include/vendor_model.h"  // For mesh_tx_cmd_rsp

/******************************************************************************/
/*                            EXPORT FUNCTIONS                                */
/******************************************************************************/

/**
 * @brief   Get element address based on button index
 */
uint16_t get_ele_addr_base_btn_idx(uint8_t idx) {
  if (idx < NUMBER_INPUT) {  // NUMBER_INPUT defined in app_device.h
    uint16_t prim_addr = network_get_unicast_address();
    if (prim_addr == 0xFFFF) return 0;  // Unassigned
    return (prim_addr + idx);
  }
  return 0;  // ADR_UNASSIGNED
}

/**
 * @brief   Send key report
 */
void key_report_send(uint8_t idx, uint8_t key_code, bool en_execution) {
  uint16_t ele_addr;
  vd_rc_key_report_t key_report = {0};

  if (network_is_main_network_provisioned() == false) return;

  key_report.code = key_code;
  ele_addr = get_ele_addr_base_btn_idx(idx);

  if (ele_addr != 0) {  // ADR_UNASSIGNED
    if (en_execution) {
      execution_scene_active(idx, key_code);
    }
    mesh_tx_cmd_rsp(VD_RC_KEY_REPORT, (uint8_t*)&key_report,
                    sizeof(key_report.code),  // 1 bytes
                    ele_addr, GATEWAY_UNICAST_ADDR,
                    NULL,  // uuid
                    NULL   // model
    );

/* Retry scene active if needed (defined in execution_scene.h or config) */
#if defined(EXECUTION_SCENE_RETRY_TIME) && (EXECUTION_SCENE_RETRY_TIME > 1)
    execution_scene_active(idx, key_code);
#endif
  }
}
