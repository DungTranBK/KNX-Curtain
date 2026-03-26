# Project Instructions & Skills

This file contains project-specific rules, guidelines, and knowledge to help the AI assistant better understand the context of the `KNX-Actuator_4_relay` project.

## Project Context
- **Target SoC:** nRF54L15 (Nordic Semiconductor)
- **SDK:** nRF Connect SDK (Zephyr RTOS)
- **Application Type:** Bluetooth Mesh (KNX Actuator)

## Coding Standards & Naming Conventions
- **General:** Follow Zephyr's standard (Snake Case).
- **Functions:** Use `snake_case` (e.g., `relay_init`, `mesh_message_hook`). 
  - Callbacks should end in `_handler` or `_cb`.
- **Variables:** Use `snake_case`. Global/Static variables should be descriptive.
- **Files:** Lower case with underscores (e.g., `mesh_node.c`).
- **Logging:** Use `LOG_INF`, `LOG_ERR`, `LOG_WRN`, `LOG_DBG` via `zephyr/logging/log.h`.
  - Always register module: `LOG_MODULE_REGISTER(filename, CONFIG_LOG_DEFAULT_LEVEL)`.
- **Documentation:** Use Doxygen-style comments for function headers (`@brief`, `@param`, `@return`).
- **Hardware Guards:** Guard hardware-specific code with appropriate Kconfig symbols (e.g., `#if CONFIG_RELAY_ENABLE`).

## Agent Persona & Skills
- **Expertise:** Professional Embedded Software Engineer.
- **Specializations:**
    - Deep experience in **Bluetooth Mesh** (both **Nordic nRF Connect SDK** and **Telink SDK**).
    - Proficient in **Zephyr RTOS** and porting logic between different SDK architectures.
    - Expert in firmware architecture for KNX-related hardware.

## Quick Technical Context
- **Target SoC:** nRF54L15 (Nordic Semiconductor)
- **SDK:** nRF Connect SDK (Zephyr RTOS)
- **Application Type:** Bluetooth Mesh (KNX Actuator)

## Reference Projects
- **Telink Reference:** `C:\ncs\v3.0.1\.telink\ONE_WIRE_SWITCH`
  - *Note: Primary logic reference for porting features to Nordic.*
- **Nordic Reference:** `C:\ncs\v3.0.1\.telink\BLE_NORDIC_LUTO_SWITCH`
  - *Note: Reference for features to be ported from existing Nordic implementations.*
