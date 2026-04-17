/*
 * Copyright (c) 2024 ZMK-Config Contributors
 * SPDX-License-Identifier: MIT
 *
 * Battery Report Behavior
 *
 * Types the battery level of both keyboard halves as text, e.g.:
 *   L:87% R:92%
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

/* Time between press and release, and between successive taps.
 * Must be long enough that the HID layer can flush a BLE report
 * between transitions (BLE connection interval is typically 7.5-15ms). */
#define BAT_RPT_TAP_MS 15

/* Max chars we ever enqueue: "L:" + 3 digits + "%" + " R:" + 3 digits + "%" = 13 */
#define BAT_RPT_MAX_KEYS 16

struct bat_rpt_state {
    uint32_t keys[BAT_RPT_MAX_KEYS];
    uint8_t  len;
    uint8_t  idx;
    bool     pressed;   /* current phase for keys[idx] */
    bool     busy;
    struct k_work_delayable work;
};

static struct bat_rpt_state state;

static void bat_rpt_enqueue(uint32_t encoded) {
    if (state.len < BAT_RPT_MAX_KEYS) {
        state.keys[state.len++] = encoded;
    }
}

static void bat_rpt_enqueue_uint8(uint8_t n) {
    uint8_t digits[3];
    uint8_t ndigits = 0;
    if (n == 0) {
        digits[ndigits++] = 0;
    } else {
        uint8_t v = n;
        while (v > 0 && ndigits < 3) {
            digits[ndigits++] = v % 10;
            v /= 10;
        }
    }
    /* emit most-significant first */
    while (ndigits--) {
        uint8_t d = digits[ndigits];
        bat_rpt_enqueue(d == 0 ? N0 : (N1 + d - 1));
    }
}

static void bat_rpt_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (state.idx >= state.len) {
        state.busy = false;
        state.len = 0;
        state.idx = 0;
        return;
    }

    uint32_t encoded = state.keys[state.idx];
    raise_zmk_keycode_state_changed_from_encoded(encoded, state.pressed, k_uptime_get());

    if (state.pressed) {
        /* just pressed; next tick releases the same key */
        state.pressed = false;
    } else {
        /* just released; advance to next key, next tick presses it */
        state.idx++;
        state.pressed = true;
    }

    k_work_schedule(&state.work, K_MSEC(BAT_RPT_TAP_MS));
}

static int on_battery_report_binding_pressed(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (state.busy) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state.len = 0;
    state.idx = 0;
    state.pressed = true;

    uint8_t central_pct = zmk_battery_state_of_charge();

    bat_rpt_enqueue(LS(L));
    bat_rpt_enqueue(LS(SEMICOLON));
    bat_rpt_enqueue_uint8(central_pct);
    bat_rpt_enqueue(LS(N5));

    bat_rpt_enqueue(SPACE);
    bat_rpt_enqueue(LS(R));
    bat_rpt_enqueue(LS(SEMICOLON));

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t right_pct = 0;
    int rc = zmk_split_central_get_peripheral_battery_level(0, &right_pct);
    if (rc == 0) {
        bat_rpt_enqueue_uint8(right_pct);
        bat_rpt_enqueue(LS(N5));
    } else {
        LOG_WRN("bat_rpt: peripheral battery fetch failed: %d", rc);
        bat_rpt_enqueue(LS(SLASH));
    }
#else
    bat_rpt_enqueue(LS(SLASH));
#endif

    state.busy = true;
    k_work_schedule(&state.work, K_NO_WAIT);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_battery_report_binding_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_battery_report_driver_api = {
    .binding_pressed  = on_battery_report_binding_pressed,
    .binding_released = on_battery_report_binding_released,
};

static int behavior_battery_report_init(const struct device *dev) {
    ARG_UNUSED(dev);
    k_work_init_delayable(&state.work, bat_rpt_work_handler);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0,
    behavior_battery_report_init, NULL,
    NULL, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
    &behavior_battery_report_driver_api);
