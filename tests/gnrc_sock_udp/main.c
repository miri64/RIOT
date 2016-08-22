/*
 * Copyright (C) 2015 Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Test for raw IPv6 connections
 *
 * @author      Martine Lenders <mlenders@inf.fu-berlin.de>
 *
 * This test application tests the gnrc_conn_ip module. If you select protocol 58 you can also
 * test if gnrc is able to deal with multiple subscribers to ICMPv6 (gnrc_icmpv6 and this
 * application).
 *
 * @}
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "net/af.h"
#include "net/conn/udp.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/udp.h"
#include "sched.h"

#define _MSG_QUEUE_SIZE     (4)
#define _TEST_BUFFER_SIZE   (128)
#define _TEST_PORT_LOCAL    (0x2c94)
#define _TEST_PORT_REMOTE   (0xa615)
#define _TEST_NETIF         (31)
#define _TEST_TIMEOUT       (5000)
#define _TEST_ADDR_LOCAL    { { 0x7f, 0xc4, 0x11, 0x5a, 0xe6, 0x91, 0x8d, 0x5d, \
                                0x8c, 0xd1, 0x47, 0x07, 0xb7, 0x6f, 0x9b, 0x48 } }
#define _TEST_ADDR_REMOTE   { { 0xe8, 0xb3, 0xb2, 0xe6, 0x70, 0xd4, 0x55, 0xba, \
                                0x93, 0xcf, 0x11, 0xe1, 0x72, 0x44, 0xc5, 0x9d } }
#define _TEST_ADDR_WRONG    { { 0x2a, 0xce, 0x5d, 0x4e, 0xc8, 0xbf, 0x86, 0xf7, \
                                0x85, 0x49, 0xb4, 0x19, 0xf2, 0x28, 0xde, 0x9b } }
#define CALL(fn)            puts("Calling " #fn); fn; tear_down()

static msg_t _msg_queue[_MSG_QUEUE_SIZE];
static gnrc_netreg_entry_t _udp_handler;
static conn_udp_t _conn;
static uint8_t _test_buffer[_TEST_BUFFER_SIZE];

static bool _inject_packet(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                           uint16_t src_port, uint16_t dst_port,
                           void *data, size_t data_len, uint16_t netif);
static bool _check_stack(void);
static bool _check_packet(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                          uint16_t src_port, uint16_t dst_port,
                          void *data, size_t data_len, uint16_t netif,
                          bool random_src_port);

static void tear_down(void)
{
    const int pre_close_num = gnrc_netreg_num(GNRC_NETTYPE_UDP,
                                              _TEST_PORT_LOCAL);

    conn_udp_close(&_conn);
    const int post_close_num = gnrc_netreg_num(GNRC_NETTYPE_UDP,
                                               _TEST_PORT_LOCAL);
    assert(pre_close_num >= post_close_num);
    assert(0 == post_close_num);
    memset(&_conn, 0, sizeof(_conn));
}

static void test_conn_udp_create__EAFNOSUPPORT(void)
{
    /* port may not be NULL according to doc */
    const conn_ep_udp_t local = { .family = AF_UNSPEC,
                                  .port = _TEST_PORT_LOCAL };
    /* port may not be NULL according to doc */
    const conn_ep_udp_t remote = { .family = AF_UNSPEC,
                                   .port = _TEST_PORT_REMOTE };

    assert(-EAFNOSUPPORT == conn_udp_create(&_conn, &local, NULL));
    assert(-EAFNOSUPPORT == conn_udp_create(&_conn, NULL, &remote));
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_create__EINVAL(void)
{
    /* port may not be NULL according to doc */
    const conn_ep_udp_t local = { .family = AF_INET6, .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    /* port may not be NULL according to doc */
    const conn_ep_udp_t remote = { .family = AF_INET6,
                                   .netif = (_TEST_NETIF + 1),
                                   .port = _TEST_PORT_REMOTE };

    assert(-EINVAL == conn_udp_create(&_conn, &local, &remote));
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_create__no_endpoints(void)
{
    conn_ep_udp_t ep;

    assert(0 == conn_udp_create(&_conn, NULL, NULL));
    assert(-EADDRNOTAVAIL == conn_udp_get_local(&_conn, &ep));
    assert(-ENOTCONN == conn_udp_get_remote(&_conn, &ep));
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_create__only_local(void)
{
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    conn_ep_udp_t ep;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(0 == conn_udp_get_local(&_conn, &ep));
    assert(AF_INET6 == ep.family);
    assert(memcmp(&ipv6_addr_unspecified, &ep.addr.ipv6,
                  sizeof(ipv6_addr_t)) == 0);
    assert(CONN_EP_ANY_NETIF == ep.netif);
    assert(_TEST_PORT_LOCAL == ep.port);
    assert(-ENOTCONN == conn_udp_get_remote(&_conn, &ep));
    assert(1 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_create__only_remote(void)
{
    const conn_ep_udp_t remote = { .family = AF_INET6,
                                   .port = _TEST_PORT_LOCAL };
    conn_ep_udp_t ep;

    assert(0 == conn_udp_create(&_conn, NULL, &remote));
    assert(-EADDRNOTAVAIL == conn_udp_get_local(&_conn, &ep));
    assert(0 == conn_udp_get_remote(&_conn, &ep));
    assert(AF_INET6 == ep.family);
    assert(memcmp(&ipv6_addr_unspecified, &ep.addr.ipv6,
                  sizeof(ipv6_addr_t)) == 0);
    assert(CONN_EP_ANY_NETIF == ep.netif);
    assert(_TEST_PORT_LOCAL == ep.port);
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_create__full(void)
{
    const conn_ep_udp_t local = { .family = AF_INET6, .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };
    conn_ep_udp_t ep;

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(0 == conn_udp_get_local(&_conn, &ep));
    assert(AF_INET6 == ep.family);
    assert(memcmp(&ipv6_addr_unspecified, &ep.addr.ipv6,
                  sizeof(ipv6_addr_t)) == 0);
    assert(_TEST_NETIF == ep.netif);
    assert(_TEST_PORT_LOCAL == ep.port);
    assert(0 == conn_udp_get_remote(&_conn, &ep));
    assert(AF_INET6 == ep.family);
    assert(memcmp(&ipv6_addr_unspecified, &ep.addr.ipv6,
                  sizeof(ipv6_addr_t)) == 0);
    assert(CONN_EP_ANY_NETIF == ep.netif);
    assert(_TEST_PORT_REMOTE == ep.port);
    assert(1 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
}

static void test_conn_udp_recvfrom__EADDRNOTAVAIL(void)
{
    assert(0 == conn_udp_create(&_conn, NULL, NULL));

    assert(-EADDRNOTAVAIL == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer), 0, NULL));
}

static void test_conn_udp_recvfrom__ENOBUFS(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"), _TEST_NETIF));
    assert(-ENOBUFS == conn_udp_recvfrom(&_conn, _test_buffer, 2, 0, NULL));
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__EPROTO(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_WRONG;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(-EPROTO == conn_udp_recvfrom(&_conn, _test_buffer,
                                        sizeof(_test_buffer), 0, NULL));
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__ETIMEDOUT(void)
{
    const conn_ep_udp_t local = { .family = AF_INET6, .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };

    assert(0 == conn_udp_create(&_conn, &local, NULL));

    puts(" * Calling conn_udp_recvfrom()");
    assert(-ETIMEDOUT == conn_udp_recvfrom(&_conn, _test_buffer,
                                           sizeof(_test_buffer), _TEST_TIMEOUT,
                                           NULL));
    puts(" * (timed out)");
}

static void test_conn_udp_recvfrom__connected(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer), 0, NULL));
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__connected_with_remote(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };
    conn_ep_udp_t result;

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer), 0,
                                               &result));
    assert(AF_INET6 == result.family);
    assert(memcmp(&result.addr, &src_addr, sizeof(result.addr)) == 0);
    assert(_TEST_PORT_REMOTE == result.port);
    assert(_TEST_NETIF == result.netif);
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__unconnected(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer), 0, NULL));
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__unconnected_with_remote(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    conn_ep_udp_t result;

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer), 0,
                                               &result));
    assert(AF_INET6 == result.family);
    assert(memcmp(&result.addr, &src_addr, sizeof(result.addr)) == 0);
    assert(_TEST_PORT_REMOTE == result.port);
    assert(_TEST_NETIF == result.netif);
    assert(_check_stack());
}

