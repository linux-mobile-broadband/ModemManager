# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_conf = {
  'abs_top_builddir': build_root,
  'abs_top_srcdir': source_root,
}

subdir('services')

test_wrapper = configure_file(
  input: 'test-wrapper.sh.in',
  output: '@BASENAME@',
  configuration: test_conf,
)

# common
if enable_gir
  test_unit = 'test-stub'

  exe = executable(
    test_unit,
    test_unit + '.c',
    include_directories: [top_inc, src_inc],
    dependencies: libmm_glib_dep,
    c_args: '-DTEST_SERVICES="@0@"'.format(meson.current_build_dir() / 'services'),
  )

  test(test_unit, exe)
endif
