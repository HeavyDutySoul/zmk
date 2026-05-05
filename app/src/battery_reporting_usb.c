/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#include <zmk/usb_hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint8_t last_sent[ZMK_HID_BATTERY_SOURCE_COUNT] = {
    [0 ...(ZMK_HID_BATTERY_SOURCE_COUNT - 1)] = ZMK_HID_BATTERY_LEVEL_UNKNOWN,
};

static uint8_t current_snapshot[ZMK_HID_BATTERY_SOURCE_COUNT] = {
    [0 ...(ZMK_HID_BATTERY_SOURCE_COUNT - 1)] = ZMK_HID_BATTERY_LEVEL_UNKNOWN,
};

static void push_report_if_changed(void) {
    if (memcmp(last_sent, current_snapshot, sizeof(last_sent)) == 0) {
        return;
    }

    for (uint8_t i = 0; i < ZMK_HID_BATTERY_SOURCE_COUNT; i++) {
        zmk_hid_battery_set(i, current_snapshot[i]);
    }

    int rc = zmk_usb_hid_send_battery_report();
    if (rc == 0) {
        memcpy(last_sent, current_snapshot, sizeof(last_sent));
    } else if (rc != -ENODEV) {
        LOG_DBG("Failed to send USB HID battery report: %d", rc);
    }
}

static int battery_reporting_usb_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *l_ev = as_zmk_battery_state_changed(eh);
    if (l_ev != NULL) {
        current_snapshot[0] = l_ev->state_of_charge;
        push_report_if_changed();
        return 0;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    const struct zmk_peripheral_battery_state_changed *p_ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (p_ev != NULL) {
        const uint8_t slot = 1 + p_ev->source;
        if (slot < ZMK_HID_BATTERY_SOURCE_COUNT) {
            current_snapshot[slot] = p_ev->state_of_charge;
            push_report_if_changed();
        }
        return 0;
    }
#endif

    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        // Force a resend after USB (re)attach: host may not have received the
        // last report while we were detached/suspended.
        if (zmk_usb_get_conn_state() == ZMK_USB_CONN_HID) {
            memset(last_sent, ZMK_HID_BATTERY_LEVEL_UNKNOWN, sizeof(last_sent));
            push_report_if_changed();
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
