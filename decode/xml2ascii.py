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
# --- Dumps UsbSnoopy XML export files

from xml.sax import saxutils
from xml.sax import handler

packets = []
counts = {}

class FindPackets(handler.ContentHandler):
    def __init__(self):
        self.inFunction = False
        self.inPayload = False
        self.ignore = False
        self.inTimestamp = False
        self.timestamp = None
        self.packet = None

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
                import binascii
                bytes = binascii.a2b_hex(self.packet)
                print bytes
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

