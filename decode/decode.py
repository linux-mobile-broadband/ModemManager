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

    in_packet = False
    finish_packet = False
    pkt_lines = []
    for l in lines:
        if l[0] == '[':
            # Start of a packet
            if "]  >>>  URB" in l or "]  <<<  URB" in l:
                if in_packet == True:
                    in_packet = False
                    finish_packet = True
                else:
                    in_packet = True
            elif "] UsbSnoop - " in l:
                # Packet done?
                if in_packet == True:
                    in_packet = False
                    finish_packet = True

        if finish_packet == True:
            packets.append(Packet(pkt_lines, control, transfer))
            pkt_lines = []
            finish_packet = False

        if in_packet == True:
            pkt_lines.append(l)

    for p in packets:
        p.show()
