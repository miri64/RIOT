/*
 * Copyright (C) 2016 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/ieee802154.h"

#include "common.h"

#include "od.h"

#define _MAX_ADDR_LEN   (8)

static int _parse_addr(uint8_t *out, size_t out_len, const char *in);
static void send(int iface, uint16_t dst_pan, uint8_t *dst_addr, size_t dst_len,
                 char *data);

int ifconfig_list(int idx)
{
    int res;
    uint8_t array_val[_MAX_ADDR_LEN];
    netdev2_t *dev = (netdev2_t *)(&devs[idx]);

    int (*get)(netdev2_t *, netopt_t, void *, size_t) = dev->driver->get;
    netopt_enable_t enable_val;
    uint16_t u16_val;

    printf("Iface %3d  HWaddr: ", idx);
    res = get(dev, NETOPT_ADDRESS, array_val, sizeof(array_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    print_addr(array_val, res);
    printf(", Long HWaddr: ");
    res = get(dev, NETOPT_ADDRESS_LONG, array_val, sizeof(array_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    print_addr(array_val, res);
    printf(", PAN: ");
    res = get(dev, NETOPT_NID, array_val, sizeof(array_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    print_addr(array_val, res);

    res = get(dev, NETOPT_ADDR_LEN, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf("\n           Address length: %u", (unsigned)u16_val);

    res = get(dev, NETOPT_SRC_LEN, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf(", Source address length: %u", (unsigned)u16_val);
    res = get(dev, NETOPT_MAX_PACKET_SIZE, &u16_val, sizeof(u16_val));

    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf(", Max.Payload: %u", (unsigned)u16_val);

    res = get(dev, NETOPT_IPV6_IID, array_val, sizeof(array_val));
    if (res > 0) {
        printf("\n           IPv6 IID: ");
        print_addr(array_val, res);
    }

    res = get(dev, NETOPT_CHANNEL, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf("\n           Channel: %u", (unsigned)u16_val);

    res = get(dev, NETOPT_CHANNEL_PAGE, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf(", Ch.page: %u", (unsigned)u16_val);

    res = get(dev, NETOPT_TX_POWER, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts("(err)");
        return 1;
    }
    printf(", TXPower: %d dBm", (int)u16_val);
    res = get(dev, NETOPT_IS_WIRED, &u16_val, sizeof(u16_val));
    if (res < 0) {
        puts(", wireless");
    }
    else {
        puts(", wired");
    }

    printf("         ");
    res = get(dev, NETOPT_PRELOADING, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  PRELOAD");
    }
    res = get(dev, NETOPT_AUTOACK, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  AUTOACK");
    }
    res = get(dev, NETOPT_NO_COMP, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  NO_COMP");
    }
    res = get(dev, NETOPT_RAWMODE, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  RAW");
    }
    res = get(dev, NETOPT_AUTOCCA, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  AUTOCCA");
    }
    res = get(dev, NETOPT_CSMA, &enable_val, sizeof(u16_val));
    if ((res > 0) && (enable_val == NETOPT_ENABLE)) {
        printf("  CSMA");
    }
    puts("");

    return 0;
}

int ifconfig(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < AT86RF2XX_NUM; i++) {
        ifconfig_list(i);
    }
    return 0;
}

static void txtsnd_usage(char *cmd_name)
{
    printf("usage: %s <iface> [<pan>] <addr> <text>\n", cmd_name);
}

int txtsnd(int argc, char **argv)
{
    char *text;
    uint8_t addr[_MAX_ADDR_LEN];
    int iface, idx = 2, res;
    uint16_t pan = 0;

    switch (argc) {
        case 4:
            break;
        case 5:
            res = _parse_addr((uint8_t *)&pan, sizeof(pan), argv[idx++]);
            if ((res <= 0) || (res > sizeof(pan))) {
                txtsnd_usage(argv[0]);
                return 1;
            }
            break;
        default:
            txtsnd_usage(argv[0]);
            return 1;
    }

    iface = atoi(argv[1]);
    res = _parse_addr(addr, sizeof(addr), argv[idx++]);
    if (res <= 0) {
        txtsnd_usage(argv[0]);
        return 1;
    }
    text = argv[idx++];
    send(iface, pan, addr, (size_t)res, text);

    return 0;
}

static inline int _dehex(char c, int default_)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    }
    else {
        return default_;
    }
}

static int _parse_addr(uint8_t *out, size_t out_len, const char *in)
{
    const char *end_str = in;
    uint8_t *out_end = out;
    size_t count = 0;
    int assert_cell = 1;

    if (!in || !*in) {
        return 0;
    }
    while (end_str[1]) {
        ++end_str;
    }

    while (end_str >= in) {
        int a = 0, b = _dehex(*end_str--, -1);
        if (b < 0) {
            if (assert_cell) {
                return 0;
            }
            else {
                assert_cell = 1;
                continue;
            }
        }
        assert_cell = 0;

        if (end_str >= in) {
            a = _dehex(*end_str--, 0);
        }

        if (++count > out_len) {
            return 0;
        }
        *out_end++ = (a << 4) | b;
    }
    if (assert_cell) {
        return 0;
    }
    /* out is reversed */

    while (out < --out_end) {
        uint8_t tmp = *out_end;
        *out_end = *out;
        *out++ = tmp;
    }

    return count;
}

