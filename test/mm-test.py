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
# Copyright (C) 2008 Novell, Inc.
# Copyright (C) 2009 Red Hat, Inc.
#

import sys, dbus, time, os, string, subprocess, socket

DBUS_INTERFACE_PROPERTIES='org.freedesktop.DBus.Properties'
MM_DBUS_SERVICE='org.freedesktop.ModemManager'
MM_DBUS_PATH='/org/freedesktop/ModemManager'
MM_DBUS_INTERFACE='org.freedesktop.ModemManager'
MM_DBUS_INTERFACE_MODEM='org.freedesktop.ModemManager.Modem'
MM_DBUS_INTERFACE_MODEM_CDMA='org.freedesktop.ModemManager.Modem.Cdma'
MM_DBUS_INTERFACE_MODEM_GSM_CARD='org.freedesktop.ModemManager.Modem.Gsm.Card'
MM_DBUS_INTERFACE_MODEM_GSM_NETWORK='org.freedesktop.ModemManager.Modem.Gsm.Network'
MM_DBUS_INTERFACE_MODEM_SIMPLE='org.freedesktop.ModemManager.Modem.Simple'

def get_cdma_band_class(band_class):
    if band_class == 1:
        return "800MHz"
    elif band_class == 2:
        return "1900MHz"
    else:
        return "Unknown"

def get_reg_state(state):
    if state == 1:
        return "registered (roaming unknown)"
    elif state == 2:
        return "registered on home network"
    elif state == 3:
        return "registered on roaming network"
    else:
        return "unknown"

def cdma_inspect(proxy, dump_private):
    cdma = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_CDMA)

    esn = "<private>"
    if dump_private:
        try:
            esn = cdma.GetEsn()
        except dbus.exceptions.DBusException:
            esn = "<unavailable>"

    print ""
    print "ESN: %s" % esn

    try:
        (cdma_1x_state, evdo_state) = cdma.GetRegistrationState()
        print "1x State:   %s" % get_reg_state (cdma_1x_state)
        print "EVDO State: %s" % get_reg_state (evdo_state)
    except dbus.exceptions.DBusException, e:
        print "Error reading registration state: %s" % e

    try:
        quality = cdma.GetSignalQuality()
        print "Signal quality: %d" % quality
    except dbus.exceptions.DBusException, e:
        print "Error reading signal quality: %s" % e

    try:
        info = cdma.GetServingSystem()
        print "Class: %s" % get_cdma_band_class(info[0])
        print "Band:  %s" % info[1]
        print "SID:   %d" % info[2]
    except dbus.exceptions.DBusException, e:
        print "Error reading serving system: %s" % e

def cdma_connect(proxy, user, password):
    # Modem.Simple interface
    simple = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_SIMPLE)
    try:
        simple.Connect({'number':"#777"}, timeout=92)
        print "\nConnected!"
        return True
    except Exception, e:
        print "Error connecting: %s" % e
    return False


def get_gsm_network_mode(modem):
    mode = modem.GetNetworkMode()
    if mode == 0x0:
        mode = "Unknown"
    elif mode == 0x1:
        mode = "Any"
    elif mode == 0x2:
        mode = "GPRS"
    elif mode == 0x4:
        mode = "EDGE"
    elif mode == 0x8:
        mode = "UMTS"
    elif mode == 0x10:
        mode = "HSDPA"
    elif mode == 0x20:
        mode = "2G Preferred"
    elif mode == 0x40:
        mode = "3G Preferred"
    elif mode == 0x80:
        mode = "2G Only"
    elif mode == 0x100:
        mode = "3G Only"
    elif mode == 0x200:
        mode = "HSUPA"
    elif mode == 0x400:
        mode = "HSPA"
    else:
        mode = "(Unknown)"

    print "Mode: %s" % mode

def get_gsm_band(modem):
    band = modem.GetBand()
    if band == 0x0:
        band = "Unknown"
    elif band == 0x1:
        band = "Any"
    elif band == 0x2:
        band = "EGSM (900 MHz)"
    elif band == 0x4:
        band = "DCS (1800 MHz)"
    elif band == 0x8:
        band = "PCS (1900 MHz)"
    elif band == 0x10:
        band = "G850 (850 MHz)"
    elif band == 0x20:
        band = "U2100 (WCSMA 2100 MHZ, Class I)"
    elif band == 0x40:
        band = "U1700 (WCDMA 3GPP UMTS1800 MHz, Class III)"
    elif band == 0x80:
        band = "17IV (WCDMA 3GPP AWS 1700/2100 MHz, Class IV)"
    elif band == 0x100:
        band = "U800 (WCDMA 3GPP UMTS800 MHz, Class VI)"
    elif band == 0x200:
        band = "U850 (WCDMA 3GPP UMT850 MHz, Class V)"
    elif band == 0x400:
        band = "U900 (WCDMA 3GPP UMTS900 MHz, Class VIII)"
    elif band == 0x800:
        band = "U17IX (WCDMA 3GPP UMTS MHz, Class IX)"
    else:
        band = "(invalid)"

    print "Band: %s" % band


