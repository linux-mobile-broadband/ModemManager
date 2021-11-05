# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_units = {
  'mmrules': libkerneldevice_dep,
  'mmsmsmonitor': libhelpers_dep,
  'mmsmspdu': libhelpers_dep,
  'mmtty': libport_dep,
}

if enable_udev
  test_units += {'lsudev': gudev_dep}
endif

foreach test_unit, test_deps: test_units
  executable(
    test_unit,
    test_unit + '.c',
    include_directories: top_inc,
    dependencies: test_deps,
  )
endforeach
