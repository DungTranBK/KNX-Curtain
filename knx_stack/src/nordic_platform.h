#pragma once

/**
 * @file nordic_platform.h
 * @brief Nordic Platform Implementation cho KNX Stack
 * @details Platform abstraction layer cho nRF54L15DK (Zephyr RTOS)
 *
 * Tương thích với KNX stack platform interface
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <cstddef>
#include <cstdint>

#include "knx/platform.h"

// Conditional includes - chỉ include khi cần
#ifdef USE_KNX_UART_API
#include <zephyr/drivers/uart.h>
#endif

#ifdef USE_KNX_MEMORY_API
#include <zephyr/settings/settings.h>
#endif

// Forward declaration for UART interrupt handler (friend function)
struct device;
#ifdef USE_KNX_UART_API
void uart_irq_handler(const struct device* dev, void* user_data);
#endif

/**
 * @brief Nordic Platform class - Implement Platform interface cho KNX stack
 *
 * @details
 * Class này implement tất cả các hàm cần thiết cho KNX stack trên nRF54L15DK:
 * - UART: Cho KNX TP (Twisted Pair) communication
 * - Memory: EEPROM emulation sử dụng Zephyr Settings API
 * - GPIO: Cho LED, Button (nếu cần)
 * - Basic: Restart, Fatal Error, Serial Number, MAC Address
 */
class NordicPlatform : public Platform {
 public:
  NordicPlatform();
  virtual ~NordicPlatform();

  // ====================================================================
  // Basic Functions (Required)
  // ====================================================================

  /**
   * @brief Restart chip
   */
  void restart() override;

  /**
   * @brief Fatal error handler - blink LED và reboot
   */
  void fatalError() override;

  /**
   * @brief Get unique serial number từ hardware
   * @return 32-bit unique serial number
   */
  uint32_t uniqueSerialNumber() override;

  /**
   * @brief Get MAC address
   * @param data Buffer 6 bytes để lưu MAC address
   */
  void macAddress(uint8_t* data) override;

  // ====================================================================
  // UART Functions (Required for KNX TP)
  // ====================================================================

  /**
   * @brief Setup UART cho KNX TP
   * Configuration: 19200 baud, 8E1 (8 data, Even parity, 1 stop)
   */
  void setupUart() override;

  /**
   * @brief Close UART
   */
  void closeUart() override;

  /**
   * @brief Check if UART has data available
   * @return Number of bytes available
   */
  int uartAvailable() override;

  /**
   * @brief Write single byte to UART
   * @param data Byte to write
   * @return Number of bytes written (1 or 0)
   */
  size_t writeUart(const uint8_t data) override;

  /**
   * @brief Write buffer to UART
   * @param buffer Data buffer
   * @param size Number of bytes to write
   * @return Number of bytes written
   */
  size_t writeUart(const uint8_t* buffer, size_t size) override;

  /**
   * @brief Read single byte from UART
   * @return Byte read, or -1 if no data
   */
  int readUart() override;

  /**
   * @brief Read multiple bytes from UART
   * @param buffer Buffer to store data
   * @param length Maximum number of bytes to read
   * @return Number of bytes actually read
   */
  size_t readBytesUart(uint8_t* buffer, size_t length) override;

  /**
   * @brief Check UART overflow
   * @return true if overflow occurred
   */
  bool overflowUart() override;

  /**
   * @brief Flush UART buffers
   */
  void flushUart() override;

#ifdef USE_KNX_UART_API
  // NOTE: Ring buffer được khai báo static trong .cpp, không cần getter

  /**
   * @brief Get UART overflow flag pointer (for interrupt handler)
   * @return Pointer to overflow flag
   */
  bool* getUartOverflow() { return &_uart_overflow; }

  /**
   * @brief Get KNX processRx ISR function pointer (for interrupt handler)
   * @return Function pointer
   */
  void (*getKnxProcessRxIsr())(void) { return _knx_process_rx_isr; }

  // ====================================================================
  // ISR-Triggered Polling Mode Functions
  // ====================================================================

  /**
   * @brief Check if RX is active (data pending)
   * @return true if polling should run
   */
  bool isRxActive();

