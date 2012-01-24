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

def complete(data, direction):
    if direction == defs.TO_MODEM:
        if data[len(data) - 2:] == "0d" or data[len(data) - 2:] == "7e":
            return True
    elif direction == defs.TO_HOST:
        if data[len(data) - 6:] == "30307e":
            # UML190 and UML290 fake CRC + term
            return True
        elif data[len(data) - 2:] == "7e":
            # PC5740 uses a real CRC
            return True
    else:
        raise ValueError("No data direction")
    return False


def unpack(data, direction):
    # unpack the data
    if direction == defs.TO_MODEM:
        if data[:14] == "41542a574d433d":
            # remove the AT*WMC= bits, and the newline and CRC at the end
            data = data[14:]
            if data[len(data) - 2:] == "0d":
                data = data[:len(data) - 6]
        elif data[:2] == "c8" and data[len(data) - 2:] == "7e":
            # PC5740 doesn't use AT*WMC= framing
            data = data[:len(data) - 6]
        else:
            print "asdfasdfasfaf"
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
    fmt += "27s"  # unknown1
    fmt += "64s"  # manf
    fmt += "64s"  # model
    fmt += "64s"  # fwrev
    fmt += "64s"  # hwrev
    fmt += "64s"  # unknown2
    fmt += "64s"  # unknown3
    fmt += "10s"  # min
    fmt += "12s"  # unknown4
    fmt += "H"    # home_sid
    fmt += "2s"   # unknown5
    fmt += "H"    # prlver
    fmt += "2s"   # unknown6
    fmt += "H"    # eriver
    fmt += "4s"   # unknown7

    expected = struct.calcsize(fmt)
    if len(data) >= expected:
        (u1, manf, model, fwrev, hwrev, u2, u3, cdmamin, u4, homesid, u5, prlver, \
         u6, eriver, u7) = struct.unpack(fmt, data[:expected])
        print prefix + "  Manf:     %s" % manf
        print prefix + "  Model:    %s" % model
        print prefix + "  FW Rev:   %s" % fwrev
        print prefix + "  HW Rev:   %s" % hwrev
        print prefix + "  MIN:      %s" % cdmamin
        print prefix + "  Home SID: %d" % homesid
        print prefix + "  PRL Ver:  %d" % prlver
        print prefix + "  ERI Ver:  %d" % eriver
    else:
        raise ValueError("Unexpected Info command response len (got %d expected %d)" % (len(data), expected))

    fmt2 = "<"
    fmt2 += "64s"  # unknown8
    fmt2 += "14s"  # meid
    fmt2 += "6s"   # unknown10
    fmt2 += "16s"  # imei
    fmt2 += "6s"   # unknown11
    fmt2 += "16s"  # unknown12
    fmt2 += "20s"  # iccid
    fmt2 += "6s"   # unknown13

    expected2 = struct.calcsize(fmt2)
    if len(data) >= expected + expected2:
        (u8, meid, u10, imei, u11, something, iccid, u13) = struct.unpack(fmt2, data[expected:expected + expected2])
        print prefix + "  MEID:     %s" % meid
        print prefix + "  IMEI:     %s" % imei
        print prefix + "  ??? :     %s" % something
        print prefix + "  ICCID:    %s" % iccid

    fmt3 = "<"
    fmt3 += "16s"  # MCC
    fmt3 += "16s"  # MNC
    fmt3 += "4s"   # unknown11
    fmt3 += "4s"   # unknown12
    fmt3 += "4s"   # unknown13
    expected3 = struct.calcsize(fmt3)
    if len(data) >= expected + expected2 + expected3:
        (mcc, mnc, u11, u12, u13) = struct.unpack(fmt3, data[expected + expected2:])
        print prefix + "  MCC:      %s" % mcc
        print prefix + "  MNC:      %s" % mnc


def state_to_string(state):
    states = { 0: "unknown",
               1: "idle",
               2: "connecting",
               3: "authenticating",
               4: "connected",
               5: "dormant",
               6: "updating NAM",
               7: "updating PRL",
               8: "disconnecting",
               9: "error",
              10: "updating UICC",
              11: "updating PLMN" }
    try:
        return states[state]
    except KeyError:
        return "unknown"