static void test_conn_udp_recvfrom__with_timeout(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    conn_ep_udp_t result;

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recvfrom(&_conn, _test_buffer,
                                               sizeof(_test_buffer),
                                               _TEST_TIMEOUT, &result));
    assert(AF_INET6 == result.family);
    assert(memcmp(&result.addr, &src_addr, sizeof(result.addr)) == 0);
    assert(_TEST_PORT_REMOTE == result.port);
    assert(_TEST_NETIF == result.netif);
    assert(_check_stack());
}

static void test_conn_udp_recv__EADDRNOTAVAIL(void)
{
    assert(0 == conn_udp_create(&_conn, NULL, NULL));

    assert(-EADDRNOTAVAIL == conn_udp_recv(&_conn, _test_buffer,
                                           sizeof(_test_buffer), 0));
}

static void test_conn_udp_recv__ENOBUFS(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(-ENOBUFS == conn_udp_recv(&_conn, _test_buffer, 2, 0));
    assert(_check_stack());
}

static void test_conn_udp_recv__EPROTO(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    src_addr.u8[15] += 4;
    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(-EPROTO == conn_udp_recv(&_conn, _test_buffer, sizeof(_test_buffer),
                                    0));
    assert(_check_stack());
}

static void test_conn_udp_recv__ETIMEDOUT(void)
{
    const conn_ep_udp_t local = { .family = AF_INET6, .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };

    assert(0 == conn_udp_create(&_conn, &local, NULL));

    puts(" * Calling conn_udp_recv()");
    assert(-ETIMEDOUT == conn_udp_recv(&_conn, _test_buffer,
                                       sizeof(_test_buffer), _TEST_TIMEOUT));
    puts(" * (timed out)");
    assert(_check_stack());
}

