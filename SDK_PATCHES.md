# Hướng dẫn sửa SDK để dùng MAC từ Fast Provision

## Tổng quan

Để thay thế MAC từ bt_hci_core bằng MAC từ fast provision, cần sửa 2 file trong SDK:

1. `C:/ncs/v3.0.1/zephyr/subsys/bluetooth/host/addr.c` - Hàm `bt_addr_le_create_static()`
2. `C:/ncs/v3.0.1/zephyr/subsys/bluetooth/host/id.c` - Hàm `bt_setup_random_id_addr()`

## File 1: addr.c

### Vị trí: `C:/ncs/v3.0.1/zephyr/subsys/bluetooth/host/addr.c`

### Hàm cần sửa: `bt_addr_le_create_static()` (Line 40-52)

### Code gốc:
```c
int bt_addr_le_create_static(bt_addr_le_t *addr)
{
	int err;

	err = create_random_addr(addr);
	if (err) {
		return err;
	}

	BT_ADDR_SET_STATIC(&addr->a);

	return 0;
}
```

### Code sau khi sửa:
```c
int bt_addr_le_create_static(bt_addr_le_t *addr)
{
	int err;
	uint8_t mac[6] = {0};

	/* Thử lấy MAC từ fast provision */
	extern bool fast_provision_get_mac_for_bt_id(uint8_t *mac);
	if (fast_provision_get_mac_for_bt_id(mac)) {
		/* Copy MAC vào address */
		memcpy(addr->a.val, mac, 6);
		addr->type = BT_ADDR_LE_RANDOM;
		
		/* Đảm bảo static random address bit được set */
		addr->a.val[5] |= 0xC0;
		
		return 0;
	}

	/* Fallback: tạo random address như bình thường */
	err = create_random_addr(addr);
	if (err) {
		return err;
	}

	BT_ADDR_SET_STATIC(&addr->a);

	return 0;
}
```

### Thêm include (nếu cần):
Thêm vào đầu file nếu chưa có:
```c
#include <string.h>  // For memcpy
```

## File 2: id.c

### Vị trí: `C:/ncs/v3.0.1/zephyr/subsys/bluetooth/host/id.c`

### Hàm cần sửa: `bt_setup_random_id_addr()` (Line 1652-1705)

### Code gốc:
```c
int bt_setup_random_id_addr(void)
{
	/* Only read the addresses if the user has not already configured one or
	 * more identities (!bt_dev.id_count).
	 */
	if (IS_ENABLED(CONFIG_BT_HCI_VS) && !bt_dev.id_count) {
		struct bt_hci_vs_static_addr addrs[CONFIG_BT_ID_MAX];

		bt_dev.id_count = vs_read_static_addr(addrs, CONFIG_BT_ID_MAX);

		for (uint8_t i = 0; i < bt_dev.id_count; i++) {
			int err;
			bt_addr_le_t addr;
			uint8_t *irk = NULL;
			uint8_t ir_irk[16];

			if (IS_ENABLED(CONFIG_BT_PRIVACY) &&
			    !IS_ENABLED(CONFIG_BT_PRIVACY_RANDOMIZE_IR)) {
				if (!bt_smp_irk_get(addrs[i].ir, ir_irk)) {
					irk = ir_irk;
				}
			}

			/* If true, `id_create` will randomize the IRK. */
			if (!irk && IS_ENABLED(CONFIG_BT_PRIVACY)) {
				/* `id_create` will not store the id when called before
				 * BT_DEV_READY. But since part of the id will be
				 * randomized, it needs to be stored.
				 */
				if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
					atomic_set_bit(bt_dev.flags, BT_DEV_STORE_ID);
				}
			}

			bt_addr_copy(&addr.a, &addrs[i].bdaddr);
			addr.type = BT_ADDR_LE_RANDOM;

			err = id_create(i, &addr, irk);
			if (err) {
				return err;
			}
		}

		if (bt_dev.id_count > 0) {
			return 0;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PRIVACY) && IS_ENABLED(CONFIG_BT_SETTINGS)) {
		atomic_set_bit(bt_dev.flags, BT_DEV_STORE_ID);
	}

	return bt_id_create(NULL, NULL);
}
```

