/**
 * @file nordic_platform.cpp
 * @brief Nordic Platform Implementation cho KNX Stack
 * @details Implementation của NordicPlatform class
 */

#include "nordic_platform.h"

#include <dk_buttons_and_leds.h> // Nordic DK library cho LEDs
#include <stdio.h>               // Thay vì <cstdio> cho Zephyr compatibility
#include <stdlib.h>              // For strtol
#include <string.h>              // Thay vì <cstring> cho Zephyr compatibility
#include <zephyr/cache.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/ring_buffer.h>

// ============================================================================
// Conditional Compilation Flags
// ============================================================================
// Uncomment để bật UART API (cần cho KNX TP)
#define USE_KNX_UART_API

// Uncomment để bật Memory API (cần cho KNX EEPROM)
#define USE_KNX_MEMORY_API

// Uncomment để bật UART Debug GPIO (P1.00, P1.01, P1.02)
// #define DEBUG_UART_GPIO

// ============================================================================
// Conditional Includes
// ============================================================================

#ifdef USE_KNX_UART_API
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#endif

#ifdef USE_KNX_MEMORY_API
#include <zephyr/settings/settings.h> // Settings API (TF-M compatible)

#include "knx/memory.h"
#endif

// HAL Include for UART Parity Fix
#include <hal/nrf_uarte.h>

// ============================================================================
// Device Tree Definitions & Constants
// ============================================================================
LOG_MODULE_REGISTER(nordic_platform, LOG_LEVEL_NONE);

#ifdef USE_KNX_UART_API
// UART device cho KNX TP
// Overlay file (nrf54l15dk_nrf54l15_cpuapp_ns.overlay) đã enable uart00
#define KNX_UART_DEVICE DT_NODELABEL(uart00)

// KNX TP UART configuration: 19200 baud, 8E1
static const struct uart_config uart_cfg = {.baudrate = 19200,
                                            .parity = UART_CFG_PARITY_EVEN,
                                            .stop_bits = UART_CFG_STOP_BITS_1,
                                            .data_bits = UART_CFG_DATA_BITS_8,
                                            .flow_ctrl =
                                                UART_CFG_FLOW_CTRL_NONE};
#endif

#ifdef USE_KNX_MEMORY_API
// KNX EEPROM size - nâng lên 4096 bytes để hỗ trợ Data Secure và nhiều Group
// Objects
#define KNX_EEPROM_SIZE 4096

// Settings API - save in chunks to stay within NVS record limits
// nRF52840 NVS typically handles 256-512 byte records better than 1024+
#define SETTINGS_CHUNK_SIZE 512
#define SETTINGS_CHUNK_COUNT (KNX_EEPROM_SIZE / SETTINGS_CHUNK_SIZE)

// Settings key prefix for KNX EEPROM data
#define KNX_SETTINGS_PREFIX "knx/eeprom"

// Static member definitions - share giữa các NordicPlatform instances
static uint8_t s_eeprom_static_buf[KNX_EEPROM_SIZE];
uint8_t *NordicPlatform::_eeprom_buffer = nullptr;
uint32_t NordicPlatform::_eeprom_size = 0;
uint64_t NordicPlatform::_seq_send = 0;
uint64_t NordicPlatform::_seq_tool = 0;
#endif

// ============================================================================
// UART RX Ring Buffer (thay thế k_malloc/k_fifo)
// ============================================================================
#ifdef USE_KNX_UART_API
#define UART_RX_RINGBUF_SIZE 256

static uint8_t uart_rx_rb_data[UART_RX_RINGBUF_SIZE];
static struct ring_buf uart_rx_rb;

// ============================================================================
// ISR-Triggered Polling Mode
// ============================================================================
// rx_active: true khi đang có data cần xử lý, polling sẽ chạy
// Khi buffer empty một thời gian, rx_active = false, polling dừng
static volatile bool rx_active = false;
static volatile uint32_t rx_last_byte_time = 0; // Thời điểm nhận byte cuối
#define RX_IDLE_TIMEOUT_MS 10 // Tắt polling sau 10ms không có data mới

// Trigger callback: gọi từ ISR để kick work queue ngay lập tức
static void (*_rx_trigger_callback)(void) = nullptr;
#endif

#ifdef DEBUG_UART_GPIO
// Debug GPIO pins for UART tracing
// Switch to Port 2 (same as UART) to ensure Non-Secure access
#define DEBUG_PIN_ISR_ENTRY DT_NODELABEL(gpio2)   // P2.00 - ISR Entry/Exit
#define DEBUG_PIN_CALLBACK DT_NODELABEL(gpio2)    // P2.01 - process_rx_isr call
#define DEBUG_PIN_MALLOC_FAIL DT_NODELABEL(gpio2) // P2.02 - malloc failure

static const struct device *debug_gpio_dev = nullptr;
static const gpio_pin_t debug_pin_isr_entry = 0;   // P2.00
static const gpio_pin_t debug_pin_callback = 1;    // P2.01
static const gpio_pin_t debug_pin_malloc_fail = 2; // P2.02

