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
import defs
import struct

from qmiprotocol import services

TP_REQUEST = 0x00
TP_RESPONSE = 0x02
TP_INDICATION = 0x04

def complete(data, direction):
    # We don't handle QMUX frames spanning packets yet
    return True

def unpack(data, direction):
    return binascii.unhexlify(data)

def service_to_string(s):
    try:
        return services[s][0]
    except KeyError:
        return ""

def qmi_cmd_to_string(cmdno, service):
    (name, cmds) = services[service]
    return cmds[cmdno][0]

class Tlv:
    def __init__(self, tlvid, size, data, service, cmdno, direction):
        self.id = tlvid
        self.size = size
        self.data = data
        if size != len(data):
            raise ValueError("Mismatched TLV size! (got %d expected %d)" % (len(data), size))
        self.service = service
        self.cmdno = cmdno
        self.direction = direction

    def show_data(self, prefix):
        line = ""
        for i in self.data:
            line += " %02x" % ord(i)
        print prefix + "  Data:  %s" % line

    def show(self, prefix):
        svc = services[self.service]
        cmd = [ None, None, None ]
        try:
            cmd = svc[1][self.cmdno]
        except KeyError:
            pass
        except TypeError:
            pass
        tlvlist = None
        if self.direction == TP_REQUEST:
            tlvlist = cmd[1]
        elif self.direction == TP_RESPONSE:
            tlvlist = cmd[2]
        elif self.direction == TP_INDICATION:
            tlvlist = cmd[3]
        else:
            raise ValueError("Unknown TLV direction %s" % self.direction)

        tlvname = "!!! UNKNOWN !!!"
        if self.service == 1 and self.cmdno == 77: # WDS/SET_IP_FAMILY
            tlvname = "WDS/Set IP Family/IP Family !!! NOT DEFINED !!!"
        else:
            try:
                tlvname = tlvlist[self.id]
            except KeyError:
                pass
            except TypeError:
                pass

        print prefix + "  TLV:    0x%02x (%s)" % (self.id, tlvname)
        print prefix + "  Size:   0x%04x" % self.size
        if self.id == 2:
            # Status response
            (status, error) = struct.unpack("<HH", self.data)
            if status == 0:
                sstatus = "SUCCESS"
            else:
                sstatus = "ERROR"
            print prefix + "  Status: %d (%s)" % (status, sstatus)

            print prefix + "  Error:  %d" % error
        else:
            self.show_data(prefix)
        print ""

def get_tlvs(data, service, cmdno, direction):
    tlvs = []
    while len(data) >= 3:
        (tlvid, size) = struct.unpack("<BH", data[:3])
        if size > len(data) - 3:
            raise ValueError("Malformed TLV ID %d size %d (len left %d)" % (tlvid, size, len(data)))
        tlvs.append(Tlv(tlvid, size, data[3:3 + size], service, cmdno, direction))
        data = data[size + 3:]
    if len(data) != 0:
        raise ValueError("leftover data parsing tlvs")
    return tlvs

def show(data, prefix, direction):
    if len(data) < 7:
        return

    qmuxfmt = "<BHBBB"
    sz = struct.calcsize(qmuxfmt)
    (ifc, l, sender, service, cid) = struct.unpack(qmuxfmt, data[:sz])

    if ifc != 0x01:
        raise ValueError("Packet not QMUX")

    print prefix + "QMUX Header:"
    print prefix + "  len:    0x%04x" % l

    ssender = ""
    if sender == 0x00:
        ssender = "(client)"
    elif sender == 0x80:
        ssender = "(service)"
    print prefix + "  sender: 0x%02x %s" % (sender, ssender)

    sservice = service_to_string(service)
    print prefix + "  svc:    0x%02x (%s)" % (service, sservice)

    scid = ""
    if cid == 0xff:
        scid = "(broadcast)"
    print prefix + "  cid:    0x%02x %s" % (cid, scid)

    print ""

    # QMI header
    data = data[sz:]
    if service == 0:
        qmifmt = "<BBHH"
    else:
        qmifmt = "<BHHH"

    sz = struct.calcsize(qmifmt)
    (flags, txnid, cmdno, size) = struct.unpack(qmifmt, data[:sz])

    print prefix + "QMI Header:"

    sflags = ""
    if service == 0:
        # Besides the CTL service header being shorter, the flags are different
        if flags == 0x00:
            flags = TP_REQUEST
        elif flags == 0x01:
            flags = TP_RESPONSE
        elif flags == 0x02:
            flags = TP_INDICATION

    if flags == TP_REQUEST:
        sflags = "(request)"
    elif flags == TP_RESPONSE:
        sflags = "(response)"
    elif flags == TP_INDICATION:
        sflags = "(indication)"
    else:
        raise ValueError("Unknown flags %d" % flags)
    print prefix + "  Flags:  0x%02x %s" % (flags, sflags)

    print prefix + "  TXN:    0x%04x" % txnid

    scmd = "!!! UNKNOWN !!!"
    try:
        scmd = qmi_cmd_to_string(cmdno, service)
    except KeyError:
        pass
    except TypeError:
        pass
    print prefix + "  Cmd:    0x%04x (%s)" % (cmdno, scmd)

    print prefix + "  Size:   0x%04x" % size
    print ""

    data = data[sz:]
    tlvs = get_tlvs(data, service, cmdno, flags)
    for tlv in tlvs:
        tlv.show(prefix)

    print ""

def get_funcs():
    return (complete, unpack, show)

