# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

# DBus Introspection files
mm_ifaces_all = files('all.xml')
if enable_tests
  mm_ifaces_test = files('tests/org.freedesktop.ModemManager1.Test.xml')
endif

mm_ifaces = files('org.freedesktop.ModemManager1.xml')

mm_ifaces_bearer = files('org.freedesktop.ModemManager1.Bearer.xml')
mm_ifaces_call = files('org.freedesktop.ModemManager1.Call.xml')

mm_ifaces_modem = files(
  'org.freedesktop.ModemManager1.Modem.Firmware.xml',
  'org.freedesktop.ModemManager1.Modem.Location.xml',
  'org.freedesktop.ModemManager1.Modem.Messaging.xml',
  'org.freedesktop.ModemManager1.Modem.Modem3gpp.ProfileManager.xml',
  'org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml',
  'org.freedesktop.ModemManager1.Modem.Modem3gpp.xml',
  'org.freedesktop.ModemManager1.Modem.ModemCdma.xml',
  'org.freedesktop.ModemManager1.Modem.Oma.xml',
  'org.freedesktop.ModemManager1.Modem.Sar.xml',
  'org.freedesktop.ModemManager1.Modem.Signal.xml',
  'org.freedesktop.ModemManager1.Modem.Simple.xml',
  'org.freedesktop.ModemManager1.Modem.Time.xml',
  'org.freedesktop.ModemManager1.Modem.Voice.xml',
  'org.freedesktop.ModemManager1.Modem.xml',
)

mm_ifaces_sim = files('org.freedesktop.ModemManager1.Sim.xml')
mm_ifaces_sms = files('org.freedesktop.ModemManager1.Sms.xml',)

install_data(
  mm_ifaces + mm_ifaces_bearer + mm_ifaces_call + mm_ifaces_modem + mm_ifaces_sim + mm_ifaces_sms,
  install_dir: dbus_interfaces_dir,
)
