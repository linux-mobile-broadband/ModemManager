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

gen_sources += gnome.mkenums(
  enums_types + '.c',
  sources: mm_enums_header,
  c_template: build_aux_dir / enums_types + '.c.template',
  fhead: '#include "mm-enums-types.h"\n',
)

gen_headers += gnome.mkenums(
  enums_types + '.h',
  sources: mm_enums_header,
  h_template: build_aux_dir / enums_types + '.h.template',
  fhead: '#include <ModemManager.h>\n#ifndef __MM_ENUMS_TYPES_H__\n#define __MM_ENUMS_TYPES_H__\n',
  ftail: '#endif /* __MM_ENUMS_TYPES_H__ */\n',
  install_header: true,
  install_dir: mm_glib_pkgincludedir,
)

# Error types & quarks
errors_types = 'mm-errors-types'

gen_sources += gnome.mkenums(
  enums_types + '.c',
  sources: mm_errors_header,
  c_template: build_aux_dir / errors_types + '.c.template',
  fhead: '#include <ModemManager.h>\n#include "mm-errors-types.h"\n',
)

gen_headers += gnome.mkenums(
  errors_types + '.h',
  sources: mm_errors_header,
  h_template: build_aux_dir / errors_types + '.h.template',
  fhead: '#ifndef __MM_ERRORS_TYPES_H__\n#define __MM_ERRORS_TYPES_H__\n',
  ftail: '#endif /* __MM_ERRORS_TYPES_H__ */\n',
  install_header: true,
  install_dir: mm_glib_pkgincludedir,
)

errors_quarks = 'mm-errors-quarks'

gen_sources += gnome.mkenums(
  errors_quarks + '.c',
  sources: mm_errors_header,
  c_template: build_aux_dir / errors_quarks + '.c.template',
  fhead: '#include <ModemManager.h>\n#include "mm-errors-types.h"\n',
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

subdir('tests')
