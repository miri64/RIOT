/*
 * Copyright (C) 2016 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    emb6_conn_udp   udp_conn wrapper for emb6
 * @ingroup     emb6
 * @brief       UDP conn for emb6
 * @{
 *
 * @file
 * @brief   UDP conn definitions
 *
 * @author  Martine Lenders <mlenders@inf.fu-berlin.de>
 */
#ifndef EMB6_CONN_UDP_H_
#define EMB6_CONN_UDP_H_

#include <stdint.h>

#include "kernel_types.h"
#include "mutex.h"
#include "net/ipv6/addr.h"

#include "uip.h"
#include "udp-socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct conn_udp {
    struct udp_socket sock;
    mutex_t mutex;
    kernel_pid_t waiting_thread;
    struct {
        uint16_t src_port;
        const ipv6_addr_t *src;
        const void *data;
        size_t datalen;
    } recv_info;
};

#ifdef __cplusplus
}
#endif

#endif /* EMB6_CONN_UDP_H_ */
/** @} */
