# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

include_inc = include_directories('.')

mm_enums_header = files('ModemManager-enums.h')
mm_errors_header = files('ModemManager-errors.h')

headers = files(
  'ModemManager-compat.h',
  'ModemManager.h',
)

install_headers(
  headers + mm_enums_header + mm_errors_header,
  install_dir: mm_pkgincludedir,
)

header = 'ModemManager-names.h'

mm_names_header = custom_target(
  header,
  input: [build_aux_dir / 'header-generator.xsl', mm_ifaces_all],
  output: header,
  command: [find_program('xsltproc'), '--xinclude', '--nonet', '--output', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: mm_pkgincludedir,
)

mm_version_header = configure_file(
  input: 'ModemManager-version.h.in',
  output: '@BASENAME@',
  configuration: version_conf,
  install: true,
  install_dir: mm_pkgincludedir,
)

include_dep = declare_dependency(
  sources: [mm_names_header, mm_version_header],
  include_directories: include_inc,
)
