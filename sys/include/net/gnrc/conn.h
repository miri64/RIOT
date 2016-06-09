/*
 * Copyright (C) 2016 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_gnrc_conn   GNRC-specific implementation of the connectivity
 *                              API
 * @ingroup     net_gnrc
 * @brief       Provides an implementation of the @ref net_conn by the
 *              @ref net_gnrc
 *
 * @{
 *
 * @file
 * @brief   GNRC-specific types and function definitions
 *
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 */
#ifndef GNRC_CONN_H_
#define GNRC_CONN_H_

#include <stdbool.h>
#include <stdint.h>
#include "mbox.h"
#include "net/conn/ep.h"
#include "net/gnrc.h"
#include "net/gnrc/netreg.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONN_HAS_CALLBACKS
#undef CONN_HAS_CALLBACKS   /* not implemented yet */
#endif

#ifndef CONN_MBOX_SIZE
#define CONN_MBOX_SIZE      (8)         /**< Size for gnrc_conn_reg_t::mbox_queue */
#endif

/**
 * @brief   Stack connectivity/registry entry.
 * @internal
 */
typedef struct {
    gnrc_netreg_entry_t entry;          /**< @ref net_gnrc_netreg entry for mbox */
    mbox_t mbox;                        /**< @ref core_mbox target for the connectivity */
    msg_t mbox_queue[CONN_MBOX_SIZE];   /**< queue for gnrc_conn_reg_t::mbox */
} gnrc_conn_reg_t;

/**
 * @brief   Raw connectivity type
 * @internal
 * @extends gnrc_conn_t
 */
struct conn_ip {
    gnrc_conn_reg_t reg;                /**< stack connectivity */
    conn_ep_ip_t local;                 /**< local end-point */
    conn_ep_ip_t remote;                /**< remote end-point */
};

/**
 * @brief   UDP connectivity type
 * @internal
 * @extends gnrc_conn_t
 */
struct conn_udp {
    gnrc_conn_reg_t reg;                /**< stack connectivity */
    conn_ep_udp_t local;                /**< local end-point */
    conn_ep_udp_t remote;               /**< remote end-point */
};

/**
 * @brief   Internal helper functions for GNRC
 * @internal
 * @{
 */
void gnrc_conn_create(gnrc_conn_reg_t *reg, gnrc_nettype_t type, uint32_t demux_ctx);
int gnrc_conn_recv(gnrc_conn_reg_t *reg, gnrc_pktsnip_t **pkt, uint32_t timeout,
                   conn_ep_ip_t *remote);
int gnrc_conn_send(gnrc_pktsnip_t *payload, conn_ep_ip_t *local,
                   const conn_ep_ip_t *remote, uint8_t nh);
/**
 * @}
 */


#ifdef __cplusplus
}
#endif

#endif /* GNRC_CONN_H_ */
/** @} */