// ISR Loop Detection
static uint32_t isr_call_count = 0;
static uint32_t isr_bytes_read = 0;
static uint32_t last_log_time = 0;
#define ISR_LOOP_THRESHOLD                                                     \
  1000 // If ISR called 1000 times in 1 second without reading bytes
#endif

// ============================================================================
// UART Interrupt Handler (chỉ khi USE_KNX_UART_API)
// ============================================================================

#ifdef USE_KNX_UART_API
/**
 * @brief UART interrupt callback
 *
 * @param dev UART device
 * @param user_data NordicPlatform pointer
 */
void uart_irq_handler(const struct device *dev, void *user_data) {
  NordicPlatform *platform = (NordicPlatform *)user_data;

#ifdef DEBUG_UART_GPIO
  // ISR Loop Detection
  isr_call_count++;
  uint32_t now = k_uptime_get_32();
  if (now - last_log_time >= 1000) { // Every 1 second
    if (isr_call_count > ISR_LOOP_THRESHOLD && isr_bytes_read == 0) {
      LOG_ERR("UART ISR LOOP DETECTED: %u calls, %u bytes read in 1s. "
              "Disabling RX IRQ!",
              isr_call_count, isr_bytes_read);
      uart_irq_rx_disable(dev);
      // Reset pin to LOW before disabling
      if (debug_gpio_dev) {
        gpio_pin_set_raw(debug_gpio_dev, debug_pin_isr_entry, 0);
      }
      return;
    }
    if (isr_call_count > 10 || isr_bytes_read > 0) { // Only log if activity
      LOG_INF("UART ISR Stats: %u calls, %u bytes in 1s", isr_call_count,
              isr_bytes_read);
    }
    isr_call_count = 0;
    isr_bytes_read = 0;
    last_log_time = now;
  }

  // T002: Debug - ISR Entry (P2.00 = HIGH)
  if (debug_gpio_dev) {
    gpio_pin_set_raw(debug_gpio_dev, debug_pin_isr_entry, 1);
  }
#endif

  // Check for errors first!
  int err_src = 0;

  // Zephyr 3.x UART API: uart_err_check
  err_src = uart_err_check(dev);
  if (err_src > 0) {
    LOG_ERR("UART Error: %d", err_src);
    // If BREAK or FRAMING error, it might mean floating pin or wrong baudrate
    // We should probably flush or ignore
    // But we MUST clear the error to exit ISR (uart_err_check usually clears it
    // or we need re-enable)
#ifdef DEBUG_UART_GPIO
    if (debug_gpio_dev) {
      gpio_pin_set_raw(debug_gpio_dev, debug_pin_isr_entry, 0);
    }
#endif
    return;
  }

  if (!uart_irq_update(dev)) {
#ifdef DEBUG_UART_GPIO
    // T002: Debug - ISR Exit early (P2.00 = LOW)
    if (debug_gpio_dev) {
      gpio_pin_set_raw(debug_gpio_dev, debug_pin_isr_entry, 0);
    }
#endif
    return;
  }
  // Log-debug
  //  RX ready - có data đến
  if (uart_irq_rx_ready(dev)) {
    // LOG_INF("UART IRQ"); //LOG-NOTE
    uint8_t byte;
    bool data_received = false;

    while (uart_fifo_read(dev, &byte, 1) == 1) {
#ifdef DEBUG_UART_GPIO
      isr_bytes_read++; // Track bytes actually read
#endif
      // *** RING BUFFER: No malloc needed! ***
      if (ring_buf_put(&uart_rx_rb, &byte, 1) == 1) {
        data_received = true;
      } else {
        // Buffer full - set overflow flag
        platform->_uart_overflow = true;
#ifdef DEBUG_UART_GPIO
        // T004: Debug - Buffer overflow (P1.02 = HIGH)
        if (debug_gpio_dev) {
          gpio_pin_set_raw(debug_gpio_dev, debug_pin_malloc_fail, 1);
        }
#endif
      }
    }

    // *** ISR-TRIGGERED POLLING MODE ***
    // Chỉ set flag và gọi trigger callback, không xử lý KNX trong ISR
    if (data_received) {
      rx_active = true;
      rx_last_byte_time = k_uptime_get_32();

      // Trigger work queue ngay lập tức nếu có callback
      if (_rx_trigger_callback) {
        _rx_trigger_callback();
      }
    }
  }

  // TX ready - có thể gửi tiếp
  if (uart_irq_tx_ready(dev)) {
    // Prevent Interrupt Storm: Clear TXDRDY event manually
    // We do NOT disable the interrupt (which kills Async API)
    // We just acknowledge the event so ISR exits and doesn't loop
#if defined(NRF_UARTE00_NS)
    nrf_uarte_event_clear(NRF_UARTE00_NS, NRF_UARTE_EVENT_TXDRDY);
#elif defined(NRF_UARTE00)
    nrf_uarte_event_clear(NRF_UARTE00, NRF_UARTE_EVENT_TXDRDY);
#endif
  }

  // Error handling - already done at start, but check pending just in case
  if (uart_irq_is_pending(dev)) {
    if (!uart_irq_rx_ready(dev) && !uart_irq_tx_ready(dev)) {
      // Log if unknown pending
      // LOG_WRN("UART Pending but not RX/TX ready");
    }
  }

#ifdef DEBUG_UART_GPIO
  // T002: Debug - ISR Exit (P1.00 = LOW)
  if (debug_gpio_dev) {
    gpio_pin_set_raw(debug_gpio_dev, debug_pin_isr_entry, 0);
  }
#endif
}
#endif

