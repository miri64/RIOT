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
 */

#include <stdlib.h>

#include "net/gnrc/ipv6.h"
#include "net/gnrc/ndp.h"
#include "random.h"
#include "timex.h"
#include "vtimer.h"

#include "net/gnrc/ndp/internal.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"

#if ENABLE_DEBUG
static char addr_str[IPV6_ADDR_MAX_STR_LEN];
#endif

static gnrc_ipv6_nc_t *_last_router = NULL; /* last router chosen as default
                                             * router. Only used if reachability
                                             * is suspect (i. e. incomplete or
                                             * not at all) */
static gnrc_pktsnip_t *_build_headers(kernel_pid_t iface, gnrc_pktsnip_t *payload,
                                      ipv6_addr_t *dst, ipv6_addr_t *src);
static size_t _get_l2src(kernel_pid_t iface, uint8_t *l2src, size_t l2src_maxlen);

/**
 * @brief   Sends @ref GNRC_NETAPI_MSG_TYPE_SND delayed.
 *
 * @param[in] t         Timer for the delay.
 * @param[in] interval  Delay interval.
 * @param[in] pkt       Packet to send delayed.
 */
static inline void _send_delayed(vtimer_t *t, timex_t interval, gnrc_pktsnip_t *pkt)
{
    vtimer_remove(t);
    vtimer_set_msg(t, interval, gnrc_ipv6_pid, GNRC_NETAPI_MSG_TYPE_SND, pkt);
}


ipv6_addr_t *gnrc_ndp_internal_default_router(void)
{
    gnrc_ipv6_nc_t *router = gnrc_ipv6_nc_get_next_router(NULL);

    /* first look if there is any reachable router */
    while (router != NULL) {
        if ((gnrc_ipv6_nc_get_state(router) != GNRC_IPV6_NC_STATE_INCOMPLETE) &&
            (gnrc_ipv6_nc_get_state(router) != GNRC_IPV6_NC_STATE_UNREACHABLE)) {
            _last_router = NULL;

            return &router->ipv6_addr;
        }

        router = gnrc_ipv6_nc_get_next_router(router);
    }

    /* else take the first one, but keep round-robin in further selections */
    router = gnrc_ipv6_nc_get_next_router(_last_router);

    if (router == NULL) {   /* end of router list or there is none => wrap around */
        router = gnrc_ipv6_nc_get_next_router(router);

        if (router == NULL) {   /* still nothing found => no router in list */
            return NULL;
        }
    }

    _last_router = router;

    return &router->ipv6_addr;
}

void gnrc_ndp_internal_set_state(gnrc_ipv6_nc_t *nc_entry, uint8_t state)
{
    gnrc_ipv6_netif_t *ipv6_iface;
    timex_t t = { GNRC_NDP_FIRST_PROBE_DELAY, 0 };

    nc_entry->flags &= ~GNRC_IPV6_NC_STATE_MASK;
    nc_entry->flags |= state;

    DEBUG("ndp internal: set %s state to ",
          ipv6_addr_to_str(addr_str, &nc_entry->ipv6_addr, sizeof(addr_str)));

    switch (state) {
        case GNRC_IPV6_NC_STATE_REACHABLE:
            ipv6_iface = gnrc_ipv6_netif_get(nc_entry->iface);
            DEBUG("REACHABLE (reachable time = %" PRIu32 ".%06" PRIu32 ")\n",
                  ipv6_iface->reach_time.seconds,
                  ipv6_iface->reach_time.microseconds);
            t = ipv6_iface->reach_time;

            /* we intentionally fall through here to set the desired timeout t */
        case GNRC_IPV6_NC_STATE_DELAY:
#if ENABLE_DEBUG
            if (state == GNRC_IPV6_NC_STATE_DELAY) {
                DEBUG("DELAY (probe with unicast NS in %u seconds)\n",
                      GNRC_NDP_FIRST_PROBE_DELAY);
            }
#endif
            vtimer_remove(&nc_entry->nbr_sol_timer);
            vtimer_set_msg(&nc_entry->nbr_sol_timer, t, gnrc_ipv6_pid,
                           GNRC_NDP_MSG_NC_STATE_TIMEOUT, nc_entry);
            break;

        case GNRC_IPV6_NC_STATE_PROBE:
            ipv6_iface = gnrc_ipv6_netif_get(nc_entry->iface);

            nc_entry->probes_remaining = GNRC_NDP_MAX_UC_NBR_SOL_NUMOF;
            DEBUG("PROBE (probe with %" PRIu8 " unicast NS every %" PRIu32
                  ".%06" PRIu32 " seconds)\n", nc_entry->probes_remaining,
                  ipv6_iface->retrans_timer.seconds,
                  ipv6_iface->retrans_timer.microseconds);

            gnrc_ndp_internal_send_nbr_sol(nc_entry->iface, &nc_entry->ipv6_addr,
                                           &nc_entry->ipv6_addr);

            mutex_lock(&ipv6_iface->mutex);
            vtimer_remove(&nc_entry->nbr_sol_timer);
            vtimer_set_msg(&nc_entry->nbr_sol_timer,
                           ipv6_iface->retrans_timer, gnrc_ipv6_pid,
                           GNRC_NDP_MSG_NBR_SOL_RETRANS, nc_entry);
            mutex_unlock(&ipv6_iface->mutex);
            break;

#ifdef ENABLE_DEBUG
        case GNRC_IPV6_NC_STATE_STALE:
            DEBUG("STALE (go into DELAY on next packet)\n");
            break;
#endif

        default:
            DEBUG("errorneous or unknown\n");
            break;
    }
}

