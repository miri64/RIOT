/*
 * Copyright 2019 Marian Buschsieweke
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup tests
 * @{
 *
 * @file
 * @brief   Test application for the WS281x RGB LED driver
 *
 * @author  Marian Buschsieweke <marian.buschsieweke@ovgu.de>
 *
 * @}
 */

#include <stdatomic.h>
#include <stdio.h>

#include "periph/gpio.h"
#include "ws281x.h"
#include "ws281x_params.h"
#include "ztimer.h"

#define RAINBOW_LEN     ARRAY_SIZE(rainbow)

static void _enable_button(void *arg);
static void _reset_counters(void *arg);

static atomic_uint_fast8_t _value = 2, _left = 0, _right = 0;
static ztimer_t _debounce_timer = { .callback = _enable_button };
static ztimer_t _reset_timer = { .callback = _reset_counters };

static void _enable_button(void *arg)
{
    gpio_irq_enable((gpio_t)arg);
}

static void _reset_counters(void *arg)
{
    (void)arg;
    atomic_store(&_left, 0);
    atomic_store(&_right, 0);
}

static void _disable_button(void *arg)
{
    gpio_irq_disable((gpio_t)arg);
    _debounce_timer.arg = arg;
    ztimer_set(ZTIMER_MSEC, &_debounce_timer, 200U);
}

static void _start_reset_counter(void)
{
    ztimer_set(ZTIMER_MSEC, &_reset_timer, 1000);
}

static void _increment_value(void *arg)
{
    uint8_t left = atomic_fetch_add(&_left, 1), right;
    _disable_button(arg);
    _start_reset_counter();
    right = atomic_load(&_right);
    if (left == 1 && right == 1) {
        atomic_store(&_value, 2);
    }
    else if (_value < 100U) {
        atomic_fetch_add(&_value, 2);
    }
}

static void _decrement_value(void *arg)
{
    atomic_fetch_add(&_right, 1);
    _disable_button(arg);
    _start_reset_counter();
    if (_value > 0U) {
        atomic_fetch_sub(&_value, 2);
    }
}


int main(void)
{
    ws281x_t dev;
    int retval;

    if (0 != (retval = ws281x_init(&dev, &ws281x_params[0]))) {
        printf("Initialization failed with error code %d\n", retval);
        return retval;
    }
    gpio_init_int(GPIO2, GPIO_IN_PU, GPIO_FALLING, _increment_value, (void *)GPIO2);
    gpio_init_int(GPIO8, GPIO_IN_PU, GPIO_FALLING, _decrement_value, (void *)GPIO8);

    while (1) {
        unsigned offset = 0;
        /* puts("\nAnimation: Moving rainbow..."); */
        uint32_t last_wakeup = ztimer_now(ZTIMER_MSEC);
        for (unsigned i = 0; i < (360 * 5); i++) {
            for (uint16_t j = 0; j < dev.params.numof; j++) {
                color_hsv_t hsv = {
                    .h = (((j + i) * 360) / (5 * dev.params.numof)) % 360,
                    .s = 1,
                    .v = (float)atomic_load(&_value) / 100,
                };
                color_rgb_t col;
                color_hsv2rgb(&hsv, &col);
                ws281x_set(&dev, j, col);
            }
            offset++;
            ws281x_write(&dev);
            ztimer_periodic_wakeup(ZTIMER_MSEC, &last_wakeup, 100);
        }
    }

    return 0;
}
