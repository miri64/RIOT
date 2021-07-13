/*
 * Copyright (C) 2021 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @author  Martine Lenders <m.lenders@fu-berlin.de>
 */

#include <assert.h>

#include "event.h"
#include "event/thread.h"
#include "log.h"
#include "net/dhcpv6.h"
#include "net/dhcpv6/client.h"      /* required for dhcpv6_duid_l2_t in _dhcpv6.h */
#include "net/dhcpv6/relay.h"
#include "net/netif.h"
#include "net/sock/async/event.h"
#include "net/sock/udp.h"
#include "thread.h"

#include "_dhcpv6.h"

#define ENABLE_DEBUG 1
#include "debug.h"

#define AUTO_INIT_PRIO      (THREAD_PRIORITY_MAIN - 1)

static char _auto_init_stack[THREAD_STACKSIZE_DEFAULT];
static struct {
    uint8_t inbuf[CONFIG_DHCPV6_RELAY_BUFLEN];
    uint8_t outbuf[CONFIG_DHCPV6_RELAY_BUFLEN];
    sock_udp_t listen_sock;
    sock_udp_t fwd_sock;
} _relay_state;

static void *_dhcpv6_relay_auto_init_thread(void *);
static void _udp_handler(sock_udp_t *sock, sock_async_flags_t type,
                         void *arg);
static void _dhcpv6_handler(const sock_udp_ep_t *remote, const uint8_t *msg,
                            size_t msg_size);
static void _forward_msg(const sock_udp_ep_t *remote, const uint8_t *msg, size_t
                         msg_size, bool client_msg);
static void _forward_reply(const uint8_t *in_msg, size_t in_msg_size);

static int16_t _only_one_netif(void)
{
    if (IS_USED(MODULE_NETIF)) {
        netif_t *netif = netif_iter(NULL);

        return (netif_iter(netif) == NULL) ? netif_get_id(netif) : -1;
    }
    else {
        return -1;
    }
}

void dhcpv6_relay_auto_init(void)
{
    if (IS_USED(MODULE_AUTO_INIT_DHCPV6_RELAY)) {
        int16_t netif = _only_one_netif();
        if (netif > 0) {
            if (IS_USED(MODULE_EVENT_THREAD)) {
                dhcpv6_relay_init(EVENT_PRIO_LOWEST, netif, netif);
            }
            else {
                thread_create(_auto_init_stack, ARRAY_SIZE(_auto_init_stack),
                              AUTO_INIT_PRIO, THREAD_CREATE_STACKTEST,
                              _dhcpv6_relay_auto_init_thread,
                              (void *)(intptr_t)netif, "dhcpv6_relay");
            }
        }
        else {
            LOG_WARNING("DHCPv6 relay: auto init failed, more than 1 interface\n");
        }
    }
}

void dhcpv6_relay_init(event_queue_t *eq, uint16_t listen_netif,
                       uint16_t fwd_netif)
{
    sock_udp_ep_t local = { .family = AF_INET6, .port = DHCPV6_SERVER_PORT,
                            .netif = listen_netif };
    sock_udp_ep_t remote = {
        .family = AF_INET6,
        .port = DHCPV6_SERVER_PORT,
        .netif = fwd_netif,
        .addr = {
            .ipv6 = DHCPV6_ALL_RELAY_AGENTS_AND_SERVERS
        }
    };
    int res;

    assert(eq->waiter != NULL);
    /* initialize client-listening sock */
    res = sock_udp_create(&_relay_state.listen_sock, &local, NULL, 0);

    if (res < 0) {
        DEBUG("DHCPv6 relay: unable to open listen sock: %d\n", -res);
        return;
    }
    sock_udp_event_init(&_relay_state.listen_sock, eq, _udp_handler, NULL);

    /* TODO: what if fwd_netif and listen_netif are the same? */
    /* initialize forwarding / reply-listening sock */
    local.netif = fwd_netif;
    res = sock_udp_create(&_relay_state.fwd_sock, &local, &remote, 0);
    if (res < 0) {
        DEBUG("DHCPv6 relay: unable to open fwd sock: %d\n", -res);
        return;
    }
    sock_udp_event_init(&_relay_state.fwd_sock, eq, _udp_handler, NULL);
}

