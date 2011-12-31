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
import struct
import defs

def unpack(data, direction):
    # unpack the data
    if direction == defs.TO_MODEM:
        if data[:14] == "41542a574d433d":
            # remove the AT*WMC= bits, and the newline and CRC at the end
            data = data[14:]
            if data[len(data) - 2:] == "0d":
                data = data[:len(data) - 6]
    elif direction == defs.TO_HOST:
        if data[len(data) - 2:] == "7e":
            # remove HDLC terminator and CRC
            data = data[:len(data) - 6]
    else:
        raise ValueError("No data direction")

    data = binascii.unhexlify(data)

    # PPP-unescape it
    escape = False
    new_data = ""
    for i in data:
        if ord(i) == 0x7D:
            escape = True
        elif escape == True:
            new_data += chr(ord(i) ^ 0x20)
            escape = False
        else:
            new_data += i

    return new_data

def show_data(data, prefix):
    line = ""
    for i in data:
        line += " %02x" % ord(i)
    print prefix + "  Data:  %s" % line

def show_device_info(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt = fmt + "27s"  # unknown1
    fmt = fmt + "64s"  # manf
    fmt = fmt + "64s"  # model
    fmt = fmt + "64s"  # fwrev
    fmt = fmt + "64s"  # hwrev
    fmt = fmt + "64s"  # unknown2
    fmt = fmt + "64s"  # unknown3
    fmt = fmt + "10s"  # min
    fmt = fmt + "12s"  # unknown4
    fmt = fmt + "H"    # home_sid
    fmt = fmt + "6s"   # unknown5
    fmt = fmt + "H"    # eri_ver?
    fmt = fmt + "3s"   # unknown6
    fmt = fmt + "64s"  # unknown7
    fmt = fmt + "20s"  # meid
    fmt = fmt + "22s"  # imei
    fmt = fmt + "16s"  # unknown9
    fmt = fmt + "22s"  # iccid
    fmt = fmt + "4s"   # unknown10
    fmt = fmt + "16s"  # MCC
    fmt = fmt + "16s"  # MNC
    fmt = fmt + "4s"   # unknown11
    fmt = fmt + "4s"   # unknown12
    fmt = fmt + "4s"   # unknown13
    fmt = fmt + "1s"

    expected = struct.calcsize(fmt)
    if len(data) != expected:
        raise ValueError("Unexpected Info command response len (got %d expected %d)" % (len(data), expected))
    (u1, manf, model, fwrev, hwrev, u2, u3, cdmamin, u4, homesid, u5, eriver, \
        u6, u7, meid, imei, u9, iccid, u10, mcc, mnc, u11, u12, u13, u14) = struct.unpack(fmt, data)

    print prefix + "  Manf:     %s" % manf
    print prefix + "  Model:    %s" % model
    print prefix + "  FW Rev:   %s" % fwrev
    print prefix + "  HW Rev:   %s" % hwrev
    print prefix + "  MIN:      %s" % cdmamin
    print prefix + "  Home SID: %d" % homesid
    print prefix + "  ERI Ver:  %d" % eriver
    print prefix + "  MEID:     %s" % meid
    print prefix + "  IMEI:     %s" % imei
    print prefix + "  Unk9:     %s" % u9
    print prefix + "  ICCID:    %s" % iccid
    print prefix + "  MCC:      %s" % mcc
    print prefix + "  MNC:      %s" % mnc

def show_ip_info(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt = fmt + "I"   # rx_bytes
    fmt = fmt + "I"   # tx_bytes
    fmt = fmt + "8s"  # unknown3
    fmt = fmt + "B"   # unknown4
    fmt = fmt + "7s"  # unknown7
    fmt = fmt + "16s" # ip4_address
    fmt = fmt + "8s"  # netmask?
    fmt = fmt + "40s" # ip6_address

    expected = struct.calcsize(fmt)
    if len(data) != expected:
        raise ValueError("Unexpected IP Info command response len (got %d expected %d)" % (len(data), expected))
    (rxb, txb, u3, u4, u7, ip4addr, netmask, ip6addr) = struct.unpack(fmt, data)

    print prefix + "  RX Bytes: %d" % rxb
    print prefix + "  TX Bytes: %d" % txb
    print prefix + "  IP4 Addr: %s" % ip4addr
    print prefix + "  IP6 Addr: %s" % ip6addr

def get_signal(item):
    if item == 0x7D:
        return (item * -1, "(NO SIGNAL)")
    else:
        return (item * -1, "")

def show_status(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt = fmt + "B"   # unknown1
    fmt = fmt + "3s"  # unknown2
    fmt = fmt + "B"   # unknown3
    fmt = fmt + "B"   # unknown4
    fmt = fmt + "10s" # magic
    fmt = fmt + "H"   # counter1
    fmt = fmt + "H"   # counter2
    fmt = fmt + "B"   # unknown5
    fmt = fmt + "3s"  # unknown6
    fmt = fmt + "B"   # cdma1x_dbm
    fmt = fmt + "3s"  # unknown7
    fmt = fmt + "16s" # cdma_opname
    fmt = fmt + "18s" # unknown8
    fmt = fmt + "B"   # hdr_dbm
    fmt = fmt + "3s"  # unknown9
    fmt = fmt + "B"   # unknown10
    fmt = fmt + "3s"  # unknown11
    fmt = fmt + "B"   # unknown12
    fmt = fmt + "8s"  # lte_opname
    fmt = fmt + "60s" # unknown13
    fmt = fmt + "B"   # lte_dbm
    fmt = fmt + "3s"  # unknown14
    fmt = fmt + "4s"  # unknown15

    expected = struct.calcsize(fmt)
    if len(data) != expected:
        raise ValueError("Unexpected Status command response len (got %d expected %d)" % (len(data), expected))
    (u1, u2, u3, u4, magic, counter1, counter2, u5, u6, cdma_dbm, u7, cdma_opname, \
        u8, hdr_dbm, u9, u10, u11, u12, lte_opname, u13, lte_dbm, u14, u15) = struct.unpack(fmt, data)

    print prefix + "  Counter1: %s" % counter1
    print prefix + "  Counter2: %s" % counter2
    print prefix + "  CDMA dBm: %d dBm %s" % get_signal(cdma_dbm)
    print prefix + "  CDMA Op:  %s" % cdma_opname
    print prefix + "  HDR dBm:  %d dBm %s" % get_signal(hdr_dbm)
    print prefix + "  LTE Op:   %s" % lte_opname
    print prefix + "  LTE dBm:  %d dBm %s" % get_signal(lte_dbm)

def show_init(data, prefix, direction):
    show_data(data, prefix)

def show_bearer_info(data, prefix, direction):
    pass

cmds = { 0x06: ("DEVICE_INFO", show_device_info),
         0x0A: ("IP_INFO", show_ip_info),
         0x0B: ("STATUS", show_status),
         0x0D: ("INIT", show_init),
         0x4D: ("EPS_BEARER_INFO", show_bearer_info)
       }

def show(data, prefix, direction):
    data = data[1:]  # skip 0xC8 header
    cmdno = ord(data[:1])
    try:
        cmdinfo = cmds[cmdno]
    except KeyError:
        return
    data = data[1:]  # skip cmdno

    print prefix + "WMC Packet:"
    print prefix + "  Cmd:    0x%02x (%s)" % (cmdno, cmdinfo[0])
    cmdinfo[1](data, prefix, direction)
    print ""

def get_funcs():
    return (unpack, show)

