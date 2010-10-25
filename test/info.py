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

import sys, dbus

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

def cdma_inspect(proxy, props):
    cdma = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_CDMA)

    esn = cdma.GetEsn()
    print "ESN: %s" % esn

    try:
        (cdma_1x_state, evdo_state) = cdma.GetRegistrationState()
        print "1x State:   %s" % get_reg_state (cdma_1x_state)
        print "EVDO State: %s" % get_reg_state (evdo_state)
    except dbus.exceptions.DBusException, e:
        print "Error reading registration state: %s" % e

    try:
        quality = cdma.GetSignalQuality()
        print "Signal quality: %d" % quality
    except dbus.exceptions.DBusException, e:
        print "Error reading signal quality: %s" % e

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


mm_allowed = { 0: "any",
               1: "2G preferred",
               2: "3G preferred",
               3: "2G only",
               4: "3G only"
             }

mm_act =     { 0: "unknown",
               1: "GSM",
               2: "GSM Compact",
               3: "GPRS",
               4: "EDGE",
               5: "UMTS",
               6: "HSDPA",
               7: "HSUPA",
               8: "HSPA"
             }

mm_reg = { 0: "idle",
           1: "home",
           2: "searching",
           3: "denied",
           4: "unknown",
           5: "roaming"
         }

def gsm_inspect(proxy, props):
    # Gsm.Card interface
    card = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_CARD)

    simid = "<unavailable>"
    try:
        simid = props.Get(MM_DBUS_INTERFACE_MODEM_GSM_CARD, "SimIdentifier")
    except dbus.exceptions.DBusException:
        pass
    print "SIM ID: %s" % simid

    imei = "<unavailable>"
    try:
        imei = card.GetImei()
    except dbus.exceptions.DBusException:
        pass
    print "IMEI: %s" % imei

    imsi = "<unavailable>"
    try:
        imsi = card.GetImsi()
    except dbus.exceptions.DBusException:
        pass
    print "IMSI: %s" % imsi

    opid = "<unavailable>"
    try:
        opid = card.GetOperatorId()
    except dbus.exceptions.DBusException:
        pass
    print "Operator ID: %s" % opid

    # Gsm.Network interface
    net = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)
    try:
        quality = net.GetSignalQuality()
        print "Signal quality: %d" % quality
    except dbus.exceptions.DBusException, e:
        print "Error reading signal quality: %s" % e

    try:
        reg = net.GetRegistrationInfo()
        print "Reg status: %s (%s, '%s')" % (mm_reg[int(reg[0])], reg[1], reg[2])
    except dbus.exceptions.DBusException, e:
        print "Error reading registration: %s" % e

    try:
        allowed = props.Get(MM_DBUS_INTERFACE_MODEM_GSM_NETWORK, "AllowedMode")
        print "Allowed mode: %s" % mm_allowed[allowed]
    except dbus.exceptions.DBusException, e:
        print "Error reading allowed mode: %s" % e

    try:
        act = props.Get(MM_DBUS_INTERFACE_MODEM_GSM_NETWORK, "AccessTechnology")
        print "Access Tech: %s" % mm_act[act]
    except dbus.exceptions.DBusException, e:
        print "Error reading current access technology: %s" % e



bus = dbus.SystemBus()
bus = dbus.SystemBus()
proxy = bus.get_object(MM_DBUS_SERVICE, sys.argv[1])

# Properties
props = dbus.Interface(proxy, dbus_interface='org.freedesktop.DBus.Properties')

mtype = props.Get(MM_DBUS_INTERFACE_MODEM, 'Type')
if mtype == 1:
    print "Type: GSM"
elif mtype == 2:
    print "Type: CDMA"

print "Driver: %s" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'Driver'))
print "Modem device: %s" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'MasterDevice'))
print "Data device: %s" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'Device'))
print "Device ID: %s" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'DeviceIdentifier'))
print ""

modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)
info = modem.GetInfo()
print "Vendor:  %s" % info[0]
print "Model:   %s" % info[1]
print "Version: %s" % info[2]
print ""

if mtype == 1:
    gsm_inspect(proxy, props)
elif mtype == 2:
    cdma_inspect(proxy, props)

