/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file watchdog_process.h
 * @brief Watchdog module cho Relay Node
 *
 * @details
 * Module này cung cấp tính năng watchdog để bảo vệ thiết bị khỏi bị treo.
 * Nếu thiết bị không feed watchdog trong thời gian timeout, hệ thống sẽ tự động
 * reset.
 *
 * Watchdog sẽ tự động feed định kỳ trong background thread.
 */

#ifndef WATCHDOG_PROCESS_H_
#define WATCHDOG_PROCESS_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo và bật watchdog
 *
 * @return 0 nếu thành công
 * @return -ENODEV nếu watchdog device không sẵn sàng
 * @return -EFAULT nếu không thể cài đặt timeout
 * @return -errno các lỗi khác
 *
 * @note
 * Hàm này nên được gọi một lần trong main() sau khi hardware đã sẵn sàng.
 * Watchdog sẽ tự động feed trong background, không cần gọi thêm hàm nào khác.
 */
int watchdog_process_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_PROCESS_H_ */
