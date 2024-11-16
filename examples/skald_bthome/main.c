/*
 * Copyright (C) 2024 Martine S. Lenders
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       BLE BTHome example using Skald
 *
 * @author      Martine S. Lenders <mail@martine-lenders.eu>
 *
 * @}
 */

#include <errno.h>
#include <stdio.h>

#include "saul_reg.h"
#include "ztimer.h"

#include "net/skald/bthome.h"

#ifndef CONFIG_BTHOME_SAUL_REG_DEVS
#define CONFIG_BTHOME_SAUL_REG_DEVS   (16U)
#endif

static skald_bthome_ctx_t _ctx;
static skald_bthome_saul_t _saul_devs[CONFIG_BTHOME_SAUL_REG_DEVS];

int main(void)
{
    saul_reg_t *dev = saul_reg;
    unsigned i = 0;

    ztimer_sleep(ZTIMER_MSEC, 2000);
    printf("Skald and the tale of Harald's home\n");

    _ctx.skald.update_pkt = NULL;
    _ctx.devs = NULL;
    if (skald_bthome_init(&_ctx, NULL, BTHOME_NAME, 0) < 0) {
        return 1;
    }
    if (!saul_reg) {
        puts("Hark! The board does not know SAUL. :-(");
        return 1;
    }
    while (dev && (i < CONFIG_BTHOME_SAUL_REG_DEVS)) {
        int res;
        printf("Adding %s to BTHome.\n", dev->name);
        _saul_devs[i].saul = *dev;  /* copy registry entry */
        _saul_devs[i].saul.next = NULL;
        if ((res = skald_bthome_saul_add(&_ctx, &_saul_devs[i])) < 0) {
            errno = -res;
            perror("Unable to add sensor to BTHome");
        };
        dev = dev->next;
        i++;
    }
    assert(!saul_reg || _ctx.devs);
    skald_bthome_advertise(&_ctx, 5000);
    return 0;
}
