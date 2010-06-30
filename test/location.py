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

import sys, dbus, time

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_LOCATION='org.freedesktop.ModemManager.Modem.Location'

MM_MODEM_LOCATION_CAPABILITY_UNKNOWN    = 0x00000000
MM_MODEM_LOCATION_CAPABILITY_GPS_NMEA   = 0x00000001
MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI = 0x00000002
MM_MODEM_LOCATION_CAPABILITY_GPS_RAW    = 0x00000004

bus = dbus.SystemBus()
proxy = bus.get_object(MM_DBUS_SERVICE, sys.argv[1])
modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)

props = dbus.Interface(proxy, dbus_interface=DBUS_INTERFACE_PROPERTIES)
caps = props.Get(MM_DBUS_INTERFACE_MODEM_LOCATION, "Capabilities")

print "Location Capabilities:"
if caps & MM_MODEM_LOCATION_CAPABILITY_GPS_NMEA:
    print "    GPS_NMEA"
if caps & MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI:
    print "    GSM_LAC_CI"
if caps & MM_MODEM_LOCATION_CAPABILITY_GPS_RAW:
    print "    GPS_RAW"
print ""

loc = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_LOCATION)
loc.Enable(True, True)

for i in range(0, 5):
    locations = loc.GetLocation()
    if locations.has_key(MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI):
        print "GSM_LAC_CI: %s" % str(locations[MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI])
    time.sleep(1)

loc.Enable(False, False)

