/*
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @brief       GNRC implementation of the udp interface defined by net/gnrc/udp.h
 *
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 */

#include <errno.h>

#include "byteorder.h"
#include "net/af.h"
#include "net/protnum.h"
#include "net/gnrc/conn.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/udp.h"
#include "net/udp.h"
#include "random.h"

#include "net/conn/udp.h"

static inline bool _af_not_supported(const conn_ep_udp_t *ep)
{
    return ((ep->family != AF_INET6) && (ep->family != AF_INET));
}

int conn_udp_create(conn_udp_t *conn, const conn_ep_udp_t *local,
                    const conn_ep_udp_t *remote)
{
    assert(conn);
    assert(local == NULL || local->port != 0);
    assert(remote == NULL || remote->port != 0);
    if ((local != NULL) && (remote != NULL) &&
        (local->netif != CONN_EP_ANY_NETIF) &&
        (remote->netif != CONN_EP_ANY_NETIF) &&
        (local->netif != remote->netif)) {
        return -EINVAL;
    }
    memset(&conn->local, 0, sizeof(conn_udp_t));
    if (local != NULL) {
        if (_af_not_supported(local)) {
            return -EAFNOSUPPORT;
        }
        memcpy(&conn->local, local, sizeof(conn_udp_t));
        gnrc_conn_create(&conn->reg, GNRC_NETTYPE_UDP,
                         local->port);
    }
    memset(&conn->remote, 0, sizeof(conn_udp_t));
    if (remote != NULL) {
        if (_af_not_supported(remote)) {
            return -EAFNOSUPPORT;
        }
        memcpy(&conn->remote, remote, sizeof(conn_udp_t));
    }
    return 0;
}

void conn_udp_close(conn_udp_t *conn)
{
    gnrc_netreg_unregister(GNRC_NETTYPE_UDP, &conn->reg.entry);
}

int conn_udp_get_local(conn_udp_t *conn, conn_ep_udp_t *local)
{
    assert(conn && local);
    if (conn->local.port == 0) {
        return -EADDRNOTAVAIL;
    }
    memcpy(local, &conn->local, sizeof(conn_ep_udp_t));
    return 0;
}

int conn_udp_get_remote(conn_udp_t *conn, conn_ep_udp_t *remote)
{
    assert(conn && remote);
    if (conn->remote.port == 0) {
        return -ENOTCONN;
    }
    memcpy(remote, &conn->remote, sizeof(conn_ep_udp_t));
    return 0;
}

int conn_udp_recvfrom(conn_udp_t *conn, void *data, size_t max_len,
                      uint32_t timeout, conn_ep_udp_t *remote)
{
    gnrc_pktsnip_t *pkt, *udp;
    udp_hdr_t *hdr;
    conn_ep_ip_t tmp;
    int res;

    assert((conn != NULL) && (data != NULL) && (max_len > 0));
    if (conn->local.port == 0) {
        return -EADDRNOTAVAIL;
    }
    tmp.family = conn->local.family;
    res = gnrc_conn_recv((gnrc_conn_reg_t *)conn, &pkt, timeout, &tmp);
    if (res < 0) {
        return res;
    }
    if (pkt->size > max_len) {
        gnrc_pktbuf_release(pkt);
        return -ENOBUFS;
    }
    udp = gnrc_pktsnip_search_type(pkt, GNRC_NETTYPE_UDP);
    assert(udp);
    hdr = udp->data;
    if (remote != NULL) {
        /* return remote to possibly block if wrong remote */
        memcpy(remote, &tmp, sizeof(tmp));
        remote->port = byteorder_ntohs(hdr->src_port);
    }
    if (((conn->remote.port != 0) &&    /* check remote end-point if set */
         (conn->remote.port != byteorder_ntohs(hdr->src_port))) ||
        /* We only have IPv6 for now, so just comparing the whole end point
         * should suffice */
        ((memcmp(&conn->remote.addr, &ipv6_addr_unspecified,
                 sizeof(ipv6_addr_t)) != 0) &&
         (memcmp(&conn->remote.addr, &tmp.addr, sizeof(ipv6_addr_t)) != 0))) {
        gnrc_pktbuf_release(pkt);
        return -EPROTO;
    }
    memcpy(data, pkt->data, pkt->size);
    gnrc_pktbuf_release(pkt);
    return (int)pkt->size;
}

int conn_udp_sendto(conn_udp_t *conn, const void *data, size_t len,
                    const conn_ep_udp_t *remote)
{
    int res;
    gnrc_pktsnip_t *payload, *pkt;
    uint16_t src_port = 0, dst_port;
    conn_ep_ip_t local;
    conn_ep_ip_t rem;

    assert((len == 0) || (data != NULL)); /* (len != 0) => (data != NULL) */
    if ((remote != NULL) && (conn != NULL) &&
        (conn->local.netif != CONN_EP_ANY_NETIF) &&
        (remote->netif != CONN_EP_ANY_NETIF) &&
        (conn->local.netif != remote->netif)) {
        return -EINVAL;
    }
    if ((remote != NULL) && (remote->port == 0)) {
        return -EINVAL;
    }
    if ((remote == NULL) && ((conn == NULL) || (conn->remote.port == 0))) {
        return -ENOTCONN;
    }
    if ((conn == NULL) || (conn->local.port == 0)) {
        while (src_port == 0) {
            src_port = (uint16_t)(random_uint32() & UINT16_MAX);
        }
        memset(&local, 0, sizeof(local));
        if (conn != NULL) {
            /* bind conn object implicitly */
            conn->local.port = src_port;
        }
    }
    else {
        src_port = conn->local.port;
        memcpy(&local, &conn->local, sizeof(local));
    }
    if (remote == NULL) {
        /* conn can't be NULL at this point */
        memcpy(&rem, &conn->remote, sizeof(rem));
        dst_port = conn->remote.port;
    }
    else {
        memcpy(&rem, remote, sizeof(rem));
        dst_port = remote->port;
    }
    if ((remote != NULL) && (remote->family == AF_UNSPEC) &&
        (conn->remote.family != AF_UNSPEC)) {
        /* remote was set on create so take its family */
        rem.family = conn->remote.family;
    }
    else if ((remote != NULL) && _af_not_supported(remote)) {
        return -EAFNOSUPPORT;
    }
    else if ((local.family == AF_UNSPEC) && (rem.family != AF_UNSPEC)) {
        /* local was set to 0 above */
        local.family = rem.family;
    }
    else if ((local.family != AF_UNSPEC) && (rem.family == AF_UNSPEC)) {
        /* local was given on create, but remote family wasn't given by user and
         * there was no remote given on create, take from local */
        rem.family = local.family;
    }
    payload = gnrc_pktbuf_add(NULL, (void *)data, len, GNRC_NETTYPE_UNDEF);
    if (payload == NULL) {
        return -ENOMEM;
    }
    pkt = gnrc_udp_hdr_build(payload, src_port, dst_port);
    if (pkt == NULL) {
        gnrc_pktbuf_release(payload);
        return -ENOMEM;
    }
    res = gnrc_conn_send(pkt, &local, &rem, PROTNUM_UDP);
    if (res <= 0) {
        return res;
    }
    return res - sizeof(udp_hdr_t);
}

/** @} */