// ============================================================================
// Constructor/Destructor
// ============================================================================

NordicPlatform::NordicPlatform()
    :
#ifdef USE_KNX_UART_API
      _uart_dev(nullptr), _uart_overflow(false),
#endif
      _knx_process_rx_isr(nullptr) {

  // Initialize reboot work
  k_work_init_delayable(&_reboot_work, [](struct k_work *work) {
    LOG_ERR("!!! [REBOOT] ASYNCHRONOUS REBOOT EXECUTING NOW !!!");
    sys_reboot(SYS_REBOOT_COLD);
  });

  // Initialize reboot work
  k_work_init_delayable(&_reboot_work, [](struct k_work *work) {
    LOG_ERR("!!! [REBOOT] ASYNCHRONOUS REBOOT EXECUTING NOW !!!");
    sys_reboot(SYS_REBOOT_COLD);
  });

  // Register settings handler for optimized loading
  static struct settings_handler knx_handler = {
      .name = "knx",
      .h_set = NordicPlatform::settings_load_callback,
  };
  settings_register(&knx_handler);

#ifdef USE_KNX_UART_API
  k_mutex_init(&_uart_mutex);
  // *** RING BUFFER: Init thay vì k_fifo_init ***
  ring_buf_init(&uart_rx_rb, UART_RX_RINGBUF_SIZE, uart_rx_rb_data);
#endif
#ifdef USE_KNX_MEMORY_API
  k_mutex_init(&_memory_mutex);
#endif
}

NordicPlatform::~NordicPlatform() {
#ifdef USE_KNX_UART_API
  closeUart();
#endif

#ifdef USE_KNX_MEMORY_API
  if (_eeprom_buffer) {
    k_free(_eeprom_buffer);
    _eeprom_buffer = nullptr;
  }
#endif
}

// ============================================================================
// Basic Functions
// ============================================================================

void NordicPlatform::restart() {
  LOG_ERR(">>> [REBOOT] NordicPlatform::restart() - ASYNCHRONOUS REBOOT "
          "REQUESTED!");

  // Schedule reboot with a delay to allow KNX stack to finish transmission
  // (e.g., sending the Restart Response or ACK to ETS)
  int delay_ms = 1000;
  LOG_INF(">>> Rebooting in %d ms...", delay_ms);

  k_work_reschedule(&_reboot_work, K_MSEC(delay_ms));
}

void NordicPlatform::fatalError() {
  LOG_ERR(">>> [DEBUG] fatalError() called - KNX stack triggered fatal error!");
  // Blink LED và reboot sau 5 giây
  for (int i = 0; i < 10; i++) {
    dk_set_leds(DK_ALL_LEDS_MSK);
    k_sleep(K_MSEC(250));
    dk_set_leds(DK_NO_LEDS_MSK);
    k_sleep(K_MSEC(250));
  }

  restart();
}

uint32_t NordicPlatform::uniqueSerialNumber() {
  uint8_t device_id[16];
  size_t len = hwinfo_get_device_id(device_id, sizeof(device_id));

  if (len < 4) {
    // Fallback: dùng random nếu không có device ID
    return (uint32_t)k_uptime_get();
  }

  // Tạo unique number từ device ID (XOR 4 bytes đầu)
  uint32_t serial = 0;
  for (size_t i = 0; i < 4 && i < len; i++) {
    serial ^= ((uint32_t)device_id[i] << (i * 8));
  }

  return serial;
}

void NordicPlatform::macAddress(uint8_t *data) {
  if (!data) {
    return;
  }

  // Lấy MAC từ hwinfo (6 bytes)
  size_t len = hwinfo_get_device_id(data, 6);

  if (len < 6) {
    // Fallback: dùng MAC từ BLE Mesh (nếu có)
    // Hoặc generate từ unique serial number
    uint32_t serial = uniqueSerialNumber();
    data[0] = 0x02; // Locally administered
    data[1] = (serial >> 24) & 0xFF;
    data[2] = (serial >> 16) & 0xFF;
    data[3] = (serial >> 8) & 0xFF;
    data[4] = serial & 0xFF;
    data[5] = 0x01;
  }
}

// ============================================================================
// UART Functions
// ============================================================================

