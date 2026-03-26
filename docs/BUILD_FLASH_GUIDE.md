# BLE_NORDIC SDK - Build & Flash Guide

## Tổng quan

Hướng dẫn này mô tả cách build và flash firmware BLE_NORDIC lên Nordic nRF54L15 DK.

## Yêu cầu

### Hardware
- Nordic nRF54L15 DK
- USB cable (Type-A to Micro-USB)
- Computer với USB port

### Software
- Nordic nRF Connect SDK v3.0.1
- GNU Arm Embedded Toolchain 12.2.1 hoặc mới hơn
- CMake 3.20.0 hoặc mới hơn
- Python 3.8 hoặc mới hơn
- nRF Command Line Tools

## Cài đặt Environment

### 1. Cài đặt NCS v3.0.1

```bash
# Tạo thư mục workspace
mkdir ncs_workspace
cd ncs_workspace

# Clone NCS
west init -m https://github.com/nordicsemiconductor/nrf --mr v3.0.1
west update

# Setup environment
source zephyr/zephyr-env.sh
```

### 2. Cài đặt Toolchain

1. Download **GNU Arm Embedded Toolchain** từ:
   - https://developer.arm.com/downloads/-/gnu-rm
   - Chọn version 12.2.1 hoặc mới hơn

2. Extract và thêm vào PATH:
```bash
# Linux/Mac
export PATH="/path/to/gcc-arm-none-eabi-12.2/bin:$PATH"

# Windows (PowerShell)
$env:Path += ";C:\path\to\gcc-arm-none-eabi-12.2\bin"
```

3. Verify installation:
```bash
arm-none-eabi-gcc --version
```

### 3. Cài đặt nRF Command Line Tools

Download từ: https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools

## Build Project

### Method 1: CMake + Ninja (Recommended)

```bash
# Navigate to project
cd /path/to/BLE_NORDIC

# Create build directory
mkdir build
cd build

# Configure build
cmake -GNinja -DBOARD=nrf54l15dk/nrf54l15/cpuapp ..

# Build
ninja
```

### Method 2: West Build

```bash
# From project root
west build -b nrf54l15dk/nrf54l15/cpuapp
```

### Build Options

#### Debug Build
```bash
cmake -GNinja -DBOARD=nrf54l15dk/nrf54l15/cpuapp -DCMAKE_BUILD_TYPE=Debug ..
```

#### Release Build
```bash
cmake -GNinja -DBOARD=nrf54l15dk/nrf54l15/cpuapp -DCMAKE_BUILD_TYPE=Release ..
```

#### Custom Configuration
```bash
# Sử dụng custom prj.conf
cmake -GNinja -DBOARD=nrf54l15dk/nrf54l15/cpuapp -DCONF_FILE=prj_custom.conf ..
```

## Flash Firmware

### Method 1: Ninja Flash (Recommended)

```bash
# From build directory
ninja flash
```

### Method 2: nRF Connect Programmer

```bash
# Flash application
nrfjprog --program build/zephyr/app_update.bin --sectorerase

# Reset device
nrfjprog --reset
```

### Method 3: West Flash

```bash
west flash
```

### Method 4: Manual với J-Link

```bash
# Erase chip
nrfjprog --eraseall

# Flash bootloader (nếu cần)
nrfjprog --program build/zephyr/merged.hex --verify

# Reset
nrfjprog --reset
```

## Debug

### 1. UART Console

```bash
# Linux/Mac
screen /dev/ttyACM0 115200

# Windows
putty -serial COM3 -sercfg 115200,8,n,1,N
```

### 2. RTT Logging

```bash
# Sử dụng J-Link RTT Viewer
JLinkRTTViewer.exe
```

### 3. GDB Debug

```bash
# Start GDB server
JLinkGDBServer -device nRF54L15_xxAA -if swd -speed 4000

# Connect GDB
arm-none-eabi-gdb build/zephyr/zephyr.elf
```

## Troubleshooting

### Build Errors

