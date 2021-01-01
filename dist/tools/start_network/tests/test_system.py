#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

import os

import pytest

from start_network.iface import Interface, Bridge
from start_network.iface.errors import (IllegalInterfaceNameError,
                                        IllegalOperationError,
                                        InterfaceAlreadyExistsError)


def create_tuntap(iface_name, mode):
    assert not Interface.exists(iface_name)
    iface = Interface.create_tuntap(iface_name, mode=mode,
                                    user=os.getlogin())
    try:
        assert isinstance(iface, Interface)
        assert Interface.exists(iface_name)
    except Exception:
        iface.delete()
        raise
    return iface


def create_bridge(bridge_name):
    assert not Bridge.exists(bridge_name)
    bridge = Bridge.create(bridge_name)
    try:
        assert isinstance(bridge, Bridge)
        assert Bridge.exists(bridge_name)
    except Exception:
        bridge.delete()
        raise
    return bridge


@pytest.mark.systemtest
def test_interface_not_exists():
    assert not Interface.exists('foobar-abcdef')


@pytest.mark.systemtest
@pytest.mark.parametrize('iface_name,mode',
                         [('tap133', 'tap'), ('tun133', 'tun')])
def test_interface_bridge(iface_name, mode):
    assert not Interface.exists(iface_name)
    iface = Interface.create_tuntap(iface_name, mode=mode,
                                    user=os.getlogin())
    try:
        assert iface.bridge is None
    finally:
        iface.delete()
    assert iface.bridge is None


@pytest.mark.systemtest
@pytest.mark.parametrize('iface_name,mode',
                         [('tun133', 'tun')])
def test_interface_delete(iface_name, mode):
    iface = create_tuntap(iface_name, mode)
    iface.delete()
    assert not Interface.exists(iface_name)


@pytest.mark.systemtest
@pytest.mark.parametrize('iface_name,mode',
                         [('tap133', 'tap'), ('tun133', 'tun')])
def test_interface_create_tuntap(iface_name, mode):
    assert not Interface.exists(iface_name)
    iface = Interface.create_tuntap(iface_name, mode=mode,
                                    user=os.getlogin())
    try:
        assert isinstance(iface, Interface)
        assert Interface.exists(iface_name)
    finally:
        iface.delete()


@pytest.mark.systemtest
@pytest.mark.parametrize('iface_name,mode',
                         [('tap /0', 'tap'), ('tun /0', 'tun')])
def test_interface_create_tuntap_illegal_name(iface_name, mode):
    assert not Interface.exists(iface_name)
    with pytest.raises(IllegalInterfaceNameError):
        Interface.create_tuntap(iface_name, mode=mode,
                                user=os.getlogin())


@pytest.mark.systemtest
@pytest.mark.parametrize('iface_name,mode',
                         [('tap62', 'tap'), ('tun62', 'tun')])
def test_interface_create_tuntap_twice(iface_name, mode):
    assert not Interface.exists(iface_name)
    iface = Interface.create_tuntap(iface_name, mode=mode,
                                    user=os.getlogin())
    try:
        with pytest.raises(InterfaceAlreadyExistsError):
            Interface.create_tuntap(iface_name, mode=mode,
                                    user=os.getlogin())
    finally:
        iface.delete()


@pytest.mark.systemtest
def test_bridge_create():
    bridge_name = 'bridge174'
    assert not Bridge.exists(bridge_name)
    bridge = Bridge.create(bridge_name)
    try:
        assert isinstance(bridge, Bridge)
        assert Bridge.exists(bridge_name)
    finally:
        bridge.delete()


@pytest.mark.systemtest
def test_bridge_illegal_name():
    bridge_name = 'bridge /0'
    assert not Bridge.exists(bridge_name)
    with pytest.raises(IllegalInterfaceNameError):
        Bridge.create(bridge_name)