#ifdef USE_KNX_UART_API
// Real UART implementation
void NordicPlatform::setupUart() {
#ifdef DEBUG_UART_GPIO
  // T001: Configure Debug GPIO pins (P2.00, P2.01, P2.02)
  debug_gpio_dev = DEVICE_DT_GET(DEBUG_PIN_ISR_ENTRY);
  if (device_is_ready(debug_gpio_dev)) {
    // Force Active High, Init Low
    gpio_pin_configure(debug_gpio_dev, debug_pin_isr_entry,
                       GPIO_OUTPUT_INACTIVE | GPIO_ACTIVE_HIGH);
    gpio_pin_configure(debug_gpio_dev, debug_pin_callback,
                       GPIO_OUTPUT_INACTIVE | GPIO_ACTIVE_HIGH);
    gpio_pin_configure(debug_gpio_dev, debug_pin_malloc_fail,
                       GPIO_OUTPUT_INACTIVE | GPIO_ACTIVE_HIGH);
    LOG_INF("Debug GPIO configured: P2.00, P2.01, P2.02");
  } else {
    LOG_WRN("Debug GPIO device not ready");
    debug_gpio_dev = nullptr;
  }
#endif

// KNX_UART_DEVICE đã được define trong overlay file
#ifdef KNX_UART_DEVICE
  _uart_dev = DEVICE_DT_GET(KNX_UART_DEVICE);
  if (!_uart_dev || !device_is_ready(_uart_dev)) {
    // dùng log , không dùng printk
    LOG_ERR("KNX UART device not ready: %p\n", _uart_dev);
  } else {
    // T005: Log UART pin configuration
    LOG_INF("KNX UART device ready: %s (TX=P2.08, RX=P2.07)\n",
            _uart_dev->name);
  }
#else
  // KNX_UART_DEVICE chưa được define - kiểm tra overlay file
  _uart_dev = nullptr;
  return;
#endif

  if (!device_is_ready(_uart_dev)) {
    // Device chưa ready - có thể cần enable trong device tree overlay
    // Tạm thời return để không crash (các hàm UART sẽ trả về stub values)
    _uart_dev = nullptr;
    LOG_INF("KNX UART configured==================================1");
    return;
  }

  // Configure UART for KNX (19200, 8E1)
  // KNX yêu cầu 19200 8E1. DTS đã được cấu hình trong overlay.
  // Note: uart_configure runtime không được support (trả về -134 ENOTSUP)
  // nên ta tin tưởng vào DTS configuration.

  // Force Parity Configuration using HAL (Bypass Driver Limitation)
  // Reason: User was confused by TI driver definitions (Error 4=Break).
  // On nRF/Zephyr, Error 4 = FRAMING (Stop Bit Error) -> Parity Mismatch.

// Try to get base address from device config if possible, or use macro
// For nRF54L15 NS build, usually NRF_UARTE00_NS
#if defined(NRF_UARTE00_NS)
  // Base address handled viaHAL in writeUart
#elif defined(NRF_UARTE00)
  NRF_UARTE_Type *uarte_regs = NRF_UARTE00;
#elif defined(NRF_UARTE0)
  NRF_UARTE_Type *uarte_regs = NRF_UARTE0;
#else
  NRF_UARTE_Type *uarte_regs = nullptr;
#endif

  // Configure UART for KNX (19200, 8E1) using Zephyr API
  // Requires CONFIG_UART_USE_RUNTIME_CONFIGURE=y in prj.conf
  /// apply uart config
  struct uart_config cfg;
  cfg.baudrate = 19200;
  cfg.parity = UART_CFG_PARITY_EVEN;
  cfg.stop_bits = UART_CFG_STOP_BITS_1;
  cfg.data_bits = UART_CFG_DATA_BITS_8;
  cfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;

  int ret = uart_configure(_uart_dev, &cfg);
  if (ret == 0) {
    LOG_INF("UART configured to 19200 8E1 via API");
  } else {
    LOG_ERR("Failed to configure UART via API: %d. Relying on DTS.", ret);
  }
  //======================

  // Setup UART IRQ callback
  uart_irq_callback_user_data_set(_uart_dev, uart_irq_handler, this);

  // CRITICAL: Flush RX FIFO before enabling interrupt
  // This clears any garbage bytes that might trigger interrupt storm
  uint8_t dummy;
  int flushed = 0;
  while (uart_fifo_read(_uart_dev, &dummy, 1) == 1) {
    flushed++;
    if (flushed > 100)
      break; // Safety limit
  }
  if (flushed > 0) {
    LOG_INF("Flushed %d garbage bytes from UART RX FIFO", flushed);
  }

  // Enable UART RX interrupt
  uart_irq_rx_enable(_uart_dev);
  LOG_INF("KNX UART configured with ISR-triggered polling mode");
}

void NordicPlatform::closeUart() {
  if (_uart_dev) {
    uart_irq_rx_disable(_uart_dev);
    uart_irq_tx_disable(_uart_dev);
    uart_irq_callback_user_data_set(_uart_dev, nullptr, nullptr);
  }
}

int NordicPlatform::uartAvailable() {
  // *** RING BUFFER: Simple size check ***
  return ring_buf_size_get(&uart_rx_rb) > 0 ? 1 : 0;
}

// ============================================================================
// ISR-Triggered Polling Helper Functions
// ============================================================================
#ifdef USE_KNX_UART_API
bool NordicPlatform::isRxActive() { return rx_active; }

void NordicPlatform::checkRxIdle() {
  // Nếu không có data mới trong RX_IDLE_TIMEOUT_MS, tắt polling
  if (rx_active && ring_buf_size_get(&uart_rx_rb) == 0) {
    uint32_t now = k_uptime_get_32();
    if ((now - rx_last_byte_time) > RX_IDLE_TIMEOUT_MS) {
      rx_active = false;
    }
  }
}

