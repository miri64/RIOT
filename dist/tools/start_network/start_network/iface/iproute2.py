#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

import re
import os
import subprocess

from .base import BaseInterface, BaseBridge
from .errors import (IllegalInterfaceNameError,
                     IllegalOperationError,
                     InterfaceAlreadyExistsError,
                     UnknownInterfaceError)
from .linux_sysctl import LinuxInterfaceSystemControl


class IPRoute2Interface(BaseInterface):
    _sysctl_class = LinuxInterfaceSystemControl

    def __init__(self, name, *args, **kwargs):
        super().__init__(name, *args, **kwargs)
        self._sysctl = None

    @classmethod
    def _ip(cls, ip_args, *args, check=True, **kwargs):
        ip_args.insert(0, "ip")
        if os.environ.get("START_NETWORK_SUDO", "0") == "1":
            ip_args.insert(0, "sudo")
        return subprocess.run(ip_args, check=check, *args, **kwargs)

    @classmethod
    def _ip_create_iface(cls, ip_args, *args, check=True, **kwargs):
        try:
            res = cls._ip(ip_args, check=check, stderr=subprocess.PIPE,
                          *args, **kwargs)
        except subprocess.CalledProcessError as err:
            if err.returncode in [1, 2]:
                raise InterfaceAlreadyExistsError(err.stderr.decode().strip())
            if err.returncode == 255:
                raise IllegalInterfaceNameError(err.stderr.decode().strip())
            raise UnknownInterfaceError((err.returncode, err.stderr))
        else:
            return res

    @classmethod
    def exists(cls, iface_name):
        try:
            cls._ip(['link', 'show', 'dev', iface_name])
        except subprocess.CalledProcessError:
            return False
        else:
            return True

    @property
    def bridge(self):
        try:
            res = self._ip(['link', 'show', 'dev', self.name],
                           stdout=subprocess.PIPE).stdout.decode()
        except subprocess.CalledProcessError:
            return None
        else:
            comp = re.compile(r'master\s+(\S+)\b')
            for line in res.splitlines():
                match = comp.search(line)
                if match:
                    return IPRoute2Bridge(name=match[1])
            return None

    @property
    def sysctl(self):
        if not self._sysctl:
            self._sysctl = self._sysctl_class(self.name)
        return self._sysctl

    @classmethod
    def iter(cls, iface_names=None):
        print(cls)

    @classmethod
    def create_tuntap(cls, iface_name, user, mode="tap"):
        cls._ip_create_iface(['tuntap', 'add', 'dev', iface_name, 'mode', mode,
                             'user', user])
        return cls(iface_name)

    def delete(self):
        self._ip(['link', 'delete', self.name])

    def link_set_up(self):
        pass

    def link_set_down(self):
        pass

    def add_address(self, addr, prefix_len=64):
        pass

    def remove_address(self, addr, prefix_len=64):
        pass

    def add_route(self, route, next_hop=None):
        pass

    def remove_route(self, route, next_hop=None):
        pass


class IPRoute2Bridge(BaseBridge, IPRoute2Interface):
    @classmethod
    def create(cls, bridge_name):
        cls._ip_create_iface(['link', 'add', 'name', bridge_name,
                              'type', 'bridge'])
        return cls(bridge_name)

    def add_member(self, iface):
        try:
            self._ip(['link', 'set', 'dev', iface.name, 'master', self.name],
                     stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as err:
            if err.returncode in (1, 255):
                raise IllegalInterfaceNameError(
                    err.stderr.strip().decode().strip()
                )
            if err.returncode == 2:
                raise IllegalOperationError(
                    err.stderr.strip().decode().strip()
                )
            raise UnknownInterfaceError(err.stderr.strip())

    def remove_member(self, iface):
        if iface.bridge == self:
            self._ip(['link', 'set', 'dev', iface.name, 'nomaster'])

    def list_members(self):
        res = self._ip(['link', 'show'],
                       stdout=subprocess.PIPE).stdout.decode()
        comp = re.compile(r'^\d+:\s+(\S+\d+):.+master (\S+\b)')
        for line in res.splitlines():
            print(line)
            match = comp.search(line)
            if match and match[2] == self.name:
                yield IPRoute2Interface(name=match[1])
