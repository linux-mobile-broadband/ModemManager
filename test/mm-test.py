#!/usr/bin/python

import sys
import dbus

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_CDMA='org.freedesktop.ModemManager.Modem.Cdma'
MM_DBUS_INTERFACE_MODEM_GSM_CARD='org.freedesktop.ModemManager.Modem.Gsm.Card'
MM_DBUS_INTERFACE_MODEM_GSM_NETWORK='org.freedesktop.ModemManager.Modem.Gsm.Network'

def get_cdma_band_class(band_class):
    if band_class == 1:
        return "800MHz"
    elif band_class == 2:
        return "1900MHz"
    else:
        return "Unknown"

def inspect_cdma(proxy):
    cdma = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_CDMA)
    try:
        print "ESN: %s" % cdma.GetEsn()
    except dbus.exceptions.DBusException:
        pass
    print "-------------------"
    info = cdma.GetServingSystem()
    print "Class: %s" % get_cdma_band_class(info[0])
    print "Band:  %s" % info[1]
    print "SID:   %d" % info[2]


def get_gsm_network_mode(modem):
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

def get_gsm_band(modem):
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


def inspect_gsm(proxy):
    # Gsm.Card interface
    card = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_CARD)
    try:
        print "IMEI: %s" % card.GetImei()
    except dbus.exceptions.DBusException:
        pass
    print "IMSI: %s" % card.GetImsi()

    # Gsm.Network interface
    net = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)
    print "Signal quality: %d" % net.GetSignalQuality()

    print "Scanning..."
    results = net.Scan()
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

        if len(r['operator-long']):
            print "%s: %s" % (r['operator-long'], status)
        else:
            print "%s: %s" % (r['operator-short'], status)


bus = dbus.SystemBus()

# Get available modems:
manager_proxy = bus.get_object('org.freedesktop.ModemManager', '/org/freedesktop/ModemManager')
manager_iface = dbus.Interface(manager_proxy, dbus_interface='org.freedesktop.ModemManager')
modems = manager_iface.EnumerateDevices()

if not modems:
    print "No modems found"
    sys.exit(1)

for m in modems:
    proxy = bus.get_object(MM_DBUS_SERVICE, m)

    # Properties
    props_iface = dbus.Interface(proxy, dbus_interface='org.freedesktop.DBus.Properties')

    type = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Type')
    if type == 1:
        print "GSM modem"
    elif type == 2:
        print "CDMA modem"
    else:
        print "Invalid modem type: %d" % type

    print "Driver: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Driver'))
    print "Modem device: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'MasterDevice'))
    print "Data device: '%s'" % (props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Device'))

    # Modem interface
    modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)
    modem.Enable(True)

    info = modem.GetInfo()
    print "Vendor:  %s" % info[0]
    print "Model:   %s" % info[1]
    print "Version: %s" % info[2]

    if type == 1:
        inspect_gsm(proxy)
    elif type == 2:
        inspect_cdma(proxy)
    print

    modem.Enable(False)
