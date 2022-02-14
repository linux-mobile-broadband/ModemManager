# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

libmm_glib_inc = include_directories('.')

subdir('generated')

headers = files(
  'libmm-glib.h',
  'mm-3gpp-profile.h',
  'mm-bearer.h',
  'mm-bearer-ip-config.h',
  'mm-bearer-properties.h',
  'mm-bearer-stats.h',
  'mm-call-audio-format.h',
  'mm-call.h',
  'mm-call-properties.h',
  'mm-cdma-manual-activation-properties.h',
  'mm-cell-info.h',
  'mm-cell-info-cdma.h',
  'mm-cell-info-gsm.h',
  'mm-cell-info-lte.h',
  'mm-cell-info-nr5g.h',
  'mm-cell-info-tdscdma.h',
  'mm-cell-info-umts.h',
  'mm-compat.h',
  'mm-firmware-properties.h',
  'mm-firmware-update-settings.h',
  'mm-helper-types.h',
  'mm-kernel-event-properties.h',
  'mm-location-3gpp.h',
  'mm-location-cdma-bs.h',
  'mm-location-common.h',
  'mm-location-gps-nmea.h',
  'mm-location-gps-raw.h',
  'mm-manager.h',
  'mm-modem-3gpp.h',
  'mm-modem-3gpp-profile-manager.h',
  'mm-modem-3gpp-ussd.h',
  'mm-modem-cdma.h',
  'mm-modem-firmware.h',
  'mm-modem.h',
  'mm-modem-location.h',
  'mm-modem-messaging.h',
  'mm-modem-oma.h',
  'mm-modem-sar.h',
  'mm-modem-signal.h',
  'mm-modem-simple.h',
  'mm-modem-time.h',
  'mm-modem-voice.h',
  'mm-network-timezone.h',
  'mm-nr5g-registration-settings.h',
  'mm-object.h',
  'mm-pco.h',
  'mm-signal.h',
  'mm-signal-threshold-properties.h',
  'mm-sim.h',
  'mm-simple-connect-properties.h',
  'mm-simple-status.h',
  'mm-sim-preferred-network.h',
  'mm-sms.h',
  'mm-sms-properties.h',
  'mm-unlock-retries.h',
)

install_headers(
  headers,
  install_dir: mm_glib_pkgincludedir,
)

sources = files(
  'mm-3gpp-profile.c',
  'mm-bearer.c',
  'mm-bearer-ip-config.c',
  'mm-bearer-properties.c',
  'mm-bearer-stats.c',
  'mm-call-audio-format.c',
  'mm-call.c',
  'mm-call-properties.c',
  'mm-cdma-manual-activation-properties.c',
  'mm-cell-info.c',
  'mm-cell-info-cdma.c',
  'mm-cell-info-gsm.c',
  'mm-cell-info-lte.c',
  'mm-cell-info-nr5g.c',
  'mm-cell-info-tdscdma.c',
  'mm-cell-info-umts.c',
  'mm-common-helpers.c',
  'mm-compat.c',
  'mm-firmware-properties.c',
  'mm-firmware-update-settings.c',
  'mm-helper-types.c',
  'mm-kernel-event-properties.c',
  'mm-location-3gpp.c',
  'mm-location-cdma-bs.c',
  'mm-location-gps-nmea.c',
  'mm-location-gps-raw.c',
  'mm-manager.c',
  'mm-modem-3gpp.c',
  'mm-modem-3gpp-profile-manager.c',
  'mm-modem-3gpp-ussd.c',
  'mm-modem.c',
  'mm-modem-cdma.c',
  'mm-modem-firmware.c',
  'mm-modem-location.c',
  'mm-modem-messaging.c',
  'mm-modem-oma.c',
  'mm-modem-sar.c',
  'mm-modem-signal.c',
  'mm-modem-simple.c',
  'mm-modem-time.c',
  'mm-modem-voice.c',
  'mm-network-timezone.c',
  'mm-nr5g-registration-settings.c',
  'mm-object.c',
  'mm-pco.c',
  'mm-signal.c',
  'mm-signal-threshold-properties.c',
  'mm-sim.c',
  'mm-simple-connect-properties.c',
  'mm-simple-status.c',
  'mm-sim-preferred-network.c',
  'mm-sms.c',
  'mm-sms-properties.c',
  'mm-unlock-retries.c',
)

deps = [include_dep]

libname = 'mm-glib'

libmm_glib = shared_library(
  libname,
  version: mm_glib_version,
  sources: sources,
  include_directories: top_inc,
  dependencies: deps + [libmm_generated_dep],
  c_args: '-DLIBMM_GLIB_COMPILATION',
  install: true,
)

libmm_glib_dep = declare_dependency(
  include_directories: libmm_glib_inc,
  # FIXME: glib_deps is included because `dependencies` parameter is not part of partial_dependency
  dependencies: deps + [glib_deps, libmm_generated_dep.partial_dependency(sources: true, includes: true)],
  link_with: libmm_glib,
)

pkg.generate(
  libraries: libmm_glib,
  version: mm_version,
  name: libname,
  description: 'Library to control and monitor the ModemManager',
  subdirs: mm_glib_name,
  # FIXME: produced by the inhability of meson to use internal dependencies
  requires: ['gio-2.0', 'glib-2.0', 'gobject-2.0', 'ModemManager'],
  variables: 'exec_prefix=${prefix}',
)

if enable_gir
  incs = [
    'Gio-2.0',
    'GLib-2.0',
    'GObject-2.0',
  ]

  gir_ns = 'ModemManager'
  gir_prefix = 'Mm'

  args = [
    '-DLIBMM_GLIB_COMPILATION',
    '--identifier-prefix=' + gir_prefix.to_upper(),
  ]

  libmm_glib_gir = gnome.generate_gir(
    libmm_glib,
    sources: sources + headers + gen_sources + gen_headers + [mm_names_header, mm_version_header],
    includes: incs,
    namespace: gir_ns,
    nsversion: mm_gir_version,
    identifier_prefix: gir_prefix,
    symbol_prefix: gir_prefix.to_lower(),
    extra_args: args,
    header: 'libmm-glib.h',
    export_packages: libname,
    install: true,
  )

  if enable_vapi
    libmm_glib_vapi = gnome.generate_vapi(
      'libmm-glib',
      sources: libmm_glib_gir[0],
      packages: 'gio-2.0',
      install: true,
    )
  endif
endif

subdir('tests')
