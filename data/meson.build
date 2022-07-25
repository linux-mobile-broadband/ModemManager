# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

if enable_tests
  subdir('tests')
endif

service_conf = {
  'sbindir': mm_prefix / mm_sbindir,
  'MM_POLKIT_SERVICE': (enable_polkit ? 'polkit.service' : ''),
}

# DBus Service file
configure_file(
  input: 'org.freedesktop.ModemManager1.conf.@0@polkit'.format(enable_polkit ? '' : 'no'),
  output: '@BASENAME@',
  copy: true,
  install_dir: dbus_policy_dir,
)

# DBus Activation file
configure_file(
  input: 'org.freedesktop.ModemManager1.service.in',
  output: '@BASENAME@',
  configuration: service_conf,
  install_dir: dbus_system_bus_services_dir,
)

# systemd unit file
if install_systemdunitdir
  configure_file(
    input: 'ModemManager.service.in',
    output: '@BASENAME@',
    configuration: service_conf,
    install_dir: systemd_systemdsystemunitdir,
  )
endif

# Polkit
if enable_polkit
  policy = 'org.freedesktop.ModemManager1.policy'

  # build file with translations, which we will include in dist
  i18n.merge_file(
    input: configure_file(
      input: policy + '.in.in',
      output: '@BASENAME@',
      configuration: policy_conf,
    ),
    output: '@BASENAME@',
    po_dir: po_dir,
    install: true,
    install_dir: polkit_gobject_policydir,
  )
endif

if enable_gtk_doc
  # Logos
  logos_pngs = files(
    'ModemManager-logo-square.png',
    'ModemManager-logo-wide.png',
    'ModemManager-logo-wide-text.png',
  )

  # Diagrams
  diagrams_pngs = files(
    'ModemManager-interface-initialization-sequence.png',
    'ModemManager-interface-initialization-sequence-subclassed.png',
    'ModemManager-states.png',
  )
endif

# Icon
install_data(
  'ModemManager.png',
  install_dir: mm_datadir / 'icons/hicolor/22x22/apps',
)
