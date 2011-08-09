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
# Copyright (C) 2009 - 2010Red Hat, Inc.
#

import sys, dbus

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_GSM_NETWORK='org.freedesktop.ModemManager.Modem.Gsm.Network'

gsm_act =     { 0: "(GSM)",
                1: "(GSM Compact)",
                2: "(UMTS)",
                3: "(EDGE)",
                4: "(HSDPA)",
                5: "(HSUPA)",
                6: "(HSPA)"
              }

bus = dbus.SystemBus()
objpath = sys.argv[1]
if objpath[:1] != '/':
    objpath = "/org/freedesktop/ModemManager/Modems/" + str(objpath)
proxy = bus.get_object(MM_DBUS_SERVICE, objpath)

# Properties
props = dbus.Interface(proxy, dbus_interface='org.freedesktop.DBus.Properties')

mtype = props.Get(MM_DBUS_INTERFACE_MODEM, 'Type')
if mtype == 2:
    print "CDMA modems do not support network scans"
    sys.exit(1)

print "Driver: '%s'" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'Driver'))
print "Modem device: '%s'" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'MasterDevice'))
print "Data device: '%s'" % (props.Get(MM_DBUS_INTERFACE_MODEM, 'Device'))
print ""

net = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)
print "Scanning..."
try:
    results = net.Scan(timeout=120)
except dbus.exceptions.DBusException, e:
    print "Error scanning: %s" % e
    results = {}

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
        access_tech_num = int(r['access-tech'])
        access_tech = gsm_act[access_tech_num]
    except KeyError:
        pass

    opnum = "(%s):" % r['operator-num']
    # Extra space for 5-digit MCC/MNC
    if r['operator-num'] == 5:
        opnum += " "

    if r.has_key('operator-long') and len(r['operator-long']):
        print "%s %s %s %s" % (r['operator-long'], opnum, status, access_tech)
    elif r.has_key('operator-short') and len(r['operator-short']):
        print "%s %s %s %s" % (r['operator-short'], opnum, status, access_tech)
    else:
        print "%s: %s %s" % (r['operator-num'], status, access_tech)

