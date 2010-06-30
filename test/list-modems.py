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
# Copyright (C) 2009 - 2010 Red Hat, Inc.
#

import sys, dbus

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'

bus = dbus.SystemBus()

# Get available modems:
manager_proxy = bus.get_object(MM_DBUS_SERVICE, MM_DBUS_PATH)
manager_iface = dbus.Interface(manager_proxy, dbus_interface=MM_DBUS_INTERFACE)
modems = manager_iface.EnumerateDevices()

if not modems:
    print "No modems found"
    sys.exit(1)

for m in modems:
    proxy = bus.get_object(MM_DBUS_SERVICE, m)

    # Properties
    props_iface = dbus.Interface(proxy, dbus_interface=DBUS_INTERFACE_PROPERTIES)

    driver = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Driver')
    mtype = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Type')
    device = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'MasterDevice')

    strtype = ""
    if mtype == 1:
        strtype = "GSM"
    elif mtype == 2:
        strtype = "CDMA"

    print "%s (%s [%s], device %s)" % (m, strtype, driver, device)

