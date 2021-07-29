# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

doc_module = mm_name

private_headers = [
  'ModemManager.h',
  'ModemManager-names.h',
]

mm_doc_path = mm_prefix / gnome.gtkdoc_html_dir(doc_module)

version_xml = configure_file(
  input: 'version.xml.in',
  output: '@BASENAME@',
  configuration: version_conf,
)

expand_content_files = [
  'ModemManager-dbus-reference.xml',
  'ModemManager-migration-reference.xml',
  'ModemManager-overview.xml',
  # FIXME: workaround because only strings can be included and not custom targets (gen_docs)
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Firmware.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Location.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Messaging.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.ProfileManager.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.ModemCdma.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Oma.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Signal.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Simple.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Time.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml',
  generated_build_dir / 'mm-gdbus-doc-org.freedesktop.ModemManager1.xml',
]

gnome.gtkdoc(
  doc_module,
  main_xml: doc_module + '-docs.xml',
  src_dir: include_inc,
  ignore_headers: private_headers,
  include_directories: top_inc,
  gobject_typesfile: doc_module + '.types',
  dependencies: glib_deps,
  namespace: 'mm',
  scan_args: '--deprecated-guards="MM_DISABLE_DEPRECATED"',
  fixxref_args: '--html-dir=' + mm_doc_path,
  html_assets: logos_pngs + diagrams_pngs,
  content_files: version_xml,
  expand_content_files: expand_content_files,
  install: true,
)
