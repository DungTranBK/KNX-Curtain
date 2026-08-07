cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(knx_actuator)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# KNX Stack Configuration
# =============================================================================
target_compile_definitions(app PRIVATE
    MASK_VERSION=0x07B0
    USE_TP
    USE_KNX_UART_API
    USE_KNX_MEMORY_API
    NCN5120
    KNX_NO_PRINT
    USE_DATASECURE
    KNX_NO_AUTOMATIC_GLOBAL_INSTANCE
    KNX_FLASH_SIZE=1024
)

# =============================================================================
# Include Directories
# =============================================================================
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(app PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/knx_stack/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/knx
)

# =============================================================================
# Source Files
# =============================================================================
target_sources(app PRIVATE 
    src/main.c
    src/network/normal_provision.c
    src/network/fast_provision.c
    src/network/default_network.c
    src/network/network.c
    src/network/mesh_node.c
    src/network/vendor_model.c
    src/mid/watchdog_process.c
    src/mid/led.c
    src/mid/button.c
    src/mid/relay.c
    src/mid/fifo.c
    src/mid/execution_scene.c
    src/mid/scene.c
    src/mid/led_ev.c
    src/mid/fact.c
    src/mid/key_report.c
    src/mid/sw_binding.c
    src/mid/net_message.c
    src/mid/pre_update.c
    src/mid/timestamp.c
    src/app/app.c
    src/driver/at24c02.c
    src/app/curtain.c

    # KNX Adapter
    src/knx/knx_adapter.cpp
    src/knx/knx_app_layer.cpp
    src/knx/knx_provision.c

    # KNX Stack - Platform & Facade
    knx_stack/src/nordic_platform.cpp
    knx_stack/src/knx_facade.cpp
)

# Global KNX Core sources
file(GLOB KNX_CORE_SOURCES 
    "knx_stack/src/knx/*.cpp"
    "knx_stack/src/knx/*.c"
)
target_sources(app PRIVATE ${KNX_CORE_SOURCES})