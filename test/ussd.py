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
# Copyright (C) 2010 Guido Guenther <agx@sigxcpu.org>
#
# Usage: ./test/ussd.py /org/freedesktop/ModemManager/Modems/0 '*130#'

import sys, dbus

MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_USSD='org.freedesktop.ModemManager.Modem.Gsm.Ussd'

bus = dbus.SystemBus()
proxy = bus.get_object(MM_DBUS_SERVICE, sys.argv[1])
modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_USSD)
ret = modem.Initiate (sys.argv[2])
print ret