def gsm_inspect(proxy, dump_private, do_scan):
    # Gsm.Card interface
    card = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_CARD)

    imei = "<private>"
    imsi = "<private>"
    if dump_private:
        try:
            imei = card.GetImei()
        except dbus.exceptions.DBusException:
            imei = "<unavailable>"
        try:
            imsi = card.GetImsi()
        except dbus.exceptions.DBusException:
            imsi = "<unavailable>"

    print "IMEI: %s" % imei
    print "IMSI: %s" % imsi

    # Gsm.Network interface
    net = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_GSM_NETWORK)
    try:
        quality = net.GetSignalQuality()
        print "Signal quality: %d" % quality
    except dbus.exceptions.DBusException, e:
        print "Error reading signal quality: %s" % e

    if not do_scan:
        return

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
            access_tech_num = r['access-tech']
            if access_tech_num == "0":
                access_tech = "(GSM)"
            elif access_tech_num == "1":
                access_tech = "(Compact GSM)"
            elif access_tech_num == "2":
                access_tech = "(UMTS)"
            elif access_tech_num == "3":
                access_tech = "(EDGE)"
            elif access_tech_num == "4":
                access_tech = "(HSDPA)"
            elif access_tech_num == "5":
                access_tech = "(HSUPA)"
            elif access_tech_num == "6":
                access_tech = "(HSPA)"
        except KeyError:
            pass

        if r.has_key('operator-long') and len(r['operator-long']):
            print "%s: %s %s" % (r['operator-long'], status, access_tech)
        elif r.has_key('operator-short') and len(r['operator-short']):
            print "%s: %s %s" % (r['operator-short'], status, access_tech)
        else:
            print "%s: %s %s" % (r['operator-num'], status, access_tech)

def gsm_connect(proxy, apn, user, password):
    # Modem.Simple interface
    simple = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM_SIMPLE)
    try:
        opts = {'number':"*99#"}
        if apn is not None:
            opts['apn'] = apn
        if user is not None:
            opts['username'] = user
        if password is not None:
            opts['password'] = password
        simple.Connect(opts, timeout=120)
        print "\nConnected!"
        return True
    except Exception, e:
        print "Error connecting: %s" % e
    return False

def pppd_find():
    paths = ["/usr/local/sbin/pppd", "/usr/sbin/pppd", "/sbin/pppd"]
    for p in paths:
        if os.path.exists(p):
            return p
    return None

def ppp_start(device, user, password, tmpfile):
    path = pppd_find()
    if not path:
        return None

    args = [path]
    args += ["nodetach"]
    args += ["lock"]
    args += ["nodefaultroute"]
    args += ["debug"]
    if user:
        args += ["user"]
        args += [user]
    args += ["noipdefault"]
    args += ["115200"]
    args += ["noauth"]
    args += ["crtscts"]
    args += ["modem"]
    args += ["usepeerdns"]
    args += ["ipparam"]

    ipparam = ""
    if user:
        ipparam += user
    ipparam += "+"
    if password:
        ipparam += password
    ipparam += "+"
    ipparam += tmpfile
    args += [ipparam]

    args += ["plugin"]
    args += ["mm-test-pppd-plugin.so"]

    args += [device]

    return subprocess.Popen(args, close_fds=True, cwd="/", env={})

def ppp_wait(p, tmpfile):
    i = 0
    while p.poll() == None and i < 30:
        time.sleep(1)
        if os.path.exists(tmpfile):
            f = open(tmpfile, 'r')
            stuff = f.read(500)
            idx = string.find(stuff, "DONE")
            f.close()
            if idx >= 0:
                return True
        i += 1
    return False

def ppp_stop(p):
    import signal
    p.send_signal(signal.SIGTERM)
    p.wait()

def ntop_helper(ip):
    ip = socket.ntohl(ip)
    n1 = ip >> 24 & 0xFF
    n2 = ip >> 16 & 0xFF
    n3 = ip >> 8 & 0xFF
    n4 = ip & 0xFF
    a = "%c%c%c%c" % (n1, n2, n3, n4)
    return socket.inet_ntop(socket.AF_INET, a)

def static_start(iface, modem):
    (addr_num, dns1_num, dns2_num, dns3_num) = modem.GetIP4Config()
    addr = ntop_helper(addr_num)
    dns1 = ntop_helper(dns1_num)
    dns2 = ntop_helper(dns2_num)
    configure_iface(iface, addr, 0, dns1, dns2)

def down_iface(iface):
    ip = ["ip", "addr", "flush", "dev", iface]
    print " ".join(ip)
    subprocess.call(ip)
    ip = ["ip", "link", "set", iface, "down"]
    print " ".join(ip)
    subprocess.call(ip)

