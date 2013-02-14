#! /bin/env python
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
# Copyright (C) 2012 Red Hat, Inc.
#

import os
import sys
import select
import struct
import string
from termios import *

debug = False

def lg_pack(data, seqno):
    l = len(data)

    fmt = "<"
    fmt += "I"       # magic
    fmt += "I"       # sequence number
    fmt += "I"       # data length
    fmt += "H"       # MUX channel
    fmt += "%ds" % l # AT data

    # Packets always padded to 4-byte boundaries
    sz = struct.calcsize(fmt)
    padding = 0
    if sz % 4 > 0:
        padding = 4 - (sz % 4)
    fmt += "%ds" % padding

    return struct.pack(fmt, 0xa512485a, seqno, l, 0xf011, data, "\0" * padding)

def lg_unpack(data):
    fmt = "<"
    fmt += "I"    # magic
    fmt += "I"    # sequence number
    fmt += "I"    # data length
    fmt += "H"    # MUX channel
    fmt += "%ds" % (len(data) - 14)  # AT data

    (magic, seq, l, chan, resp) = struct.unpack(fmt, data)
    resp = resp[:l]

    if magic != 0xa512485a:
        raise Exception("Bad magic: 0x%08x" % magic)
    if chan != 0xf011:
        print "Unhandled channel 0x%04x" % chan

    # It appears that we're supposed to ignore any data after \r\n, or if
    # we don't get a \r\n we ignore all of it.  The modem adds random
    # data to the end of the response, for example:
    #
    # > 5a 48 12 a5 08 00 00 00 0a 00 00 00 11 f0 41 54 2b 43 45 52 45 47 3f 0a 
    # < 5a 48 12 a5 4e 00 00 00 15 00 00 00 11 f0 2b 43 45 52 45 47 3a 20 30 2c 31 0d 0a 00 00 4f 4b 0d 0a 00 47 74 
    #
    # where there's a trailing "00 47" (the 0x74 is not included in the packet
    # due to the data length field).  The trailing bytes appear totally random
    # in value and length.

    # status is last two bytes for most commands if there are trailing bytes
    status = []
    if (resp >= 2):
        statbytes = resp[len(resp) - 2:]
        status = [ ord(statbytes[0]), ord(statbytes[1]) ]

    crlf = resp.rfind("\r\n")
    if crlf == -1:
        # if last char is a newline then it's probably an echo, otherwise status
        if resp[len(resp) - 1:] == '\n':
            status = []
        resp = ""
    else:
        if crlf == len(resp) - 2:
            status = []
        resp = resp[:crlf + 2]

    return (resp, status)

def dump_raw(data, to_modem):
    if debug:
        line = ""
        if to_modem:
            line += "> "
        else:
            line += "< "
        for c in data:
            line += "%02x " % ord(c)
        print line

def make_printable(data):
    p = ""
    for c in data:
        if c in string.printable and ord(c) >= 32 or c == '\n' or c == '\r':
            p += c
        else:
            p += "<%02x>" % ord(c)
    return p


#########################################

if len(sys.argv) != 2 and len(sys.argv) != 3:
    print "Usage: %s <port> [--debug]" % sys.argv[0]
    sys.exit(1)

if len(sys.argv) > 2 and sys.argv[2] == "--debug":
    debug = True

fd = os.open(sys.argv[1], os.O_RDWR)

# read existing port attributes and mask the ones we don't want
attrs = tcgetattr(fd)
attrs[0] = attrs[0] & ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON)  # iflag
attrs[1] = attrs[1] & ~OPOST                              # oflag
attrs[2] = attrs[2] & ~(CSIZE | PARENB)                   # cflag
attrs[3] = attrs[3] & ~(ECHO | ICANON | IEXTEN | ISIG)    # lflag

# Set up the attributes we do want
attrs[2] = attrs[2] | CS8  # cflag
attrs[4] = B115200         # ispeed
attrs[5] = B115200         # ospeed
attrs[6][VMIN] = 1         # cc
attrs[6][VTIME] = 0        # cc
tcsetattr(fd, TCSAFLUSH, attrs)

infd = sys.stdin.fileno()
seqno = 0
while 1:
    try:
        rfd, wfd, xfd = select.select([ fd, infd ], [], [])
    except KeyboardInterrupt:
        print ""
        break

    if fd in rfd:
        data = os.read(fd, 4096)
        dump_raw(data, False)
        (line, status) = lg_unpack(data)
        if line:
            print make_printable(line)
        if (len(status) == 2):
            if status[0] == 0x30 and status[1] == 0x0d:
                print "OK\n"
            elif status[0] == 0x34 and status[1] == 0x0d:
                print "ERROR\n"
            elif status[0] == 0x33 and status[1] == 0x0d:
                print "ERROR\n"
            else:
                print "STAT: 0x%02x 0x%02x" % (status[0], status[1])

    if infd in rfd:
        line = os.read(infd, 512)
        if line:
            data = lg_pack(line, seqno)
            seqno += 1
            dump_raw(data, True)
            os.write(fd, data)

os.close(fd)
