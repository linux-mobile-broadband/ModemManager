# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

generated_inc = include_directories('.')
generated_build_dir = meson.current_build_dir()

common_c_args = cc.get_supported_arguments([
  '-Wno-float-equal',
  '-Wno-shadow',
  '-Wno-unused-function',
])

gen_sources = []
gen_headers = []
gen_docs = []

# Enum types
enums_types = 'mm-enums-types'

gen_sources += custom_target(
  enums_types + '.c',
  input: mm_enums_header,
  output: enums_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--enums-only',
    '--fhead', '#include "mm-enums-types.h"\n',
    '--template', files(templates_dir / enums_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

gen_headers += custom_target(
  enums_types + '.h',
  input: mm_enums_header,
  output: enums_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--enums-only',
    '--fhead', '#include <ModemManager.h>\n#ifndef __MM_ENUMS_TYPES_H__\n#define __MM_ENUMS_TYPES_H__\n',
    '--template', files(templates_dir / enums_types + '.h.template'),
    '--ftail', '#endif /* __MM_ENUMS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mm_glib_pkgincludedir,
)

# Flag types
enums_types = 'mm-flags-types'

gen_sources += custom_target(
  enums_types + '.c',
  input: mm_enums_header,
  output: enums_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--flags-only',
    '--fhead', '#include "mm-flags-types.h"\n',
    '--template', files(templates_dir / enums_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

gen_headers += custom_target(
  enums_types + '.h',
  input: mm_enums_header,
  output: enums_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--flags-only',
    '--fhead', '#include <ModemManager.h>\n#ifndef __MM_FLAGS_TYPES_H__\n#define __MM_FLAGS_TYPES_H__\n',
    '--template', files(templates_dir / enums_types + '.h.template'),
    '--ftail', '#endif /* __MM_FLAGS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mm_glib_pkgincludedir,
)

# Error types & quarks
errors_types = 'mm-errors-types'

gen_sources += custom_target(
  errors_types + '.c',
  input: mm_errors_header,
  output: errors_types + '.c',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include <ModemManager.h>\n#include "mm-errors-types.h"\n',
    '--template', files(templates_dir / errors_types + '.c.template'),
    '@INPUT@'],
  capture: true,
)

gen_headers += custom_target(
  errors_types + '.h',
  input: mm_errors_header,
  output: errors_types + '.h',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#ifndef __MM_ERRORS_TYPES_H__\n#define __MM_ERRORS_TYPES_H__\n',
    '--template', files(templates_dir / errors_types + '.h.template'),
    '--ftail', '#endif /* __MM_ERRORS_TYPES_H__ */\n',
    '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mm_glib_pkgincludedir,
)

errors_quarks = 'mm-errors-quarks'

gen_sources += custom_target(
  errors_quarks + '.c',
  input: mm_errors_header,
  output: errors_quarks + '.c',
  command: [
    python,
    mm_mkenums,
    '--fhead', '#include <ModemManager.h>\n#include "mm-errors-types.h"\n',
    '--template', files(templates_dir / errors_quarks + '.c.template'),
    '@INPUT@'],
  capture: true,
)

gdbus_ifaces = {
  'bearer': {'sources': mm_ifaces_bearer, 'object_manager': false},
  'call': {'sources':  mm_ifaces_call, 'object_manager': false},
  'manager': {'sources': mm_ifaces, 'object_manager': false},
  'sim': {'sources': mm_ifaces_sim, 'object_manager': false},
}

annotations = [
  ['org.freedesktop.ModemManager1.Modem.ModemCdma', 'org.gtk.GDBus.C.Name', 'ModemCdma'],
  ['org.freedesktop.ModemManager1.Modem.Modem3gpp', 'org.gtk.GDBus.C.Name', 'Modem3gpp'],
  ['org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd', 'org.gtk.GDBus.C.Name', 'Modem3gppUssd'],
  ['org.freedesktop.ModemManager1.Modem.Modem3gpp.ProfileManager', 'org.gtk.GDBus.C.Name', 'Modem3gppProfileManager'],
]

gdbus_ifaces += {'modem': {'sources': mm_ifaces_modem, 'annotations': annotations, 'object_manager': true}}

annotations = [['org.freedesktop.ModemManager1.Sms:Data', 'org.gtk.GDBus.C.ForceGVariant', 'True']]

gdbus_ifaces += {'sms': {'sources': mm_ifaces_sms, 'annotations': annotations, 'object_manager': false}}

foreach name, kwargs: gdbus_ifaces
  gdbus_sources = gnome.gdbus_codegen(
    'mm-gdbus-' + name,
    interface_prefix: 'org.freedesktop.ModemManager1.',
    namespace: 'MmGdbus',
    docbook: 'mm-gdbus-doc',
    autocleanup: 'objects',
    kwargs: kwargs,
    # FIXME: due to the lack of possibility to add `docbook targets` to the `expand_content_files`.
    build_by_default: true,
    install_header: true,
    install_dir: mm_glib_pkgincludedir,
  )

  gen_sources += gdbus_sources[0]
  gen_headers += gdbus_sources[1]
  # FIXME: the `expand_content_files` must be strings
  gen_docs += gdbus_sources[2]
endforeach

deps = [
  include_dep,
  gio_unix_dep,
  glib_deps,
]

libmm_generated = static_library(
  'mm-generated',
  sources: gen_sources + gen_headers,
  include_directories: top_inc,
  dependencies: deps,
  c_args: common_c_args,
)

libmm_generated_dep = declare_dependency(
  sources: gen_headers,
  include_directories: generated_inc,
  dependencies: glib_deps,
  link_whole: libmm_generated,
)

if enable_tests
  subdir('tests')
endif
