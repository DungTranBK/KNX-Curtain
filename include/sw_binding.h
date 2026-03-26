/*
 * sw_binding.h
 *
 * Ported from Telink ONE_WIRE_SWITCH sw_binding.h to Nordic nRF Connect SDK.
 *
 * Manages group-binding configuration: links one relay element to a
 * specific mesh group address so it sends control commands to that group
 * whenever the relay state changes due to local (manual/button) control.
 *
 *  Created on: Jan 28, 2021
 *      Author: DungTranBK
 */

#ifndef SW_BINDING_H_
#define SW_BINDING_H_

/******************************************************************************/
/*                              INCLUDE FILES                                 */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include "app_device.h"
#include "utilities.h"

/******************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                         */
/******************************************************************************/

/**
 * @brief Per-element binding control state.
 *        Tracks last sent state and same-state retry counter.
 */
typedef struct {
  u8 st;       /* Last state sent to binding group (G_ON/G_OFF/G_ONOFF_RSV) */
  u8 cnt_same; /* Count of consecutive same-state messages (unused/future)  */
  u16 op;      /* Last opcode used for binding send (G_ONOFF_SET etc.)       */
} control_binding_t;

/**
 * @brief Vendor config packet for setting/clearing binding.
 *        Received from gateway via VD_CONFIG_GROUP_ASSOCIATION.
 */
typedef struct {
  u16 ele_adr;   /* Element unicast address                                   */
  u8 en;         /* 1=enable binding, 0=disable binding                       */
  u16 group_adr; /* Group address to bind to                                  */
} __attribute__((packed)) cfg_binding_format_t;

/**
 * @brief Vendor config packet for getting binding status.
 */
typedef struct {
  u16 ele_adr; /* Element unicast address to query                          */
} __attribute__((packed)) get_binding_format_t;

/**
 * @brief Per-element binding parameters (persisted to NVS).
 */
typedef struct {
  u8 en;         /* true = binding enabled for this element                   */
  u16 group_dst; /* Binding target group address                              */
} binding_para_t;

/**
 * @brief Response packet sent back to gateway after binding set/get.
 */
typedef struct {
  u8 op;         /* Sub-opcode VD_CONFIG_GROUP_ASSOCIATION                    */
  u8 st;         /* Status code (BINDING_SUCCESS, GROUP_NOT_SET, …)           */
  u16 ele_adr;   /* Element address                                           */
  u8 en;         /* Current binding enabled flag                              */
  u16 group_adr; /* Current binding group address                             */
} __attribute__((packed)) binding_msg_response_t;

/* Binding result codes */
enum {
  BINDING_SUCCESS = 0,
  GROUP_NOT_SET = 1,
  GROUP_INVALID = 2,
  MODEL_IDX_INVALID = 3,
  BINDING_ERR_UNKNOWN = 4,
  BINDING_DISABLE = 5,
};

/* Telink compat: ENABLE/DISABLE */
#ifndef ENABLE
#define ENABLE 1
#define DISABLE 0
#endif

/* Sub-opcode for vendor binding config messages */
#define VD_CONFIG_GROUP_ASSOCIATION 0x30

/******************************************************************************/
/*                              EXPORT DATA                                   */
/******************************************************************************/

extern binding_para_t binding_para_st[ELE_CNT];
extern control_binding_t control_binding_st[ELE_CNT];

/** If true for element i, the next binding send uses "force" marker byte. */
extern u8 force_control_binding[ELE_CNT];

/******************************************************************************/
/*                             EXPORT FUNCTIONS                               */
/******************************************************************************/

/**
 * @brief Get the binding group address for a given element index.
 *        Validates that the group is actually in the model's subscription list.
 * @param idx  Element index (0..ELE_CNT-1)
 * @return Group address if binding is enabled and valid,
 *         BT_MESH_ADDR_UNASSIGNED (0x0000) otherwise.
 */
uint16_t get_group_binding_adr(int idx);

/**
 * @brief Initialize the binding module.
 *        Restores persisted binding parameters from NVS.
 *        Must be called during application init.
 */
void binding_init(void);

/**
 * @brief Handle an incoming vendor config message from gateway
 *        with sub-opcode VD_CONFIG_GROUP_ASSOCIATION.
 * @param type CONFIG_NODE_SET or CONFIG_NODE_GET
 * @param par  Payload: cfg_binding_format_t (set) or get_binding_format_t (get)
 */
void binding_handle_nw_message(u8 type, u8 *par);

#endif /* SW_BINDING_H_ */
