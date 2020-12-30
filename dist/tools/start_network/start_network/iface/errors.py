#!/usr/bin/env python3

# Copyright (C) 2020-21 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#
# @author   Martine Lenders <m.lenders@fu-berlin.de>

class InterfaceAlreadyExistsError(FileExistsError):
    pass


class IllegalInterfaceNameError(ValueError):
    pass


class IllegalOperationError(TypeError):
    pass


class UnknownInterfaceError(Exception):
    pass