def configure_iface(iface, addr, gw, dns1, dns2):
    print "\n\n******************************"
    print "iface: %s" % iface
    print "addr:  %s" % addr
    print "gw:    %s" % gw
    print "dns1:  %s" % dns1
    print "dns2:  %s" % dns2

    ifconfig = ["ifconfig", iface, "%s/32" % addr]
    if gw != 0:
        ifconfig += ["pointopoint", gw]
    print " ".join(ifconfig)
    print "\n******************************\n"

    subprocess.call(ifconfig)

def file_configure_iface(tmpfile):
    addr = None
    gw = None
    iface = None
    dns1 = None
    dns2 = None

    f = open(tmpfile, 'r')
    lines = f.readlines()
    for l in lines:
        if l.startswith("addr"):
            addr = l[len("addr"):].strip()
        if l.startswith("gateway"):
            gw = l[len("gateway"):].strip()
        if l.startswith("iface"):
            iface = l[len("iface"):].strip()
        if l.startswith("dns1"):
            dns1 = l[len("dns1"):].strip()
        if l.startswith("dns2"):
            dns2 = l[len("dns2"):].strip()
    f.close()

    configure_iface(iface, addr, gw, dns1, dns2)
    return iface

def try_ping(iface):
    cmd = ["ping", "-I", iface, "-c", "4", "-i", "3", "-w", "20", "4.2.2.1"]
    print " ".join(cmd)
    retcode = subprocess.call(cmd)
    if retcode != 0:
        print "PING: failed"
    else:
        print "PING: success"


dump_private = False
connect = False
apn = None
user = None
password = None
do_ip = False
do_scan = True
x = 1
while x < len(sys.argv):
    if sys.argv[x] == "--private":
        dump_private = True
    elif sys.argv[x] == "--connect":
        connect = True
    elif (sys.argv[x] == "--user" or sys.argv[x] == "--username"):
        x += 1
        user = sys.argv[x]
    elif sys.argv[x] == "--apn":
        x += 1
        apn = sys.argv[x]
    elif sys.argv[x] == "--password":
        x += 1
        password = sys.argv[x]
    elif sys.argv[x] == "--ip":
        do_ip = True
        if os.geteuid() != 0:
            print "You probably want to be root to use --ip"
            sys.exit(1)
    elif sys.argv[x] == "--no-scan":
        do_scan = False
    x += 1

bus = dbus.SystemBus()

# Get available modems:
manager_proxy = bus.get_object('org.freedesktop.ModemManager', '/org/freedesktop/ModemManager')
manager_iface = dbus.Interface(manager_proxy, dbus_interface='org.freedesktop.ModemManager')
modems = manager_iface.EnumerateDevices()

if not modems:
    print "No modems found"
    sys.exit(1)

for m in modems:
    connect_success = False
    data_device = None

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
    data_device = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'Device')
    print "Data device: '%s'" % data_device

    # Modem interface
    modem = dbus.Interface(proxy, dbus_interface=MM_DBUS_INTERFACE_MODEM)

    try:
        modem.Enable(True)
    except dbus.exceptions.DBusException, e:
        print "Error enabling modem: %s" % e
        sys.exit(1)

    info = modem.GetInfo()
    print "Vendor:  %s" % info[0]
    print "Model:   %s" % info[1]
    print "Version: %s" % info[2]

    if type == 1:
        gsm_inspect(proxy, dump_private, do_scan)
        if connect == True:
            connect_success = gsm_connect(proxy, apn, user, password)
    elif type == 2:
        cdma_inspect(proxy, dump_private)
        if connect == True:
            connect_success = cdma_connect(proxy, user, password)
    print

    if connect_success and do_ip:
        tmpfile = "/tmp/mm-test-%d.tmp" % os.getpid()
        success = False
        try:
            ip_method = props_iface.Get(MM_DBUS_INTERFACE_MODEM, 'IpMethod')
            if ip_method == 0:
                # ppp
                p = ppp_start(data_device, user, password, tmpfile)
                if ppp_wait(p, tmpfile):
                    data_device = file_configure_iface(tmpfile)
                    success = True
            elif ip_method == 1:
                # static
                static_start(data_device, modem)
                success = True
            elif ip_method == 2:
                # dhcp
                pass
        except Exception, e:
            print "Error setting up IP: %s" % e

        if success:
            try_ping(data_device)
            print "Waiting for 30s..."
            time.sleep(30)

        print "Disconnecting..."
        try:
            if ip_method == 0:
                ppp_stop(p)
                try:
                    os.remove(tmpfile)
                except:
                    pass
            elif ip_method == 1:
                # static
                down_iface(data_device)
            elif ip_method == 2:
                # dhcp
                down_iface(data_device)

            modem.Disconnect()
        except Exception, e:
            print "Error tearing down IP: %s" % e

    time.sleep(5)

    modem.Enable(False)

