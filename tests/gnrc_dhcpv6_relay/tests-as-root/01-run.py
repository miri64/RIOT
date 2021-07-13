#!/usr/bin/env python3

# Copyright (C) 2021 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.

import os
import re
import subprocess
import sys
import time

from scapy.all import Ether, IPv6, ICMPv6DestUnreach, UDP, DHCP6, \
    DHCP6_Solicit, DHCP6_Advertise, DHCP6_Request, DHCP6_Confirm, \
    DHCP6_Renew, DHCP6_Rebind, DHCP6_Reply, DHCP6_Release, DHCP6_Decline, \
    DHCP6_Reconf, DHCP6_InfoRequest, DHCP6_RelayForward, DHCP6_RelayReply, \
    DHCP6OptIfaceId, DHCP6OptRelayMsg, DHCP6OptClientId, DHCP6OptUnknown, \
    sendp, AsyncSniffer
from testrunner import run


CLIENT_PORT = 545
SERVER_PORT = 547
IFACE_ID_LEN = 2        # iface ID is a uint16_t, i.e. the netif of the sock


def check_and_search_output(cmd, pattern, res_group, *args, **kwargs):
    output = subprocess.check_output(cmd, *args, **kwargs).decode()
    for line in output.splitlines():
        match = re.search(pattern, line)
        if match is not None:
            return match.group(res_group)
    return None


def get_bridge(tap):
    res = check_and_search_output(
            ["bridge", "link"],
            r"{}.+master\s+(?P<master>[^\s]+)".format(tap),
            "master"
        )
    return tap if res is None else res


def get_host_lladdr(tap):
    res = check_and_search_output(
            ["ip", "addr", "show", "dev", tap, "scope", "link"],
            r"inet6\s+(?P<lladdr>[0-9A-Fa-f:]+)/\d+",
            "lladdr"
        )
    if res is None:
        raise AssertionError(
            "Can't find host link-local address on interface {}".format(tap)
        )
    return res


def headers():
    return Ether(dst="33:33:00:01:00:02") / IPv6(dst="ff02::1:2") / \
           UDP(dport=SERVER_PORT)


def send_and_exp_pkts(tap, pkt, exp_type):
    sniffer = AsyncSniffer(iface=tap)
    sniffer.start()
    time.sleep(.5)
    sendp(headers() / pkt, iface=tap, verbose=0)
    return [pkt for pkt in sniffer.stop() if exp_type in pkt and
            ICMPv6DestUnreach not in pkt]


def contains_dhcp(pkt):
    # just using DHCP6 in pkt does not work
    def expand(pkt):
        yield pkt
        while pkt.payload:
            pkt = pkt.payload
            yield pkt
    return any(isinstance(layer,
                          (DHCP6, DHCP6_RelayForward, DHCP6_RelayReply))
               for layer in expand(pkt))


def send_and_exp_any_dhcp6(tap, send_pkt):
    sniffer = AsyncSniffer(iface=tap)
    sniffer.start()
    time.sleep(.5)
    sendp(headers() / send_pkt, iface=tap, verbose=0)
    return [pkt for pkt in sniffer.stop() if contains_dhcp(pkt) and
            # filter out sent packet
            not isinstance(pkt[UDP].payload, type(send_pkt)) and
            ICMPv6DestUnreach not in pkt]


def check_if_crashed(child):
    child.sendline("help")
    child.expect(r"Command\s+Description")


def get_iface_id(tap):
    pkt = DHCP6_Solicit()
    result = send_and_exp_pkts(tap, pkt, DHCP6_RelayForward)
    assert len(result) > 0
    return result[0][DHCP6OptIfaceId].ifaceid