void NordicPlatform::setRxTriggerCallback(void (*callback)(void)) {
  _rx_trigger_callback = callback;
}
#endif

size_t NordicPlatform::writeUart(const uint8_t data) {
  return writeUart(&data, 1);
}

size_t NordicPlatform::writeUart(const uint8_t *buffer, size_t size) {
  if (!_uart_dev || !buffer || size == 0) {
    return 0;
  }

  bool in_isr = k_is_in_isr();
  if (!in_isr) {
    k_mutex_lock(&_uart_mutex, K_FOREVER);
  }

  // Get UART Base Address
  NRF_UARTE_Type *uarte = nullptr;
#if defined(NRF_UARTE00_NS)
  uarte = NRF_UARTE00_NS;
#elif defined(NRF_UARTE00)
  uarte = NRF_UARTE00;
#elif defined(NRF_UARTE0)
  uarte = NRF_UARTE0;
#endif

  if (uarte) {
    // *** RAW HAL DMA IMPLEMENTATION (ROBUST) ***

    // 0. Flush Cache (Critical for DMA from Stack/RAM)
    sys_cache_data_flush_range((void *)buffer, size);

    // 1. Disable TX Interrupts (Prevent Storm & Zephyr Interference)
    nrf_uarte_int_disable(uarte, NRF_UARTE_INT_TXDRDY_MASK |
                                     NRF_UARTE_INT_ENDTX_MASK |
                                     NRF_UARTE_INT_TXSTARTED_MASK);

    // 2. Safety: Stop any ongoing TX and Clear Events
    nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPTX);
    nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_ENDTX);
    nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_TXSTOPPED);
    nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_TXDRDY);

    // 3. Configure DMA
    nrf_uarte_tx_buffer_set(uarte, buffer, size);

    // 4. Start TX
    nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STARTTX);

    // 5. Wait for ENDTX (Busy Wait - mimics synchronous API)
    // Timeout safety: ~600us/byte -> size*1000us + margin
    int timeout_us = size * 1000 + 2000;
    while (!nrf_uarte_event_check(uarte, NRF_UARTE_EVENT_ENDTX)) {
      if (timeout_us <= 0)
        break;
      k_busy_wait(10);
      timeout_us -= 10;
    }

    // 6. Stop TX (Clean up state)
    nrf_uarte_task_trigger(uarte, NRF_UARTE_TASK_STOPTX);

    // Clear event again
    nrf_uarte_event_clear(uarte, NRF_UARTE_EVENT_ENDTX);

  } else {
    // Fallback
    for (size_t i = 0; i < size; i++) {
      uart_poll_out(_uart_dev, buffer[i]);
    }
  }

  if (!in_isr) {
    k_mutex_unlock(&_uart_mutex);
  }
  return size;
}

int NordicPlatform::readUart() {
  // *** RING BUFFER: No malloc/free needed! ***
  uint8_t byte;
  if (ring_buf_get(&uart_rx_rb, &byte, 1) == 1) {
    return byte;
  }
  return -1;
}

size_t NordicPlatform::readBytesUart(uint8_t *buffer, size_t length) {
  if (!buffer || length == 0) {
    return 0;
  }
  // *** RING BUFFER: One call reads all available bytes ***
  return ring_buf_get(&uart_rx_rb, buffer, length);
}

bool NordicPlatform::overflowUart() {
  bool overflow = _uart_overflow;
  _uart_overflow = false; // Clear flag
  return overflow;
}

void NordicPlatform::flushUart() {
  // Zephyr UART không có flush trực tiếp
  // Đợi một chút để đảm bảo buffer trống
  k_sleep(K_MSEC(10));
}
#else
// Stub implementations khi tắt UART API
void NordicPlatform::setupUart() {}
void NordicPlatform::closeUart() {}
int NordicPlatform::uartAvailable() { return 0; }
size_t NordicPlatform::writeUart(const uint8_t data) { return 0; }
size_t NordicPlatform::writeUart(const uint8_t *buffer, size_t size) {
  (void)buffer;
  (void)size;
  return 0;
}
int NordicPlatform::readUart() { return -1; }
size_t NordicPlatform::readBytesUart(uint8_t *buffer, size_t length) {
  (void)buffer;
  (void)length;
  return 0;
}
bool NordicPlatform::overflowUart() { return false; }
void NordicPlatform::flushUart() {}
#endif

void NordicPlatform::set_knx_process_rx_isr(void (*func)(void)) {
  _knx_process_rx_isr = func;
}

// ============================================================================
// Memory Functions
// ============================================================================

// Static handler removed in favor of direct load

#ifdef USE_KNX_MEMORY_API
// Settings API Chunked Storage - TF-M compatible (giống pattern của BLE Mesh)

/**
 * @brief Load EEPROM buffer from Settings (giống BLE Mesh pattern)
 * @param size Size of EEPROM buffer to allocate and load
 * @return Pointer to EEPROM buffer
 */
