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
URBF_SET_FEATURE = 7
URBF_ABORT_PIPE = 8
URBF_CLASS_IFACE = 9
URBF_CLEAR_FEATURE = 10
URBF_VENDOR_DEVICE = 11

funcs = {
    "-- URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:": (URBF_GET_DESC, False, None),
    "-- URB_FUNCTION_SELECT_CONFIGURATION:":       (URBF_SEL_CONF, False, None),
    "-- URB_FUNCTION_RESET_PIPE:":                 (URBF_RESET_PIPE, False, None),
    "-- URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:": (URBF_TRANSFER, True, "T"),
    "-- URB_FUNCTION_GET_STATUS_FROM_DEVICE:":     (URBF_GET_STATUS, False, None),
    "-- URB_FUNCTION_CONTROL_TRANSFER:":           (URBF_CONTROL, True, "C"),
    "-- URB_FUNCTION_SET_FEATURE_TO_DEVICE:":      (URBF_SET_FEATURE, False, None),
    "-- URB_FUNCTION_ABORT_PIPE:":                 (URBF_SET_FEATURE, False, None),
    "-- URB_FUNCTION_CLASS_INTERFACE:":            (URBF_CLASS_IFACE, True, "C"),
    "-- URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE:":    (URBF_CLEAR_FEATURE, False, None),
    "-- URB_FUNCTION_VENDOR_DEVICE:":              (URBF_VENDOR_DEVICE, False, None)
}

def get_urb_info(l):
    direction = defs.TO_UNKNOWN

    tsstr = ""
    if l[0] == '[':
        idx = l.find(" ms]")
        if idx <= 0:
            return (defs.TO_UNKNOWN, -1, -1)
        tsstr = l[1:idx]
    
    idx = string.find(l, ">>>  URB ")
    if idx >= 0:
        direction = defs.TO_MODEM
    else:
        idx = string.find(l, "<<<  URB ")
        if idx >= 0:
            direction = defs.TO_HOST
        else:
            return (defs.TO_UNKNOWN, -1, -1)

    # Yay, valid packet, grab URB number
    numstr = ""
    for c in l[idx + 9:]:
        if c.isdigit():
            numstr = numstr + c
        else:
            break

    if not len(numstr):
        raise Exception("Failed to get URB number ('%s')" % l)

    return (direction, int(numstr), int(tsstr))

class Packet:
    def __init__(self, line, control_prot, transfer_prot):
        self.direction = defs.TO_UNKNOWN
        self.func = URBF_UNKNOWN
        self.control_prot = control_prot
        self.transfer_prot  = transfer_prot
        self.data = None
        self.urbnum = 0
        self.timestamp = 0
        self.protocol = None
        self.has_data = False
        self.typecode = None
        self.lines = []
        self.in_data = False
        self.data_complete = False
        self.tmpdata = ""
        self.fcomplete = None
        self.funpack = None
        self.fshow = None

        # Check if this is actually a packet
        self.lines.append(line)
        (self.direction, self.urbnum, self.timestamp) = get_urb_info(line)

    def add_line(self, line):
        line = line.strip()
        if not len(line):
            return
        self.lines.append(line)

        if line[0] == '[':
            # Usually the end of a packet, but if we need data from the next
            # packet keep going
            if self.has_data and not self.data_complete:
                return False
            return True

        if not self.typecode:
            # haven't gotten our "-- URB_FUNCTION_xxxx" line yet
            if line.find("-- URB_FUNCTION_") >= 0:
                try:
                    (self.func, self.has_data, self.typecode) = funcs[line]
                except KeyError:
                    raise KeyError("URB function %s not handled" % line)

                if self.func == URBF_TRANSFER:
                    self.protocol = self.transfer_prot
                elif self.func == URBF_CONTROL or self.func == URBF_CLASS_IFACE:
                    self.protocol = self.control_prot

                if self.protocol:
                    exec "from %s import get_funcs" % self.protocol
                    (self.fcomplete, self.funpack, self.fshow) = get_funcs()
            else:
                return False  # not done; need more lines

        if line.find("TransferBufferMDL    = ") >= 0 and self.has_data:
            self.in_data = True
            return False   # not done; need more lines

        if line.find("UrbLink              = ") >= 0 or line.find("UrbLink                 =") >= 0:
            if self.in_data:
                self.in_data = False

                # special case: zero-length data means complete
                if len(self.tmpdata) == 0:
                    self.data_complete = True
                    return True

                if self.fcomplete:
                    self.data_complete = self.fcomplete(self.tmpdata, self.direction)
                    if self.data_complete:
                        self.data = self.funpack(self.tmpdata, self.direction)
                    return self.data_complete
                else:
                    self.data = binascii.unhexlify(self.tmpdata)
                    self.data_complete = True
            return False   # not done; need more lines

        if self.in_data:
            if len(line) and not "no data supplied" in line:
                d = line[line.index(": ") + 2:] # get data alone
                self.tmpdata += d.replace(" ", "")

        return False  # not done; need more lines

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

        if self.fshow:
            self.fshow(self.data, " " * 8, self.direction)

