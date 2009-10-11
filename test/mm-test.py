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
# Copyright (C) 2008 Novell, Inc.
# Copyright (C) 2009 Red Hat, Inc.
#

import sys
import dbus

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_CDMA='org.freedesktop.ModemManager.Modem.Cdma'
MM_DBUS_INTERFACE_MODEM_GSM_CARD='org.freedesktop.ModemManager.Modem.Gsm.Card'
MM_DBUS_INTERFACE_MODEM_GSM_NETWORK='org.freedesktop.ModemManager.Modem.Gsm.Network'

def get_cdma_band_class(band_class):
    if band_class == 1:
        return "800MHz"
    elif band_class == 2:
        return "1900MHz"
    else:
        return "Unknown"

def get_reg_state(state):
    if state == 1:
        return "registered (roaming unknown)"
    elif state == 2:
        return "registered on home network"
    elif state == 3:
        return "registered on roaming network"
    else:
        return "unknown"

def inspect_cdma(proxy, dump_private):
    cdma = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_CDMA)

    esn = "<private>"
    if dump_private:
        try:
            esn = cdma.GetEsn()
        except dbus.exceptions.DBusException:
            esn = "<unavailable>"

    print ""
    print "ESN: %s" % esn

    try:
        state = cdma.GetRegistrationState()
        print "Registration: %s" % get_reg_state (state)
    except dbus.exceptions.DBusException, e:
        print "Error reading registration state: %s" % e

    try:
        info = cdma.GetServingSystem()
        print "Class: %s" % get_cdma_band_class(info[0])
        print "Band:  %s" % info[1]
        print "SID:   %d" % info[2]
    except dbus.exceptions.DBusException, e:
        print "Error reading serving system: %s" % e


def get_gsm_network_mode(modem):
    mode = modem.GetNetworkMode()
    if mode == 0x0:
        mode = "Unknown"
    elif mode == 0x1:
        mode = "Any"
    elif mode == 0x2:
        mode = "GPRS"
    elif mode == 0x4:
        mode = "EDGE"
    elif mode == 0x8:
        mode = "UMTS"
    elif mode == 0x10:
        mode = "HSDPA"
    elif mode == 0x20:
        mode = "2G Preferred"
    elif mode == 0x40:
        mode = "3G Preferred"
    elif mode == 0x80:
        mode = "2G Only"
    elif mode == 0x100:
        mode = "3G Only"
    elif mode == 0x200:
        mode = "HSUPA"
    elif mode == 0x400:
        mode = "HSPA"
    else:
        mode = "(Unknown)"

    print "Mode: %s" % mode

def get_gsm_band(modem):
    band = modem.GetBand()
    if band == 0x0:
        band = "Unknown"
    elif band == 0x1:
        band = "Any"
    elif band == 0x2:
        band = "EGSM (900 MHz)"
    elif band == 0x4:
        band = "DCS (1800 MHz)"
    elif band == 0x8:
        band = "PCS (1900 MHz)"
    elif band == 0x10:
        band = "G850 (850 MHz)"
    elif band == 0x20:
        band = "U2100 (WCSMA 2100 MHZ, Class I)"
    elif band == 0x40:
        band = "U1700 (WCDMA 3GPP UMTS1800 MHz, Class III)"
    elif band == 0x80:
        band = "17IV (WCDMA 3GPP AWS 1700/2100 MHz, Class IV)"
    elif band == 0x100:
        band = "U800 (WCDMA 3GPP UMTS800 MHz, Class VI)"
    elif band == 0x200:
        band = "U850 (WCDMA 3GPP UMT850 MHz, Class V)"
    elif band == 0x400:
        band = "U900 (WCDMA 3GPP UMTS900 MHz, Class VIII)"
    elif band == 0x800:
        band = "U17IX (WCDMA 3GPP UMTS MHz, Class IX)"
    else:
        band = "(invalid)"

    print "Band: %s" % band


def inspect_gsm(proxy, dump_private):
    # Gsm.Card interface
    card = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_CARD)

    imei = "<private>"
    imsi = "<private>"
    if dump_private:
        try:
            imei = card.GetImei()
        except dbus.exceptions.DBusException:
            imei = "<unavailable>"
        try:
            imsi = card.GetImsi()
        except dbus.exceptions.DBusException:
            imsi = "<unavailable>"

    print "IMEI: %s" % imei
    print "IMSI: %s" % imsi

    # Gsm.Network interface
    net = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)
    print "Signal quality: %d" % net.GetSignalQuality()

    print "Scanning..."
    results = net.Scan(timeout=120)
    for r in results:
        status = r['status']
        if status == "1":
            status = "available"
        elif status == "2":
            status = "current"
        elif status == "3":
            status = "forbidden"
        else:
            status = "(Unknown)"

        access_tech = ""
        try:
            access_tech_num = r['access-tech']
            if access_tech_num == "0":
                access_tech = "(GSM)"
            elif access_tech_num == "1":
                access_tech = "(Compact GSM)"
            elif access_tech_num == "2":
                access_tech = "(UMTS)"
            elif access_tech_num == "3":
                access_tech = "(EDGE)"
            elif access_tech_num == "4":
                access_tech = "(HSDPA)"
            elif access_tech_num == "5":
                access_tech = "(HSUPA)"
            elif access_tech_num == "6":
                access_tech = "(HSPA)"
        except KeyError:
            pass

        if len(r['operator-long']):
            print "%s: %s %s" % (r['operator-long'], status, access_tech)
        else:
            print "%s: %s %s" % (r['operator-short'], status, access_tech)


dump_private = False
if len(sys.argv) == 2:
    if sys.argv[1] == "--private":
        dump_private = True

bus = dbus.SystemBus()

# Get available modems:
manager_proxy = bus.get_object('org.freedesktop.ModemManager', '/org/freedesktop/ModemManager')
manager_iface = dbus.Interface(manager_proxy, dbus_interface='org.freedesktop.ModemManager')
modems = manager_iface.EnumerateDevices()

if not modems:
    print "No modems found"
    sys.exit(1)

for m in modems:
    proxy = bus.get_object(MM_DBUS_SERVICE, m)

    # Properties
    props_iface = dbus.Interface(proxy, dbus_interface='org.freedesktop.DBus.Properties')

    type = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Type')
    if type == 1:
        print "GSM modem"
    elif type == 2:
        print "CDMA modem"
    else:
        print "Invalid modem type: %d" % type

    print "Driver: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Driver'))
    print "Modem device: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'MasterDevice'))
    print "Data device: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Device'))

    # Modem interface
    modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)
    modem.Enable(True)

    info = modem.GetInfo()
    print "Vendor:  %s" % info[0]
    print "Model:   %s" % info[1]
    print "Version: %s" % info[2]

    if type == 1:
        inspect_gsm(proxy, dump_private)
    elif type == 2:
        inspect_cdma(proxy, dump_private)
    print

    modem.Enable(False)