uint8_t *NordicPlatform::getEepromBuffer(uint32_t size) {
  k_mutex_lock(&_memory_mutex, K_FOREVER);

  if (_eeprom_buffer == nullptr) {
    _eeprom_size = (size <= KNX_EEPROM_SIZE) ? size : KNX_EEPROM_SIZE;
    _eeprom_buffer = s_eeprom_static_buf;

    memset(_eeprom_buffer, 0xFF, _eeprom_size);
    LOG_INF(">>> EEPROM buffer using static memory: %d bytes (Subtree Load "
            "Optimized)",
            _eeprom_size);

    // OPTIMIZED LOAD: Load all settings in one pass using subtree
    // This calls settings_load_callback for each found key under "knx/"
    settings_load_subtree(KNX_SETTINGS_PREFIX);
  }

  k_mutex_unlock(&_memory_mutex);
  return _eeprom_buffer;
}

/**
 * @brief Save EEPROM buffer to Settings (giống BLE Mesh pattern)
 */
void NordicPlatform::commitToEeprom() {
  k_mutex_lock(&_memory_mutex, K_FOREVER);
  if (_eeprom_buffer == nullptr) {
    k_mutex_unlock(&_memory_mutex);
    return;
  }

  LOG_INF(">>> commitToEeprom: Saving %d bytes in %d chunks", _eeprom_size,
          SETTINGS_CHUNK_COUNT);
  LOG_HEXDUMP_INF(_eeprom_buffer, 20, "First 20 bytes to save:");
  int saved_chunks = 0;
  int failed_chunks = 0;

  for (uint32_t i = 0;
       i < SETTINGS_CHUNK_COUNT && (i * SETTINGS_CHUNK_SIZE) < _eeprom_size;
       i++) {
    size_t chunk_offset = i * SETTINGS_CHUNK_SIZE;
    size_t chunk_size = (chunk_offset + SETTINGS_CHUNK_SIZE <= _eeprom_size)
                            ? SETTINGS_CHUNK_SIZE
                            : (_eeprom_size - chunk_offset);

    // Build settings key: "knx/eeprom/0", "knx/eeprom/1", etc.
    char key[32];
    snprintf(key, sizeof(key), "%s/%d", KNX_SETTINGS_PREFIX, i);

    LOG_INF(">>> About to call settings_save_one('%s', size=%d)...", key,
            chunk_size);
    int rc = settings_save_one(key, _eeprom_buffer + chunk_offset, chunk_size);
    LOG_INF(">>> settings_save_one() returned rc=%d", rc);
    if (rc < 0) {
      LOG_ERR(">>> Chunk %d SAVE FAILED, rc=%d", i, rc);
      failed_chunks++;
    } else {
      LOG_INF(">>> Chunk %d saved OK to Settings", i);
      saved_chunks++;
    }
  }

  if (failed_chunks == 0) {
    LOG_INF(">>> Settings: ALL %d chunks saved successfully!", saved_chunks);
  } else {
    LOG_ERR(">>> Settings: %d/%d chunks FAILED!", failed_chunks,
            saved_chunks + failed_chunks);
  }
  k_mutex_unlock(&_memory_mutex);
}

uint8_t *NordicPlatform::getNonVolatileMemoryStart() {
  // Trả về EEPROM buffer để KNX stack có thể đọc/ghi
  // Nếu chưa allocate, allocate với kích thước mặc định
  if (_eeprom_buffer == nullptr) {
    getEepromBuffer(KNX_EEPROM_SIZE);
  }
  return _eeprom_buffer;
}

size_t NordicPlatform::getNonVolatileMemorySize() { return KNX_EEPROM_SIZE; }

void NordicPlatform::commitNonVolatileMemory() {
  // Gọi commitToEeprom để ghi toàn bộ buffer vào NVS
  LOG_INF("commitNonVolatileMemory called -> commitToEeprom");
  commitToEeprom();
}

uint32_t NordicPlatform::writeNonVolatileMemory(uint32_t relativeAddress,
                                                uint8_t *buffer, size_t size) {
  // CRITICAL DEBUG: Check what is being written to address 0 (Header)
  if (relativeAddress == 0 && size >= 2 && buffer != nullptr) {
    LOG_ERR(">>> writeNonVolatileMemory(addr=0) [FIX VERIFIED]: First 2 bytes "
            "[0x%02X 0x%02X]",
            buffer[0], buffer[1]);
  }

  if (relativeAddress + size > KNX_EEPROM_SIZE) {
    LOG_ERR("NVS write: address 0x%x + size %d > max %d", relativeAddress, size,
            KNX_EEPROM_SIZE);
    return 0;
  }

  if (!buffer || size == 0) {
    // If size is 0, do nothing but return the current address to maintain
    // flashPos
    return relativeAddress;
  }

  // CRITICAL: Nếu buffer chưa được allocate, allocate MỚI với 0xFF
  // KHÔNG load từ NVS vì sẽ overwrite data mới!
  // CRITICAL: Nếu buffer chưa được allocate, sử dụng static buffer
  if (_eeprom_buffer == nullptr) {
    k_mutex_lock(&_memory_mutex, K_FOREVER);
    if (_eeprom_buffer == nullptr) {
      _eeprom_buffer = s_eeprom_static_buf;
      _eeprom_size = KNX_EEPROM_SIZE;
      memset(_eeprom_buffer, 0xFF, KNX_EEPROM_SIZE);
      LOG_INF("writeNonVolatileMemory: using static buffer %d bytes",
              KNX_EEPROM_SIZE);
    }
    k_mutex_unlock(&_memory_mutex);
  }

  // Copy vào EEPROM buffer
  if (_eeprom_buffer != nullptr && relativeAddress + size <= _eeprom_size) {
    memcpy(_eeprom_buffer + relativeAddress, buffer, size);
  }

  return relativeAddress + size;
}

