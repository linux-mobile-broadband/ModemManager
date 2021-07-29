# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

doc_module = mm_glib_name

src_dirs = [
  generated_inc,
  libmm_glib_inc,
]

private_headers = [
  'mm-common-helpers.h',
  'mm-gdbus-test.h',
  'mm-helpers.h',
]

scan_args = [
  '--deprecated-guards="MM_DISABLE_DEPRECATED"',
  '--rebuild-types',
]

mkdb_args = [
  '--ignore-files=mm-gdbus-test.h',
  '--ignore-files=mm-gdbus-test.c',
]

fixxref_args = [
  '--html-dir=' + (mm_prefix / gnome.gtkdoc_html_dir(doc_module)),
  '--extra-dir=' + mm_doc_path,
]

version_xml = configure_file(
  input: 'version.xml.in',
  output: '@BASENAME@',
  configuration: version_conf,
)

gnome.gtkdoc(
  doc_module,
  main_xml: doc_module + '-docs.xml',
  src_dir: src_dirs,
  ignore_headers: private_headers,
  include_directories: top_inc,
  dependencies: libmm_glib_dep,
  namespace: 'mm',
  scan_args: scan_args,
  mkdb_args: mkdb_args,
  fixxref_args: fixxref_args,
  html_assets: logos_pngs,
  content_files: version_xml,
  install: true,
)