### Code sau khi sửa:
```c
int bt_setup_random_id_addr(void)
{
	/* Only read the addresses if the user has not already configured one or
	 * more identities (!bt_dev.id_count).
	 */
	if (IS_ENABLED(CONFIG_BT_HCI_VS) && !bt_dev.id_count) {
		struct bt_hci_vs_static_addr addrs[CONFIG_BT_ID_MAX];

		bt_dev.id_count = vs_read_static_addr(addrs, CONFIG_BT_ID_MAX);

		for (uint8_t i = 0; i < bt_dev.id_count; i++) {
			int err;
			bt_addr_le_t addr;
			uint8_t *irk = NULL;
			uint8_t ir_irk[16];

			if (IS_ENABLED(CONFIG_BT_PRIVACY) &&
			    !IS_ENABLED(CONFIG_BT_PRIVACY_RANDOMIZE_IR)) {
				if (!bt_smp_irk_get(addrs[i].ir, ir_irk)) {
					irk = ir_irk;
				}
			}

			/* If true, `id_create` will randomize the IRK. */
			if (!irk && IS_ENABLED(CONFIG_BT_PRIVACY)) {
				/* `id_create` will not store the id when called before
				 * BT_DEV_READY. But since part of the id will be
				 * randomized, it needs to be stored.
				 */
				if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
					atomic_set_bit(bt_dev.flags, BT_DEV_STORE_ID);
				}
			}

			bt_addr_copy(&addr.a, &addrs[i].bdaddr);
			addr.type = BT_ADDR_LE_RANDOM;

			err = id_create(i, &addr, irk);
			if (err) {
				return err;
			}
		}

		if (bt_dev.id_count > 0) {
			return 0;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PRIVACY) && IS_ENABLED(CONFIG_BT_SETTINGS)) {
		atomic_set_bit(bt_dev.flags, BT_DEV_STORE_ID);
	}

	/* Thử lấy MAC từ fast provision */
	uint8_t mac[6] = {0};
	extern bool fast_provision_get_mac_for_bt_id(uint8_t *mac);
	if (fast_provision_get_mac_for_bt_id(mac)) {
		/* Tạo bt_addr_le_t từ MAC */
		bt_addr_le_t fast_prov_addr;
		fast_prov_addr.type = BT_ADDR_LE_RANDOM;
		memcpy(fast_prov_addr.a.val, mac, 6);
		
		/* Đảm bảo static random address bit được set */
		fast_prov_addr.a.val[5] |= 0xC0;
		
		/* Set MAC vào Bluetooth Identity */
		return bt_id_create(&fast_prov_addr, NULL);
	}

	/* Fallback: tạo random identity như bình thường */
	return bt_id_create(NULL, NULL);
}
```

### Thêm include (nếu cần):
Thêm vào đầu file nếu chưa có:
```c
#include <string.h>  // For memcpy
```

## Lưu ý quan trọng

1. **Backup SDK**: Nên backup các file SDK trước khi sửa
2. **Update SDK**: Khi update SDK, các thay đổi sẽ bị mất, cần apply lại
3. **Testing**: Test kỹ sau khi sửa để đảm bảo Bluetooth stack hoạt động bình thường
4. **Logging**: Có thể thêm logging để debug nếu cần

## Kết quả

Sau khi sửa:
- MAC từ fast provision sẽ được set vào `bt_dev.id_addr[BT_ID_DEFAULT]`
- Bluetooth stack sẽ dùng MAC này cho:
  - Advertising (khi dùng `BT_LE_ADV_OPT_USE_IDENTITY`)
  - Scanning (khi dùng identity address)
  - Connections (identity address)
  - Privacy/RPA generation

