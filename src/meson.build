# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

# helpers library
src_inc = include_directories('.')
kerneldevice_inc = include_directories('kerneldevice')
plugins_inc = include_directories('plugins')

headers = files(
  'mm-modem-helpers.h',
  'mm-sms-part.h',
)

enums_types = 'mm-helper-enums-types'

enums_sources = []

enums_sources += custom_target(
  enums_types + '.c',
  input: headers,
  output: enums_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "mm-helper-enums-types.h"\n',
    '--template', files(templates_dir / enums_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

enums_sources += custom_target(
  enums_types + '.h',
  input: headers,
  output: enums_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "mm-sms-part.h"\n#include "mm-modem-helpers.h"\n#ifndef __MM_HELPER_ENUMS_TYPES_H__\n#define __MM_HELPER_ENUMS_TYPES_H__\n',
    '--template', files(templates_dir / enums_types + '.h.template'),
    '--ftail', '#endif /* __MM_HELPER_ENUMS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
)

sources = files(
  'mm-cbm-part.c',
  'mm-charsets.c',
  'mm-error-helpers.c',
  'mm-location-cache.c',
  'mm-log.c',
  'mm-log-object.c',
  'mm-modem-helpers.c',
  'mm-sms-part-3gpp.c',
  'mm-sms-part.c',
  'mm-sms-part-cdma.c',
)

incs = [
  top_inc,
  # FIXME: only necessary if qmi is enabled?
  kerneldevice_inc,
]

deps = [libmm_glib_dep]

private_deps = []

if enable_qmi
  sources += files('mm-modem-helpers-qmi.c')

  deps += qmi_glib_dep
endif

if enable_mbim
  sources += files('mm-modem-helpers-mbim.c')

  deps += mbim_glib_dep
endif

if enable_systemd_journal
  private_deps += libsystemd_dep
endif

c_args = [
  '-DPKGSTATEDIR="@0@"'.format(mm_pkgsharedstatedir),
]

libhelpers = static_library(
  'helpers',
  sources: sources + enums_sources,
  include_directories: incs,
  dependencies: deps + private_deps,
  c_args: c_args,
)

libhelpers_dep = declare_dependency(
  sources: enums_sources[1],
  include_directories: ['.', kerneldevice_inc],
  dependencies: deps,
  link_with: libhelpers,
)

# kerneldevice library
sources = files(
  'kerneldevice/mm-kernel-device.c',
  'kerneldevice/mm-kernel-device-generic.c',
  'kerneldevice/mm-kernel-device-generic-rules.c',
  'kerneldevice/mm-kernel-device-helpers.c',
)

deps = [libhelpers_dep]

if enable_qrtr
  sources += files('kerneldevice/mm-kernel-device-qrtr.c')

  deps += qrtr_glib_dep
endif

if enable_udev
  sources += files('kerneldevice/mm-kernel-device-udev.c')

  deps += gudev_dep
endif

libkerneldevice = static_library(
  'kerneldevice',
  sources: sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DUDEVRULESDIR="@0@"'.format(udev_rulesdir),
)

libkerneldevice_dep = declare_dependency(
  dependencies: deps,
  link_with: libkerneldevice,
)

# ports library
headers = files(
  'mm-iface-port-at.h',
  'mm-port.h',
  'mm-port-serial-at.h',
  'mm-port-scheduler.h',
  'mm-port-scheduler-rr.h',
)

sources = files(
  'mm-iface-port-at.c',
  'mm-netlink.c',
  'mm-port.c',
  'mm-port-net.c',
  'mm-port-serial-at.c',
  'mm-port-serial.c',
  'mm-port-serial-gps.c',
  'mm-port-serial-qcdm.c',
  'mm-serial-parsers.c',
  'mm-port-scheduler.c',
  'mm-port-scheduler-rr.c',
)

deps = [libkerneldevice_dep]

private_deps = [gio_unix_dep]

if enable_qmi
  headers += files('mm-port-qmi.h')

  sources += files('mm-port-qmi.c')
endif

if enable_mbim
  sources += files('mm-port-mbim.c')
endif

enums_types = 'mm-port-enums-types'

port_enums_sources = []
port_enums_sources += custom_target(
  enums_types + '.c',
  input: headers,
  output: enums_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "mm-port-enums-types.h"\n',
    '--template', files(templates_dir / enums_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

port_enums_sources += custom_target(
  enums_types + '.h',
  input: headers,
  output: enums_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "config.h"\n#include "mm-port.h"\n#include "mm-port-serial-at.h"\n#if defined WITH_QMI\n#include "mm-port-qmi.h"\n#endif\n#ifndef __MM_PORT_ENUMS_TYPES_H__\n#define __MM_PORT_ENUMS_TYPES_H__\n',
    '--template', files(templates_dir / enums_types + '.h.template'),
    '--ftail', '#endif /* __MM_PORT_ENUMS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
)

libport = static_library(
  'port',
  sources: sources + port_enums_sources,
  include_directories: top_inc,
  dependencies: deps + private_deps,
)

libport_dep = declare_dependency(
  sources: port_enums_sources[1],
  include_directories: '.',
  dependencies: deps,
  link_with: libport,
)

# auth provider library
sources = files(
  'mm-auth-provider.c',
  'mm-context.c',
  'mm-bind.c',
)

incs = [
  top_inc,
  kerneldevice_inc,
]

deps = [libmm_glib_dep, libhelpers_dep]

if enable_polkit
  deps += polkit_gobject_dep
endif

private_deps = []

libauth = static_library(
  'auth',
  sources: sources,
  include_directories: incs,
  dependencies: deps,
  c_args: '-DPLUGINDIR="@0@"'.format(mm_prefix / mm_pkglibdir),
)

libauth_dep = declare_dependency(
  include_directories: ['.', kerneldevice_inc],
  dependencies: deps,
  link_with: libauth,
)

# SMS library
sources = files(
  'mm-sms-list.c',
  'mm-base-sms.c',
)

incs = [
  top_inc,
]

deps = [libmm_glib_dep, libhelpers_dep, libauth_dep]

private_deps = []

libsms = static_library(
  'sms',
  sources: sources,
  include_directories: incs,
  dependencies: deps,
)

libsms_dep = declare_dependency(
  include_directories: ['.'],
  dependencies: deps,
  link_with: libsms,
)

# Daemon enums, required by plugins
headers = files(
  'mm-base-bearer.h',
  'mm-iface-op-lock.h',
  'mm-filter.h',
  'mm-port-probe.h',
)

enums_types = 'mm-daemon-enums-types'

daemon_enums_sources = []

daemon_enums_sources += custom_target(
  enums_types + '.c',
  input: headers,
  output: enums_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "mm-daemon-enums-types.h"\n',
    '--template', files(templates_dir / enums_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

daemon_enums_sources += custom_target(
  enums_types + '.h',
  input: headers,
  output: enums_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include "mm-filter.h"\n#include "mm-base-bearer.h"\n#include "mm-port-probe.h"\n#ifndef __MM_DAEMON_ENUMS_TYPES_H__\n#define __MM_DAEMON_ENUMS_TYPES_H__\n',
    '--template', files(templates_dir / enums_types + '.h.template'),
    '--ftail', '#endif /* __MM_DAEMON_ENUMS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
)

daemon_enums_types_dep = declare_dependency(
  sources: daemon_enums_sources[1],
  include_directories: '.',
)

base_sources = files(
  'mm-base-bearer.c',
  'mm-base-call.c',
  'mm-call-at.c',
  'mm-base-cbm.c',
  'mm-base-modem-at.c',
  'mm-base-modem.c',
  'mm-base-sim.c',
  'mm-sms-at.c',
  'mm-bearer-list.c',
  'mm-broadband-bearer.c',
  'mm-broadband-modem.c',
  'mm-call-list.c',
  'mm-cbm-list.c',
  'mm-device.c',
  'mm-dispatcher.c',
  'mm-dispatcher-connection.c',
  'mm-dispatcher-fcc-unlock.c',
  'mm-dispatcher-modem-setup.c',
  'mm-iface-modem-3gpp.c',
  'mm-iface-modem-3gpp-profile-manager.c',
  'mm-iface-modem-3gpp-ussd.c',
  'mm-iface-modem.c',
  'mm-iface-modem-cdma.c',
  'mm-iface-modem-cell-broadcast.c',
  'mm-iface-modem-firmware.c',
  'mm-iface-modem-location.c',
  'mm-iface-modem-messaging.c',
  'mm-iface-modem-oma.c',
  'mm-iface-modem-sar.c',
  'mm-iface-modem-signal.c',
  'mm-iface-modem-simple.c',
  'mm-iface-modem-time.c',
  'mm-iface-modem-voice.c',
  'mm-iface-op-lock.c',
  'mm-log-helpers.c',
  'mm-private-boxed-types.c',
  'mm-sleep-context.c',
)

# MM base library (used by MM and tests)
incs = [
  top_inc,
  kerneldevice_inc,
]

deps = [
  libmm_glib_dep,
  libhelpers_dep,
  libauth_dep,
  libport_dep,
  libqcdm_dep,
  daemon_enums_types_dep,
]

private_deps = []

c_args = [
  '-DMM_COMPILATION',
  '-DPLUGINDIR="@0@"'.format(mm_prefix / mm_pkglibdir),
  '-DMODEMSETUPDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'modem-setup.d'),
  '-DMODEMSETUPDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'modem-setup.d'),
  '-DFCCUNLOCKDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'fcc-unlock.d'),
  '-DFCCUNLOCKDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'fcc-unlock.d'),
  '-DCONNECTIONDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'connection.d'),
  '-DCONNECTIONDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'connection.d'),
]

libmmbase = static_library(
  'mmbase',
  sources: base_sources + daemon_enums_sources,
  include_directories: incs,
  dependencies: deps,
  c_args: c_args,
)

libmmbase_dep = declare_dependency(
  include_directories: ['.', kerneldevice_inc],
  dependencies: deps,
  link_with: libmmbase,
)

# Additional vendor plugins
subdir('plugins')

# ModemManager daemon
sources = files(
  'main.c',
  'mm-base-manager.c',
  'mm-filter.c',
  'mm-plugin.c',
  'mm-plugin-manager.c',
  'mm-port-probe.c',
  'mm-port-probe-at.c',
)

sources += daemon_enums_sources
sources += base_sources

deps = [
  gmodule_dep,
  libport_dep,
  libqcdm_dep,
  libsms_dep,
  libauth_dep,
]

if enable_tests
  deps += [libmm_test_generated_dep]
endif

c_args = [
  '-DMM_COMPILATION',
  '-DMODEMSETUPDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'modem-setup.d'),
  '-DMODEMSETUPDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'modem-setup.d'),
  '-DFCCUNLOCKDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'fcc-unlock.d'),
  '-DFCCUNLOCKDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'fcc-unlock.d'),
  '-DCONNECTIONDIRPACKAGE="@0@"'.format(mm_prefix / mm_pkglibdir / 'connection.d'),
  '-DCONNECTIONDIRUSER="@0@"'.format(mm_prefix / mm_pkgsysconfdir / 'connection.d'),
]

if enable_qrtr
  sources += files('mm-qrtr-bus-watcher.c')
endif

# Additional suspend/resume support via systemd
if enable_systemd_suspend_resume
  sources += files('mm-sleep-monitor-systemd.c')

  deps += [
    gio_unix_dep,
    libsystemd_dep,
  ]
endif

#suspend/resume support via powerd
if enable_powerd_suspend_resume
  sources += files('mm-sleep-monitor-powerd.c')
endif

if enable_polkit
  deps += polkit_gobject_dep
endif

# Additional QMI support in ModemManager
if enable_qmi
  sources += files(
    'mm-bearer-qmi.c',
    'mm-broadband-modem-qmi.c',
    'mm-call-qmi.c',
    'mm-shared-qmi.c',
    'mm-sim-qmi.c',
    'mm-sms-qmi.c',
  )
endif

# Additional MBIM support in ModemManager
if enable_mbim
  sources += files(
    'mm-bearer-mbim.c',
    'mm-broadband-modem-mbim.c',
    'mm-sim-mbim.c',
    'mm-sms-mbim.c',
  )
endif

executable(
  'ModemManager',
  sources: [sources, builtin_sources],
  include_directories: [ top_inc, plugins_inc ],
  dependencies: deps,
  c_args: c_args,
  link_whole: builtin_plugins,
  install: true,
  install_dir: mm_sbindir,
)

pkg.generate(
  version: mm_version,
  name: mm_name,
  description: 'Common headers provided by ModemManager',
  subdirs: mm_name,
  variables: 'exec_prefix=${prefix}',
)

# generic udev rules
install_data(
  '80-mm-candidate.rules',
  install_dir: udev_rulesdir,
)

# empty directory for cached position
install_emptydir(
  mm_pkgsharedstatedir,
)

if enable_tests
  subdir('tests')
endif
