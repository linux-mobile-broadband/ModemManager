# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_units = [
  'common-helpers',
  'pco',
]

foreach test_unit: test_units
  test_name = 'test-' + test_unit

  exe = executable(
    test_name,
    test_name + '.c',
    include_directories: top_inc,
    dependencies: libmm_glib_dep,
    c_args: '-DLIBMM_GLIB_COMPILATION',
  )

  test(test_name, exe)
endforeach