uint32_t NordicPlatform::readNonVolatileMemory(uint32_t relativeAddress,
                                               uint8_t *buffer, size_t size) {
  if (relativeAddress + size > KNX_EEPROM_SIZE) {
    LOG_ERR("NVS read: address 0x%x + size %d > max %d", relativeAddress, size,
            KNX_EEPROM_SIZE);
    return 0;
  }

  if (!buffer || size == 0) {
    return 0;
  }

  // Đọc từ EEPROM buffer (đã được load từ NVS trong getEepromBuffer)
  if (_eeprom_buffer != nullptr && relativeAddress + size <= _eeprom_size) {
    memcpy(buffer, _eeprom_buffer + relativeAddress, size);
    return size;
  } else {
    // Buffer chưa được allocate - allocate trước
    getEepromBuffer(KNX_EEPROM_SIZE);
    if (_eeprom_buffer != nullptr && relativeAddress + size <= _eeprom_size) {
      memcpy(buffer, _eeprom_buffer + relativeAddress, size);
      return size;
    }
  }

  // Fallback: fill với 0xFF
  memset(buffer, 0xFF, size);
  return size;
}

uint32_t NordicPlatform::writeNonVolatileMemory(uint32_t relativeAddress,
                                                uint8_t value, size_t repeat) {
  if (relativeAddress + repeat > KNX_EEPROM_SIZE) {
    return 0;
  }

  // Tạo buffer và fill với value
  uint8_t *buffer = (uint8_t *)k_malloc(repeat);
  if (!buffer) {
    return 0;
  }

  memset(buffer, value, repeat);
  uint32_t written = writeNonVolatileMemory(relativeAddress, buffer, repeat);
  k_free(buffer);

  return written;
}
#else
// Stub implementations khi tắt Memory API
uint8_t *NordicPlatform::getEepromBuffer(uint32_t size) {
  (void)size;
  return nullptr;
}
void NordicPlatform::commitToEeprom() {}
uint8_t *NordicPlatform::getNonVolatileMemoryStart() { return nullptr; }
size_t NordicPlatform::getNonVolatileMemorySize() { return 0; }
void NordicPlatform::commitNonVolatileMemory() {}
uint32_t NordicPlatform::writeNonVolatileMemory(uint32_t relativeAddress,
                                                uint8_t *buffer, size_t size) {
  (void)relativeAddress;
  (void)buffer;
  (void)size;
  return 0;
}
uint32_t NordicPlatform::readNonVolatileMemory(uint32_t relativeAddress,
                                               uint8_t *buffer, size_t size) {
  (void)relativeAddress;
  (void)buffer;
  (void)size;
  return 0;
}
uint32_t NordicPlatform::writeNonVolatileMemory(uint32_t relativeAddress,
                                                uint8_t value, size_t repeat) {
  (void)relativeAddress;
  (void)value;
  (void)repeat;
  return 0;
}
#endif

// ============================================================================
// Arduino-compatible functions for KNX stack
// ============================================================================
// KNX stack sử dụng các hàm Arduino này, cần implement cho Zephyr

/**
 * @brief Get milliseconds since boot (Arduino-compatible)
 * @return Milliseconds since boot
 */
uint32_t millis() { return (uint32_t)k_uptime_get(); }

/**
 * @brief Delay in milliseconds (Arduino-compatible)
 * @param ms Milliseconds to delay
 */
void delay(uint32_t ms) { k_sleep(K_MSEC(ms)); }

/**
 * @brief Set GPIO pin mode (Arduino-compatible)
 * @param pin GPIO pin number
 * @param mode Pin mode (INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN)
 */
void pinMode(uint32_t pin, uint32_t mode) {
  // TODO: Implement GPIO pin mode configuration
  // Có thể dùng Zephyr GPIO API hoặc Nordic DK library
  // Tạm thời để trống vì KNX có thể không cần GPIO cho LED/Button
  (void)pin;
  (void)mode;
}

/**
 * @brief Write digital value to GPIO pin (Arduino-compatible)
 * @param pin GPIO pin number
 * @param value Pin value (LOW=0, HIGH=1)
 */
void digitalWrite(uint32_t pin, uint32_t value) {
  // TODO: Implement GPIO write
  // Có thể dùng Zephyr GPIO API hoặc Nordic DK library (dk_set_led)
  // Tạm thời để trống vì KNX có thể không cần GPIO cho LED
  (void)pin;
  (void)value;
}

/**
 * @brief Read digital value from GPIO pin (Arduino-compatible)
 * @param pin GPIO pin number
 * @return Pin value (LOW=0, HIGH=1)
 */
