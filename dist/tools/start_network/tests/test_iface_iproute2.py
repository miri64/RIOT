#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>
# pylint: disable=no-member,redefined-outer-name,unused-argument

import subprocess

import pytest

from start_network.iface.errors import (IllegalInterfaceNameError,
                                        IllegalOperationError,
                                        InterfaceAlreadyExistsError,
                                        UnknownInterfaceError)
from start_network.iface.iproute2 import IPRoute2Interface, IPRoute2Bridge


@pytest.fixture
def patch_subprocess_run(mocker):
    return mocker.patch('subprocess.run')


def test_interface_str(patch_subprocess_run):
    iface_name = 'foobar'
    assert str(IPRoute2Interface(name=iface_name)) == iface_name


def test_interface_repr(patch_subprocess_run):
    iface_name = 'foobar'
    assert repr(IPRoute2Interface(name=iface_name)) == \
        '<IPRoute2Interface: {}>'.format(iface_name)


def test_interface_exists(patch_subprocess_run):
    iface_name = 'foobar'
    assert IPRoute2Interface.exists(iface_name)
    subprocess.run.assert_called_once_with(['ip', 'link', 'show', 'dev',
                                            iface_name], check=True)


def test_interface_not_exists(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=0,
        cmd='test',
    )
    iface_name = 'foobar'
    assert not IPRoute2Interface.exists(iface_name)
    subprocess.run.assert_called_once_with(['ip', 'link', 'show', 'dev',
                                            iface_name], check=True)


def test_interface_bridge(mocker, patch_subprocess_run):
    subprocess.run.return_value = mocker.Mock(
        stdout=b'1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue '
               b'state UNKNOWN mode DEFAULT group default qlen 1000\n'
               b'    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n'
               b'4: tapbr0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc noqueue state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 06:63:d7:f7:c4:fc brd ff:ff:ff:ff:ff:ff\n'
               b'5: tap0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc fq_codel master tapbr0 state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 46:e1:f9:b6:fb:57 brd ff:ff:ff:ff:ff:ff\n'
    )
    iface_name = 'foobar'
    assert IPRoute2Interface(name=iface_name).bridge.name == 'tapbr0'
    subprocess.run.assert_called_once_with(['ip', 'link', 'show', 'dev',
                                            iface_name], check=True,
                                           stdout=subprocess.PIPE)


@pytest.mark.parametrize('mode', ['tap', 'tun'])
def test_interface_create_tuntap(mode, patch_subprocess_run):
    iface_name = 'foobar'
    iface = IPRoute2Interface.create_tuntap(iface_name, mode=mode,
                                            user='myuser')
    assert isinstance(iface, IPRoute2Interface)
    subprocess.run.assert_called_once_with(['ip', 'tuntap', 'add', 'dev',
                                            iface_name, 'mode', mode,
                                            'user', 'myuser'],
                                           check=True, stderr=subprocess.PIPE)


@pytest.mark.parametrize('mode', ['tap', 'tun'])
def test_interface_create_tuntap_illegal_name(mode, patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=255,
        cmd='test',
        stderr=b'Illegal name!'
    )
    iface_name = 'foobar'
    with pytest.raises(IllegalInterfaceNameError):
        IPRoute2Interface.create_tuntap(iface_name, mode=mode,
                                        user='myuser')
    subprocess.run.assert_called_once_with(['ip', 'tuntap', 'add', 'dev',
                                            iface_name, 'mode', mode,
                                            'user', 'myuser'],
                                           check=True, stderr=subprocess.PIPE)


