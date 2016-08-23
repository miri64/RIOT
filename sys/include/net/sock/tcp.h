/*
 * Copyright (C) 2016 Alexander Aring <aar@pengutronix.de>
 *                    Freie Universität Berlin
 *                    HAW Hamburg
 *                    Kaspar Schleiser <kaspar@schleiser.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    net_sock_tcp    TCP sock API
 * @ingroup     net_sock
 * @brief       Sock submodule for TCP
 * @{
 *
 * @file
 * @brief   TCP sock definitions
 *
 * @author  Alexander Aring <aar@pengutronix.de>
 * @author  Simon Brummer <simon.brummer@haw-hamburg.de>
 * @author  Cenk Gündoğan <mail@cgundogan.de>
 * @author  Peter Kietzmann <peter.kietzmann@haw-hamburg.de>
 * @author  Martine Lenders <m.lenders@fu-berlin.de>
 * @author  Kaspar Schleiser <kaspar@schleiser.de>
 */
#ifndef NET_SOCK_TCP_H_
#define NET_SOCK_TCP_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include "net/sock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _sock_tl_ep sock_tcp_ep_t;   /**< An end point for a TCP sock object */

/**
 * @brief   Implementation-specific type of a TCP sock object
 *
 * `struct sock_tcp` needs to be defined by stack-specific `sock_types.h`.
 */
typedef struct sock_tcp sock_tcp_t;

/**
 * @brief Initializes a socket object.
 *
 * @pre 'sock != NULL'
 *
 * @param[out]         The resulting sock object.
 *
 * @return   0 on success
 * @return   -ENOMEM, if the system was not able to allocate sufficient memory.
 */
int sock_tcp_init(sock_tcp_t *sock);

/**
 * @brief   Establishes a new TCP sock connection
 *
 * @pre `sock != NULL`
 *
 * @param[out] sock     The resulting sock object.
 * @param[in] remote    Remote end point for the sock object.
                        Must be not NULL for an active connection.
 * @param[in] local     Local end point for the sock object.
                        Must be not NULL for an passive connection.
 * @param[in] flags     Flags for the sock object. See also @ref net_sock_flags.
 *                      May be 0.
 *
 * @return  0 on success.
 * @return  -EADDRINUSE, if `(flags & SOCK_FLAGS_REUSE_EP) == 0` and
 *          the port number in @p local is already used elsewhere
 * @return  -EAFNOSUPPORT, if sock_tcp_ep_t::family of @p remote or @p local is not
 *          supported.
 * @return  -ECONNREFUSED, if no-one is listening on the @p remote end point.
 * @return  -EINVAL, if sock_tcp_ep_t::netif of @p remote is not a valid
 *          interface.
 * @return  -ENETUNREACH, if network defined by @p remote is not reachable.
 * @return  -EPERM, if connections to @p remote are not permitted on the system
 *          (e.g. by firewall rules).
 * @return  -ETIMEDOUT, if the connection attempt to @p remote timed out.
 */
int sock_tcp_connect(sock_tcp_t *sock, const sock_tcp_ep_t *remote,
                     const sock_tcp_ep_t *local, uint16_t flags);

/**
 * @brief   Disconnects a TCP connection
 *
 * @pre `(sock != NULL)`
 *
 * @param[in] sock  A TCP sock object.
 *
 * @return  0 on success.
 * @return  -ETIMEDOUT, if the disconnect attempt to @p remote timed out.
 */
int sock_tcp_disconnect(sock_tcp_t *sock);

/**
 * @brief   Releases resources used by sock.
 *
 * @pre '(sock != NULL)'
 *
 * @param[in] sock   A TCP sock object.
 */
void sock_tcp_release(sock_tcp_t *sock);

/**
 * @brief   Gets the local end point of a TCP sock object
 *
 * @pre `(sock != NULL) && (ep != NULL)`
 *
 * @param[in] sock  A TCP sock object.
 * @param[out] ep   The local end point.
 *
 * @return  0 on success.
 * @return  -EADDRNOTAVAIL, when @p sock has no local end point.
 */
int sock_tcp_get_local(sock_tcp_t *sock, sock_tcp_ep_t *ep);

/**
 * @brief   Gets the remote end point of a TCP sock object
 *
 * @pre `(sock != NULL) && (ep != NULL)`
 *
 * @param[in] sock  A TCP sock object.
 * @param[out] ep   The remote end point.
 *
 * @return  0 on success.
 * @return  -ENOTCONN, when @p sock is not connected to a remote end point.
 */
int sock_tcp_get_remote(sock_tcp_t *sock, sock_tcp_ep_t *ep);

/**
 * @brief   Reads data from an established TCP stream
 *
 * @pre `(sock != NULL) && (data != NULL) && (max_len > 0)`
 *
 * @param[in] sock      A TCP sock object.
 * @param[out] data     Pointer where the read data should be stored.
 * @param[in] max_len   Maximum space available at @p data.
 *                      If read data exceeds @p max_len the data is
 *                      truncated and the remaining data can be retrieved
 *                      later on.
 * @param[in] timeout   Timeout for receive in microseconds.
 *                      Must be less than the TCP User Space Timeout.
 *                      May be 0 for no timeout.
 *
 * @note    Function may block.
 *
 * @return  The number of bytes read on success.
 * @return  0, if no read data is available, but everything is in order.
 * @return  -ECONNREFUSED, if remote end point of @p sock refused to allow the
 *          connection.
 * @retunr  -EINVAL, when the specified timeout is larger that the TCP User
            Space Timeout.
 * @return  -ENOTCONN, when @p sock is not connected to a remote end point.
 * @return  -ETIMEDOUT, if @p timeout expired.
 */
ssize_t sock_tcp_read(sock_tcp_t *sock, void *data, size_t max_len,
                      uint32_t timeout);

/**
 * @brief   Writes data to an established TCP stream
 *
 * @pre `(sock != NULL) && (data != NULL) && (len > 0)`
 *
 * @param[in] sock  A TCP sock object.
 * @param[in] data  Pointer to the data to be written to the stream.
 * @param[in] len   Maximum space available at @p data.
 *
 * @note    Function may block.
 *
 * @return  The number of bytes written on success.
 * @return  -ECONNRESET, if connection was reset by remote end point.
 * @return  -ENOMEM, if no memory was available to written @p data.
 * @return  -ENOTCONN, if @p sock is not connected to a remote end point.
 */
ssize_t sock_tcp_write(sock_tcp_t *sock, const void *data, size_t len);

#include "sock_types.h"

#ifdef __cplusplus
}
#endif

#endif /* NET_SOCK_TCP_H_ */
/** @} */
