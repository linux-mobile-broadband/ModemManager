#!/usr/bin/python

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
