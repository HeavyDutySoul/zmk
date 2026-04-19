/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/hid.h>
#include <zmk/usb_hid.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t last_reported_level = UINT8_MAX;

static uint8_t battery_reporting_usb_min_level(void) {
    uint8_t min_level = 100;
    bool any_value = false;

    uint8_t local = zmk_battery_state_of_charge();
    if (local > 0) {
        min_level = local;
        any_value = true;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    for (uint8_t i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        uint8_t level = 0;
        int rc = zmk_split_central_get_peripheral_battery_level(i, &level);
        if (rc == 0 && level > 0) {
            if (!any_value || level < min_level) {
                min_level = level;
                any_value = true;
            }
        }
    }
#endif

    return any_value ? min_level : 0;
}

static void battery_reporting_usb_update(void) {
    uint8_t level = battery_reporting_usb_min_level();
    if (level == last_reported_level) {
        return;
    }
    last_reported_level = level;
    zmk_hid_battery_set(level);

    int rc = zmk_usb_hid_send_battery_report();
    if (rc && rc != -ENODEV) {
        LOG_DBG("Failed to send USB HID battery report: %d", rc);
    }
}

static int battery_reporting_usb_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    battery_reporting_usb_update();
    return 0;
}

ZMK_LISTENER(battery_reporting_usb, battery_reporting_usb_listener);
ZMK_SUBSCRIPTION(battery_reporting_usb, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(battery_reporting_usb, zmk_peripheral_battery_state_changed);
#endif
