# BLE_NORDIC SDK Documentation Index

## Tổng quan Documentation

Chào mừng đến với **BLE_NORDIC SDK Documentation** - tài liệu hoàn chỉnh cho việc phát triển các thiết bị Bluetooth Mesh dựa trên Nordic nRF54L15.

## Cấu trúc Documentation

### 📖 Core Documentation
- **[README.md](../README.md)** - Tổng quan SDK, tính năng, cài đặt
- **[API_REFERENCE.md](API_REFERENCE.md)** - Tham khảo API chi tiết
- **[BUILD_FLASH_GUIDE.md](BUILD_FLASH_GUIDE.md)** - Hướng dẫn build và flash

### 📚 Module Documentation
- **[ADDING_NEW_MODELS.md](ADDING_NEW_MODELS.md)** - Thêm models mới
- **[SDK_PATCHES.md](../SDK_PATCHES.md)** - Patches cho Nordic SDK

### 🔧 Configuration Files
- **[prj.conf](../prj.conf)** - Zephyr configuration với comments
- **[CMakeLists.txt](../CMakeLists.txt)** - Build configuration
- **[Kconfig](../Kconfig)** - Custom Kconfig options
- **[nrf54l15dk.overlay](../nrf54l15dk.overlay)** - Device tree overlay

## Quick Start

### 1. Đọc Tổng quan
Bắt đầu với [README.md](../README.md) để hiểu tổng quan project.

### 2. Cài đặt Environment
Theo hướng dẫn trong [BUILD_FLASH_GUIDE.md](BUILD_FLASH_GUIDE.md).

### 3. Phát triển
Sử dụng [API_REFERENCE.md](API_REFERENCE.md) để tham khảo APIs.

## Modules chính

### Core Modules
- **main.c** - Entry point và system initialization
- **mesh_message_handler.c** - Bluetooth Mesh message handling
- **network.c** - Network và key management

### Provisioning Modules
- **fast_provision.c** - Fast Provision protocol (Telink compatible)
- **normal_provision.c** - Standard Bluetooth Mesh provisioning
- **default_network.c** - Temporary network cho provisioning

### Hardware Modules
- **led.c** - LED control (4 LEDs)
- **button.c** - Button input handling (4 buttons)
- **uart_driver.c** - UART communication
- **watchdog_process.c** - System watchdog

### Utility Modules
- **light.c** - Lighting control logic
- **vendor.h** - Vendor-specific definitions

## Key Features

### 🔗 Bluetooth Mesh
- Relay Node với retransmit optimization
- Friend Node cho Low Power Nodes
- Generic OnOff Server/Client models
- Health model cho diagnostics

### ⚡ Fast Provision
- Telink SDK compatible
- Batch provisioning
- Automatic key management
- Default network support

### 🛠️ Development
- Nordic nRF54L15 DK support
- Zephyr RTOS integration
- Comprehensive logging
- UART debug interface

## API Categories

### Fast Provision APIs
```c
fast_prov_init()
fast_prov_start()
fast_prov_dev_add()
bind_all_models_with_appkey()
```

### Network Management APIs
```c
network_init()
network_add_appkey()
network_get_appkey_index()
```

### Hardware Control APIs
```c
led_init(), led_set(), led_toggle()
button_init(), button_pressed()
uart_send_data(), uart_receive_data()
```

### Mesh Communication APIs
```c
mesh_message_handler_init()
send_onoff_message()
bt_mesh_cfg_cli_comp_data_get()
bt_mesh_cfg_cli_mod_app_bind()
```

## Configuration Options

### Bluetooth Mesh
```properties
CONFIG_BT_MESH=y
CONFIG_BT_MESH_RELAY=y
CONFIG_BT_MESH_FRIEND=y
CONFIG_BT_MESH_MODEL_KEY_COUNT=2
```

### Fast Provision
```properties
CONFIG_BT_MESH_APP_KEY_COUNT=4
CONFIG_BT_MESH_SUBNET_COUNT=2
```

### Hardware
```properties
CONFIG_DK_LIBRARY=y
CONFIG_WDT_NRFX=y
CONFIG_SERIAL=y
```

## Troubleshooting

### Build Issues
- Toolchain not found → Check PATH
- Board not found → Verify board name
- Missing dependencies → Install requirements

### Runtime Issues
- No LED response → Check device tree
- Provisioning fails → Verify keys
- Communication issues → Check UART config

### Debug Tips
- Enable logging: `CONFIG_LOG=y`
- UART console: `CONFIG_UART_CONSOLE=y`
- RTT logging cho advanced debug

## Contributing

### Code Style
- Doxygen comments cho functions
- LOG_MODULE_REGISTER cho modules
- Error handling cho tất cả APIs
- Thread-safe design

### Adding Features
1. Thêm code trong thư mục `src/`
2. Update header trong `include/`
3. Thêm documentation trong `docs/`
4. Update `CMakeLists.txt` nếu cần

## Support Resources

### Nordic Resources
- [Nordic Developer Zone](https://devzone.nordicsemi.com/)
- [nRF Connect SDK Documentation](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/)
- [Zephyr Documentation](https://docs.zephyrproject.org/)

### Bluetooth Mesh
- [Bluetooth Mesh Specification](https://www.bluetooth.com/specifications/mesh-specifications/)
- [Mesh Model Specifications](https://www.bluetooth.com/specifications/specs/)

## Version Information

- **SDK Version**: BLE_NORDIC v1.0.0
- **NCS Version**: Nordic nRF Connect SDK v3.0.1
- **Zephyr Version**: Included in NCS
- **Target Board**: nRF54L15 DK
- **Date**: January 2026

## License

This documentation is part of BLE_NORDIC SDK. See project LICENSE for details.

---

**For more information, see individual documentation files.**</content>
<parameter name="filePath">d:\Lumi\LM_2025\Nordic\.src\BLE_NORDIC\docs\index.md