/*
 * Copyright (C) 2017 HAW Hamburg
 * Copyright (C) 2020 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_gnrc_rpl_opt Option for carrying RPL information in data-plane datagrams
 * @ingroup     net_gnrc_rpl
 * @brief       Implementation of IPv6 option for carrying RPL information
 *              in data-plane datagrams
 * @see [RFC 6553](https://tools.ietf.org/html/rfc6553)
 * @see @ref IPV6_EXT_OPT_RPL
 * @{
 *
 * @file
 * @brief       Definitions for Carrying RPL Information in Data-Plane Datagrams
 *
 * @author  Martin Landsmann <martin.landsmann@haw-hamburg.de>
 * @author  Martine Sophie Lenders <m.lenders@fu-berlin.de>
 */
#ifndef NET_GNRC_RPL_OPT_H
#define NET_GNRC_RPL_OPT_H

#include "net/ipv6/hdr.h"
#include "net/ipv6/addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Return types used in @ref gnrc_rpl_opt_process()
 */
enum {
    GNRC_RPL_OPT_NOT_FOR_ME = -4,
    GNRC_RPL_OPT_FLAG_R_SET = -3,
    GNRC_RPL_OPT_FLAG_F_SET = -2,
    GNRC_RPL_OPT_INCONSISTENCY = -1,
    GNRC_RPL_OPT_SUCCESS = 0,
};

/**
 * @name Option flags
 * @anchor net_gnrc_rpl_opt_flags
 * @see [RFC6553, section 3](https://tools.ietf.org/html/rfc6553#section-3)
 * @{
 */
#define GNRC_RPL_HOP_OPT_FLAG_O  (1 << 0)   /**< Down */
#define GNRC_RPL_HOP_OPT_FLAG_R  (1 << 1)   /**< Rank error */
#define GNRC_RPL_HOP_OPT_FLAG_F  (1 << 2)   /**< Forwarding error */
/** @} */

/**
 * @brief parse the given hop-by-hop option, check for inconsistencies,
 *        adjust the option for further processing and return the result.
 *
 * @param[in,out] hop pointer to the hop-by-hop header option
 *
 * @returns GNRC_RPL_OPT_SUCCESS on success
 *          GNRC_RPL_OPT_INCONSISTENCY if the F flag was already set
 *          GNRC_RPL_OPT_FLAG_R_SET if we set the R flag
 *          GNRC_RPL_OPT_FLAG_F_SET if we set the F flag
 *          GNRC_RPL_OPT_NOT_FOR_ME if the content is not for us
 */
int gnrc_rpl_opt_process(uint8_t *opt_data, uint8_t opt_len);

#ifdef __cplusplus
}
#endif

#endif /* NET_GNRC_RPL_OPT_H */
/** @} */
