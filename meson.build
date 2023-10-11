# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

project(
  'ModemManager', 'c',
  version: '1.22.0',
  license: 'GPL2',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=gnu89',
    'warning_level=2',
  ],
  meson_version: '>= 0.53.0',
)

mm_name = meson.project_name()
mm_version = meson.project_version()
version_array = mm_version.split('.')
mm_major_version = version_array[0].to_int()
mm_minor_version = version_array[1].to_int()
mm_micro_version = version_array[2].to_int()

mm_prefix = get_option('prefix')
mm_datadir = get_option('datadir')
mm_includedir = get_option('includedir')
mm_libdir = get_option('libdir')
mm_sbindir = get_option('sbindir')
mm_sysconfdir = get_option('sysconfdir')

mm_pkgdatadir = mm_datadir / mm_name
mm_pkgincludedir = mm_includedir / mm_name
mm_pkglibdir = mm_libdir / mm_name
mm_pkgsysconfdir = mm_sysconfdir / mm_name

mm_glib_name = 'libmm-glib'
mm_glib_pkgincludedir = mm_includedir / mm_glib_name

# libtool versioning for libmm-glib (-version-info c:r:a)
# - If the interface is unchanged, but the implementation has changed or been fixed, then increment r
# - Otherwise, increment c and zero r.
#   - If the interface has grown (that is, the new library is compatible with old code), increment a.
#   - If the interface has changed in an incompatible way (that is, functions have changed or been removed), then zero a.
current = 10
revision = 0
age = 10
mm_glib_version = '@0@.@1@.@2@'.format(current - age, age, revision)

mm_gir_version = '1.0'

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')
python = import('python').find_installation('python3')

source_root = meson.current_source_dir()
build_root = meson.current_build_dir()

build_aux_dir = source_root / 'build-aux'
templates_dir = source_root / 'build-aux/templates'
po_dir = source_root / 'po'
src_dir = source_root / 'src'
plugins_dir = source_root / 'src/plugins'

mm_mkenums = find_program(source_root / 'build-aux/mm-mkenums')

top_inc = include_directories('.')

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', mm_version)
config_h.set_quoted('VERSION', mm_version)

# Globally define_GNU_SOURCE and therefore enable the GNU extensions
config_h.set('_GNU_SOURCE', true)

# compiler flags
common_args = ['-DHAVE_CONFIG_H']

# compiler flags that are always enabled, even in release builds
cc_args = cc.get_supported_arguments([
  # warning on unused parameters is overkill, never do that
  '-Wno-unused-parameter',
  # function type cast disabled: used throughout the code especially to
  # cast GAsyncReadyCallbacks with the real object type instead of GObject
  '-Wno-cast-function-type',
  # all message protocol structs are packed, never complain about it
  '-Wno-packed',
  # we use some floating point ids as unknown, so we want to compare with them
  '-Wno-float-equal',
  # avoid warning if we're ignoring fields on purpose
  '-Wno-missing-field-initializers',
])

# tests are enabled by default
enable_tests = get_option('tests')
config_h.set('WITH_TESTS', enable_tests)

# strict flags to use in debug builds
if get_option('buildtype').contains('debug')
  cc_args += cc.get_supported_arguments([
    '-fno-strict-aliasing',
    '-Waggregate-return',
    '-Wcast-align',
    '-Wdeclaration-after-statement',
    '-Wdouble-promotion',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
    '-Wformat=2',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Winit-self',
    '-Winline',
    '-Wjump-misses-init',
    '-Wlogical-op',
    '-Wnested-externs',
    '-Wmaybe-uninitialized',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wmissing-noreturn',
    '-Wmissing-prototypes',
    '-Wnull-dereference',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wrestrict',
    '-Wreturn-type',
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Wsuggest-attribute=format',
    '-Wswitch-default',
    '-Wswitch-enum',
    '-Wundef',
    '-Wunused-but-set-variable',
    '-Wwrite-strings',
  ])
endif

add_project_arguments(common_args + cc_args, language: 'c')

glib_version = '2.56'

gio_unix_dep = dependency('gio-unix-2.0')
glib_dep = dependency('glib-2.0', version: '>= ' + glib_version)
gmodule_dep = dependency('gmodule-2.0')