static void test_conn_udp_recv__connected(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recv(&_conn, _test_buffer,
                                           sizeof(_test_buffer), 0));
    assert(_check_stack());
}

static void test_conn_udp_recv__unconnected(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recv(&_conn, _test_buffer,
                                           sizeof(_test_buffer), 0));
    assert(_check_stack());
}

static void test_conn_udp_recv__with_timeout(void)
{
    ipv6_addr_t src_addr = _TEST_ADDR_REMOTE;
    ipv6_addr_t dst_addr = _TEST_ADDR_LOCAL;
    const conn_ep_udp_t local = { .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };

    dst_addr.u8[15] += 8;

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(_inject_packet(&src_addr, &dst_addr, _TEST_PORT_REMOTE,
                          _TEST_PORT_LOCAL, "ABCD", sizeof("ABCD"),
                          _TEST_NETIF));
    assert(sizeof("ABCD") == conn_udp_recv(&_conn, _test_buffer,
                                           sizeof(_test_buffer),
                                           _TEST_TIMEOUT));
    assert(_check_stack());
}

static void test_conn_udp_sendto__AFNOSUPPORT(void)
{
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET,
                                   .port = _TEST_PORT_REMOTE };

    assert(-EAFNOSUPPORT == conn_udp_sendto(NULL, "ABCD", sizeof("ABCD"),
                                            &remote));
    assert(_check_stack());
}

static void test_conn_udp_sendto__EINVAL_netif(void)
{
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .port = _TEST_PORT_REMOTE,
                                  .netif = _TEST_NETIF };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE,
                                   .netif = _TEST_NETIF + 1 };

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(-EINVAL == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"), &remote));
    assert(_check_stack());
}

static void test_conn_udp_sendto__EINVAL_port(void)
{
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6 };

    assert(-EINVAL == conn_udp_sendto(NULL, "ABCD", sizeof("ABCD"), &remote));
    assert(_check_stack());
}

static void test_conn_udp_sendto__ENOTCONN_no_conn(void)
{
    assert(-ENOTCONN == conn_udp_sendto(NULL, "ABCD", sizeof("ABCD"), NULL));
    assert(_check_stack());
}

