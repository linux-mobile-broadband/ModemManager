# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_units = [
  ['ipv6pref', files('ipv6pref.c'), false],
  ['modepref', files('modepref.c'), false],
  ['reset', files('reset.c'), false],
]

sources = files(
  'test-qcdm.c',
  'test-qcdm-com.c',
  'test-qcdm-crc.c',
  'test-qcdm-escaping.c',
  'test-qcdm-result.c',
  'test-qcdm-utils.c',
)

test_units += [['test-qcdm', sources, true]]

deps = [
  glib_deps,
  libqcdm_dep,
]

foreach test_unit: test_units
  exe = executable(
    test_unit[0],
    test_unit[1],
    include_directories: top_inc,
    dependencies: deps,
  )

  if test_unit[2]
    test(test_unit[0], exe)
  endif
endforeach