deps = [
  glib_dep,
  dependency('gio-2.0'),
  dependency('gobject-2.0'),
]

c_args = [
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_' + glib_version.underscorify(),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_' + glib_version.underscorify(),
]

glib_deps = declare_dependency(
  dependencies: deps,
  compile_args: c_args,
)

# DBus system directory
dbus_dep = dependency('dbus-1')
dbus_interfaces_dir = dbus_dep.get_pkgconfig_variable('interfaces_dir', define_variable: ['datadir', mm_datadir])
dbus_system_bus_services_dir = dbus_dep.get_pkgconfig_variable('system_bus_services_dir', define_variable: ['datadir', mm_datadir])

dbus_policy_dir = get_option('dbus_policy_dir')
if dbus_policy_dir == ''
  dbus_policy_dir = dbus_dep.get_pkgconfig_variable('sysconfdir', define_variable: ['sysconfdir', mm_sysconfdir]) / 'dbus-1/system.d'
endif

enable_bash_completion = get_option('bash_completion')
if enable_bash_completion
  bash_completion_dep = dependency('bash-completion')
  bash_completion_completionsdir = bash_completion_dep.get_pkgconfig_variable(
    'completionsdir',
    # bash-completion 2.10 changed the substitutions
    define_variable: bash_completion_dep.version().version_compare('>= 2.10') ? ['datadir', mm_datadir] : ['prefix', mm_prefix],
  )
endif

# udev support (enabled by default)
enable_udev = get_option('udev')
if enable_udev
  gudev_dep = dependency('gudev-1.0', version: '>= 232')
endif
config_h.set('WITH_UDEV', enable_udev)

# udev base directory (required to install rules even when udev support is disabled)
udev_udevdir = get_option('udevdir')
if udev_udevdir == ''
  assert(enable_udev, 'udevdir must be explicitly given if udev support is disabled')
  udev_udevdir = dependency('udev').get_pkgconfig_variable('udevdir')
endif
udev_rulesdir = udev_udevdir / 'rules.d'

# systemd unit / service files
systemd_systemdsystemunitdir = get_option('systemdsystemunitdir')
install_systemdunitdir = (systemd_systemdsystemunitdir != 'no')

if install_systemdunitdir and systemd_systemdsystemunitdir == ''
  systemd_dep = dependency('systemd', not_found_message: 'systemd required but not found, please provide a valid systemd user unit dir or disable it')
  systemd_systemdsystemunitdir = systemd_dep.get_pkgconfig_variable('systemdsystemunitdir', define_variable: ['root_prefix', mm_prefix])
endif

# Suspend/resume support
enable_systemd_suspend_resume = get_option('systemd_suspend_resume')
enable_powerd_suspend_resume = get_option('powerd_suspend_resume')
assert(not (enable_systemd_suspend_resume and enable_powerd_suspend_resume), 'systemd_suspend_resume and powerd_suspend_resume are not supported at the same time')

# systemd journal support
enable_systemd_journal = get_option('systemd_journal')

if enable_systemd_suspend_resume or enable_systemd_journal
  libsystemd_dep = dependency('libsystemd', version: '>= 209', required: false)
  if not libsystemd_dep.found()
    libsystemd_dep = dependency('libsystemd-login', version: '>= 183', required: false)
    if not libsystemd_dep.found()
      libsystemd_dep = dependency(
        'libelogind',
        version: '>= 209',
        not_found_message: 'libsystemd, libsystemd-login or elogind must be available at runtime for suspend/resume or systemd journal support',
      )
    endif
  endif
endif
config_h.set('WITH_SUSPEND_RESUME', enable_systemd_suspend_resume or enable_powerd_suspend_resume)
config_h.set('WITH_SYSTEMD_JOURNAL', enable_systemd_journal)

# PolicyKit
polkit_opt = get_option('polkit')
enable_polkit = (polkit_opt != 'no')
if enable_polkit
  polkit_gobject_dep = dependency('polkit-gobject-1', version: '>= 0.97', not_found_message: 'PolicyKit development headers are required')

  polkit_gobject_policydir = polkit_gobject_dep.get_pkgconfig_variable('policydir', define_variable: ['prefix', mm_prefix])

  policy_conf = {'MM_DEFAULT_USER_POLICY': (polkit_opt == 'permissive' ? 'yes' : 'auth_self_keep')}
