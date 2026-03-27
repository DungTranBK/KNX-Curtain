/**********************************************************************
 * nordic_platform.cpp
 * Nordic Platform Implementation cho KNX Stack (Zephyr / Nordic)
 *
 * UART RX: MIGRATED FROM k_fifo + k_malloc TO ring_buffer
 * MEMORY  : KEPT ORIGINAL (Settings API)
 *********************************************************************/

#include "nordic_platform.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <stdio.h>
#include <dk_buttons_and_leds.h>

LOG_MODULE_REGISTER(nordic_platform, LOG_LEVEL_INF);

// ============================================================================
// UART CONFIG
// ============================================================================

#ifdef USE_KNX_UART_API
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>

#define KNX_UART_DEVICE DT_NODELABEL(uart00)

static const struct uart_config uart_cfg = {
    .baudrate  = 19200,
    .parity    = UART_CFG_PARITY_EVEN,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
};
#endif

// ============================================================================
// UART RING BUFFER (NEW)
// ============================================================================

#ifdef USE_KNX_UART_API
#define UART_RX_RINGBUF_SIZE 256

static uint8_t uart_rx_rb_data[UART_RX_RINGBUF_SIZE];
static struct ring_buf uart_rx_rb;
#endif

// ============================================================================
// UART IRQ HANDLER
// ============================================================================

#ifdef USE_KNX_UART_API
void uart_irq_handler(const struct device* dev, void* user_data)
{
    NordicPlatform* platform = (NordicPlatform*)user_data;

    if (!uart_irq_update(dev)) {
        return;
    }

    if (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        bool data_received = false;

        while (uart_fifo_read(dev, &byte, 1) == 1) {

            /* ===================== OLD IMPLEMENTATION =====================
            uint8_t* byte_ptr = (uint8_t*)k_malloc(sizeof(uint8_t));
            if (byte_ptr) {
                *byte_ptr = byte;
                k_fifo_put(platform->getUartRxFifo(), byte_ptr);
                data_received = true;
            } else {
                *(platform->getUartOverflow()) = true;
            }
            =============================================================== */

            /* ===================== NEW IMPLEMENTATION ===================== */
            if (ring_buf_put(&uart_rx_rb, &byte, 1) == 1) {
                data_received = true;
            } else {
                *(platform->getUartOverflow()) = true;
            }
            /* =============================================================== */
        }

        void (*process_rx_isr)(void) = platform->getKnxProcessRxIsr();
        if (data_received && process_rx_isr) {
            process_rx_isr(); // timing-critical ACK
        }
    }
}
#endif

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

NordicPlatform::NordicPlatform() :
#ifdef USE_KNX_UART_API
    _uart_dev(nullptr),
    _uart_overflow(false),
#endif
#ifdef USE_KNX_MEMORY_API
    _eeprom_buffer(nullptr),
    _eeprom_size(0),
#endif
    _knx_process_rx_isr(nullptr)
{
#ifdef USE_KNX_UART_API
    k_mutex_init(&_uart_mutex);

    ring_buf_init(&uart_rx_rb,
                  UART_RX_RINGBUF_SIZE,
                  uart_rx_rb_data);
#endif

#ifdef USE_KNX_MEMORY_API
    k_mutex_init(&_memory_mutex);
#endif
}

NordicPlatform::~NordicPlatform()
{
#ifdef USE_KNX_UART_API
    closeUart();
#endif

#ifdef USE_KNX_MEMORY_API
    if (_eeprom_buffer) {
        k_free(_eeprom_buffer);
    }
#endif
}

// ============================================================================
// UART API
// ============================================================================

#ifdef USE_KNX_UART_API

void NordicPlatform::setupUart()
{
    _uart_dev = DEVICE_DT_GET(KNX_UART_DEVICE);
    if (!_uart_dev || !device_is_ready(_uart_dev)) {
        LOG_ERR("KNX UART not ready");
        _uart_dev = nullptr;
        return;
    }

    uart_irq_callback_user_data_set(_uart_dev, uart_irq_handler, this);
    uart_irq_rx_enable(_uart_dev);
}

void NordicPlatform::closeUart()
{
    if (_uart_dev) {
        uart_irq_rx_disable(_uart_dev);
        uart_irq_callback_user_data_set(_uart_dev, nullptr, nullptr);
    }
}

int NordicPlatform::uartAvailable()
{
    return ring_buf_size_get(&uart_rx_rb) > 0;
}

int NordicPlatform::readUart()
{
    uint8_t byte;

    /* ===================== OLD =====================
    uint8_t* p = (uint8_t*)k_fifo_get(&_uart_rx_fifo, K_NO_WAIT);
    if (p) {
        byte = *p;
        k_free(p);
        return byte;
    }
    ================================================= */

    if (ring_buf_get(&uart_rx_rb, &byte, 1) == 1) {
        return byte;
    }

    return -1;
}

size_t NordicPlatform::readBytesUart(uint8_t* buffer, size_t length)
{
    if (!buffer || length == 0) return 0;

    /* ===================== OLD =====================
    size_t read = 0;
    for (...) { k_fifo_get + k_free }
    ================================================= */

    return ring_buf_get(&uart_rx_rb, buffer, length);
}

bool NordicPlatform::overflowUart()
{
    bool o = _uart_overflow;
    _uart_overflow = false;
    return o;
}

#endif // USE_KNX_UART_API

// ============================================================================
// MEMORY API (UNCHANGED – ORIGINAL)
// ============================================================================
// >>>>>> TOÀN BỘ PHẦN MEMORY CỦA BẠN GIỮ NGUYÊN <<<<<<
// (Settings API, EEPROM buffer, mutex, commit, read/write)
// ❗ Không thay đổi vì KHÔNG nằm trong ISR và đã đúng
