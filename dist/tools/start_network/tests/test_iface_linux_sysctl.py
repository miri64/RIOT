#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>
# pylint: disable=no-member,protected-access,redefined-outer-name
# pylint: disable=unused-argument

import subprocess

import pytest

from start_network.iface.errors import SystemControlError
from start_network.iface.linux_sysctl import LinuxInterfaceSystemControl


@pytest.fixture
def patch_sysctl(mocker):
    return mocker.patch('start_network.iface.linux_sysctl.' +
                        'LinuxInterfaceSystemControl._sysctl')


@pytest.mark.parametrize('check', [True, False])
def test_basic_sysctl(mocker, check):
    mocker.patch('subprocess.run')
    LinuxInterfaceSystemControl._sysctl(['test'], check=check)
    subprocess.run.assert_called_once_with(['sysctl', 'test'], check=check,
                                           stderr=subprocess.PIPE)


def test_basic_sysctl_error(mocker):
    mocker.patch(
        'subprocess.run',
        side_effect=subprocess.CalledProcessError(
            returncode=1,
            cmd='test',
        )
    )
    with pytest.raises(SystemControlError):
        LinuxInterfaceSystemControl._sysctl(['test'])
    subprocess.run.assert_called_once_with(
        ['sysctl', 'test'], check=True, stderr=subprocess.PIPE
    )


@pytest.mark.parametrize('return_value', [b'0', b'1'])
def test_all_ipv6_forwarding_enabled(mocker, patch_sysctl, return_value):
    patch_sysctl.return_value = mocker.MagicMock(
        stdout=b'value = ' + return_value
    )
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=True)
    if return_value == b'0':
        assert not sysctl.all_ipv6_forwarding_enabled()
    else:
        assert sysctl.all_ipv6_forwarding_enabled()


def test_enable_ipv6_forwarding_all_enabled(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=True)
    sysctl.enable_ipv6_forwarding()
    sysctl._sysctl.assert_any_call(['-w', 'net.ipv6.conf.foobar.forwarding=1'])


def test_enable_ipv6_forwarding_all_disabled(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.enable_ipv6_forwarding()
    sysctl._sysctl.has_calls([
        sysctl._sysctl(['-w', 'net.ipv6.conf.all.forwarding=1']),
        sysctl._sysctl(['-w', 'net.ipv6.conf.foobar.forwarding=1'])
    ], any_order=True)


@pytest.mark.parametrize('all_enabled_return_value,accept_ra_return_value',
                         [(True, b'0'), (False, b'0'),
                          (True, b'1'), (False, b'1'),
                          (True, b'2'), (False, b'2')])
def test_enable_ipv6_forwarding_all_unspecfied(mocker,
                                               patch_sysctl,
                                               all_enabled_return_value,
                                               accept_ra_return_value):
    patch_sysctl.return_value = mocker.MagicMock(
        stdout=b'value = ' + accept_ra_return_value
    )
    mocker.patch(
        'start_network.iface.linux_sysctl.' +
        'LinuxInterfaceSystemControl.all_ipv6_forwarding_enabled',
        return_value=all_enabled_return_value
    )
    sysctl = LinuxInterfaceSystemControl('foobar')
    sysctl.enable_ipv6_forwarding()
    sysctl._sysctl.assert_any_call(['-w', 'net.ipv6.conf.foobar.forwarding=1'])
    if not all_enabled_return_value:
        sysctl._sysctl.assert_any_call(
            ['-w', 'net.ipv6.conf.all.forwarding=1']
        )
    if accept_ra_return_value == b'1':
        sysctl._sysctl.assert_any_call(
            ['-w', 'net.ipv6.conf.foobar.accept_ra=2']
        )


def test_disable_ipv6_forwarding_all_enabled(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=True)
    sysctl.disable_ipv6_forwarding()
    sysctl._sysctl.assert_called_once_with(
            ['-w', 'net.ipv6.conf.foobar.forwarding=0']
        )


def test_disable_ipv6_forwarding_all_disabled(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.disable_ipv6_forwarding()
    sysctl._sysctl.has_calls([
        sysctl._sysctl(
           ['-w', 'net.ipv6.conf.all.forwarding=0']
        ),
        sysctl._sysctl(
           ['-w', 'net.ipv6.conf.foobar.forwarding=0']
        )
    ])


@pytest.mark.parametrize('all_enabled_return_value', [False, True])
def test_disable_ipv6_forwarding_all_unspecfied(mocker, patch_sysctl,
                                                all_enabled_return_value):
    mocker.patch(
        'start_network.iface.linux_sysctl.' +
        'LinuxInterfaceSystemControl.all_ipv6_forwarding_enabled',
        return_value=all_enabled_return_value
    )
    sysctl = LinuxInterfaceSystemControl('foobar')
    sysctl.disable_ipv6_forwarding()
    sysctl._sysctl.has_calls([
        sysctl._sysctl(['net.ipv6.conf.all.forwarding']),
        sysctl._sysctl(['-w', 'net.ipv6.conf.foobar.forwarding=0'])
    ])
    if not all_enabled_return_value:
        sysctl._sysctl.assert_any_call(
           ['-w', 'net.ipv6.conf.all.forwarding=0']
        )


def test_activate_ipv6(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.activate_ipv6()
    sysctl._sysctl.assert_called_once_with(
        ['-w', 'net.ipv6.conf.foobar.disable_ipv6=0']
    )


def test_deactivate_ipv6(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.deactivate_ipv6()
    sysctl._sysctl.assert_called_once_with(
        ['-w', 'net.ipv6.conf.foobar.disable_ipv6=1']
    )


@pytest.mark.parametrize('forwarding_return_value', [b'0', b'1'])
def test_do_accept_ipv6_rtr_adv(mocker, patch_sysctl, forwarding_return_value):
    patch_sysctl.return_value = mocker.MagicMock(
        stdout=b'value = ' + forwarding_return_value
    )
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    if forwarding_return_value == b'1':
        value = 2
    else:
        value = 1
    sysctl.accept_ipv6_rtr_adv()
    sysctl._sysctl.assert_called_with(
        ['-w', 'net.ipv6.conf.foobar.accept_ra={}'.format(value)]
    )


def test_do_not_accept_ipv6_rtr_adv(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.do_not_accept_ipv6_rtr_adv()
    sysctl._sysctl.assert_called_once_with(
        ['-w', 'net.ipv6.conf.foobar.accept_ra=0']
    )


def test_ipv6_rtr_sol_retries(patch_sysctl):
    sysctl = LinuxInterfaceSystemControl('foobar',
                                         prev_ipv6_forwarding_all=False)
    sysctl.ipv6_rtr_sol_retries(42)
    sysctl._sysctl.assert_called_once_with(
        ['-w', 'net.ipv6.conf.foobar.router_solicitations=42']
    )