#### "arm-none-eabi-gcc: command not found"
```bash
# Check PATH
echo $PATH

# Add toolchain to PATH
export PATH="/path/to/gcc-arm-none-eabi/bin:$PATH"
```

#### "BOARD not found"
```bash
# List available boards
find /path/to/ncs -name "*nrf54l15*"

# Use correct board name
cmake -GNinja -DBOARD=nrf54l15dk/nrf54l15/cpuapp ..
```

#### Missing dependencies
```bash
# Install missing packages
pip install -r /path/to/ncs/scripts/requirements.txt
```

### Flash Errors

#### "No J-Link found"
```bash
# Check USB connection
lsusb | grep J-Link

# Install J-Link drivers
# Download từ: https://www.segger.com/downloads/jlink/
```

#### "Protection enabled"
```bash
# Erase protection
nrfjprog --recover
```

#### "Invalid image"
```bash
# Check file exists
ls -la build/zephyr/app_update.bin

# Rebuild project
ninja clean
ninja
```

### Runtime Issues

#### No LED blinking
- Check device tree overlay
- Verify GPIO pins in `nrf54l15dk.overlay`

#### Bluetooth not working
- Check CONFIG_BT=y in prj.conf
- Verify antenna connection

#### Fast Provision fails
- Check AppKey configuration
- Verify network parameters

## Advanced Configuration

### Custom Board Support

1. Create custom board directory:
```
boards/arm/my_board/
├── board.cmake
├── board.h
├── Kconfig.board
└── my_board.dts
```

2. Update CMakeLists.txt:
```cmake
set(BOARD my_board)
```

### Multi-Image Builds

```cmake
# Build với MCUboot
set(PM_MCUBOOT_SECONDARY_SIZE 0x40000)
set(CONFIG_BOOTLOADER_MCUBOOT y)

# Build child image
add_child_image(
    NAME mcuboot
    SOURCE_DIR ${ZEPHYR_BASE}/../bootloader/mcuboot/boot/zephyr
    DOMAIN CPUNET
)
```

### OTA Updates

```cmake
# Enable DFU
set(CONFIG_BT_MESH_DFU_SRV y)
set(CONFIG_BT_MESH_BLOB_SRV y)

# Build DFU image
west build -b nrf54l15dk/nrf54l15/cpuapp -- -DCONFIG_BT_MESH_DFU=y
```

## Performance Optimization

### Memory Optimization
```properties
# Reduce stack sizes
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096

# Optimize Bluetooth buffers
CONFIG_BT_L2CAP_TX_BUF_COUNT=8
CONFIG_BT_BUF_COUNT=8
```

### Power Optimization
```properties
# Disable unused features
CONFIG_BT_CTLR_LE_ENC=n
CONFIG_BT_DATA_LEN_UPDATE=n

# Optimize sleep
CONFIG_PM=y
CONFIG_PM_DEVICE=y
```

## File Structure sau khi Build

```
build/
├── build.ninja          # Ninja build file
├── CMakeCache.txt       # CMake cache
├── CMakeFiles/          # CMake temporary files
├── zephyr/
│   ├── app_update.bin   # Application binary
│   ├── zephyr.elf       # ELF executable
│   ├── zephyr.hex       # Intel HEX file
│   └── merged.hex       # Combined image
├── BLE_NORDIC/
│   └── BLE_NORDIC.elf   # Application ELF
└── modules/
    └── ...              # External modules
```

## Logs và Debug Output

### Enable Debug Logs
```properties
CONFIG_LOG=y
CONFIG_LOG_MODE_IMMEDIATE=y
CONFIG_BT_MESH_LOG_LEVEL_DBG=y
CONFIG_FAST_PROVISION_LOG_LEVEL_INF=y
```

### UART Configuration
```properties
CONFIG_SERIAL=y
CONFIG_UART_CONSOLE=y
CONFIG_UART_LINE_CTRL=y
```

---

**Version**: 1.0.0
**Date**: January 2026
**Board**: nRF54L15 DK</content>
<parameter name="filePath">d:\Lumi\LM_2025\Nordic\.src\BLE_NORDIC\docs\BUILD_FLASH_GUIDE.md