def show_connection_info(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt += "I"   # rx_bytes
    fmt += "I"   # tx_bytes
    fmt += "8s"  # unknown1
    fmt += "B"   # state
    fmt += "3s"  # unknown2

    expected = struct.calcsize(fmt)
    if len(data) >= expected:
        (rxb, txb, u1, state, u2) = struct.unpack(fmt, data[:expected])
        print prefix + "  RX Bytes: %d" % rxb
        print prefix + "  TX Bytes: %d" % txb
        print prefix + "  State:    %d (%s)" % (state, state_to_string (state))
    else:
        raise ValueError("Unexpected Connection Info command response len (got %d expected %d)" % (len(data), expected))

    fmt3 = "<"
    fmt3 += "4s"  # unknown3
    fmt3 += "16s" # ip4_address
    fmt3 += "8s"  # netmask?
    fmt3 += "40s" # ip6_address
    expected3 = struct.calcsize(fmt3)
    if len(data) >= expected + expected3:
        (u3, ip4addr, netmask, ip6addr) = struct.unpack(fmt3, data[expected:])
        print prefix + "  IP4 Addr: %s" % ip4addr
        print prefix + "  IP6 Addr: %s" % ip6addr

def get_signal(item):
    if item == 0x7D:
        return (item * -1, "(NO SIGNAL)")
    else:
        return (item * -1, "")

def service_to_string(service):
    services = { 0: "none",
                 1: "AMPS",
                 2: "IS95-A",
                 3: "IS95-B",
                 4: "GSM",
                 5: "GPRS",
                 6: "1xRTT",
                 7: "EVDO r0",
                 8: "UMTS",
                 9: "EVDO rA",
                10: "EDGE",
                11: "HSDPA",
                12: "HSUPA",
                13: "HSPA",
                14: "LTE",
                15: "EVDO rA eHRPD" }
    try:
        return services[service]
    except KeyError:
        return "unknown"

def show_network_info(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt += "B"   # unknown1
    fmt += "3s"  # unknown2
    fmt += "B"   # service
    fmt += "B"   # unknown3
    fmt += "H"   # year
    fmt += "B"   # month
    fmt += "B"   # zero
    fmt += "B"   # day
    fmt += "B"   # zero
    fmt += "B"   # hours
    fmt += "B"   # zero
    fmt += "B"   # minutes
    fmt += "B"   # zero
    fmt += "B"   # seconds
    fmt += "H"   # counter1
    fmt += "H"   # unknown4
    fmt += "3s"  # unknown5
    fmt += "B"   # 2g_dbm

    expected = struct.calcsize(fmt)
    if len(data) >= expected:
        (u1, u2, service, u3, year, month, z1, day, z2, hours, z3, minutes, z4, \
         seconds, counter1, u4, u5, two_g_dbm) = struct.unpack(fmt, data[:expected])
        print prefix + "  Time:     %04d/%02d/%02d %02d:%02d:%02d" % (year, month, day, hours, minutes, seconds)
        print prefix + "  Service:  %d (%s)" % (service, service_to_string (service))
        print prefix + "  2G dBm:   %d dBm %s" % get_signal(two_g_dbm)
    else:
        raise ValueError("Unexpected Network Info command response len (got %d expected %d)" % (len(data), expected))

    fmt2 = "<"
    fmt2 += "3s"  # unknown7
    fmt2 += "16s" # cdma_opname
    fmt2 += "18s" # unknown8
    fmt2 += "B"   # 3g_dbm
    fmt2 += "3s"  # unknown9
    fmt2 += "B"   # unknown10
    fmt2 += "3s"  # unknown11
    fmt2 += "B"   # unknown12
    fmt2 += "8s"  # 3gpp_opname
    fmt2 += "4s"  # unknown13
    fmt2 += "I"   # unknown14
    fmt2 += "I"   # unknown15
    fmt2 += "44s" # unknown16
    fmt2 += "I"   # mcc/mnc

    expected2 = struct.calcsize(fmt2)
    if len(data) >= expected + expected2:
        (u7, cdma_opname, u8, three_g_dbm, u9, u10, u11, u12, tgpp_opname, u13, \
         u14, u15, u16, mccmnc) = struct.unpack(fmt2, data[expected:expected + expected2])
        print prefix + "  3G dBm:   %d dBm %s" % get_signal(three_g_dbm)
        print prefix + "  CDMA Op:  %s" % cdma_opname
        print prefix + "  3GPP Op:  %s" % tgpp_opname

        # handle 2-digit MNC
        if mccmnc < 100000:
           mccmnc *= 10;

        mcc = mccmnc / 1000
        mnc = mccmnc - (mcc * 1000)
        if mcc > 100:
            print prefix + "  MCC/MNC:  %u-%u" % (mcc, mnc)

    fmt3 = "<"
    fmt3 += "B"   # lte_dbm
    fmt3 += "3s"  # unknown15
    fmt3 += "4s"  # unknown16
    expected3 = struct.calcsize(fmt3)
    if len(data) >= expected + expected2 + expected3:
        (lte_dbm, u17, u18) = struct.unpack(fmt3, data[expected + expected2:])
        print prefix + "  LTE dBm:  %d dBm %s" % get_signal(lte_dbm)


def show_init(data, prefix, direction):
    if len(data) == 0:
        # PC5740/old format
        return

    if direction == defs.TO_HOST:
        show_data(data, prefix)
        return

    fmt = "<"
    fmt += "H"  # year
    fmt += "B"  # month
    fmt += "B"  # zero
    fmt += "B"  # day
    fmt += "B"  # zero
    fmt += "B"  # hours
    fmt += "B"  # zero
    fmt += "B"  # minutes
    fmt += "B"  # zero
    fmt += "B"  # seconds
    expected = struct.calcsize(fmt)
    if len(data) >= expected:
        (year, month, z1, day, z2, hours, z3, minutes, z4, seconds) = struct.unpack(fmt, data[:expected])
        print prefix + "  Time:  %04d/%02d/%02d %02d:%02d:%02d" % (year, month, day, hours, minutes, seconds)
    else:
        raise ValueError ("Unexpected Init command length (got %d expected %d)" % (len(data), expected))

def show_bearer_info(data, prefix, direction):
    pass

def mode_to_string(mode):
    if mode == 0x00:
        return "CDMA/EVDO"
    elif mode == 0x01:
        return "CDMA only"
    elif mode == 0x02:
        return "EVDO only"
    elif mode == 0x0A:
        return "GSM/UMTS"
    elif mode == 0x0B:
        return "GSM/GPRS/EDGE only"
    elif mode == 0x0C:
        return "UMTS/HSPA only"
    elif mode == 0x14:
        return "Auto"
    return "unknown"

def show_get_global_mode(data, prefix, direction):
    if direction != defs.TO_HOST:
        return

    fmt = "<"
    fmt += "B"   # unknown1
    fmt += "B"   # mode
    fmt += "B"   # unknown2
    fmt += "B"   # unknown3

    expected = struct.calcsize(fmt)
    if len(data) != expected:
        raise ValueError("Unexpected GET_GLOBAL_MODE command response len (got %d expected %d)" % (len(data), expected))
    (u1, mode, u2, u3) = struct.unpack(fmt, data)

    print prefix + "  Mode:   0x%X (%s)" % (mode, mode_to_string(mode))

def show_set_global_mode(data, prefix, direction):
    if direction != defs.TO_MODEM:
        return;

    fmt = "<"
    fmt += "B"   # unknown1
    fmt += "B"   # mode
    fmt += "B"   # unknown2
    fmt += "B"   # unknown3

    expected = struct.calcsize(fmt)
    if len(data) != expected:
        raise ValueError("Unexpected SET_GLOBAL_MODE command response len (got %d expected %d)" % (len(data), expected))
    (u1, mode, u2, u3) = struct.unpack(fmt, data)

    print prefix + "  Mode:   0x%X (%s)" % (mode, mode_to_string(mode))


cmds = { 0x03: ("GET_GLOBAL_MODE", show_get_global_mode),
         0x04: ("SET_GLOBAL_MODE", show_set_global_mode),
         0x06: ("DEVICE_INFO", show_device_info),
         0x0A: ("CONNECTION_INFO", show_connection_info),
         0x0B: ("NETWORK_INFO", show_network_info),
         0x0D: ("INIT", show_init),
         0x4D: ("EPS_BEARER_INFO", show_bearer_info)
       }

def show(data, prefix, direction):
    if ord(data[:1]) != 0xC8:
        return

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
    return (complete, unpack, show)

