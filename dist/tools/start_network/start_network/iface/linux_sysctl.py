#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

from .base import BaseInterfaceSystemControl


class LinuxInterfaceSystemControl(BaseInterfaceSystemControl):
    # See: https://www.kernel.org/doc/html/latest/networking/ip-sysctl.html
    def __init__(self, interface_name: str, *args,
                 prev_ipv6_forwarding_all: bool = None,
                 **kwargs):
        super().__init__(interface_name, *args, **kwargs)
        if prev_ipv6_forwarding_all is None:
            self.prev_ipv6_forwarding_all = self.all_ipv6_forwarding_enabled()
        else:
            self.prev_ipv6_forwarding_all = bool(prev_ipv6_forwarding_all)

    def _ipv6_conf_read(self, variable):
        return self._sysctl_read(
            'net.ipv6.conf.{}.{}'.format(self.interface_name, variable)
        ).stdout.decode().split('=')[-1].strip()

    def _ipv6_conf_write(self, variable, value, scope=None):
        if not scope:
            scope = self.interface_name
        self._sysctl_write('net.ipv6.conf.{}.{}'.format(scope, variable),
                           value)

    @classmethod
    def all_ipv6_forwarding_enabled(cls):
        return bool(int(cls._sysctl_read('net.ipv6.conf.all.forwarding')
                        .stdout.decode().split('=')[-1].strip()))

    def enable_ipv6_forwarding(self):
        self._ipv6_conf_write('forwarding', 1)
        if self._ipv6_conf_read('accept_ra') == '1':
            self._ipv6_conf_write('accept_ra', 2)
        if not self.prev_ipv6_forwarding_all:
            self._ipv6_conf_write('forwarding', 1, scope='all')

    def disable_ipv6_forwarding(self):
        self._ipv6_conf_write('forwarding', 0)
        if not self.prev_ipv6_forwarding_all:
            self._ipv6_conf_write('forwarding', 0, scope='all')

    def activate_ipv6(self):
        self._ipv6_conf_write('disable_ipv6', 0)

    def deactivate_ipv6(self):
        self._ipv6_conf_write('disable_ipv6', 1)

    def accept_ipv6_rtr_adv(self):
        if self._ipv6_conf_read('forwarding') == '1':
            self._ipv6_conf_write('accept_ra', 2)
        else:
            self._ipv6_conf_write('accept_ra', 1)

    def do_not_accept_ipv6_rtr_adv(self):
        self._ipv6_conf_write('accept_ra', 0)

    def ipv6_rtr_sol_retries(self, value: int):
        self._ipv6_conf_write('router_solicitations', value)