void gnrc_ndp_internal_send_nbr_adv(kernel_pid_t iface, ipv6_addr_t *tgt, ipv6_addr_t *dst,
                                    bool supply_tl2a, gnrc_pktsnip_t *ext_opts)
{
    gnrc_pktsnip_t *hdr, *pkt = ext_opts;
    uint8_t adv_flags = 0;

    DEBUG("ndp internal: send neighbor advertisement (iface: %" PRIkernel_pid ", tgt: %s, ",
          iface, ipv6_addr_to_str(addr_str, tgt, sizeof(addr_str)));
    DEBUG("dst: %s, supply_tl2a: %d)\n",
          ipv6_addr_to_str(addr_str, dst, sizeof(addr_str)), supply_tl2a);

    if ((gnrc_ipv6_netif_get(iface)->flags & GNRC_IPV6_NETIF_FLAGS_ROUTER) &&
        (gnrc_ipv6_netif_get(iface)->flags & GNRC_IPV6_NETIF_FLAGS_RTR_ADV)) {
        adv_flags |= NDP_NBR_ADV_FLAGS_R;
    }

    if (ipv6_addr_is_unspecified(dst)) {
        ipv6_addr_set_all_nodes_multicast(dst, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);
    }
    else {
        adv_flags |= NDP_NBR_ADV_FLAGS_S;
    }

    if (supply_tl2a) {
        uint8_t l2src[8];
        size_t l2src_len;
        /* we previously checked if we are the target, so we can take our L2src */
        l2src_len = _get_l2src(iface, l2src, sizeof(l2src));

        if (l2src_len > 0) {
            /* add target address link-layer address option */
            pkt = gnrc_ndp_opt_tl2a_build(l2src, l2src_len, pkt);

            if (pkt == NULL) {
                DEBUG("ndp internal: error allocating Target Link-layer address option.\n");
                gnrc_pktbuf_release(ext_opts);
                return;
            }
        }
    }

    /* TODO: also check if the node provides proxy servies for tgt */
    if ((pkt != NULL) && !gnrc_ipv6_netif_addr_is_non_unicast(tgt)) {
        /* TL2A is not supplied and tgt is not anycast */
        adv_flags |= NDP_NBR_ADV_FLAGS_O;
    }

    hdr = gnrc_ndp_nbr_adv_build(adv_flags, tgt, pkt);

    if (hdr == NULL) {
        DEBUG("ndp internal: error allocating Neighbor advertisement.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    pkt = hdr;
    hdr = _build_headers(iface, pkt, dst, NULL);
    if (hdr == NULL) {
        DEBUG("ndp internal: error adding lower-layer headers.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    if (gnrc_ipv6_netif_addr_is_non_unicast(tgt)) {
        /* avoid collision for anycast addresses
         * (see https://tools.ietf.org/html/rfc4861#section-7.2.7) */
        timex_t delay = { 0, genrand_uint32_range(0, GNRC_NDP_MAX_AC_TGT_DELAY * SEC_IN_USEC) };
        timex_normalize(&delay);
        gnrc_ipv6_nc_t *nc_entry = gnrc_ipv6_nc_get(iface, tgt);
        DEBUG("ndp internal: delay neighbor advertisement for %" PRIu32 " sec.",
              delay.seconds);

        /* nc_entry must be set so no need to check it */
        _send_delayed(&nc_entry->nbr_adv_timer, delay, hdr);
    }
    else {
        gnrc_netapi_send(gnrc_ipv6_pid, hdr);
    }
}

void gnrc_ndp_internal_send_nbr_sol(kernel_pid_t iface, ipv6_addr_t *tgt,
                                    ipv6_addr_t *dst)
{
    gnrc_pktsnip_t *hdr, *pkt = NULL;
    ipv6_addr_t *src = NULL;

    DEBUG("ndp internal: send neighbor solicitation (iface: %" PRIkernel_pid ", tgt: %s, ",
          iface, ipv6_addr_to_str(addr_str, tgt, sizeof(addr_str)));
    DEBUG("dst: %s)\n", ipv6_addr_to_str(addr_str, dst, sizeof(addr_str)));

    /* check if there is a fitting source address to target */
    if ((src = gnrc_ipv6_netif_find_best_src_addr(iface, tgt)) != NULL) {
        uint8_t l2src[8];
        size_t l2src_len;
        l2src_len = _get_l2src(iface, l2src, sizeof(l2src));

        if (l2src_len > 0) {
            /* add source address link-layer address option */
            pkt = gnrc_ndp_opt_sl2a_build(l2src, l2src_len, NULL);

            if (pkt == NULL) {
                DEBUG("ndp internal: error allocating Source Link-layer address option.\n");
                gnrc_pktbuf_release(pkt);
                return;
            }
        }
    }

    hdr = gnrc_ndp_nbr_sol_build(tgt, pkt);

    if (hdr == NULL) {
        DEBUG("ndp internal: error allocating Neighbor solicitation.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    pkt = hdr;
    hdr = _build_headers(iface, pkt, dst, src);
    if (hdr == NULL) {
        DEBUG("ndp internal: error adding lower-layer headers.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    gnrc_netapi_send(gnrc_ipv6_pid, hdr);
}

void gnrc_ndp_internal_send_rtr_sol(kernel_pid_t iface, ipv6_addr_t *dst)
{
    gnrc_pktsnip_t *hdr, *pkt = NULL;
    ipv6_addr_t *src = NULL, all_routers = IPV6_ADDR_ALL_ROUTERS_LINK_LOCAL;
    DEBUG("ndp internal: send router solicitation (iface: %" PRIkernel_pid ", dst: ff02::2)\n",
          iface);
    if (dst == NULL) {
        dst = &all_routers;
    }
    /* check if there is a fitting source address to target */
    if ((src = gnrc_ipv6_netif_find_best_src_addr(iface, dst)) != NULL) {
        uint8_t l2src[8];
        size_t l2src_len;
        l2src_len = _get_l2src(iface, l2src, sizeof(l2src));
        if (l2src_len > 0) {
            /* add source address link-layer address option */
            pkt = gnrc_ndp_opt_sl2a_build(l2src, l2src_len, NULL);

            if (pkt == NULL) {
                DEBUG("ndp internal: error allocating Source Link-layer address option.\n");
                gnrc_pktbuf_release(pkt);
                return;
            }
        }
    }
    hdr = gnrc_ndp_rtr_sol_build(pkt);
    if (hdr == NULL) {
        DEBUG("ndp internal: error allocating router solicitation.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    pkt = hdr;
    hdr = _build_headers(iface, pkt, dst, src);
    if (hdr == NULL) {
        DEBUG("ndp internal: error adding lower-layer headers.\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    gnrc_netapi_send(gnrc_ipv6_pid, hdr);
}

int gnrc_ndp_internal_sl2a_opt_handle(gnrc_pktsnip_t *pkt, ipv6_hdr_t *ipv6, uint8_t icmpv6_type,
                                      ndp_opt_t *sl2a_opt, uint8_t *l2src)
{
    int sl2a_len = 0;
    uint8_t *sl2a = (uint8_t *)(sl2a_opt + 1);

    if ((sl2a_opt->len == 0) || ipv6_addr_is_unspecified(&ipv6->src)) {
        DEBUG("ndp: invalid source link-layer address option received\n");
        return -EINVAL;
    }

    while (pkt) {
        if (pkt->type == GNRC_NETTYPE_NETIF) {
            gnrc_netif_hdr_t *hdr = pkt->data;
            sl2a_len = hdr->src_l2addr_len;
            break;
        }
        pkt = pkt->next;
    }

    DEBUG("ndp: received SL2A (link-layer address: %s)\n",
          gnrc_netif_addr_to_str(addr_str, sizeof(addr_str), sl2a, sl2a_len));

    switch (icmpv6_type) {
        case ICMPV6_RTR_ADV:
        case ICMPV6_NBR_SOL:
            if (sl2a_len == 0) {  /* in case there was no source address in l2 */
                sl2a_len = (sl2a_opt->len / 8) - sizeof(ndp_opt_t);

                /* ignore all zeroes at the end for length */
                for (; sl2a[sl2a_len - 1] == 0x00; sl2a_len--);
            }

            memcpy(l2src, sl2a, sl2a_len);
            return sl2a_len;

        default:    /* wrong encapsulating message: silently discard */
            DEBUG("ndp: silently discard sl2a_opt for ICMPv6 message type %"
                  PRIu8 "\n", icmpv6_type);
            return -ENOTSUP;
    }
}

int gnrc_ndp_internal_tl2a_opt_handle(gnrc_pktsnip_t *pkt, ipv6_hdr_t *ipv6,
                                      uint8_t icmpv6_type, ndp_opt_t *tl2a_opt,
                                      uint8_t *l2addr)
{
    uint8_t tl2a_len = 0;
    uint8_t *tl2a = (uint8_t *)(tl2a_opt + 1);

    if ((tl2a_opt->len == 0) || ipv6_addr_is_unspecified(&ipv6->src)) {
        DEBUG("ndp: invalid target link-layer address option received\n");
        return -EINVAL;
    }

    switch (icmpv6_type) {
        case ICMPV6_NBR_ADV:
            while (pkt) {
                if (pkt->type == GNRC_NETTYPE_NETIF) {
                    gnrc_netif_hdr_t *hdr = pkt->data;
                    tl2a_len = hdr->src_l2addr_len;
                    break;
                }
                pkt = pkt->next;
            }

            if (tl2a_len == 0) {  /* in case there was no source address in l2 */
                tl2a_len = (tl2a_opt->len / 8) - sizeof(ndp_opt_t);

                /* ignore all zeroes at the end for length */
                for (; tl2a[tl2a_len - 1] == 0x00; tl2a_len--);
            }

            DEBUG("ndp: received TL2A (link-layer address: %s)\n",
                  gnrc_netif_addr_to_str(addr_str, sizeof(addr_str), tl2a, tl2a_len));

            memcpy(l2addr, tl2a, tl2a_len);

            return (int)tl2a_len;

        default:    /* wrong encapsulating message: silently discard */
            DEBUG("ndp: silently discard tl2a_opt for ICMPv6 message type %"
                  PRIu8 "\n", icmpv6_type);
            return 0;
    }
}

bool gnrc_ndp_internal_mtu_opt_handle(kernel_pid_t iface, uint8_t icmpv6_type,
                                      ndp_opt_mtu_t *mtu_opt)
{
    gnrc_ipv6_netif_t *if_entry = gnrc_ipv6_netif_get(iface);
    assert(if_entry != NULL);
    if ((mtu_opt->len != NDP_OPT_MTU_LEN)) {
        DEBUG("ndp: invalid MTU option received\n");
        return false;
    }
    if (icmpv6_type != ICMPV6_RTR_ADV) {
        /* else discard silently */
        return true;
    }
    mutex_lock(&if_entry->mutex);
    if_entry->mtu = byteorder_ntohl(mtu_opt->mtu);
    mutex_unlock(&if_entry->mutex);
    return true;
}

bool gnrc_ndp_internal_pi_opt_handle(kernel_pid_t iface, uint8_t icmpv6_type,
                                     ndp_opt_pi_t *pi_opt)
{
    ipv6_addr_t *prefix;
    gnrc_ipv6_netif_addr_t *a;
    if ((pi_opt->len != NDP_OPT_MTU_LEN)) {
        DEBUG("ndp: invalid MTU option received\n");
        return false;
    }
    if (icmpv6_type != ICMPV6_RTR_ADV || ipv6_addr_is_link_local(&pi_opt->prefix)) {
        /* else discard silently */
        return true;
    }
    prefix = gnrc_ipv6_netif_find_addr(iface, &pi_opt->prefix);
    if (((prefix == NULL) ||
         (gnrc_ipv6_netif_addr_get(prefix)->prefix_len != pi_opt->prefix_len)) &&
        (pi_opt->valid_ltime.u32 != 0)) {
        prefix = gnrc_ipv6_netif_add_addr(iface, &pi_opt->prefix,
                                          pi_opt->prefix_len,
                                          pi_opt->flags & NDP_OPT_PI_FLAGS_MASK);
        if (prefix == NULL) {
            DEBUG("ndp: could not add prefix to interface %d\n", iface);
            return false;
        }
    }
    a = gnrc_ipv6_netif_addr_get(prefix);
    if (pi_opt->valid_ltime.u32 == 0) {
        if (prefix != NULL) {
            gnrc_ipv6_netif_remove_addr(iface, &a->addr);
        }

        return true;
    }
    a->valid = byteorder_ntohl(pi_opt->valid_ltime);
    a->preferred = byteorder_ntohl(pi_opt->pref_ltime);
    vtimer_remove(&a->valid_timeout);
    if (a->valid != UINT32_MAX) {
        vtimer_set_msg(&a->valid_timeout,
                       timex_set(byteorder_ntohl(pi_opt->valid_ltime), 0),
                       thread_getpid(), GNRC_NDP_MSG_ADDR_TIMEOUT, &a->addr);
    }
    /* TODO: preferred lifetime for address auto configuration */
    /* on-link flag MUST stay set if it was */
    a->flags &= ~NDP_OPT_PI_FLAGS_A;
    a->flags |= (pi_opt->flags & NDP_OPT_PI_FLAGS_MASK);
    return true;
}

static size_t _get_l2src(kernel_pid_t iface, uint8_t *l2src, size_t l2src_maxlen)
{
    bool try_long = false;
    int res;
    size_t l2src_len;
    /* maximum address length that fits into a minimum length (8) S/TL2A option */
    const uint16_t max_short_len = 6;

    /* try getting source address */
    if ((gnrc_netapi_get(iface, NETOPT_SRC_LEN, 0, &l2src_len,
                         sizeof(l2src_len)) >= 0) &&
        (l2src_len > max_short_len)) {
        try_long = true;
    }

    if (try_long && ((res = gnrc_netapi_get(iface, NETOPT_ADDRESS_LONG, 0,
                                            l2src, l2src_maxlen)) > max_short_len)) {
        l2src_len = (uint16_t)res;
    }
    else if ((res = gnrc_netapi_get(iface, NETOPT_ADDRESS, 0, l2src,
                                    l2src_maxlen)) >= 0) {
        l2src_len = (uint16_t)res;
    }
    else {
        DEBUG("ndp internal: no link-layer address found.\n");
        l2src_len = 0;
    }

    return l2src_len;
}

static gnrc_pktsnip_t *_build_headers(kernel_pid_t iface, gnrc_pktsnip_t *payload,
                                      ipv6_addr_t *dst, ipv6_addr_t *src)
{
    gnrc_pktsnip_t *l2hdr;
    gnrc_pktsnip_t *iphdr = gnrc_ipv6_hdr_build(payload, (uint8_t *)src, sizeof(ipv6_addr_t),
                                                (uint8_t *)dst, sizeof(ipv6_addr_t));
    if (iphdr == NULL) {
        DEBUG("ndp internal: error allocating IPv6 header.\n");
        return NULL;
    }
    ((ipv6_hdr_t *)iphdr->data)->hl = 255;
    /* add netif header for send interface specification */
    l2hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
    if (l2hdr == NULL) {
        DEBUG("ndp internal: error allocating netif header.\n");
        gnrc_pktbuf_remove_snip(iphdr, iphdr);
        return NULL;
    }
    ((gnrc_netif_hdr_t *)l2hdr->data)->if_pid = iface;
    LL_PREPEND(iphdr, l2hdr);
    return l2hdr;
}

/** @} */
