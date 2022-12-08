# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022 Aleksander Morgado <aleksander@aleksander.es>

test_plugin_dir = ''
if not enable_builtin_plugins
  test_plugin_dir = '--test-plugin-dir="' + build_root + '/src/plugins"'
endif

test_conf = {
  'abs_top_builddir': build_root,
  'test_plugin_dir': test_plugin_dir,
}

configure_file(
  input: 'org.freedesktop.ModemManager1.service.in',
  output: '@BASENAME@',
  configuration: test_conf,
)