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
#

# An example on how to send an SMS message using ModemManager

import sys
import dbus
import os

arglen = len(sys.argv)
if arglen != 4 and arglen != 6 and arglen != 8:
    print "Usage: %s --number <number> [--smsc <smsc>] [--validity <minutes>] <message>" % sys.argv[0]
    sys.exit(1)

number = None
validity = None
smsc = None
message = None
x = 1
while x < arglen - 1:
    if sys.argv[x] == "--number":
        x += 1
        number = sys.argv[x].strip()
    elif sys.argv[x] == "--validity":
        x += 1
        validity = int(sys.argv[x])
    elif sys.argv[x] == "--smsc":
        x += 1
        smsc = sys.argv[x].strip()
    else:
        raise ValueError("Unknown option '%s'" % sys.argv[x])
    x += 1

try:
    lang = os.getenv("LANG")
    idx = lang.find(".")
    if idx != -1:
        lang = lang[idx + 1:]
except KeyError:
    lang = "utf-8"
message = unicode(sys.argv[arglen - 1], "utf-8")


bus = dbus.SystemBus()

manager_proxy = bus.get_object('org.freedesktop.ModemManager', '/org/freedesktop/ModemManager')
manager_iface = dbus.Interface(manager_proxy, dbus_interface='org.freedesktop.ModemManager')
modems = manager_iface.EnumerateDevices()
if len(modems) == 0:
    print "No modems found"
    sys.exit(1)

proxy = bus.get_object('org.freedesktop.ModemManager', modems[0])
modem = dbus.Interface(proxy, dbus_interface='org.freedesktop.ModemManager.Modem')
modem.Enable(True)

msg_dict = dbus.Dictionary(
    {
        dbus.String('number') : dbus.String(number),
        dbus.String('text') : dbus.String(message)
    },
    signature=dbus.Signature("sv")
)

if smsc:
    msg_dict[dbus.String('smsc')] = dbus.String(smsc)

if validity:
    msg_dict[dbus.String('validity')] = dbus.UInt32(validity)

sms_iface = dbus.Interface(proxy, dbus_interface='org.freedesktop.ModemManager.Modem.Gsm.SMS')
try:
    indexes = sms_iface.Send(msg_dict)
    print "Message index: %d" % indexes[0]
except Exception, e:
    print "Sending message failed: %s" % e

