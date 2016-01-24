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

#include "stddef.h"
#include "od.h"

#include "net/gnrc.h"
#include "net/ieee802154.h"

#include "net/gnrc/netdev2/ieee802154.h"

#define ENABLE_DEBUG    (1)
#include "debug.h"

#define _TAIL_LEN       (4)
#define _LQI_TAIL_POS   (2)
#define _RSSI_TAIL_POS  (3)

static gnrc_pktsnip_t *_recv(gnrc_netdev2_t *gnrc_netdev2);
static int _send(gnrc_netdev2_t *gnrc_netdev2, gnrc_pktsnip_t *pkt);

int gnrc_netdev2_ieee802154_init(gnrc_netdev2_t *gnrc_netdev2, netdev2_t *dev)
{
    gnrc_netdev2->send = _send;
    gnrc_netdev2->recv = _recv;
    gnrc_netdev2->dev = dev;

    return 0;
}

static gnrc_pktsnip_t *_make_netif_hdr(uint8_t *mhr)
{
    int addr_info;
    gnrc_pktsnip_t *snip;
    ieee802154_addr_t src, dst;
    uint8_t tmp, src_len, dst_len;

    addr_info = ieee802154_get_addr(mhr, &src, &dst);
    /* figure out address sizes */
    tmp = ((uint8_t)addr_info) & IEEE802154_FCF_SRC_ADDR_MASK;
    if (tmp == IEEE802154_FCF_SRC_ADDR_SHORT) {
        src_len = 2;
    }
    else if (tmp == IEEE802154_FCF_SRC_ADDR_LONG) {
        src_len = 8;
    }
    else if (tmp == IEEE802154_FCF_SRC_ADDR_VOID) {
        src_len = 0;
    }
    else {
        return NULL;
    }
    tmp = ((uint8_t)addr_info) & IEEE802154_FCF_DST_ADDR_MASK;
    if (tmp == IEEE802154_FCF_DST_ADDR_SHORT) {
        dst_len = 2;
    }
    else if (tmp == IEEE802154_FCF_DST_ADDR_LONG) {
        dst_len = 8;
    }
    else if (tmp == IEEE802154_FCF_DST_ADDR_VOID) {
        dst_len = 0;
    }
    else {
        return NULL;
    }
    /* allocate space for header */
    snip = gnrc_netif_hdr_build((uint8_t *)&src.addr, src_len, (uint8_t *)&dst.addr, dst_len);
    if (snip == NULL) {
        DEBUG("_make_netif_hdr_ieee802154: no space left in packet buffer\n");
        return NULL;
    }
    /* set broadcast flag for broadcast destination */
    if ((dst_len == 2) && (dst.addr.s_addr.u16 == 0xffff)) {
        gnrc_netif_hdr_t *hdr = snip->data;
        hdr->flags |= GNRC_NETIF_HDR_FLAGS_BROADCAST;
    }
    return snip;
}

static gnrc_pktsnip_t *_recv(gnrc_netdev2_t *gnrc_netdev2)
{
    netdev2_t *dev = gnrc_netdev2->dev;
    gnrc_pktsnip_t *pkt = NULL;
    int bytes_expected = dev->driver->recv(dev, NULL, 0);

    if (bytes_expected > 0) {
        int nread;
        netopt_enable_t raw;

        pkt = gnrc_pktbuf_add(NULL, NULL, bytes_expected, GNRC_NETTYPE_UNDEF);
        if (pkt == NULL) {
            DEBUG("_recv_ieee802154: cannot allocate pktsnip.\n");
            return NULL;
        }
        nread = dev->driver->recv(dev, pkt->data, bytes_expected) - _TAIL_LEN;
        if (nread <= 0) {
            gnrc_pktbuf_release(pkt);
            return NULL;
        }
        if ((dev->driver->get(dev, NETOPT_RAWMODE, &raw, sizeof(raw)) <= 0) ||
            (raw != NETOPT_ENABLE)) {
            gnrc_pktsnip_t *ieee802154_hdr, *netif_hdr;
            gnrc_netif_hdr_t *hdr;
            uint8_t *data;
#if ENABLE_DEBUG
            char src_str[GNRC_NETIF_HDR_L2ADDR_MAX_LEN];
#endif
            gnrc_nettype_t proto;
            size_t mhr_len = ieee802154_get_frame_hdr_len(pkt->data);

            if (mhr_len == 0) {
                DEBUG("_recv_ieee802154: illegally formatted frame received\n");
                gnrc_pktbuf_release(pkt);
                return NULL;
            }
            nread -= mhr_len;
            /* mark IEEE 802.15.4 header */
            ieee802154_hdr = gnrc_pktbuf_mark(pkt, mhr_len, GNRC_NETTYPE_UNDEF);
            if (ieee802154_hdr == NULL) {
                DEBUG("_recv_ieee802154: no space left in packet buffer\n");
                gnrc_pktbuf_release(pkt);
                return NULL;
            }
            netif_hdr = _make_netif_hdr(ieee802154_hdr->data);
            if (netif_hdr == NULL) {
                DEBUG("_recv_ieee802154: no space left in packet buffer\n");
                gnrc_pktbuf_release(pkt);
                return NULL;
            }
            hdr = netif_hdr->data;
            data = pkt->data;
            hdr->lqi = data[nread + _LQI_TAIL_POS];
            hdr->rssi = data[nread + _RSSI_TAIL_POS];
            hdr->if_pid = thread_getpid();
            if (dev->driver->get(dev, NETOPT_PROTO, &proto, sizeof(proto)) > 0) {
                pkt->type = proto;
            }
#if ENABLE_DEBUG
            DEBUG("_recv_ieee802154: received packet from %s of length %u\n",
                  gnrc_netif_addr_to_str(src_str, sizeof(src_str),
                                         gnrc_netif_hdr_get_src_addr(hdr),
                                         hdr->src_l2addr_len),
                  nread);
#if defined(MODULE_OD)
            od_hex_dump(pkt->data, nread, OD_WIDTH_DEFAULT);
#endif
#endif
            gnrc_pktbuf_remove_snip(pkt, ieee802154_hdr);
            LL_APPEND(pkt, netif_hdr);
        }

        DEBUG("_recv_ieee802154: reallocating.\n");
        gnrc_pktbuf_realloc_data(pkt, nread);
    }

    return pkt;
}

