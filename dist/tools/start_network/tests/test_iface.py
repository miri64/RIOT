#!/usr/bin/env python3

# Copyright (C) 2020 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

import pytest

from start_network.iface.iproute2 import IPRoute2Interface, IPRoute2Bridge
from start_network.iface import set_classes, Interface, Bridge


@pytest.mark.parametrize('platform,iface_cls,bridge_cls',
                         [('linux', IPRoute2Interface, IPRoute2Bridge)])
def test_set_classes(mocker, platform, iface_cls, bridge_cls):
    mocker.patch('start_network.iface.platform', platform)
    set_classes()
    assert Interface == iface_cls
    assert Bridge == bridge_cls


def test_set_classes_unsupported(mocker):
    mocker.patch('start_network.iface.platform', 'foobar')
    with pytest.raises(ValueError):
        set_classes()
