# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

# Test interface
gdbus_sources = gnome.gdbus_codegen(
  'mm-gdbus-test',
  sources: mm_ifaces_test,
  interface_prefix: 'org.freedesktop.ModemManager1.',
  namespace: 'MmGdbus',
  autocleanup: 'objects',
)

libmm_test_generated = static_library(
  'mm-test-generated',
  sources: gdbus_sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: common_c_args,
)

libmm_test_generated_dep = declare_dependency(
  sources: gdbus_sources[1],
  include_directories: '.',
  dependencies: glib_deps,
  link_with: libmm_test_generated,
)
