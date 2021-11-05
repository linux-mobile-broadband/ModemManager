# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

sources = files(
  'mmcli-bearer.c',
  'mmcli.c',
  'mmcli-call.c',
  'mmcli-common.c',
  'mmcli-manager.c',
  'mmcli-modem-3gpp.c',
  'mmcli-modem-3gpp-profile-manager.c',
  'mmcli-modem-3gpp-ussd.c',
  'mmcli-modem.c',
  'mmcli-modem-cdma.c',
  'mmcli-modem-firmware.c',
  'mmcli-modem-location.c',
  'mmcli-modem-messaging.c',
  'mmcli-modem-oma.c',
  'mmcli-modem-sar.c',
  'mmcli-modem-signal.c',
  'mmcli-modem-simple.c',
  'mmcli-modem-time.c',
  'mmcli-modem-voice.c',
  'mmcli-output.c',
  'mmcli-sim.c',
  'mmcli-sms.c',
)

deps = [libmm_glib_dep]

if enable_udev
  deps += gudev_dep
endif

executable(
  'mmcli',
  sources: sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
)

if enable_bash_completion
  install_data(
    'mmcli-completion',
    install_dir: bash_completion_completionsdir,
    rename: 'mmcli',
  )
endif