static void test_conn_udp_sendto__ENOTCONN_conn_not_connected(void)
{
    assert(0 == conn_udp_create(&_conn, NULL, NULL));
    assert(-ENOTCONN == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"), NULL));
    assert(_check_stack());
}

static void test_conn_udp_sendto__connected_no_local_no_netif(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, &remote));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             NULL));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__connected_no_netif(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             NULL));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__connected_no_local(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .netif = _TEST_NETIF,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, &remote));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             NULL));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"), _TEST_NETIF,
                         true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__connected(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             NULL));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         _TEST_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__connected_other_remote(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t conn_remote = { .addr = { .ipv6 = _TEST_ADDR_WRONG },
                                        .family = AF_INET6,
                                        .port = _TEST_PORT_REMOTE + _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &conn_remote));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         _TEST_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__unconnected_no_local_no_netif(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, NULL));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__unconnected_no_netif(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__unconnected_no_local(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .netif = _TEST_NETIF,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, NULL));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"), _TEST_NETIF,
                         true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__unconnected(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, NULL));
    assert(sizeof("ABCD") == conn_udp_sendto(&_conn, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         _TEST_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__no_conn_no_netif(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(sizeof("ABCD") == conn_udp_sendto(NULL, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_sendto__no_conn(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .netif = _TEST_NETIF,
                                   .port = _TEST_PORT_REMOTE };

    assert(sizeof("ABCD") == conn_udp_sendto(NULL, "ABCD", sizeof("ABCD"),
                                             &remote));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         _TEST_NETIF, true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_send__ENOTCONN_conn_not_connected(void)
{
    assert(0 == conn_udp_create(&_conn, NULL, NULL));
    assert(-ENOTCONN == conn_udp_send(&_conn, "ABCD", sizeof("ABCD")));
    assert(_check_stack());
}

static void test_conn_udp_send__no_local_no_netif(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, &remote));
    assert(sizeof("ABCD") == conn_udp_send(&_conn, "ABCD", sizeof("ABCD")));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_send__no_netif(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(sizeof("ABCD") == conn_udp_send(&_conn, "ABCD", sizeof("ABCD")));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         CONN_EP_ANY_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_send__no_local(void)
{
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .netif = _TEST_NETIF,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, NULL, &remote));
    assert(sizeof("ABCD") == conn_udp_send(&_conn, "ABCD", sizeof("ABCD")));
    assert(_check_packet(&ipv6_addr_unspecified, &dst_addr, 0,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"), _TEST_NETIF,
                         true));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

static void test_conn_udp_send(void)
{
    const ipv6_addr_t src_addr = _TEST_ADDR_LOCAL;
    const ipv6_addr_t dst_addr = _TEST_ADDR_REMOTE;
    const conn_ep_udp_t local = { .addr = { .ipv6 = _TEST_ADDR_LOCAL },
                                  .family = AF_INET6,
                                  .netif = _TEST_NETIF,
                                  .port = _TEST_PORT_LOCAL };
    const conn_ep_udp_t remote = { .addr = { .ipv6 = _TEST_ADDR_REMOTE },
                                   .family = AF_INET6,
                                   .port = _TEST_PORT_REMOTE };

    assert(0 == conn_udp_create(&_conn, &local, &remote));
    assert(sizeof("ABCD") == conn_udp_send(&_conn, "ABCD", sizeof("ABCD")));
    assert(_check_packet(&src_addr, &dst_addr, _TEST_PORT_LOCAL,
                         _TEST_PORT_REMOTE, "ABCD", sizeof("ABCD"),
                         _TEST_NETIF, false));
    xtimer_usleep(1000);    /* let GNRC stack finish */
    assert(_check_stack());
}

int main(void)
{
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
    tear_down();
    msg_init_queue(_msg_queue, _MSG_QUEUE_SIZE);
    gnrc_netreg_entry_init_pid(&_udp_handler, GNRC_NETREG_DEMUX_CTX_ALL,
                               sched_active_pid);
    assert(0 == gnrc_netreg_num(GNRC_NETTYPE_UDP, _TEST_PORT_LOCAL));
    /* -EADDRINUSE currently does not apply for GNRC */
    CALL(test_conn_udp_create__EAFNOSUPPORT());
    CALL(test_conn_udp_create__EINVAL());
    CALL(test_conn_udp_create__no_endpoints());
    CALL(test_conn_udp_create__only_local());
    CALL(test_conn_udp_create__only_remote());
    CALL(test_conn_udp_create__full());
    /* gnrc_udp_close() is tested in tear_down() */
    /* gnrc_udp_get_local() is tested in conn_udp_create() tests */
    /* gnrc_udp_get_remote() is tested in conn_udp_create() tests */
    CALL(test_conn_udp_recvfrom__EADDRNOTAVAIL());
    CALL(test_conn_udp_recvfrom__ENOBUFS());
    CALL(test_conn_udp_recvfrom__EPROTO());
    CALL(test_conn_udp_recvfrom__ETIMEDOUT());
    CALL(test_conn_udp_recvfrom__connected());
    CALL(test_conn_udp_recvfrom__connected_with_remote());
    CALL(test_conn_udp_recvfrom__unconnected());
    CALL(test_conn_udp_recvfrom__unconnected_with_remote());
    CALL(test_conn_udp_recvfrom__with_timeout());
    CALL(test_conn_udp_recv__EADDRNOTAVAIL());
    CALL(test_conn_udp_recv__ENOBUFS());
    CALL(test_conn_udp_recv__EPROTO());
    CALL(test_conn_udp_recv__ETIMEDOUT());
    CALL(test_conn_udp_recv__connected());
    CALL(test_conn_udp_recv__unconnected());
    CALL(test_conn_udp_recv__with_timeout());
    gnrc_netreg_register(GNRC_NETTYPE_UDP, &_udp_handler);
    CALL(test_conn_udp_sendto__AFNOSUPPORT());
    CALL(test_conn_udp_sendto__EINVAL_netif());
    CALL(test_conn_udp_sendto__EINVAL_port());
    CALL(test_conn_udp_sendto__ENOTCONN_no_conn());
    CALL(test_conn_udp_sendto__ENOTCONN_conn_not_connected());
    CALL(test_conn_udp_sendto__connected_no_local_no_netif());
    CALL(test_conn_udp_sendto__connected_no_netif());
    CALL(test_conn_udp_sendto__connected_no_local());
    CALL(test_conn_udp_sendto__connected());
    CALL(test_conn_udp_sendto__connected_other_remote());
    CALL(test_conn_udp_sendto__unconnected_no_local_no_netif());
    CALL(test_conn_udp_sendto__unconnected_no_netif());
    CALL(test_conn_udp_sendto__unconnected_no_local());
    CALL(test_conn_udp_sendto__unconnected());
    CALL(test_conn_udp_sendto__no_conn_no_netif());
    CALL(test_conn_udp_sendto__no_conn());
    CALL(test_conn_udp_send__ENOTCONN_conn_not_connected());
    CALL(test_conn_udp_send__no_local_no_netif());
    CALL(test_conn_udp_send__no_netif());
    CALL(test_conn_udp_send__no_local());
    CALL(test_conn_udp_send());

    puts("ALL TESTS SUCCESSFUL");

    return 0;
}

static gnrc_pktsnip_t *_build_udp_packet(const ipv6_addr_t *src,
                                         const ipv6_addr_t *dst,
                                         uint16_t src_port, uint16_t dst_port,
                                         void *data, size_t data_len,
                                         uint16_t netif)
{
    gnrc_pktsnip_t *netif_hdr, *ipv6, *udp;
    udp_hdr_t *udp_hdr;
    ipv6_hdr_t *ipv6_hdr;
    uint16_t csum = 0;

    if ((netif > INT16_MAX) || ((sizeof(udp_hdr_t) + data_len) > UINT16_MAX)) {
        return NULL;
    }

    udp = gnrc_pktbuf_add(NULL, NULL, sizeof(udp_hdr_t) + data_len,
                          GNRC_NETTYPE_UNDEF);
    if (udp == NULL) {
        return NULL;
    }
    udp_hdr = udp->data;
    udp_hdr->src_port = byteorder_htons(src_port);
    udp_hdr->dst_port = byteorder_htons(dst_port);
    udp_hdr->length = byteorder_htons((uint16_t)udp->size);
    udp_hdr->checksum.u16 = 0;
    memcpy(udp_hdr + 1, data, data_len);
    csum = inet_csum(csum, (uint8_t *)udp->data, udp->size);
    ipv6 = gnrc_ipv6_hdr_build(NULL, src, dst);
    if (ipv6 == NULL) {
        return NULL;
    }
    ipv6_hdr = ipv6->data;
    ipv6_hdr->len = byteorder_htons((uint16_t)udp->size);
    ipv6_hdr->nh = PROTNUM_UDP;
    ipv6_hdr->hl = 64;
    csum = ipv6_hdr_inet_csum(csum, ipv6_hdr, PROTNUM_UDP, (uint16_t)udp->size);
    if (csum == 0xffff) {
        udp_hdr->checksum = byteorder_htons(csum);
    }
    else {
        udp_hdr->checksum = byteorder_htons(~csum);
    }
    LL_APPEND(udp, ipv6);
    netif_hdr = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
    if (netif_hdr == NULL) {
        return NULL;
    }
    ((gnrc_netif_hdr_t *)netif_hdr->data)->if_pid = (kernel_pid_t)netif;
    LL_APPEND(udp, netif_hdr);
    return udp;
}


static bool _inject_packet(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                           uint16_t src_port, uint16_t dst_port,
                           void *data, size_t data_len, uint16_t netif)
{
    gnrc_pktsnip_t *pkt = _build_udp_packet(src, dst, src_port, dst_port,
                                            data, data_len, netif);

    if (pkt == NULL) {
        return false;
    }
    return (gnrc_netapi_dispatch_receive(GNRC_NETTYPE_UDP,
                                         GNRC_NETREG_DEMUX_CTX_ALL, pkt) > 0);
}

static bool _check_stack(void)
{
    return (gnrc_pktbuf_is_sane() && gnrc_pktbuf_is_empty());
}

static inline bool _res(gnrc_pktsnip_t *pkt, bool res)
{
    gnrc_pktbuf_release(pkt);
    return res;
}

static bool _check_packet(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                          uint16_t src_port, uint16_t dst_port,
                          void *data, size_t data_len, uint16_t iface,
                          bool random_src_port)
{
    gnrc_pktsnip_t *pkt, *ipv6, *udp;
    ipv6_hdr_t *ipv6_hdr;
    udp_hdr_t *udp_hdr;
    msg_t msg;

    msg_receive(&msg);
    if (msg.type != GNRC_NETAPI_MSG_TYPE_SND) {
        return false;
    }
    pkt = msg.content.ptr;
    if (iface != CONN_EP_ANY_NETIF) {
        gnrc_netif_hdr_t *netif_hdr;

        if (pkt->type != GNRC_NETTYPE_NETIF) {
            return _res(pkt, false);
        }
        netif_hdr = pkt->data;
        if (netif_hdr->if_pid != iface) {
            return _res(pkt, false);
        }
        ipv6 = pkt->next;
    }
    else {
        ipv6 = pkt;
    }
    if (ipv6->type != GNRC_NETTYPE_IPV6) {
        return _res(pkt, false);
    }
    ipv6_hdr = ipv6->data;
    udp = gnrc_pktsnip_search_type(ipv6, GNRC_NETTYPE_UDP);
    if (udp == NULL) {
        return _res(pkt, false);
    }
    udp_hdr = udp->data;
    return _res(pkt, (memcmp(src, &ipv6_hdr->src, sizeof(ipv6_addr_t)) == 0) &&
                     (memcmp(dst, &ipv6_hdr->dst, sizeof(ipv6_addr_t)) == 0) &&
                     (ipv6_hdr->nh == PROTNUM_UDP) &&
                     (random_src_port || (src_port == byteorder_ntohs(udp_hdr->src_port))) &&
                     (dst_port == byteorder_ntohs(udp_hdr->dst_port)) &&
                     (udp->next != NULL) &&
                     (data_len == udp->next->size) &&
                     (memcmp(data, udp->next->data, data_len) == 0));
}
