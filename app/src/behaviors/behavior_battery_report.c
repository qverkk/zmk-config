/*
 * Copyright (c) 2024 ZMK-Config Contributors
 * SPDX-License-Identifier: MIT
 *
 * Battery Report Behavior
 *
 * Types the battery level of both keyboard halves as text, e.g.:
 *   L:87% R:92%
 *
 * Required .conf options:
 *   CONFIG_ZMK_BATTERY_REPORT_BEHAVIOR=y
 *   CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
 *
 * Bind in keymap with:  &bat_rpt
 */

#define DT_DRV_COMPAT zmk_bat_report

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Key-tap helpers
 * We raise keycode-state-changed events directly into the ZMK event bus.
 * The normal HID pipeline then handles them (BLE / USB).
 * -------------------------------------------------------------------------- */

static void tap_encoded(uint32_t encoded) {
    raise_zmk_keycode_state_changed_from_encoded(encoded, true,  k_uptime_get());
    k_msleep(10);
    raise_zmk_keycode_state_changed_from_encoded(encoded, false, k_uptime_get());
    k_msleep(10);
}

/* Type an ASCII digit 0-9 */
static void tap_digit(uint8_t d) {
    /* Key codes N0..N9 are consecutive starting at ZMK's N0 */
    tap_encoded(d == 0 ? N0 : (N1 + d - 1));
}

/* Type a decimal number 0-100 */
static void type_uint8(uint8_t n) {
    if (n >= 100) {
        tap_digit(n / 100);
        n %= 100;
        tap_digit(n / 10);
        tap_digit(n % 10);
    } else if (n >= 10) {
        tap_digit(n / 10);
        tap_digit(n % 10);
    } else {
        tap_digit(n);
    }
}

/* --------------------------------------------------------------------------
 * Behavior callbacks
 * -------------------------------------------------------------------------- */

static int on_battery_report_binding_pressed(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    uint8_t central_pct = zmk_battery_state_of_charge();

    /* Type "L:" */
    tap_encoded(LS(L));            /* L  (shift+l) */
    tap_encoded(LS(SEMICOLON));    /* :  (shift+;) */

    type_uint8(central_pct);

    tap_encoded(LS(N5));           /* %  (shift+5) */

    /* Type " R:" */
    tap_encoded(SPACE);
    tap_encoded(LS(R));            /* R */
    tap_encoded(LS(SEMICOLON));    /* : */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t right_pct = 0;
    int rc = zmk_split_central_get_peripheral_battery_level(0, &right_pct);
    if (rc == 0) {
        type_uint8(right_pct);
        tap_encoded(LS(N5));       /* % */
    } else {
        tap_encoded(LS(SLASH));    /* ? */
    }
#else
    tap_encoded(LS(SLASH));        /* ? – peripheral fetching not enabled */
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_battery_report_binding_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_battery_report_driver_api = {
    .binding_pressed  = on_battery_report_binding_pressed,
    .binding_released = on_battery_report_binding_released,
};

static int behavior_battery_report_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0,
    behavior_battery_report_init, NULL,
    NULL, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
    &behavior_battery_report_driver_api);
