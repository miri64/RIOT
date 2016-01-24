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

#include <assert.h>
#include <errno.h>

#include "net/eui64.h"
#include "net/ieee802154.h"
#include "net/netdev2.h"

#include "net/netdev2_ieee802154.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

static int _get_iid(netdev2_t *netdev, eui64_t *value, size_t max_len)
{
    uint8_t addr[IEEE802154_LONG_ADDRESS_LEN];
    uint16_t addr_len;

    if (max_len < sizeof(eui64_t)) {
        return -EOVERFLOW;
    }

    netdev->driver->get(netdev, NETOPT_SRC_LEN, &addr_len, sizeof(addr_len));

    switch (addr_len) {
        case 8:
            netdev->driver->get(netdev, NETOPT_ADDRESS_LONG, addr, addr_len);
            break;
        case 2:
            netdev->driver->get(netdev, NETOPT_ADDRESS, addr, addr_len);
            break;
        default:
            return -EAFNOSUPPORT;
    }
    ieee802154_get_iid(value, addr, addr_len);

    return sizeof(eui64_t);
}

int netdev2_ieee802154_get(netdev2_t *dev, netopt_t opt, void *value, size_t max_len)
{
    int res = 0;

    switch (opt) {
        case NETOPT_DEVICE_TYPE:
        {
            uint16_t *tgt = (uint16_t *)value;
            *tgt = NETDEV2_TYPE_IEEE802154;
            res = 2;
            break;
        }
        case NETOPT_IPV6_IID:
        {
            return _get_iid(dev, value, max_len);
        }
        default:
        {
            res = -ENOTSUP;
            break;
        }
    }

    return res;
}

int netdev2_ieee802154_set(netdev2_t *dev, netopt_t opt, void *value, size_t value_len)
{
    (void)dev;
    (void)value;
    (void)value_len;

    int res = 0;

    switch (opt) {
        default:
            return -ENOTSUP;
    }

    return res;
}

/** @} */