@pytest.mark.systemtest
def test_bridge_create_twice():
    bridge_name = 'bridge86'
    assert not Bridge.exists(bridge_name)
    bridge = Bridge.create(bridge_name)
    try:
        with pytest.raises(InterfaceAlreadyExistsError):
            Bridge.create(bridge_name)
    finally:
        bridge.delete()


@pytest.mark.systemtest
def test_bridge_add_member():
    iface_name = 'tap74'
    bridge_name = 'bridge853'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        assert iface.bridge is None
        bridge.add_member(iface)
        assert iface.bridge == bridge
    finally:
        bridge.delete()
        iface.delete()


@pytest.mark.systemtest
def test_bridge_add_member_bridge_does_not_exist():
    iface_name = 'tap74'
    bridge_name = 'bridge853'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        bridge.delete()
        assert iface.bridge is None
        with pytest.raises(IllegalInterfaceNameError):
            bridge.add_member(iface)
        assert iface.bridge is None
    finally:
        iface.delete()


@pytest.mark.systemtest
def test_bridge_add_member_iface_does_not_exist():
    iface_name = 'tap82'
    bridge_name = 'bridge135'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        assert bridge.num_members() == 0
        iface.delete()
        with pytest.raises(IllegalInterfaceNameError):
            bridge.add_member(iface)
        assert bridge.num_members() == 0
    finally:
        bridge.delete()


@pytest.mark.systemtest
def test_bridge_add_member_bridge():
    bridge1_name = 'bridge87'
    bridge2_name = 'bridge88'
    bridge1 = create_bridge(bridge1_name)
    bridge2 = create_bridge(bridge2_name)
    try:
        assert bridge2.bridge is None
        with pytest.raises(IllegalOperationError):
            bridge1.add_member(bridge2)
        assert bridge2.bridge is None
    finally:
        bridge1.delete()
        bridge2.delete()


@pytest.mark.systemtest
def test_bridge_remove_member():
    iface_name = 'tap74'
    bridge_name = 'bridge853'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        assert bridge.num_members() == 0
        bridge.add_member(iface)
        assert bridge.num_members() == 1
        bridge.remove_member(iface)
        assert bridge.num_members() == 0
    finally:
        bridge.delete()
        iface.delete()


@pytest.mark.systemtest
def test_bridge_remove_member_not_member():
    iface_name = 'tap74'
    bridge_name = 'bridge853'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        assert bridge.num_members() == 0
        bridge.remove_member(iface)
        assert bridge.num_members() == 0
    finally:
        bridge.delete()
        iface.delete()


@pytest.mark.systemtest
def test_bridge_list_members():
    iface_name = 'tap74'
    bridge_name = 'bridge853'
    iface = create_tuntap(iface_name, 'tap')
    bridge = create_bridge(bridge_name)
    try:
        assert iface not in set(bridge.list_members())
        bridge.add_member(iface)
        assert iface in set(bridge.list_members())
        bridge.remove_member(iface)
        assert iface not in set(bridge.list_members())
    finally:
        bridge.delete()
        iface.delete()


@pytest.mark.systemtest
def test_interface_sysctl():
    iface_name = 'tap47'
    assert not Interface.exists(iface_name)
    iface = Interface.create_tuntap(iface_name, mode='tap',
                                    user=os.getlogin())
    try:
        iface.sysctl.all_ipv6_forwarding_enabled()
        # try different combinations of having forwarding and accepting rtr_adv
        iface.sysctl.enable_ipv6_forwarding()
        iface.sysctl.accept_ipv6_rtr_adv()
        iface.sysctl.disable_ipv6_forwarding()
        iface.sysctl.enable_ipv6_forwarding()
        iface.sysctl.disable_ipv6_forwarding()
        iface.sysctl.do_not_accept_ipv6_rtr_adv()
        iface.sysctl.activate_ipv6()
        iface.sysctl.deactivate_ipv6()
        iface.sysctl.accept_ipv6_rtr_adv()
        iface.sysctl.do_not_accept_ipv6_rtr_adv()
        iface.sysctl.ipv6_rtr_sol_retries(5)
    finally:
        iface.delete()