static void send(int iface, uint16_t dst_pan, uint8_t *dst_addr, size_t dst_len,
                 char *data)
{
    netdev2_t *dev = (netdev2_t *)&devs[iface];
    const size_t count = 2;         /* mhr + payload */
    struct iovec vector[count];
    int res;
    netopt_t addr_opt;
    netopt_enable_t enable_val;
    ieee802154_addr_t dst, src;
    network_uint16_t flags = { 0 };
    uint16_t dev_pan = 0, src_len;
    uint8_t mhr[IEEE802154_MAX_HDR_LEN];

    vector[1].iov_base = data;
    vector[1].iov_len = strlen(data);
    dev->driver->get(dev, NETOPT_NID, &dev_pan, sizeof(dev_pan));
    if (dst_pan == 0) {
        dst_pan = dev_pan;
    }
    dst.pan.u16 = dst_pan;
    memcpy(&dst.addr.l_addr, dst_addr, dst_len);
    switch (dst_len) {
        case 2:
            flags.u8[1] |= IEEE802154_FCF_DST_ADDR_SHORT;
            break;
        case 8:
            memcpy(&dst.addr.l_addr, dst_addr, dst_len);
            flags.u8[1] |= IEEE802154_FCF_DST_ADDR_LONG;
            break;
        default:
            puts("txtsnd: unexpected destination address length");
            return;
    }
    if (((dst_len != 2) || (memcmp(&dst.addr.l_addr, "\xff\xff", 2) != 0)) &&
        (dev->driver->get(dev, NETOPT_AUTOACK, &enable_val, sizeof(enable_val)) > 0) &&
        (enable_val == NETOPT_ENABLE)) {
        flags.u8[0] |= IEEE802154_FCF_ACK_REQ;
    }
    /* get source address from device */
    if (dev->driver->get(dev, NETOPT_SRC_LEN, &src_len, sizeof(src_len)) < 0) {
        puts("txtsnd: unable to aquire source address length");
        return;
    }
    switch (src_len) {
        case 8:
            addr_opt = NETOPT_ADDRESS_LONG;
            flags.u8[1] |= IEEE802154_FCF_SRC_ADDR_LONG;
            break;
        default:
            addr_opt = NETOPT_ADDRESS;
            flags.u8[1] |= IEEE802154_FCF_SRC_ADDR_SHORT;
            break;
    }
    if (dev->driver->get(dev, addr_opt, &src.addr, sizeof(src.addr)) < 0) {
        puts("txtsnd: unable to aquire source address");
        return;
    }
    /* enable PAN compression if applicable */
    if ((dev_pan == dst.pan.u16) &&
        ((dev->driver->get(dev, NETOPT_NO_COMP, &enable_val, sizeof(enable_val)) < 0) ||
         (enable_val != NETOPT_DISABLE))) {
        flags.u8[0] |= IEEE802154_FCF_PAN_COMP;
    }
    flags.u8[0] |= IEEE802154_FCF_TYPE_DATA;
    flags.u8[1] |= IEEE802154_FCF_VERS_V1;
    /* fill MAC header, seq should be set by device */
    if ((res = ieee802154_set_frame_hdr(mhr, &src, &dst, flags, 0)) < 0) {
        puts("txtsnd: Error preperaring frame");
        return;
    }
    vector[0].iov_base = mhr;
    vector[0].iov_len = (size_t)res;
    res = dev->driver->send(dev, vector, count);
    if (res < 0) {
        puts("txtsnd: Error on sending");
    }
    else {
        printf("txtsnd: send %u bytes to ", (unsigned)vector[1].iov_len);
        print_addr(dst_addr, dst_len);
        printf(" (PAN: ");
        print_addr((uint8_t *)&dst_pan, sizeof(dst_pan));
        puts(")");
    }
}

/** @} */