def test_dhcpv6_client_msgs(child, tap):
    test_trid = 0xc0ffee
    for msg_type in [DHCP6_Solicit, DHCP6_Request, DHCP6_Confirm, DHCP6_Renew,
                     DHCP6_Rebind, DHCP6_Release, DHCP6_Decline,
                     DHCP6_InfoRequest]:
        pkt = msg_type(trid=test_trid)
        result = send_and_exp_pkts(tap, pkt, DHCP6_RelayForward)
        assert len(result) > 0
        pkt = result[0]
        assert pkt[UDP].dport == SERVER_PORT
        # either linkaddr is set or there is an Interface-ID option in
        # the message
        assert pkt[DHCP6_RelayForward].hopcount == 0
        assert pkt[DHCP6_RelayForward].linkaddr != "::" or \
               DHCP6OptIfaceId in pkt
        if DHCP6OptIfaceId in pkt:
            # assure optlen for later tests
            assert pkt[DHCP6OptIfaceId].optlen == IFACE_ID_LEN
        assert DHCP6OptRelayMsg in pkt
        assert msg_type in pkt[DHCP6OptRelayMsg]
        assert pkt[DHCP6OptRelayMsg][msg_type].trid == test_trid
        check_if_crashed(child)


def test_dhcpv6_server_msgs(child, tap):
    test_trid = 0xc0ffee
    for msg_type in [DHCP6_Advertise, DHCP6_Reply, DHCP6_Reconf]:
        pkt = msg_type(trid=test_trid)
        result = send_and_exp_any_dhcp6(tap, pkt)
        assert len(result) == 0
        check_if_crashed(child)


def test_dhcpv6_client_msg_too_long(child, tap):
    buflen = int(os.environ["CONFIG_DHCPV6_RELAY_BUFLEN"])
    pkt = DHCP6_Solicit() / \
        DHCP6OptUnknown(optcode=1, optlen=buflen) / (b"x" * buflen)
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_client_msg_too_small(child, tap):
    for i in range(len(DHCP6())):
        pkt = (b"\x01" * i)
        result = send_and_exp_any_dhcp6(tap, pkt)
        assert len(result) == 0
        check_if_crashed(child)


def test_dhcpv6_client_msg_too_long_for_fwd(child, tap):
    buflen = int(os.environ["CONFIG_DHCPV6_RELAY_BUFLEN"])
    buflen -= len(DHCP6_Solicit())          # remove SOLICIT header
    buflen -= len(DHCP6OptUnknown())        # remove option header of SOLICIT
    buflen -= len(DHCP6_RelayForward())     # remove RELAY-FORWARD header
    # remove Interface-ID option of RELAY-FORWARD
    buflen -= len(DHCP6OptIfaceId(optlen=IFACE_ID_LEN, ifaceid="ab"))
    # remove Relay-Message option header of RELAY-FORWARD
    buflen -= len(DHCP6OptUnknown(optlen=0))

    pkt = DHCP6_Solicit() / \
        DHCP6OptUnknown(optcode=1, optlen=buflen) / (b"x" * buflen)
    result = send_and_exp_pkts(tap, pkt, DHCP6_RelayForward)
    check_if_crashed(child)
    # should just fit
    assert len(result) == 1
    check_if_crashed(child)
    buflen += 1
    pkt = DHCP6_Solicit() / \
        DHCP6OptUnknown(optcode=1, optlen=buflen) / (b"x" * buflen)
    result = send_and_exp_any_dhcp6(tap, pkt)
    # SOLICIT should be too long
    assert len(result) == 0
    check_if_crashed(child)


def _test_dhcpv6_relay_forward(child, tap, pkt, test_trid, hopcount):
    result = send_and_exp_pkts(tap, pkt, DHCP6_RelayForward)
    assert len(result) > 1
    pkt = result[-1]    # last one is the new one, first should be the sent on
    # either linkaddr is set or there is an Interface-ID option in
    # the message
    assert pkt[DHCP6_RelayForward].linkaddr != "::" or \
           DHCP6OptIfaceId in pkt
    assert pkt[DHCP6_RelayForward].hopcount == (hopcount + 1)
    if DHCP6OptIfaceId in pkt:
        # assure optlen for later tests
        assert pkt[DHCP6OptIfaceId].optlen == IFACE_ID_LEN
    assert DHCP6OptRelayMsg in pkt
    assert DHCP6_RelayForward in pkt[DHCP6OptRelayMsg].message

    rpkt = pkt[DHCP6OptRelayMsg].message[DHCP6_RelayForward]
    assert rpkt.peeraddr == "fe80::f00:1337"
    assert rpkt[DHCP6OptIfaceId].ifaceid == b"ab"
    assert DHCP6_Rebind in rpkt[DHCP6OptRelayMsg].message
    assert rpkt[DHCP6OptRelayMsg].message[DHCP6_Rebind].trid == test_trid
    check_if_crashed(child)