  /**
   * @brief Check and update RX idle state
   * Call this from work queue to auto-disable polling when idle
   */
  void checkRxIdle();

  /**
   * @brief Set trigger callback (called from ISR when data arrives)
   * @param callback Function to call, typically reschedules work queue
   */
  void setRxTriggerCallback(void (*callback)(void));
#endif

  // ====================================================================
  // Memory Functions (Required for EEPROM emulation)
  // ====================================================================

  /**
   * @brief Get EEPROM buffer
   * @param size Required buffer size
   * @return Pointer to EEPROM buffer
   */
  uint8_t* getEepromBuffer(uint32_t size) override;

  /**
   * @brief Commit EEPROM buffer to flash
   */
  void commitToEeprom() override;

  /**
   * @brief Get non-volatile memory start address
   * @return Pointer to memory start (nullptr if using Settings API)
   */
  uint8_t* getNonVolatileMemoryStart() override;

  /**
   * @brief Get non-volatile memory size
   * @return Memory size in bytes
   */
  size_t getNonVolatileMemorySize() override;

  /**
   * @brief Commit non-volatile memory to flash
   */
  void commitNonVolatileMemory() override;

  /**
   * @brief Write to non-volatile memory
   * @param relativeAddress Relative address (offset from start)
   * @param buffer Data buffer
   * @param size Number of bytes to write
   * @return Number of bytes written
   */
  uint32_t writeNonVolatileMemory(uint32_t relativeAddress, uint8_t* buffer,
                                  size_t size) override;

  /**
   * @brief Read from non-volatile memory
   * @param relativeAddress Relative address (offset from start)
   * @param buffer Buffer to store data
   * @param size Number of bytes to read
   * @return Number of bytes read
   */
  uint32_t readNonVolatileMemory(uint32_t relativeAddress, uint8_t* buffer,
                                 size_t size) override;

  /**
   * @brief Write repeated value to non-volatile memory
   * @param relativeAddress Relative address
   * @param value Value to write
   * @param repeat Number of times to repeat
   * @return Number of bytes written
   */
  uint32_t writeNonVolatileMemory(uint32_t relativeAddress, uint8_t value,
                                  size_t repeat) override;
  void saveSequenceNumber(bool toolAccess, uint64_t seqNum) override;
  uint64_t loadSequenceNumber(bool toolAccess) override;

 protected:
  // UART device
  const struct device* _uart_dev;

  // EEPROM buffer - STATIC để share giữa các instances
  static uint8_t* _eeprom_buffer;
  static uint32_t _eeprom_size;
  static uint64_t _seq_send;
  static uint64_t _seq_tool;

  // Mutexes for thread safety
  struct k_mutex _uart_mutex;
  struct k_mutex _memory_mutex;

  // NOTE: UART RX dùng ring_buffer static trong .cpp (không cần k_fifo member)

  // UART overflow flag
  bool _uart_overflow;

  // Reboot work
  struct k_work_delayable _reboot_work;

  // KNX processRx function pointer (for interrupt processing)
  // Function này được gọi trực tiếp trong UART interrupt
  // Đảm bảo ACK được gửi kịp thời (< 1ms)
  void (*_knx_process_rx_isr)(void);

// Friend function để UART interrupt handler có thể access protected members
#ifdef USE_KNX_UART_API
  friend void uart_irq_handler(const struct device* dev, void* user_data);
#endif

 public:
  /**
   * @brief Set KNX processRx function pointer (for interrupt processing)
   *
   * @param func Function pointer to knx_process_rx_from_isr()
   *
   * @details
   * UART interrupt handler sẽ gọi function này trực tiếp trong interrupt
   * để xử lý KNX bytes và gửi ACK ngay lập tức
   *
   * ⚠️ QUAN TRỌNG: KNX TP protocol yêu cầu ACK phải được gửi
   * trong quá trình nhận frame (khi nhận byte thứ 7), không thể đợi work queue!
   */
  void set_knx_process_rx_isr(void (*func)(void));

  /**
   * @brief Helper for settings_load_subtree()
   */
  static int settings_load_callback(const char* name, size_t len,
                                    settings_read_cb read_cb, void* cb_arg);
};