static int _send(gnrc_netdev2_t *gnrc_netdev2, gnrc_pktsnip_t *pkt)
{
    netdev2_t *dev = gnrc_netdev2->dev;
    gnrc_netif_hdr_t *netif_hdr;
    struct iovec *vector;
    ieee802154_addr_t *dst_ptr;
    ieee802154_addr_t dst, src;
    int res = 0;
    size_t n;
    netopt_t addr_opt;
    netopt_enable_t enable_val;
    uint16_t dev_pan = 0, src_len;
    network_uint16_t flags = { 0 };
    uint8_t mhr[IEEE802154_MAX_HDR_LEN];

    if (pkt == NULL) {
        DEBUG("_send_ieee802154: pkt was NULL\n");
        return -EINVAL;
    }
    if (pkt->type != GNRC_NETTYPE_NETIF) {
        DEBUG("_send_ieee802154: first header is not generic netif header\n");
        return -EBADMSG;
    }
    netif_hdr = pkt->data;
    dev->driver->get(dev, NETOPT_NID, &dev_pan, sizeof(dev_pan));
    /* prepare destination address */
    if (!(netif_hdr->flags &
          (GNRC_NETIF_HDR_FLAGS_BROADCAST | GNRC_NETIF_HDR_FLAGS_MULTICAST))) {
        dst_ptr = NULL;
    }
    else {
        switch (netif_hdr->dst_l2addr_len) {
            case 2:
                dst.pan.u16 = dev_pan;
                memcpy(&dst.addr.s_addr, gnrc_netif_hdr_get_dst_addr(netif_hdr), 2);
                flags.u8[1] |= IEEE802154_FCF_DST_ADDR_SHORT;
                break;
            case 4:
                memcpy(&dst.pan, gnrc_netif_hdr_get_dst_addr(netif_hdr), 2);
                memcpy(&dst.addr.s_addr, gnrc_netif_hdr_get_dst_addr(netif_hdr) + 2, 2);
                flags.u8[1] |= IEEE802154_FCF_DST_ADDR_SHORT;
                break;
            case 8:
                dst.pan.u16 = dev_pan;
                memcpy(&dst.addr.l_addr, gnrc_netif_hdr_get_dst_addr(netif_hdr), 8);
                flags.u8[1] |= IEEE802154_FCF_DST_ADDR_LONG;
                break;
            case 10:
                memcpy(&dst.pan, gnrc_netif_hdr_get_dst_addr(netif_hdr), 2);
                memcpy(&dst.addr.l_addr, gnrc_netif_hdr_get_dst_addr(netif_hdr) + 2, 8);
                flags.u8[1] |= IEEE802154_FCF_DST_ADDR_LONG;
                break;
            default:
                DEBUG("_send_ieee802154: unexpected destination address length\n");
                return -EAFNOSUPPORT;
        }
        dst_ptr = &dst;
        if ((dev->driver->get(dev, NETOPT_AUTOACK, &enable_val, sizeof(enable_val)) > 0) &&
            (enable_val == NETOPT_ENABLE)) {
            flags.u8[0] |= IEEE802154_FCF_ACK_REQ;
        }
    }
    /* get source address from device */
    if (dev->driver->get(dev, NETOPT_SRC_LEN, &src_len, sizeof(src_len)) < 0) {
        DEBUG("_send_ieee802154: unable to aquire source address length\n");
        return -ENODEV;
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
        DEBUG("_send_ieee802154: unable to aquire source address\n");
        return -ENODEV;
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
    if ((res = ieee802154_set_frame_hdr(mhr, &src, dst_ptr, flags, 0)) < 0) {
        DEBUG("_send_ieee802154: Error preperaring frame\n");
        return res;
    }
    /* prepare packet for sending */
    pkt = gnrc_pktbuf_get_iovec(pkt, &n);
    vector = (struct iovec *)pkt->data;
    vector[0].iov_base = mhr;
    vector[0].iov_len = (size_t)res;
    dev->driver->send(dev, vector, n);
    /* release old data */
    gnrc_pktbuf_release(pkt);
    return 0;
}

/** @} */