@pytest.mark.parametrize('mode', ['tap', 'tun'])
def test_interface_create_tuntap_exists(mode, patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=1,
        cmd='test',
        stderr=b'Device busy!'
    )
    iface_name = 'foobar'
    with pytest.raises(InterfaceAlreadyExistsError):
        IPRoute2Interface.create_tuntap(iface_name, mode=mode,
                                        user='myuser')
    subprocess.run.assert_called_once_with(['ip', 'tuntap', 'add', 'dev',
                                            iface_name, 'mode', mode,
                                            'user', 'myuser'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_create(patch_subprocess_run):
    bridge_name = 'foobar'
    bridge = IPRoute2Bridge.create(bridge_name)
    assert isinstance(bridge, IPRoute2Bridge)
    subprocess.run.assert_called_once_with(['ip', 'link', 'add', 'name',
                                            bridge_name, 'type', 'bridge'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_illegal_name(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=255,
        cmd='test',
        stderr=b'Illegal name!'
    )
    bridge_name = 'foobar'
    with pytest.raises(IllegalInterfaceNameError):
        IPRoute2Bridge.create(bridge_name)
    subprocess.run.assert_called_once_with(['ip', 'link', 'add', 'name',
                                            bridge_name, 'type', 'bridge'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_create_exists(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=2,
        cmd='test',
        stderr=b'Exists!'
    )
    bridge_name = 'foobar'
    with pytest.raises(InterfaceAlreadyExistsError):
        IPRoute2Bridge.create(bridge_name)
    subprocess.run.assert_called_once_with(['ip', 'link', 'add', 'name',
                                            bridge_name, 'type', 'bridge'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_create_unknown_error(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=67456,
        cmd='test',
        stderr=b'Unknown'
    )
    bridge_name = 'foobar'
    with pytest.raises(UnknownInterfaceError):
        IPRoute2Bridge.create(bridge_name)
    subprocess.run.assert_called_once_with(['ip', 'link', 'add', 'name',
                                            bridge_name, 'type', 'bridge'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_get_or_create_exists(mocker, patch_subprocess_run):
    mocker.patch('start_network.iface.iproute2.IPRoute2Bridge.exists',
                 return_value=True)
    bridge_name = 'foobar'
    bridge = IPRoute2Bridge.get_or_create(bridge_name)
    assert isinstance(bridge, IPRoute2Bridge)
    assert len(subprocess.run.mock_calls) == 0


def test_bridge_get_or_create_not_exists(mocker, patch_subprocess_run):
    mocker.patch('start_network.iface.iproute2.IPRoute2Bridge.exists',
                 return_value=False)
    bridge_name = 'foobar'
    bridge = IPRoute2Bridge.get_or_create(bridge_name)
    assert isinstance(bridge, IPRoute2Bridge)
    subprocess.run.assert_called_once_with(['ip', 'link', 'add', 'name',
                                            bridge_name, 'type', 'bridge'],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_add_member(patch_subprocess_run):
    iface_name = 'test'
    bridge_name = 'foobar'

    IPRoute2Bridge(name=bridge_name).add_member(
        IPRoute2Interface(name=iface_name)
    )
    subprocess.run.assert_called_once_with(['ip', 'link', 'set', 'dev',
                                            iface_name, 'master', bridge_name],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_add_member_bridge_does_not_exist(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=255,
        cmd='test',
        stderr=b'Bridge does not exist!'
    )
    iface_name = 'interface'
    bridge_name = 'foobar'
    with pytest.raises(IllegalInterfaceNameError):
        IPRoute2Bridge(name=bridge_name).add_member(
            IPRoute2Interface(name=iface_name)
        )
    subprocess.run.assert_called_once_with(['ip', 'link', 'set', 'dev',
                                            iface_name, 'master', bridge_name],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_add_member_iface_does_not_exist(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=1,
        cmd='test',
        stderr=b'Interface does not exist!'
    )
    iface_name = 'interface'
    bridge_name = 'foobar'
    with pytest.raises(IllegalInterfaceNameError):
        IPRoute2Bridge(name=bridge_name).add_member(
            IPRoute2Interface(name=iface_name)
        )
    subprocess.run.assert_called_once_with(['ip', 'link', 'set', 'dev',
                                            iface_name, 'master', bridge_name],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_add_member_bridge(patch_subprocess_run):
    patch_subprocess_run.side_effect = subprocess.CalledProcessError(
        returncode=2,
        cmd='test',
        stderr=b'Can not add bridge to bridge!'
    )
    bridge1_name = 'bridge'
    bridge2_name = 'foobar'
    with pytest.raises(IllegalOperationError):
        IPRoute2Bridge(name=bridge2_name).add_member(
            IPRoute2Bridge(name=bridge1_name)
        )
    subprocess.run.assert_called_once_with(['ip', 'link', 'set', 'dev',
                                            bridge1_name, 'master',
                                            bridge2_name],
                                           check=True, stderr=subprocess.PIPE)


def test_bridge_remove_member(mocker, patch_subprocess_run):
    iface_name = 'test'
    bridge_name = 'foobar'
    iface = IPRoute2Interface(name=iface_name)
    bridge = IPRoute2Bridge(name=bridge_name)
    mocker.patch('start_network.iface.iproute2.IPRoute2Interface.bridge',
                 new_callable=lambda: bridge)

    bridge.remove_member(iface)
    subprocess.run.assert_called_once_with(['ip', 'link', 'set', 'dev',
                                            iface_name, 'nomaster'],
                                           check=True)


def test_bridge_remove_member_not_member(mocker, patch_subprocess_run):
    iface_name = 'test'
    bridge_name = 'foobar'
    iface = IPRoute2Interface(name=iface_name)
    bridge = IPRoute2Bridge(name=bridge_name)
    mocker.patch('start_network.iface.iproute2.IPRoute2Interface.bridge',
                 new_callable=lambda: None)

    bridge.remove_member(iface)
    subprocess.run.assert_not_called()


def test_bridge_list_member(mocker, patch_subprocess_run):
    subprocess.run.return_value = mocker.Mock(
        stdout=b'1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue '
               b'state UNKNOWN mode DEFAULT group default qlen 1000\n'
               b'    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n'
               b'4: tapbr0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc noqueue state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 06:63:d7:f7:c4:fc brd ff:ff:ff:ff:ff:ff\n'
               b'5: tap0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc fq_codel master tapbr0 state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 46:e1:f9:b6:fb:57 brd ff:ff:ff:ff:ff:ff\n'
    )
    iface1_name = 'tap0'
    iface2_name = 'foobar'
    bridge_name = 'tapbr0'
    iface1 = IPRoute2Interface(name=iface1_name)
    iface2 = IPRoute2Interface(name=iface2_name)
    bridge = IPRoute2Bridge(name=bridge_name)
    members = set(bridge.list_members())
    subprocess.run.assert_called_once_with(['ip', 'link', 'show'], check=True,
                                           stdout=subprocess.PIPE)
    assert iface1 in members
    assert iface2 not in members


def test_bridge_num_member(mocker, patch_subprocess_run):
    subprocess.run.return_value = mocker.Mock(
        stdout=b'1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue '
               b'state UNKNOWN mode DEFAULT group default qlen 1000\n'
               b'    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00\n'
               b'4: tapbr0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc noqueue state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 06:63:d7:f7:c4:fc brd ff:ff:ff:ff:ff:ff\n'
               b'5: tap0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc fq_codel master tapbr0 state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 46:e1:f9:b6:fb:57 brd ff:ff:ff:ff:ff:ff\n'
               b'5: tap1: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 '
               b'qdisc fq_codel master tapbr0 state DOWN mode DEFAULT '
               b'group default qlen 1000\n'
               b'    link/ether 8a:c7:7b:85:f3:82  brd ff:ff:ff:ff:ff:ff\n'
    )
    bridge_name = 'tapbr0'
    bridge = IPRoute2Bridge(name=bridge_name)
    assert bridge.num_members() == 2
    subprocess.run.assert_called_once_with(['ip', 'link', 'show'], check=True,
                                           stdout=subprocess.PIPE)
