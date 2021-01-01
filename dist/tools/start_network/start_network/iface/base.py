#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

import subprocess

from abc import ABC, abstractmethod
from collections.abc import Generator, Sequence

from .errors import SystemControlError


# pylint: disable=too-few-public-methods
class Singleton:
    _instance = None

    # pylint: disable=unused-argument
    def __new__(cls, *args, **kwargs):
        # name-based singleton pattern
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance


# pylint: disable=too-few-public-methods
class NamedMultiSingleton:
    _instances = {}

    # pylint: disable=unused-argument
    def __new__(cls, name: str, *args, **kwargs):
        # name-based singleton pattern
        if (cls, name) not in cls._instances:
            cls._instances[cls, name] = super().__new__(cls)
        return cls._instances[cls, name]


class BaseInterfaceSystemControl(ABC, Singleton):
    def __init__(self, interface_name: str):
        self.interface_name = interface_name

    @staticmethod
    def _sysctl(sysctl_args, *args, check=True, **kwargs):
        kwargs.pop('stderr', None)
        sysctl_args.insert(0, 'sysctl')
        try:
            return subprocess.run(sysctl_args, *args, check=check,
                                  stderr=subprocess.PIPE, **kwargs)
        except subprocess.CalledProcessError as err:
            raise SystemControlError(err.stderr)

    # TODO FreeBSD: https://www.freebsd.org/cgi/man.cgi?query=inet6&sektion=4&apropos=0&manpath=FreeBSD+12.2-RELEASE+and+Ports
    @classmethod
    def _sysctl_read(cls, variable):
        return cls._sysctl(['{}'.format(variable)],
                           stdout=subprocess.PIPE)

    @classmethod
    def _sysctl_write(cls, variable, value):
        cls._sysctl(['-w', '{}={}'.format(variable, value)])

    @abstractmethod
    def all_ipv6_forwarding_enabled(self):
        raise NotImplementedError

    @abstractmethod
    def enable_ipv6_forwarding(self):
        raise NotImplementedError

    @abstractmethod
    def disable_ipv6_forwarding(self):
        raise NotImplementedError

    @abstractmethod
    def activate_ipv6(self):
        raise NotImplementedError

    @abstractmethod
    def deactivate_ipv6(self):
        raise NotImplementedError

    @abstractmethod
    def accept_ipv6_rtr_adv(self):
        raise NotImplementedError

    @abstractmethod
    def do_not_accept_ipv6_rtr_adv(self):
        raise NotImplementedError

    @abstractmethod
    def ipv6_rtr_sol_retries(self, value: int):
        raise NotImplementedError


class BaseInterface(ABC, NamedMultiSingleton):
    def __init__(self, name: str):
        self._name = name

    def __str__(self):
        return self.name

    def __repr__(self):
        return '<{}: {}>'.format(type(self).__name__, self.name)

    @property
    def name(self):
        return self._name

    @property
    @abstractmethod
    def bridge(self):
        raise NotImplementedError

    @property
    @abstractmethod
    def sysctl(self) -> BaseInterfaceSystemControl:
        raise NotImplementedError

    @classmethod
    @abstractmethod
    def iter(cls, iface_names: Sequence[str] = None) \
            -> Generator['BaseInterface']:
        raise NotImplementedError

    @classmethod
    @abstractmethod
    def create_tuntap(cls, iface_name: str, user: str,
                      mode="tap") -> 'BaseInterface':
        raise NotImplementedError

    @classmethod
    @abstractmethod
    def exists(cls, iface_name: str) -> bool:
        raise NotImplementedError

    @abstractmethod
    def delete(self):
        raise NotImplementedError

    @abstractmethod
    def link_set_up(self):
        raise NotImplementedError

    @abstractmethod
    def link_set_down(self):
        raise NotImplementedError

    @abstractmethod
    def add_address(self, addr, prefix_len=64):
        raise NotImplementedError

    @abstractmethod
    def remove_address(self, addr, prefix_len=64):
        raise NotImplementedError

    @abstractmethod
    def add_route(self, route, next_hop=None):
        raise NotImplementedError

    @abstractmethod
    def remove_route(self, route, next_hop=None):
        raise NotImplementedError


class BaseBridge(BaseInterface):
    @classmethod
    @abstractmethod
    def create(cls, bridge_name: str) -> 'BaseBridge':
        raise NotImplementedError

    @classmethod
    def get_or_create(cls, bridge_name: str) -> 'BaseBridge':
        if cls.exists(bridge_name):
            return cls(bridge_name)
        return cls.create(bridge_name)

    @abstractmethod
    def add_member(self, iface: BaseInterface) -> None:
        raise NotImplementedError

    @abstractmethod
    def remove_member(self, iface: BaseInterface) -> None:
        raise NotImplementedError

    @abstractmethod
    def list_members(self) -> Generator[BaseInterface]:
        raise NotImplementedError

    def num_members(self) -> int:
        return len(list(self.list_members()))
