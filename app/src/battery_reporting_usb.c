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
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#include <zmk/usb_hid.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LEVEL_UNKNOWN UINT8_MAX

static uint8_t last_reported_level = LEVEL_UNKNOWN;

static bool local_seen = false;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
static bool peripheral_seen[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
#endif

static uint8_t battery_reporting_usb_min_level(void) {
    uint8_t min_level = 100;
    bool any_value = false;

    if (local_seen) {
        min_level = zmk_battery_state_of_charge();
        any_value = true;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    for (uint8_t i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (!peripheral_seen[i]) {
            continue;
        }
        uint8_t level = 0;
        int rc = zmk_split_central_get_peripheral_battery_level(i, &level);
        if (rc != 0) {
            continue;
        }
        if (!any_value || level < min_level) {
            min_level = level;
            any_value = true;
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
    zmk_hid_battery_set(level);

    int rc = zmk_usb_hid_send_battery_report();
    if (rc == 0) {
        last_reported_level = level;
    } else if (rc != -ENODEV) {
        LOG_DBG("Failed to send USB HID battery report: %d", rc);
    }
}

static int battery_reporting_usb_listener(const zmk_event_t *eh) {
    if (as_zmk_battery_state_changed(eh) != NULL) {
        local_seen = true;
        battery_reporting_usb_update();
        return 0;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    const struct zmk_peripheral_battery_state_changed *p_ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (p_ev != NULL) {
        if (p_ev->source < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS) {
            peripheral_seen[p_ev->source] = true;
        }
        battery_reporting_usb_update();
        return 0;
    }
#endif

    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        // Force re-send after USB (re)attach — cached value may not have reached the host.
        if (zmk_usb_get_conn_state() == ZMK_USB_CONN_HID) {
            last_reported_level = LEVEL_UNKNOWN;
            battery_reporting_usb_update();
        }
        return 0;
    }

    return 0;
}

ZMK_LISTENER(battery_reporting_usb, battery_reporting_usb_listener);
ZMK_SUBSCRIPTION(battery_reporting_usb, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(battery_reporting_usb, zmk_peripheral_battery_state_changed);
#endif
ZMK_SUBSCRIPTION(battery_reporting_usb, zmk_usb_conn_state_changed);