def test_dhcpv6_relay_forward(child, tap):
    test_trid = 0xc0ffee
    hopcount = int(os.environ["CONFIG_DHCPV6_RELAY_HOP_LIMIT"]) - 1
    assert hopcount > 0
    pkt = DHCP6_RelayForward(peeraddr="fe80::f00:1337", hopcount=hopcount) / \
        DHCP6OptIfaceId(ifaceid=b"ab") / \
        DHCP6OptRelayMsg(message=DHCP6_Rebind(trid=test_trid))
    _test_dhcpv6_relay_forward(child, tap, pkt, test_trid, hopcount)


def test_dhcpv6_relay_forward_options_reversed(child, tap):
    test_trid = 0xc0ffee
    hopcount = int(os.environ["CONFIG_DHCPV6_RELAY_HOP_LIMIT"]) - 2
    assert hopcount > 0
    pkt = DHCP6_RelayForward(peeraddr="fe80::f00:1337", hopcount=hopcount) / \
        DHCP6OptRelayMsg(message=DHCP6_Rebind(trid=test_trid)) / \
        DHCP6OptIfaceId(ifaceid=b"ab")
    _test_dhcpv6_relay_forward(child, tap, pkt, test_trid, hopcount)


def test_dhcpv6_relay_forward_hop_limit_exceeded(child, tap):
    test_trid = 0xc0ffee
    hop_limit = int(os.environ["CONFIG_DHCPV6_RELAY_HOP_LIMIT"])
    pkt = DHCP6_RelayForward(hopcount=hop_limit + 1,
                             peeraddr="fe80::f00:1337") / \
        DHCP6OptIfaceId(ifaceid=b"ab") / \
        DHCP6OptRelayMsg(message=DHCP6_Rebind(trid=test_trid))
    result = send_and_exp_pkts(tap, pkt, DHCP6_RelayForward)
    assert len(result) == 1     # only the sent RelayForward
    check_if_crashed(child)


def test_dhcpv6_relay_forward_too_small(child, tap):
    for i in range(len(DHCP6()), len(DHCP6_RelayForward())):
        pkt = (chr(12) + ("\0" * (i - 1))).encode()
        result = send_and_exp_any_dhcp6(tap, pkt)
        assert len(result) == 0     # only the sent RelayForward
        check_if_crashed(child)