uint32_t digitalRead(uint32_t pin) {
  // TODO: Implement GPIO read
  // Có thể dùng Zephyr GPIO API
  // Tạm thời trả về 0
  (void)pin;
  return 0;
}

/**
 * @brief Delay in microseconds (Arduino-compatible)
 * @param us Microseconds to delay
 */
void delayMicroseconds(unsigned int us) {
  // Zephyr không có delay microseconds chính xác
  // Dùng k_busy_wait (busy wait) cho delay ngắn
  if (us < 1000) {
    k_busy_wait(us);
  } else {
    // Nếu quá dài, dùng k_sleep với milliseconds
    k_sleep(K_MSEC((us + 999) / 1000));
  }
}

/**
 * @brief Attach interrupt to GPIO pin (Arduino-compatible)
 * @param pin GPIO pin number
 * @param callback Interrupt callback function
 * @param mode Interrupt mode (CHANGE, RISING, FALLING)
 */
typedef void (*voidFuncPtr)(void);
void attachInterrupt(uint32_t pin, voidFuncPtr callback, uint32_t mode) {
  // TODO: Implement GPIO interrupt
  // Có thể dùng Zephyr GPIO interrupt API
  // Tạm thời để trống vì KNX có thể không cần button interrupt
  (void)pin;
  (void)callback;
  (void)mode;
}

int NordicPlatform::settings_load_callback(const char *name, size_t len,
                                           settings_read_cb read_cb,
                                           void *cb_arg) {
  const char *next;
  uint32_t i = 0;

  // Key format is: knx/eeprom/<index> or knx/seq/<send|tool>
  // name here is relative to "knx", so it should be "eeprom/<index>" or
  // "seq/<send|tool>"

  if (strncmp(name, "seq/", 4) == 0) {
    const char *subkey = name + 4;
    if (strcmp(subkey, "send") == 0) {
      if (len != sizeof(_seq_send)) {
        LOG_ERR(">>> [SUBTREE] SeqSend error: length mismatch (%zu != %zu)",
                len, sizeof(_seq_send));
        return 0;
      }
      ssize_t rc = read_cb(cb_arg, &_seq_send, sizeof(_seq_send));
      if (rc == sizeof(_seq_send)) {
        LOG_INF(">>> [SUBTREE] Loaded SeqSend: %llu (0x%llX)", _seq_send,
                _seq_send);
      }
      return 0;
    } else if (strcmp(subkey, "tool") == 0) {
      if (len != sizeof(_seq_tool)) {
        LOG_ERR(">>> [SUBTREE] SeqTool error: length mismatch (%zu != %zu)",
                len, sizeof(_seq_tool));
        return 0;
      }
      ssize_t rc = read_cb(cb_arg, &_seq_tool, sizeof(_seq_tool));
      if (rc == sizeof(_seq_tool)) {
        LOG_INF(">>> [SUBTREE] Loaded SeqTool: %llu (0x%llX)", _seq_tool,
                _seq_tool);
      }
      return 0;
    }
    return 0;
  }

  if (strncmp(name, "eeprom/", 7) != 0) {
    return 0;
  }

  next = name + 7;
  if (!*next)
    return 0;

  i = strtoul(next, NULL, 10);
  uint32_t offset = i * SETTINGS_CHUNK_SIZE;

  if (_eeprom_buffer && offset < _eeprom_size) {
    size_t chunk_len = (_eeprom_size - offset) < SETTINGS_CHUNK_SIZE
                           ? (_eeprom_size - offset)
                           : SETTINGS_CHUNK_SIZE;
    if (len > chunk_len)
      len = chunk_len;

    ssize_t rc = read_cb(cb_arg, _eeprom_buffer + offset, len);
    if (rc >= 0) {
      LOG_INF(">>> [SUBTREE] Loaded chunk %u (%d bytes)", i, (int)rc);
      return 0;
    }
  }

  return 0;
}

void NordicPlatform::saveSequenceNumber(bool toolAccess, uint64_t seqNum) {
  char key[32];
  snprintf(key, sizeof(key), "knx/seq/%s", toolAccess ? "tool" : "send");
  int rc = settings_save_one(key, &seqNum, sizeof(seqNum));
  if (rc == 0) {
    LOG_INF(">>> Isolated NVS SAVE SUCCESS: %s = %llu (0x%llX)",
            toolAccess ? "TOOL" : "SEND", seqNum, seqNum);

    // Explicitly commit to persistence backend (NVS)
    settings_save();
  } else {
    LOG_ERR("!!! Isolated NVS SAVE FAILED [%d]: %s", rc, key);
  }
}

uint64_t NordicPlatform::loadSequenceNumber(bool toolAccess) {
  // Trigger a load for the specific key to ensure we have the latest
  char key[32];
  snprintf(key, sizeof(key), "knx/seq/%s", toolAccess ? "tool" : "send");

  // settings_load_subtree triggers settings_load_callback which updates
  // _seq_send/_seq_tool
  settings_load_subtree("knx/seq");

  return toolAccess ? _seq_tool : _seq_send;
}
