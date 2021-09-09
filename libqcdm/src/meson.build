# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

sources = files(
  'com.c',
  'commands.c',
  'errors.c',
  'logs.c',
  'result.c',
  'utils.c',
)

libqcdm = static_library(
  'qcdm',
  sources: sources,
  include_directories: top_inc,
)

libqcdm_dep = declare_dependency(
  include_directories: '.',
  link_with: libqcdm,
)

# FIXME: Created following autotools but actually unused
libqcdm_test = static_library(
  'qcdm-test',
  sources: 'utils.c',
  include_directories: top_inc,
)
