# BLE_NORDIC SDK - API Reference

## Fast Provision API

### Functions

#### `int fast_prov_init(void)`
Khởi tạo Fast Provision module.

**Returns:**
- `0`: Thành công
- `-EALREADY`: Đã được khởi tạo
- Error code: Lỗi khởi tạo

**Example:**
```c
if (fast_prov_init() == 0) {
    LOG_INF("Fast Provision initialized");
}
```

#### `int fast_prov_start(fast_prov_par_t *par)`
Bắt đầu session Fast Provision.

**Parameters:**
- `par`: Struct chứa thông tin provisioning

**Returns:**
- `0`: Thành công
- `-EINVAL`: Parameters không hợp lệ
- `-EBUSY`: Session đang chạy

**Example:**
```c
fast_prov_par_t prov_par = {
    .unicast_addr = 0x0002,
    .flags = 0,
    .iv_index = 0,
    .net_idx = 0,
    .app_idx = 0
};
fast_prov_start(&prov_par);
```

#### `int fast_prov_dev_add(uint8_t *dev_key, uint16_t addr)`
Thêm device vào network sau khi provision.

**Parameters:**
- `dev_key`: Device key (16 bytes)
- `addr`: Unicast address của device

**Returns:**
- `0`: Thành công
- `-EINVAL`: Parameters không hợp lệ

#### `void bind_all_models_with_appkey(uint16_t app_idx)`
Bind tất cả models với AppKey.

**Parameters:**
- `app_idx`: Application Key index

**Note:** Hàm này tự động lưu settings vào flash.

## Network Management API

### Functions

#### `int network_init(void)`
Khởi tạo network management module.

**Returns:**
- `0`: Thành công
- Error code: Lỗi khởi tạo

#### `int network_add_appkey(uint16_t net_idx, uint16_t app_idx, const uint8_t *key)`
Thêm Application Key vào network.

**Parameters:**
- `net_idx`: Network Key index
- `app_idx`: Application Key index
- `key`: AppKey data (16 bytes)

**Returns:**
- `0`: Thành công
- `-EEXIST`: AppKey đã tồn tại

#### `uint16_t network_get_appkey_index(void)`
Lấy AppKey index hiện tại.

**Returns:** AppKey index hoặc `BT_MESH_KEY_UNUSED`

## Mesh Message Handler API

### Functions

#### `int mesh_message_handler_init(void)`
Khởi tạo mesh message handlers.

**Returns:**
- `0`: Thành công
- Error code: Lỗi khởi tạo

#### `int send_onoff_message(uint16_t addr, uint8_t onoff)`
Gửi OnOff message đến device.

**Parameters:**
- `addr`: Địa chỉ unicast của device
- `onoff`: 0 = OFF, 1 = ON

**Returns:**
- `0`: Thành công
- Error code: Lỗi gửi message

**Example:**
```c
// Bật đèn
send_onoff_message(0x0002, 1);

// Tắt đèn
send_onoff_message(0x0002, 0);
```

## LED Control API

### Functions

#### `int led_init(void)`
Khởi tạo LED module.

**Returns:**
- `0`: Thành công

#### `void led_set(uint8_t led_idx, bool state)`
Bật/tắt LED.

**Parameters:**
- `led_idx`: LED index (0-3)
- `state`: true = ON, false = OFF

#### `void led_toggle(uint8_t led_idx)`
Đảo trạng thái LED.

**Parameters:**
- `led_idx`: LED index (0-3)

#### `void led_blink(uint8_t led_idx, uint32_t period_ms)`
Nhấp nháy LED.

**Parameters:**
- `led_idx`: LED index
- `period_ms`: Chu kỳ nhấp nháy (ms)

## Button API

### Functions

#### `int button_init(void)`
Khởi tạo button module.

**Returns:**
- `0`: Thành công

#### `bool button_pressed(uint8_t btn_idx)`
Kiểm tra button có được nhấn không.

**Parameters:**
- `btn_idx`: Button index (0-3)

**Returns:** true nếu button đang được nhấn

#### `void button_register_callback(button_callback_t callback)`
Đăng ký callback cho button events.

**Parameters:**
- `callback`: Function pointer cho callback

**Callback signature:**
```c
void button_callback(uint8_t btn_idx, bool pressed);
```

## UART Driver API

### Functions

#### `int uart_driver_init(void)`
Khởi tạo UART driver.

**Returns:**
- `0`: Thành công

#### `int uart_send_data(const uint8_t *data, size_t len)`
Gửi data qua UART.

**Parameters:**
- `data`: Buffer chứa data
- `len`: Độ dài data

**Returns:**
- `0`: Thành công
- Error code: Lỗi gửi

#### `int uart_receive_data(uint8_t *buffer, size_t max_len)`
Nhận data từ UART.

**Parameters:**
- `buffer`: Buffer để chứa data
- `max_len`: Kích thước tối đa của buffer

**Returns:** Số bytes nhận được

## Watchdog API

### Functions

#### `int watchdog_init(void)`
Khởi tạo watchdog timer.

**Returns:**
- `0`: Thành công

#### `void watchdog_feed(void)`
Feed watchdog để reset timer.

**Note:** Phải gọi định kỳ để tránh system reset.

## Data Structures

### `fast_prov_par_t`
```c
typedef struct {
    uint8_t dev_key[16];     // Device key
    uint16_t unicast_addr;   // Unicast address
    uint8_t flags;           // Provisioning flags
    uint32_t iv_index;       // IV index
    uint16_t net_idx;        // Network key index
    uint16_t app_idx;        // Application key index
} fast_prov_par_t;
```

### `mesh_msg_callback_t`
```c
typedef void (*mesh_msg_callback_t)(uint16_t src_addr, uint16_t dst_addr,
                                   const uint8_t *data, size_t len);
```

## Error Codes

- `0`: Success
- `-EINVAL`: Invalid parameters
- `-EEXIST`: Already exists
- `-EBUSY`: Resource busy
- `-ENOENT`: Not found
- `-EIO`: I/O error
- `-ETIMEDOUT`: Timeout
- `-ENOTSUP`: Not supported

## Configuration Macros

### Fast Provision
```c
#define FAST_PROV_VENDOR_COMPANY_ID    0x1102
#define DEFAULT_APPKEY_INDEX           0x0001
#define MAIN_APPKEY_INDEX              0x0000
```

### Network
```c
#define MAX_APP_KEYS                   4
#define MAX_NET_KEYS                   2
```

### Hardware
```c
#define MAX_LED_COUNT                  4
#define MAX_BUTTON_COUNT               4
```

---

**Version**: 1.0.0
**Generated**: January 2026</content>
<parameter name="filePath">d:\Lumi\LM_2025\Nordic\.src\BLE_NORDIC\docs\API_REFERENCE.md