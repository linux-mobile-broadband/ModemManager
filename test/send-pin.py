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
# Copyright (C) 2009 - 2010 Red Hat, Inc.
#

import sys, dbus, os

MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
DBUS_INTERFACE_PROPS='org.freedesktop.DBus.Properties'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_GSM_CARD='org.freedesktop.ModemManager.Modem.Gsm.Card'

if len(sys.argv) != 3:
    print "Usage: <modem path> <pin>"
    os._exit(1)
if not len(sys.argv[2]) in range(4,9):
    print "PIN must be between 4 or 8 characters inclusive"
    os._exit(1)
if not sys.argv[2].isdigit():
    print "PIN must be numeric"
    os._exit(1)

bus = dbus.SystemBus()
proxy = bus.get_object(MM_DBUS_SERVICE, sys.argv[1])
props = dbus.Interface(proxy, dbus_interface=DBUS_INTERFACE_PROPS)
req = props.Get(MM_DBUS_INTERFACE_MODEM, "UnlockRequired")
if req == "":
    print "SIM unlocked"
    os._exit(0)

print "Unlock Required: %s" % req
if req != "sim-pin":
    print "Only sim-pin unlock supported for now"
    os._exit(1)

# Unlock the SIM
print "Unlocking with PIN"
card = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_GSM_CARD)
try:
    card.SendPin(sys.argv[2])
except Exception, e:
    print "Unlock failed: %s" % e
    os._exit(1)

# Check to make sure it actually got unlocked
req = props.Get(MM_DBUS_INTERFACE_MODEM, "UnlockRequired")
if req != "":
    print "Unlock not successful: %s" % req
    os._exit(1)

print "Unlock successful"
os._exit(0)

