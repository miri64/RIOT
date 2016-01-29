/*
 * Copyright (C) 2016 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    emb6_netdev2    netdev2 wrapper for emb6
 * @ingroup emb6
 * @brief
 * @{
 *
 * @file
 * @brief
 *
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 */
#ifndef EMB6_NETDEV2_H_
#define EMB6_NETDEV2_H_

#include "net/netdev2.h"

#include "emb6.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const s_nsIf_t emb6_netdev2_driver;

int emb6_netdev2_setup(netdev2_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMB6_NETDEV2_H_ */
/** @} */
