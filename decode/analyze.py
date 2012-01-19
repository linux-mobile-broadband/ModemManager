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
# ---- Dumps UsbSnoopy XML captures of WMC traffic

from xml.sax import saxutils
from xml.sax import handler
import binascii
import string

packets = []
counts = {}

TO_UNKNOWN = 0
TO_MODEM = 1
TO_HOST = 2

class Packet:
    def __init__(self, data, idx):
        if len(data) % 2 != 0:
            raise Exception("bad data length")

        self.idx = idx
        self.type = TO_UNKNOWN
        if data[:14] == "41542a574d433d":
            # host->device: remove the AT*WMC= bits and newline at the end
            data = data[14:]
            if data[len(data) - 2:] == "0d":
                data = data[:len(data) - 2]
            self.type = TO_MODEM
#        elif data[len(data) - 6:] == "30307e":
#            # device->host: remove HDLC terminator and fake CRC
#            data = data[:len(data) - 6]
#            self.type = TO_HOST
        elif data[len(data) - 2:] == "7e":
            # device->host: remove HDLC terminator and CRC
            data = data[:len(data) - 6]
            self.type = TO_HOST

        self.data = binascii.unhexlify(data)
        self.four = data[:4]

        # PPP-unescape TO_MODEM data
        escape = False
        new_data = ""
        for i in self.data:
            if ord(i) == 0x7D:
                escape = True
            elif escape == True:
                new_data += chr(ord(i) ^ 0x20)
                escape = False
            else:
                new_data += i
        self.data = new_data

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
        line = "*"
        if self.type == TO_MODEM:
            line = ">"
        elif self.type == TO_HOST:
            line = "<"

        offset = 0
        items = []
        printed = False
        for i in self.data:
            printed = False
            line += " %02x" % ord(i)
            items.append(ord(i))
            if len(items) % 16 == 0:
                print "%03d: %s" % (offset, self.add_ascii(line, items))
                line = " "
                items = []
                printed = True
                offset += 16
        if not printed:
            print "%03d: %s" % (offset, self.add_ascii(line, items))
        print ""

class FindPackets(handler.ContentHandler):
    def __init__(self):
        self.inFunction = False
        self.inPayload = False
        self.ignore = False
        self.inTimestamp = False
        self.timestamp = None
        self.packet = None
        self.idx = 1

    def startElement(self, name, attrs):
        if name == "function":
            self.inFunction = True
        elif name == "payloadbytes":
            self.inPayload = True
        elif name == "timestamp":
            self.inTimestamp = True

    def characters(self, ch):
        if self.ignore:
            return

        stripped = ch.strip()
        if self.inFunction and ch != "BULK_OR_INTERRUPT_TRANSFER":
            self.ignore = True
            return
        elif self.inTimestamp:
            self.timestamp = stripped
        elif self.inPayload and len(stripped) > 0:
            if self.packet == None:
                self.packet = stripped
            else:
                self.packet += stripped

    def endElement(self, name):
        if name == "function":
            self.inFunction = False
        elif name == "payloadbytes":
            self.inPayload = False
        elif name == "payload":
            if self.packet:
                p = Packet(self.packet, self.idx)
                self.idx = self.idx + 1
                packets.append(p)
                self.packet = None

            self.ignore = False
            self.timestamp = None
        elif name == "timestamp":
            self.inTimestamp = False


from xml.sax import make_parser
from xml.sax import parse
import sys

if __name__ == "__main__":
    dh = FindPackets()
    parse(sys.argv[1], dh)

    cmds = {}
    for p in packets:
        if cmds.has_key(p.four):
            cmds[p.four].append(p)
        else:
            cmds[p.four] = [p]
        if len(sys.argv) > 2:
            if p.four == sys.argv[2]:
                p.show()
        else:
            p.show()

    print ""
    print "cmd #tot"
    for k in cmds.keys():
        print "%s (%d)" % (k, len(cmds[k]))
    print ""

