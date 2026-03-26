/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file watchdog_process.c
 * @brief Watchdog implementation for Relay Node
 *
 * @details
 * Watchdog module implementation using Zephyr watchdog driver.
 * Watchdog is automatically fed periodically to prevent system reset.
 *
 * If system hangs and does not feed watchdog within timeout,
 * watchdog will automatically reset the SoC.
 *
 * Based on sample: zephyr/samples/drivers/watchdog and nrf_desktop
 */

#include "../../include/watchdog_process.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog_process, LOG_LEVEL_NONE);

/* Watchdog timeout configuration (ms) */
#ifndef CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS
#define CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS 12000
#endif

/* Watchdog feed time (feed before timeout to have safe margin)
 * Safety Margin:
 * - Between feeds: 4s (feed interval)
 * - Timeout: 12s from last feed
 * - Margin: 12s - 4s = 8s - very safe
 * - Even if work queue delays 6-7s, still has 1-2s margin before reset
 */
#define WDT_FEED_INTERVAL_MS ((CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS) / 3)

/* Retry count when feed fails */
#define WDT_FEED_RETRY_COUNT 3

/* Watchdog device structure */
static struct {
  const struct device* wdt;
  int wdt_channel_id;
  struct k_work_delayable feed_work;
  bool initialized;
  bool enabled; /* Track watchdog enabled state */
} wdt_data = {
    .wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0)),
};

/**
 * @brief Work handler to feed watchdog periodically
 *
 * @param work Work item pointer
 *
 * @details
 * This function is called periodically to feed watchdog.
 * - Check if watchdog is enabled (avoid feeding before start)
 * - Retry if feed fails (avoid reset due to temporary error)
 * - Reschedule to continue feeding if successful
 * - If retries exhausted and still fail, do not reschedule (watchdog will reset
 * system)
 */
static void watchdog_feed_worker(struct k_work* work) {
  struct k_work_delayable* dwork = k_work_delayable_from_work(work);
  int err;
  int retry;

  /* Check if watchdog is enabled */
  if (!wdt_data.enabled) {
    LOG_DBG("Watchdog not enabled yet, rescheduling...");
    /* Reschedule after 1 second to wait for watchdog to be enabled */
    k_work_reschedule(dwork, K_SECONDS(1));
    return;
  }

  /* Retry feed if failed */
  for (retry = 0; retry < WDT_FEED_RETRY_COUNT; retry++) {
    err = wdt_feed(wdt_data.wdt, wdt_data.wdt_channel_id);

    if (err == 0) {
      /* Feed successful */
      k_work_reschedule(dwork, K_MSEC(WDT_FEED_INTERVAL_MS));
      /* Log to track feed time */
      // LOG_INF("Watchdog feed at %d, diff: %d", now, (int)(now -
      // last_feed_time));: %lld ms (retry: %d)", k_uptime_get(), retry);
      return;
    }

    /* If error -EAGAIN (watchdog busy), wait a bit then retry */
    if (err == -EAGAIN && retry < WDT_FEED_RETRY_COUNT - 1) {
      k_sleep(K_MSEC(10)); /* Wait 10ms before retry */
      continue;
    }

    /* Other errors or retries exhausted */
    LOG_ERR("Cannot feed watchdog. Error code: %d (retry: %d/%d)", err,
            retry + 1, WDT_FEED_RETRY_COUNT);
  }

  /* If retries exhausted and still fail, do not reschedule - watchdog will
   * reset system */
  LOG_ERR("Watchdog feed failed after %d retries. System will reset.",
          WDT_FEED_RETRY_COUNT);
}

/**
 * @brief Install watchdog timeout configuration
 *
 * @return 0 on success
 * @return -EFAULT if cannot install timeout
 */
static int watchdog_timeout_install(void) {
  /* Configure timeout */
  static const struct wdt_timeout_cfg wdt_config = {
      .window =
          {
              .min = 0,                                 /* No min window */
              .max = CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS /* Max timeout */
          },
      .callback = NULL,           /* No callback needed */
      .flags = WDT_FLAG_RESET_SOC /* Reset SoC on timeout */
  };

  wdt_data.wdt_channel_id = wdt_install_timeout(wdt_data.wdt, &wdt_config);

  if (wdt_data.wdt_channel_id < 0) {
    LOG_ERR("Cannot install watchdog timer! Error code: %d",
            wdt_data.wdt_channel_id);
    return -EFAULT;
  }

  LOG_INF("Watchdog timeout installed. Timeout: %d ms",
          CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS);
  return 0;
}

/**
 * @brief Start watchdog peripheral
 *
 * @return 0 on success
 * @return -errno on error
 */
static int watchdog_start(void) {
  /* Start watchdog with pause when debugger halt option */
  /* WDT_OPT_PAUSE_HALTED_BY_DBG: Pause watchdog when debugger halts CPU */
  int err = wdt_setup(wdt_data.wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);

  if (err) {
    LOG_ERR("Cannot start watchdog! Error code: %d", err);
    return err;
  }

  wdt_data.enabled = true;
  LOG_INF("Watchdog started");
  return 0;
}

/**
 * @brief Enable work queue to automatically feed watchdog
 *
 * @return 0 on success
 * @return -EALREADY if work already scheduled
 * @return -errno other errors
 */
static int watchdog_feed_enable(void) {
  k_work_init_delayable(&wdt_data.feed_work, watchdog_feed_worker);

  /* Schedule work after a short delay to ensure watchdog has started */
  /* Feed first time after 1 second to ensure watchdog is fully enabled */
  int ret = k_work_schedule(&wdt_data.feed_work, K_SECONDS(1));

  if (ret != 1) {
    LOG_ERR("Cannot start watchdog feed worker! Error code: %d", ret);
    ret = (ret == 0) ? (-EALREADY) : (ret);
  } else {
    LOG_INF("Watchdog feed enabled. Feed interval: %d ms, timeout: %d ms",
            WDT_FEED_INTERVAL_MS, CONFIG_WATCHDOG_PROCESS_TIMEOUT_MS);
    ret = 0;
  }

  return ret;
}

int watchdog_process_init(void) {
  int err;

  /* Check if watchdog device is ready */
  if (!device_is_ready(wdt_data.wdt)) {
    LOG_ERR("Watchdog device not ready");
    return -ENODEV;
  }

  /* Check if already initialized */
  if (wdt_data.initialized) {
    LOG_WRN("Watchdog already initialized");
    return 0;
  }

  LOG_INF("Initializing watchdog...");

  /* Step 1: Install timeout configuration */
  err = watchdog_timeout_install();
  if (err) {
    return err;
  }

  /* Step 2: Start watchdog peripheral BEFORE enabling feed work */
  /* IMPORTANT: Must start watchdog first to avoid feeding when not enabled */
  err = watchdog_start();
  if (err) {
    return err;
  }

  /* Step 3: Enable work queue to automatically feed (after watchdog started) */
  err = watchdog_feed_enable();
  if (err) {
    /* If failed to enable feed work, disable watchdog */
    wdt_data.enabled = false;
    return err;
  }

  wdt_data.initialized = true;
  LOG_INF("Watchdog initialized successfully");

  return 0;
}
