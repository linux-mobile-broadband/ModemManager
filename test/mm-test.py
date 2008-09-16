#!/usr/bin/python

import sys
import dbus

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_GSM_CARD='org.freedesktop.ModemManager.Modem.Gsm.Card'
MM_DBUS_INTERFACE_MODEM_GSM_NETWORK='org.freedesktop.ModemManager.Modem.Gsm.Network'

bus = dbus.SystemBus()
manager_proxy = bus.get_object(MM_DBUS_SERVICE, MM_DBUS_PATH)
manager_iface = dbus.Interface(manager_proxy, dbus_interface=MM_DBUS_INTERFACE)

def enumerate_devices(manager):
    modems = manager_iface.EnumerateDevices()
    if not modems:
        print "No modems found."
        return

    for modem_path in modems:
        proxy = bus.get_object(MM_DBUS_SERVICE, modem_path)
        modem = dbus.Interface(proxy, dbus_interface=DBUS_INTERFACE_PROPERTIES)

        type = modem.Get(MM_DBUS_INTERFACE_MODEM, "Type")
        if type == 1:
            type = "GSM"
        elif type == 2:
            type = "CDMA"
        else:
            type = "(Unknown)"

        print "%s\t%s\t%s" % (modem.Get(MM_DBUS_INTERFACE_MODEM, "DataDevice"),
                              modem.Get(MM_DBUS_INTERFACE_MODEM, "Driver"),
                              type)

def get_modem(manager):
    modems = manager_iface.EnumerateDevices()
    if not modems:
        print "No modems found."
        sys.exit(1)
    dev_proxy = bus.get_object(MM_DBUS_SERVICE, modems[0])
    modem = dbus.Interface(dev_proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)

    need_pin = False
    try:
        modem.Enable(True)
    except dbus.exceptions.DBusException, e:
        need_pin = True

    if need_pin:
        modem.SetPin("1234")

    return modem


def scan(modem):
    results = modem.Scan()
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

        print "%s: %s" % (r['operator-long'], status)

def get_quality(modem):
    modem.Enable(True)
    print "Signal Quality: %d%%" % modem.GetSignalQuality()

def get_network_mode(modem):
    mode = modem.GetNetworkMode()
    if mode == 1:
        mode = "GPRS"
    elif mode == 2:
        mode = "EDGE"
    elif mode == 3:
        mode = "3G"
    elif mode == 4:
        mode = "HSDPA"
    else:
        mode = "(Unknown)"

    print "Mode: %s" % mode

def get_band(modem):
    band = modem.GetBand()
    if band == 0:
        band = "Any"
    elif band == 1:
        band = "EGSM (900 MHz)"
    elif band == 2:
        band = "DCS (1800 MHz)"
    elif band == 3:
        band = "PCS (1900 MHz)"
    elif band == 4:
        band = "G850 (850 MHz)"
    elif band == 5:
        band = "U2100 (WCSMA 2100 MHZ, Class I)"
    elif band == 6:
        band = "U1700 (WCDMA 3GPP UMTS1800 MHz, Class III)"
    elif band == 7:
        band = "17IV (WCDMA 3GPP AWS 1700/2100 MHz, Class IV)"
    elif band == 8:
        band = "U800 (WCDMA 3GPP UMTS800 MHz, Class VI)"
    elif band == 9:
        band = "U850 (WCDMA 3GPP UMT850 MHz, Class V)"
    elif band == 10:
        band = "U900 (WCDMA 3GPP UMTS900 MHz, Class VIII)"
    elif band == 11:
        band = "U17IX (WCDMA 3GPP UMTS MHz, Class IX)"
    else:
        band = "(Unknown)"

    print "Band: %s" % band

def connect(modem):

    modem.Register("")
    modem.Connect('"*99#', '')

if '--list' in sys.argv:
    enumerate_devices(manager_iface)
    sys.exit()

modem = get_modem(manager_iface)

if '--scan' in sys.argv:
    scan(modem)
elif '--quality' in sys.argv:
    get_quality(modem)
elif '--mode' in sys.argv:
    get_network_mode(modem)
elif '--band' in sys.argv:
    get_band(modem)
elif '--disconnect' in sys.argv:
    modem = get_modem(manager_iface)
    modem.Disconnect()
elif '--connect' in sys.argv:
    connect(modem)

modem.Enable(False)
