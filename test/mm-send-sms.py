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

if len(sys.argv) != 3:
    print "Usage: %s <number> <message>" % sys.argv[0]
    sys.exit(1)

number = sys.argv[1]
message = sys.argv[2]

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

msg_dict = dbus.Dictionary({ dbus.String('number') : dbus.String(number),
                             dbus.String('text') : dbus.String(message)
                             },
                           signature=dbus.Signature("sv"))

sms_iface = dbus.Interface(proxy, dbus_interface='org.freedesktop.ModemManager.Modem.Gsm.SMS')
try:
    sms_iface.Send(msg_dict)
except:
    print "Sending message failed"
finally:
    modem.Enable(False)
