/*
 * Copyright (c) 2024 ZMK-Config Contributors
 * SPDX-License-Identifier: MIT
 *
 * Battery Report Behavior
 *
 * Types the battery level of both keyboard halves as text, e.g.:
 *   L:87% R:92%
 *
 * Bind in keymap with:  &bat_rpt  (or via the bat_rpt_macro wrapper)
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

/* Gap between each press/release tick. Must exceed BLE connection interval (7.5–15 ms). */
#define BAT_RPT_TAP_MS 20

/* Max chars: "L:" + 3 digits + "%" + " R:" + 3 digits + "%" = 13, padded to 16 */
#define BAT_RPT_MAX_KEYS 16

struct bat_rpt_state {
    uint32_t keys[BAT_RPT_MAX_KEYS];
    uint8_t  len;
    uint8_t  idx;
    bool     pressing; /* true = fire press, false = fire release for keys[idx] */
    bool     busy;
    struct k_work_delayable work;
};

static struct bat_rpt_state state;

static void enqueue(uint32_t encoded) {
    if (state.len < BAT_RPT_MAX_KEYS) {
        state.keys[state.len++] = encoded;
    }
}

static void enqueue_uint8(uint8_t n) {
    uint8_t digits[3];
    uint8_t nd = 0;
    if (n == 0) {
        digits[nd++] = 0;
    } else {
        uint8_t v = n;
        while (v > 0 && nd < 3) {
            digits[nd++] = v % 10;
            v /= 10;
        }
    }
    /* emit most-significant first */
    while (nd--) {
        uint8_t d = digits[nd];
        enqueue(d == 0 ? N0 : (N1 + d - 1));
    }
}

static void work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (state.idx >= state.len) {
        state.busy = false;
        state.len  = 0;
        state.idx  = 0;
        return;
    }

    raise_zmk_keycode_state_changed_from_encoded(state.keys[state.idx],
                                                 state.pressing,
                                                 k_uptime_get());

    if (state.pressing) {
        state.pressing = false;
    } else {
        state.idx++;
        state.pressing = true;
    }

    k_work_schedule(&state.work, K_MSEC(BAT_RPT_TAP_MS));
}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (state.busy) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state.len     = 0;
    state.idx     = 0;
    state.pressing = true;

    uint8_t left_pct = zmk_battery_state_of_charge();

    enqueue(LS(L));
    enqueue(LS(SEMICOLON));
    enqueue_uint8(left_pct);
    enqueue(LS(N5));

    enqueue(SPACE);
    enqueue(LS(R));
    enqueue(LS(SEMICOLON));

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t right_pct = 0;
    if (zmk_split_central_get_peripheral_battery_level(0, &right_pct) == 0) {
        enqueue_uint8(right_pct);
        enqueue(LS(N5));
    } else {
        enqueue(LS(SLASH)); /* '?' if peripheral not available */
    }
#else
    enqueue(LS(SLASH));
#endif

    state.busy = true;
    k_work_schedule(&state.work, K_NO_WAIT);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api driver_api = {
    .binding_pressed  = on_pressed,
    .binding_released = on_released,
};

static int bat_rpt_init(const struct device *dev) {
    ARG_UNUSED(dev);
    k_work_init_delayable(&state.work, work_handler);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0,
    bat_rpt_init, NULL,
    NULL, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
    &driver_api);
