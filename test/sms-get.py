#!/usr/bin/python
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
# Copyright (C) 2009 Novell, Inc.
# Copyright (C) 2009 - 2012 Red Hat, Inc.
#

# An example on how to read SMS messages using ModemManager

import sys
import dbus
import os

MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_SMS='org.freedesktop.ModemManager.Modem.Gsm.SMS'

arglen = len(sys.argv)
if arglen != 2 and arglen != 3:
    print "Usage: %s <modem path> [message #]" % sys.argv[0]
    sys.exit(1)

msgnum = None
if len(sys.argv) == 3:
    msgnum = int(sys.argv[2])

objpath = sys.argv[1]
if objpath[:1] != '/':
    objpath = "/org/freedesktop/ModemManager/Modems/" + str(objpath)

# Create the modem properties proxy
bus = dbus.SystemBus()
proxy = bus.get_object(MM_DBUS_SERVICE, objpath)
modem = dbus.Interface(proxy, dbus_interface="org.freedesktop.DBus.Properties")

# Make sure the modem is enabled first
if modem.Get(MM_DBUS_INTERFACE_MODEM, "Enabled") == False:
    print "Modem is not enabled"
    sys.exit(1)

# Create the SMS interface proxy
sms = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_SMS)

msgs = sms.List()
i = 0
for m in msgs:
    print "-------------------------------------------------------------------"
    smsc = ""
    try:
        smsc = m["smsc"]
    except KeyError:
        pass

    print "%d: From: %s  Time: %s  SMSC: %s" % (m["index"], m["number"], m["timestamp"], smsc)
    if len(m["text"]):
        print "   %s\n" % m["text"]
    elif len(m["data"]):
        print "   Coding: %d" % m["data-coding-scheme"]
        z = 1
        s = ""
        for c in m["data"]:
            s += "%02X " % c
            if not z % 16:
                print "   %s" % s
                s = ""
            z += 1
        if len(s):
            print "   %s" % s
    i += 1

