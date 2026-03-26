# Serial-Config Integration

This document outlines the integration between the Bluetooth Mesh Vendor Model, the Configuration module, and the Serial communication layer.

## Overview

The system bridges Bluetooth Mesh Vendor messages to a serial interface (UART) to communicate with an external MCU.

### Message Flow

1.  **Mesh to MCU (Request)**:
    *   **Source**: Vendor Model (`vendor_model.c`) receives a Mesh message.
    *   **Processing**: For specific opcodes (e.g., `VD_CONFIG_NODE_SET_ACK`), it calls `config_handle_vendor_msg`.
    *   **Config Module**: `config.c` maps the Mesh opcode to a Serial Sub-command.
    *   **Serial Layer**: Sends a frame with Command ID `0x86` (CMD_ID_DEV_CONFIG_BT_TO_MCU).
    *   **Frame Format**: `[SOF] [LEN] [CMD=0x86] [SUB_CMD] [DATA...] [CS]`

2.  **MCU to Mesh (Response)**:
    *   **Source**: MCU sends a serial frame.
    *   **Serial Layer**: `serial.c` receives and validates the frame.
    *   **Callback**: Calls the registered callback `config_serial_frame_callback`.
    *   **Config Module**: Checks for Command ID `0x06` (CMD_ID_RSP_DEV_CONFIG_MCU_TO_BT).
    *   **Processing**: Maps the Sub-command back to a Mesh opcode and forwards the response to the Mesh network (Implementation Pending).

## Constants

*   **Serial Request Command**: `0x86`
*   **Serial Response Command**: `0x06`
*   **Base Opcode Offset**: `0x20` (VD_CONFIG_ALL_SWITCH_OPT)

## How to Extend

To add new commands:
1.  Define the Mesh Opcode in `vendor.h`.
2.  Update `config_handle_vendor_msg` in `config.c` to handle the new opcode if it needs special filtering.
3.  Update `config_serial_frame_callback` in `config.c` to handle the response sub-command.

## Files

*   `src/mid/config.c`: Core logic for mapping and forwarding.
*   `src/mid/serial.c`: Low-level serial frame handling.
*   `src/network/vendor_model.c`: Mesh message handlers.
