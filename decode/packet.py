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

import wmc

URBF_UNKNOWN = 0
URBF_GET_DESC = 1
URBF_SEL_CONF = 2
URBF_RESET_PIPE = 3
URBF_TRANSFER = 4
URBF_GET_STATUS = 5
URBF_CONTROL = 6

funcs = {
    "-- URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:": (URBF_GET_DESC, False, None),
    "-- URB_FUNCTION_SELECT_CONFIGURATION:":       (URBF_SEL_CONF, False, None),
    "-- URB_FUNCTION_RESET_PIPE:":                 (URBF_RESET_PIPE, False, None),
    "-- URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:": (URBF_TRANSFER, True, "T"),
    "-- URB_FUNCTION_GET_STATUS_FROM_DEVICE:":     (URBF_GET_STATUS, False, None),
    "-- URB_FUNCTION_CONTROL_TRANSFER:":           (URBF_CONTROL, True, "C")
}

def get_urb_info(l):
    num = 0
    direction = defs.TO_UNKNOWN
    
    idx = string.find(l, ">>>  URB ")
    if idx >= 0:
        direction = defs.TO_MODEM
    else:
        idx = string.find(l, "<<<  URB ")
        if idx >= 0:
            direction = defs.TO_HOST
        else:
            raise Exception("Invalid packet start line")

    numstr = ""
    for c in l[idx + 9:]:
        if c.isdigit():
            numstr = numstr + c
        else:
            break

    if not len(numstr):
        raise Exception("Failed to get URB number ('%s')" % l)

    return (direction, int(numstr))

class Packet:
    def __init__(self, lines, control_prot, transfer_prot):
        self.direction = defs.TO_UNKNOWN
        self.func = URBF_UNKNOWN
        self.extra = []
        self.data = None
        self.urbnum = 0
        self.protocol = None
        self.has_data = False
        self.typecode = None

        # Parse the packet
        
        (self.direction, self.urbnum) = get_urb_info(lines[0])

        try:
            (self.func, self.has_data, self.typecode) = funcs[lines[1].strip()]
        except KeyError:
            raise KeyError("URB function %s not handled" % lines[1].strip())

        if self.func == URBF_TRANSFER:
            self.protocol = transfer_prot
        elif self.func == URBF_CONTROL:
            self.protocol = control_prot

        # Parse transfer buffer data
        in_data = False
        data = ""
        for i in range(2, len(lines)):
            l = lines[i].strip()
            if self.has_data:
                if l.startswith("TransferBufferMDL"):
                    if in_data == True:
                        raise Exception("Already in data")
                    in_data = True
                elif l.startswith("UrbLink"):
                    in_data = False
                elif in_data and len(l) and not "no data supplied" in l:
                    d = l[l.index(": ") + 2:] # get data alone
                    data += d.replace(" ", "")
            else:
                self.extra.append(l)

        if len(data) > 0:
            self.parse_data(data)

    def get_funcs(self):
        if self.protocol:
            exec "from %s import get_funcs" % self.protocol
            return get_funcs()
        return (None, None)

    def parse_data(self, data):
        if not self.has_data:
            raise Exception("Data only valid for URBF_TRANSFER or URBF_CONTROL")

        (unpack, show) = self.get_funcs()
        if unpack:
            self.data = unpack(data, self.direction)
        else:
            self.data = binascii.unhexlify(data)

    def add_ascii(self, line, items):
        if len(line) < 53:
            line += " " * (53 - len(line))
        for i in items:
            if chr(i) in string.printable and i >= 32:
                line += chr(i)
            else:
                line += "."
        return line

    def show(self):
        if not self.has_data or not self.data:
            return

        # Ignore URBF_TRANSFER packets that appear to be returning SetupPacket data
        if self.data == chr(0xa1) + chr(0x01) + chr(0x00) + chr(0x00) + chr(0x05) + chr(0x00) + chr(0x00) + chr(0x00):
            return

        offset = 0
        items = []
        printed = False
        line = ""

        prefix = "*"
        if self.direction == defs.TO_MODEM:
            prefix = ">"
        elif self.direction == defs.TO_HOST:
            prefix = "<"

        if self.typecode:
            prefix = prefix + " " + self.typecode + " "
        else:
            prefix = prefix + "   "

        prefix_printed = False
        for i in self.data:
            printed = False
            line += " %02x" % ord(i)
            items.append(ord(i))
            if len(items) % 16 == 0:
                output = "%04d: %s" % (offset, self.add_ascii(line, items))
                if offset == 0:
                    print prefix + output
                else:
                    print "    " + output
                line = ""
                items = []
                printed = True
                offset += 16
                prefix_printed = True

        if not printed:
            output = "%04d: %s" % (offset, self.add_ascii(line, items))
            if prefix_printed:
                print "    " + output
            else:
                print prefix + output
        print ""

        (unpack, show) = self.get_funcs()
        if show:
            show(self.data, " " * 8, self.direction)