static void *_dhcpv6_relay_auto_init_thread(void *args)
{
    event_queue_t queue;
    int16_t netif = (intptr_t)args;

    event_queue_init(&queue);
    dhcpv6_relay_init(&queue, netif, netif);
    event_loop(&queue);
    return NULL;
}

static void _udp_handler(sock_udp_t *sock, sock_async_flags_t type,
                         void *arg)
{
    (void)arg;
    if (type == SOCK_ASYNC_MSG_RECV) {
        sock_udp_ep_t remote = { .family = AF_INET6 };
        ssize_t res = sock_udp_recv(sock, _relay_state.inbuf,
                                    sizeof(_relay_state.inbuf), 0, &remote);

        if (res < 0) {
            DEBUG("DHCPv6 relay: Error receiving UDP message: %d\n", (int)-res);
            return;
        }
        _dhcpv6_handler(&remote, (uint8_t *)_relay_state.inbuf, res);
    }
}

static void _dhcpv6_handler(const sock_udp_ep_t *remote, const uint8_t *msg,
                            size_t msg_size)
{
    if (msg_size == 0) {
        DEBUG("DHCPv6 relay: incoming message size 0\n");
        return;
    }
    if (remote->family != AF_INET6) {
        DEBUG("DHCPv6 relay: incoming message source not an IPv6 address\n");
        return;
    }
    switch (msg[0]) {
        case DHCPV6_SOLICIT:
        case DHCPV6_REQUEST:
        case DHCPV6_CONFIRM:
        case DHCPV6_RENEW:
        case DHCPV6_REBIND:
        case DHCPV6_RELEASE:
        case DHCPV6_DECLINE:
        case DHCPV6_INFO_REQUEST:
            _forward_msg(remote, msg, msg_size, true);
            break;
        case DHCPV6_RELAY_FORW:
            _forward_msg(remote, msg, msg_size, false);
            break;
        case DHCPV6_RELAY_REPL:
            _forward_reply(msg, msg_size);
            break;
        default:
            DEBUG("DHCPv6 relay: unexpected incoming message type %u\n",
                  msg[0]);
            break;
    }
}

static uint16_t _compose_iid_opt(dhcpv6_opt_iid_t *opt,
                                 const sock_udp_ep_t *remote)
{
    opt->type = byteorder_htons(DHCPV6_OPT_IID);
    opt->len = byteorder_htons(sizeof(remote->netif));
    memcpy(opt->iid, &remote->netif, sizeof(remote->netif));
    return sizeof(remote->netif) + sizeof(dhcpv6_opt_iid_t);
}

static uint16_t _compose_relay_msg_opt(dhcpv6_opt_relay_msg_t *opt,
                                       const uint8_t *in_msg,
                                       size_t in_msg_size)
{
    opt->type = byteorder_htons(DHCPV6_OPT_RELAY_MSG);
    opt->len = byteorder_htons((uint16_t)in_msg_size);
    memcpy(opt->msg, in_msg, in_msg_size);
    return (uint16_t)in_msg_size + sizeof(dhcpv6_opt_relay_msg_t);
}

static void _forward_msg(const sock_udp_ep_t *remote, const uint8_t *in_msg,
                         size_t in_msg_size, bool client_msg)
{
    dhcpv6_relay_msg_t *out_fwd = (dhcpv6_relay_msg_t *)_relay_state.outbuf;
    uint16_t out_fwd_len = sizeof(dhcpv6_relay_msg_t);

    assert(in_msg_size <= UINT16_MAX);

    if (in_msg_size < sizeof(dhcpv6_msg_t)) {
        DEBUG("DHCPv6 relay: incoming message too small\n");
        return;
    }
    if (client_msg) {
        out_fwd->hop_count = 0;
    }
    else {
        const dhcpv6_relay_msg_t *in_fwd = (dhcpv6_relay_msg_t *)in_msg;

        if (in_fwd->hop_count > CONFIG_DHCPV6_RELAY_HOP_LIMIT) {
            DEBUG("DHCPv6 relay: incoming message exceeded hop limit\n");
            return;
        }
        if (in_msg_size > sizeof(dhcpv6_relay_msg_t)) {
            DEBUG("DHCPv6 relay: incoming forward message too small\n");
            return;
        }
        /* TODO: check if peer-address is myself to prevent network spam when
         * fwd_netif == listen_netif */
        out_fwd->hop_count = in_fwd->hop_count + 1;
    }

    out_fwd->type = DHCPV6_RELAY_FORW;
    /* set link-address to unspecified address, we will provide an Interface-ID
     * option instead */
    memset(&out_fwd->link_address, 0, sizeof(out_fwd->link_address));
    assert(sizeof(out_fwd->peer_address) == sizeof(remote->addr.ipv6));
    memcpy(&out_fwd->peer_address, &remote->addr.ipv6,
           sizeof(out_fwd->peer_address));

    /* set mandatory options */
    out_fwd_len += _compose_iid_opt(
        (dhcpv6_opt_iid_t *)&_relay_state.outbuf[out_fwd_len], remote
    );
    if ((out_fwd_len + in_msg_size + sizeof(dhcpv6_opt_relay_msg_t))) {
        DEBUG("DHCPv6 relay: output buffer too small to relay message\n");
        return;
    }
    out_fwd_len += _compose_relay_msg_opt(
        (dhcpv6_opt_relay_msg_t *)&_relay_state.outbuf[out_fwd_len],
        in_msg, in_msg_size
    );
    if (sock_udp_send(&_relay_state.fwd_sock, out_fwd, out_fwd_len,
                      NULL) < 0) {
        DEBUG("DHCPv6 relay: sending forward message failed\n");
    }
}