endif
config_h.set('WITH_POLKIT', enable_polkit)

# AT command via DBus support (disabled by default unless running in --debug)
# It is suggested that this option is only enabled in custom built systems and
# only if truly required.
enable_at_command_via_dbus = get_option('at_command_via_dbus')
config_h.set('WITH_AT_COMMAND_VIA_DBUS', enable_at_command_via_dbus)

# Builtin plugin support (disabled by default)
enable_builtin_plugins =  get_option('builtin_plugins')
config_h.set('WITH_BUILTIN_PLUGINS', enable_builtin_plugins)

# MBIM support (enabled by default)
enable_mbim = get_option('mbim')
if enable_mbim
  mbim_glib_dep = dependency('mbim-glib', version: '>= 1.30.0')
endif
config_h.set('WITH_MBIM', enable_mbim)

# QMI support (enabled by default)
enable_qmi = get_option('qmi')
if enable_qmi
  qmi_glib_dep = dependency('qmi-glib', version: '>= 1.34.0')
endif
config_h.set('WITH_QMI', enable_qmi)

# QRTR support (both as qrtr-glib and qmi-glib apis)
enable_qrtr = get_option('qrtr')
if enable_qrtr
  assert(enable_qmi, 'QRTR support requires QMI enabled')
  assert(qmi_glib_dep.get_pkgconfig_variable('qmi_qrtr_supported').to_int().is_odd(), 'Couldn\'t find QRTR support in qmi-glib.')
  qrtr_glib_dep = dependency('qrtr-glib', version: '>= 1.0.0')
endif
config_h.set('WITH_QRTR', enable_qrtr)

# Distribution version string
dist_version = get_option('dist_version')
if dist_version != ''
  config_h.set('MM_DIST_VERSION', dist_version)
endif

if enable_tests
  util_dep = cc.find_library('util')
endif

# introspection support
enable_gir = get_option('introspection')
if enable_gir
  dependency('gobject-introspection-1.0', version: '>= 0.9.6')
endif

# vala support
enable_vapi = get_option('vapi')

# gtkdoc support
enable_gtk_doc = get_option('gtk_doc')

enable_plugins = not get_option('auto_features').disabled()

plugins_shared_reqs = {
  'fibocom': enable_mbim,
  'foxconn': enable_mbim,
  'icera': true,
  'novatel': true,
  'option': true,
  'sierra': true,
  'telit': true,
  'xmm': true,
}


fibocom_shared_reqs = ['xmm']
dell_shared_reqs = ['novatel', 'sierra', 'telit', 'xmm']
if enable_mbim
  fibocom_shared_reqs += ['fibocom']
  dell_shared_reqs += ['foxconn']
endif

plugins_options_reqs = {
  'altair-lte': {'available': true, 'shared': []},
  'anydata': {'available': true, 'shared': []},
  'broadmobi': {'available': true, 'shared': []},
  'cinterion': {'available': true, 'shared': []},
  'dell': {'available': true, 'shared': dell_shared_reqs},
  'dlink': {'available': true, 'shared': []},
  'fibocom': {'available': true, 'shared': fibocom_shared_reqs},
  'foxconn': {'available': enable_mbim, 'shared': ['foxconn']},
  'generic': {'available': true, 'shared': []},
  'gosuncn': {'available': true, 'shared': []},
  'haier': {'available': true, 'shared': []},
  'huawei': {'available': true, 'shared': []},
  'intel': {'available': true, 'shared': ['xmm']},
  'iridium': {'available': true, 'shared': []},
  'linktop': {'available': true, 'shared': []},
  'longcheer': {'available': true, 'shared': []},
  'mbm': {'available': true, 'shared': []},
  'motorola': {'available': true, 'shared': []},
  'mtk': {'available': true, 'shared': []},
  'nokia': {'available': true, 'shared': []},
  'nokia-icera': {'available': true, 'shared': ['icera']},
  'novatel': {'available': true, 'shared': ['novatel']},
  'novatel-lte': {'available': true, 'shared': []},
  'option': {'available': true, 'shared': ['option']},
  'option-hso': {'available': true, 'shared': ['option']},
  'pantech': {'available': true, 'shared': []},
  'qcom-soc': {'available': enable_qmi, 'shared': []},
  'quectel': {'available': true, 'shared': []},
  'samsung': {'available': true, 'shared': ['icera']},
  'sierra-legacy': {'available': true, 'shared': ['icera', 'sierra']},
  'sierra': {'available': true, 'shared': ['xmm']},
  'simtech': {'available': true, 'shared': []},
  'telit': {'available': true, 'shared': ['telit']},
  'thuraya': {'available': true, 'shared': []},
  'tplink': {'available': true, 'shared': []},
  'ublox': {'available': true, 'shared': []},
  'via': {'available': true, 'shared': []},
  'wavecom': {'available': true, 'shared': []},
  'x22x': {'available': true, 'shared': []},
  'zte': {'available': true, 'shared': ['icera']},
}

