#!/usr/bin/python
# -*- Mode: python; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details:
#
# Copyright (C) 2011 Red Hat, Inc.
#

import binascii
import string
import sys
import defs
from packet import Packet

packets = []
control = None
transfer = None

def get_protocol(arg):
    return arg[arg.index("=") + 1:]

if __name__ == "__main__":
    i = 1
    if sys.argv[i].startswith("--control="):
        control = get_protocol(sys.argv[i])
        i = i + 1
    if sys.argv[i].startswith("--transfer="):
        transfer = get_protocol(sys.argv[i])
        i = i + 1

    path = sys.argv[i]
    f = open(path, 'r')
    lines = f.readlines()
    f.close()

    packet = None
    for l in lines:
        if packet:
            done = packet.add_line(l)
            if done:
                packets.append(packet)
                packet = None
        else:
            packet = Packet(l, control, transfer)
            if packet.direction == defs.TO_UNKNOWN:
                packet = None

    for p in packets:
        p.show()
