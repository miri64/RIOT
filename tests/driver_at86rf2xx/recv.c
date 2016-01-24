/*
 * Copyright (C) Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @author Martine Lenders <mlenders@inf.fu-berlin.de>
 */

#include <stdio.h>

#include "at86rf2xx.h"
#include "od.h"
#include "net/ieee802154.h"
#include "net/netdev2.h"

#include "common.h"

static uint8_t buffer[AT86RF2XX_MAX_PKT_LENGTH];

void recv(netdev2_t *dev)
{
    ieee802154_addr_t src, dst;
    int res;
    size_t mhr_len, data_len, src_len, dst_len;

    fflush(stdout);
    data_len = dev->driver->recv(dev, (char *)buffer, sizeof(buffer));
    mhr_len = ieee802154_get_frame_hdr_len(buffer);
    if (mhr_len == 0) {
        puts("Unexpected MHR for incoming packet");
        return;
    }
    if ((res = ieee802154_get_addr(buffer, &src, &dst)) < 0) {
        puts("Unable to get addresses for incoming packet");
        return;
    }
    switch (res & IEEE802154_FCF_DST_ADDR_MASK) {
        case IEEE802154_FCF_DST_ADDR_VOID:
            dst_len = 0;
            break;
        case IEEE802154_FCF_DST_ADDR_SHORT:
            dst_len = 2;
            break;
        case IEEE802154_FCF_DST_ADDR_LONG:
            dst_len = 8;
            break;
        default:
            puts("Unexpected destination address format in incoming packet");
            return;
    }
    switch (res & IEEE802154_FCF_SRC_ADDR_MASK) {
        case IEEE802154_FCF_SRC_ADDR_VOID:
            src_len = 0;
            break;
        case IEEE802154_FCF_SRC_ADDR_SHORT:
            src_len = IEEE802154_SHORT_ADDRESS_LEN;
            break;
        case IEEE802154_FCF_SRC_ADDR_LONG:
            src_len = IEEE802154_LONG_ADDRESS_LEN;
            break;
        default:
            puts("Unexpected source address format in incoming packet");
            return;
    }
    switch (buffer[0] & IEEE802154_FCF_TYPE_MASK) {
        case IEEE802154_FCF_TYPE_BEACON:
            puts("BEACON");
            break;
        case IEEE802154_FCF_TYPE_DATA:
            puts("DATA");
            break;
        case IEEE802154_FCF_TYPE_ACK:
            puts("ACK");
            break;
        case IEEE802154_FCF_TYPE_MACCMD:
            puts("MACCMD");
            break;
        default:
            puts("UNKNOWN");
            break;
    }
    printf("Dest. PAN: ");
    print_addr(dst.pan.u8, sizeof(dst.pan));
    printf(", Dest. addr.: ");
    print_addr(dst.addr.l_addr.u8, dst_len);
    printf(", Src. PAN: ");
    print_addr(src.pan.u8, sizeof(src.pan));
    printf(", Src. addr.: ");
    print_addr(src.addr.l_addr.u8, src_len);
    puts("");
    printf("Security: ");
    if (buffer[0] & IEEE802154_FCF_SECURITY_EN) {
        printf("1, ");
    }
    else {
        printf("0, ");
    }
    printf("Frame pend.: ");
    if (buffer[0] & IEEE802154_FCF_FRAME_PEND) {
        printf("1, ");
    }
    else {
        printf("0, ");
    }
    printf("ACK req.: ");
    if (buffer[0] & IEEE802154_FCF_ACK_REQ) {
        printf("1, ");
    }
    else {
        printf("0, ");
    }
    printf("PAN comp.: ");
    if (buffer[0] & IEEE802154_FCF_ACK_REQ) {
        puts("1");
    }
    else {
        puts("0");
    }
    printf("Version: ");
    printf("%u, ", (unsigned)((buffer[1] & IEEE802154_FCF_VERS_MASK) >> 4));
    printf("Seq.: %u\n", (unsigned)ieee802154_get_seq(buffer));
    /* 4 = FCS (2 byte) + LQI (1 byte) + RSSI (1 byte) */
    od_hex_dump(buffer + mhr_len, data_len - mhr_len - 4, 0);
    printf("(");
    for (int i = mhr_len; i < (data_len - mhr_len - 4); i++) {
        putchar((char)buffer[i]);
    }
    printf(")\n\n");
}

/** @} */
