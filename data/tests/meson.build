# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022 Aleksander Morgado <aleksander@aleksander.es>

test_conf = {
  'abs_top_builddir': build_root,
  'PLUGIN_BUILD_SUBDIR': 'src/plugins/',
}

configure_file(
  input: 'org.freedesktop.ModemManager1.service.in',
  output: '@BASENAME@',
  configuration: test_conf,
)