static uint16_t _get_iid(dhcpv6_opt_iid_t *opt)
{
    if (byteorder_ntohs(opt->len) != sizeof(uint16_t)) {
        DEBUG("DHCPv6 relay: unexpected interface-ID length\n");
        return 0;
    }
    return (opt->iid[0] << 8) | (opt->iid[1] & 0xff);
}

static inline size_t _opt_len(dhcpv6_opt_t *opt)
{
    return sizeof(dhcpv6_opt_t) + byteorder_ntohs(opt->len);
}

static inline dhcpv6_opt_t *_opt_next(dhcpv6_opt_t *opt)
{
    return (dhcpv6_opt_t *)(((uint8_t *)opt) + _opt_len(opt));
}

static void _forward_reply(const uint8_t *in_msg, size_t in_msg_size)
{
    const dhcpv6_relay_msg_t *in_reply = (const dhcpv6_relay_msg_t *)in_msg;
    const uint8_t *out_msg = NULL;
    size_t out_msg_len = 0;
    sock_udp_ep_t target = { .family = AF_INET6 };

    if (in_msg_size < sizeof(dhcpv6_relay_msg_t)) {
        DEBUG("DHCPv6 relay: incoming reply message too small\n");
        return;
    }
    in_msg_size += sizeof(dhcpv6_relay_msg_t);
    for (dhcpv6_opt_t *opt = (dhcpv6_opt_t *)(&in_msg[sizeof(dhcpv6_relay_msg_t)]);
         in_msg_size > 0; in_msg_size -= _opt_len(opt), opt = _opt_next(opt)) {

        switch (byteorder_ntohs(opt->type)) {
            case DHCPV6_OPT_IID:
                target.netif = _get_iid((dhcpv6_opt_iid_t *)opt);
                if (target.netif == 0) {
                    return;
                }
                break;
            case DHCPV6_OPT_RELAY_MSG: {
                uint16_t opt_len = byteorder_ntohs(opt->len);

                if (opt_len > in_msg_size) {
                    DEBUG("DHCPv6 relay: invalid relay message option size\n");
                }
                out_msg = ((uint8_t *)opt) + sizeof(dhcpv6_opt_relay_msg_t);
                out_msg_len = opt_len;
                break;
            }
            default:
                break;
        }
    }

    if ((out_msg == NULL) || (out_msg_len == 0)) {
        DEBUG("DHCPv6 relay: no reply to forward found\n");
        return;
    }
    if (out_msg[0] == DHCPV6_RELAY_REPL) {
        /* out message is heading for the next relay */
        target.port = DHCPV6_SERVER_PORT;
    }
    else {
        /* out message is heading for the client it is destined to */
        target.port = DHCPV6_CLIENT_PORT;
    }
    assert(sizeof(in_reply->peer_address) == sizeof(target.addr.ipv6));

    memcpy(&target.addr.ipv6, &in_reply->peer_address, sizeof(target.addr.ipv6));
    if (sock_udp_send(NULL, out_msg, out_msg_len, &target) < 0) {
        DEBUG("DHCPv6 relay: forwarding reply towards target failed\n");
    }
}

/** @} */
