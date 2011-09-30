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

import gobject, sys, dbus
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)

MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'

def modemAdded(modem_path):
    proxy = bus.get_object(MM_DBUS_SERVICE, modem_path)
    modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)
    modem.Enable (True)

bus = dbus.SystemBus()

manager_proxy = bus.get_object(MM_DBUS_SERVICE, MM_DBUS_PATH)
manager_iface = dbus.Interface(manager_proxy, dbus_interface=MM_DBUS_INTERFACE)

# Enable modems that are already known
for m in manager_iface.EnumerateDevices():
    modemAdded(m)

# Listen for new modems
manager_iface.connect_to_signal("DeviceAdded", modemAdded)

# Start the mainloop and listen 
loop = gobject.MainLoop()
try:
    loop.run()
except KeyboardInterrupt:
    pass
sys.exit(0)