plugins_shared = {}
foreach plugin_name, _: plugins_shared_reqs
  plugins_shared += {plugin_name: false}
endforeach

plugins_options = {}
foreach plugin_name, plugin_reqs: plugins_options_reqs
  plugin_opt = get_option('plugin_' + plugin_name.underscorify())
  assert(plugin_reqs['available'] or not plugin_opt.enabled(), '@0@ is not available'.format(plugin_name))
  plugin_enabled = not plugin_opt.disabled() and plugin_reqs['available']
  if plugin_enabled
    foreach plugin_req: plugin_reqs['shared']
      if plugins_shared_reqs[plugin_req]
        plugins_shared += {plugin_req: true}
      else
        assert(plugin_opt.enabled(), '@0@ required @1@ but is not available'.format(plugin_name, plugin_req))
        plugin_enabled = false
        break
      endif
    endforeach
    config_h.set('ENABLE_PLUGIN_' + plugin_name.underscorify().to_upper(), true)
  endif
  plugins_options += {plugin_name: plugin_enabled}
endforeach

version_conf = {
  'MM_MAJOR_VERSION': mm_major_version,
  'MM_MINOR_VERSION': mm_minor_version,
  'MM_MICRO_VERSION': mm_micro_version,
  'VERSION': mm_version,
}

subdir('po')
subdir('data')
if get_option('examples')
  subdir('data/dispatcher-connection')
  subdir('data/dispatcher-fcc-unlock')
endif
subdir('introspection')
subdir('include')

subdir('libqcdm/src')
if enable_tests
  subdir('libqcdm/tests')
endif

subdir('libmm-glib')
subdir('src')
subdir('cli')

if enable_tests
  subdir('test')
  subdir('tools/tests')
endif

subdir('examples/sms-c')

enable_man = get_option('man')
if enable_man
  subdir('docs/man')
endif

if enable_gtk_doc
  subdir('docs/reference/api')
  subdir('docs/reference/libmm-glib')
endif

enable_fuzzer = get_option('fuzzer')

configure_file(
  output: 'config.h',
  configuration: config_h,
)

summary({
  'compiler': cc.get_id(),
  'cflags': cc_args,
}, section: 'Build')

summary({
  'prefix': mm_prefix,
  'configuration directory': mm_pkgsysconfdir,
  'D-Bus policy directory': dbus_policy_dir,
  'udev base directory': udev_udevdir,
  'systemd user unit directory': systemd_systemdsystemunitdir,
}, section: 'System paths')

summary({
  'udev': enable_udev,
  'policykit': polkit_opt,
  'mbim': enable_mbim,
  'qmi': enable_qmi,
  'qrtr': enable_qrtr,
  'systemd suspend/resume': enable_systemd_suspend_resume,
  'powerd suspend/resume': enable_powerd_suspend_resume,
  'systemd journal': enable_systemd_journal,
  'at command via dbus': enable_at_command_via_dbus,
  'builtin plugins': enable_builtin_plugins,
}, section: 'Features')

summary(plugins_shared, section: 'Shared utils')

summary(plugins_options, section: 'Plugins')

summary({
  'gobject introspection': enable_gir,
  'Man': enable_man,
  'Documentation': enable_gtk_doc,
  'bash completion': enable_bash_completion,
  'vala bindings': enable_vapi,
  'code coverage': get_option('b_coverage'),
  'fuzzer': enable_fuzzer,
}, section: 'Miscellaneous')
