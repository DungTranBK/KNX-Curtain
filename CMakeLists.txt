cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(knx_actuator)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)


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
)