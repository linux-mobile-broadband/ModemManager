# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>

example_units = [
  'sms-c-async',
  'sms-c-sync',
]

foreach example_unit: example_units
  executable(
    example_unit,
    example_unit + '.c',
    include_directories: top_inc,
    dependencies: libmm_glib_dep,
  )
endforeach
