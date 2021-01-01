#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

from sys import platform

from .iproute2 import IPRoute2Interface, IPRoute2Bridge


def set_classes():
    interface_cls = None
    bridge_cls = None

    if platform.startswith("linux"):
        interface_cls = IPRoute2Interface
        bridge_cls = IPRoute2Bridge
    elif platform.startswith("darwin"):
        pass
    elif platform.startswith("freebsd"):
        pass
    else:
        raise ValueError("Host platform '{}' is not supported"
                         .format(platform))
    return interface_cls, bridge_cls


Interface, Bridge = set_classes()