def test_dhcpv6_simple_relay_reply(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_pkts(tap, pkt, DHCP6_Reply)
    assert len(result) > 0
    pkt = result[-1]    # relay reply might also be picked up so take last
    assert DHCP6_RelayReply not in pkt
    assert DHCP6OptIfaceId not in pkt
    assert DHCP6OptRelayMsg not in pkt
    assert DHCP6_Reply in pkt
    assert pkt[DHCP6_Reply].trid == test_trid
    check_if_crashed(child)


def test_dhcpv6_simple_relay_reply_options_reversed(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptRelayMsg(message=DHCP6_Advertise(trid=test_trid)) / \
        DHCP6OptIfaceId(ifaceid=ifaceid)
    result = send_and_exp_pkts(tap, pkt, DHCP6_Advertise)
    assert len(result) > 0
    pkt = result[-1]    # relay reply might also be picked up so take last
    assert DHCP6_RelayReply not in pkt
    assert DHCP6OptIfaceId not in pkt
    assert DHCP6OptRelayMsg not in pkt
    assert DHCP6_Advertise in pkt
    assert pkt[DHCP6_Advertise].trid == test_trid
    check_if_crashed(child)


def test_dhcpv6_simple_relay_reply_foreign_option(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptClientId() / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_pkts(tap, pkt, DHCP6_Reply)
    assert len(result) > 0
    pkt = result[-1]    # relay reply might also be picked up so take last
    assert DHCP6_RelayReply not in pkt
    assert DHCP6OptIfaceId not in pkt
    assert DHCP6OptRelayMsg not in pkt
    assert DHCP6_Reply in pkt
    assert pkt[DHCP6_Reply].trid == test_trid
    check_if_crashed(child)


def test_dhcpv6_simple_relay_reply_foreign_option_w_bogus_optlen(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid)) / \
        DHCP6OptClientId(optlen=32)
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_nested_relay_reply(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg(
            message=DHCP6_RelayReply(peeraddr="fe80::f00:affe") /
            DHCP6OptIfaceId(ifaceid="abcd") /
            DHCP6OptRelayMsg(
                message=DHCP6_Reconf(trid=test_trid)
            )
        )
    result = send_and_exp_pkts(tap, pkt, DHCP6_Reconf)
    assert len(result) > 0
    pkt = result[-1]    # relay reply might also be picked up so take last
    assert DHCP6_RelayReply in pkt
    assert DHCP6_RelayReply not in pkt[DHCP6_RelayReply].message
    assert pkt[DHCP6_RelayReply].peeraddr == "fe80::f00:affe"
    assert DHCP6_Reconf in pkt[DHCP6_RelayReply].message
    assert pkt[DHCP6_RelayReply].message[DHCP6_Reconf].trid == test_trid
    check_if_crashed(child)


def test_dhcpv6_relay_reply_too_small(child, tap):
    for i in range(len(DHCP6()), len(DHCP6_RelayReply())):
        pkt = (chr(13) + ("\0" * (i - 1))).encode()
        result = send_and_exp_any_dhcp6(tap, pkt)
        assert len(result) == 0     # only the sent RelayForward
        check_if_crashed(child)


def test_dhcpv6_relay_reply_unexpeted_ifaceid_len(child, tap):
    lladdr = get_host_lladdr(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid="hello!") / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_relay_reply_unexpeted_ifaceid(child, tap):
    lladdr = get_host_lladdr(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=b"ab") / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_relay_reply_unexpeted_peeraddr(child, tap):
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr="fe80::abcd:f00:1337") / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_relay_reply_invalid_optlen(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg(optlen=32, message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_relay_reply_no_ifaceid(child, tap):
    lladdr = get_host_lladdr(tap)
    test_trid = 0xc0ffee
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptRelayMsg(message=DHCP6_Reply(trid=test_trid))
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def test_dhcpv6_relay_reply_empty(child, tap):
    lladdr = get_host_lladdr(tap)
    ifaceid = get_iface_id(tap)
    pkt = DHCP6_RelayReply(peeraddr=lladdr) / \
        DHCP6OptIfaceId(ifaceid=ifaceid) / \
        DHCP6OptRelayMsg()
    result = send_and_exp_any_dhcp6(tap, pkt)
    assert len(result) == 0
    check_if_crashed(child)


def testfunc(child):
    tap = get_bridge(os.environ["TAP"])

    script = sys.modules[__name__]
    tests = [getattr(script, t) for t in script.__dict__
             if type(getattr(script, t)).__name__ == "function"
             and t.startswith("test_")]
    child.sendline('ifconfig')
    # node joined All_DHCP_Relay_Agents_and_Servers
    child.expect_exact('inet6 group: ff02::1:2')

    def run_test(func):
        if child.logfile == sys.stdout:
            print(func.__name__)
            func(child, tap)
        else:
            try:
                func(child, tap)
                print(".", end="", flush=True)
            except Exception:
                print("FAILED")
                raise

    for test in tests:
        run_test(test)


if __name__ == "__main__":
    sys.exit(run(testfunc, timeout=1, echo=False))
