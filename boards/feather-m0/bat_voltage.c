/*
 * Copyright (C)  2024 TU Dresden
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     boards_feather-m0
 * @ingroup     saul_bat_voltage
 * @{
 * @file
 * @brief       Implementation of battery voltage convert function
 *
 * @author      Martine S. Lenders <martine.lenders@tu-dresden.de>
 *
 * @}
 */

#ifdef MODULE_SAUL_BAT_VOLTAGE
#include "saul/bat_voltage.h"

int16_t saul_bat_voltage_convert(int32_t adc_sample)
{
    /* See
     * https://learn.adafruit.com/adafruit-feather-m0-basic-proto/power-management
     *
     * we return in millivolt so we set the reference voltage to 3300
     * instead of 3.3
     */
    return (int16_t)((adc_sample * 2L * 3300L) / 1024L);
}

#endif /* MODULE_SAUL_BAT_VOLTAGE */
