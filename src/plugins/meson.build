# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

if not enable_builtin_plugins
  symbol_map = plugins_dir / 'symbol.map'
  ldflags = cc.get_supported_link_arguments('-Wl,--version-script,@0@'.format(symbol_map))
endif

# common service test support
plugins_common_test_dep = []
if enable_tests
  sources = files(
    'tests/test-fixture.c',
    'tests/test-helpers.c',
    'tests/test-port-context.c',
  )

  deps = [
    libhelpers_dep,
    libmm_test_generated_dep
  ]

  libmm_test_common = library(
    'mm-test-common',
    sources: sources,
    include_directories: top_inc,
    dependencies: deps + [gio_unix_dep],
    c_args: '-DTEST_SERVICES="@0@"'.format(build_root / 'data/tests'),
  )

  libmm_test_common_dep = declare_dependency(
    include_directories: 'tests',
    dependencies: deps,
    link_with: libmm_test_common,
  )

  plugins_common_test_dep += [ libmm_test_common_dep ]
endif

# plugins
plugins = {}
plugins_data = []
plugins_udev_rules = []
plugins_test_udev_rules_dir_c_args = []
plugins_test_keyfile_c_args = []

# never include static libs as deps when building
# plugins or shared utils modules
plugins_incs = [
  top_inc,
  src_inc,
  kerneldevice_inc,
  plugins_inc,
]

plugins_deps = [
  daemon_enums_types_dep,
  libmm_glib_dep,
]

if enable_mbim
  plugins_deps += mbim_glib_dep
endif

if enable_qmi
  plugins_deps += qmi_glib_dep
endif

# common Fibocom support library (MBIM only)
if plugins_shared['fibocom']
  fibocom_inc = include_directories('fibocom')

  c_args = '-DMM_MODULE_NAME="shared-fibocom"'

  sources = files(
    'fibocom/mm-shared.c',
    'fibocom/mm-shared-fibocom.c',
  )

  plugins += {'shared-fibocom': {
    'plugin': false,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': c_args},
  }}
endif

# Common Foxconn modem support library (MBIM only)
if plugins_shared['foxconn']
  foxconn_inc = include_directories('foxconn')

  sources = files(
    'foxconn/mm-broadband-modem-mbim-foxconn.c',
    'foxconn/mm-shared.c',
  )

  c_args = [
    '-DMM_MODULE_NAME="shared-foxconn"',
    '-DPKGDATADIR="@0@"'.format(mm_pkgdatadir),
  ]

  plugins += {'shared-foxconn': {
    'plugin': false,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': c_args},
  }}
endif

# common icera support
if plugins_shared['icera']
  icera_inc = include_directories('icera')

  common_c_args = '-DMM_MODULE_NAME="shared-icera"'

  sources = files(
    'icera/mm-broadband-bearer-icera.c',
    'icera/mm-broadband-modem-icera.c',
    'icera/mm-shared.c',
  )

  plugins += {'shared-icera': {
    'plugin': false,
    'helper': {'sources': files('icera/mm-modem-helpers-icera.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('icera/tests/test-modem-helpers-icera.c'), 'include_directories': plugins_incs + [icera_inc], 'dependencies': libhelpers_dep},
  }}
endif

# common novatel support
if plugins_shared['novatel']
  novatel_inc = include_directories('novatel')

  sources = files(
    'novatel/mm-broadband-modem-novatel.c',
    'novatel/mm-common-novatel.c',
    'novatel/mm-shared.c',
  )

  plugins += {'shared-novatel': {
    'plugin': false,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="shared-novatel"'},
  }}
endif

# common option support
if plugins_shared['option']
  sources = files(
    'option/mm-broadband-modem-option.c',
    'option/mm-shared.c',
    'option/mm-shared-option.c',
    'option/mm-sim-option.c',
  )

  plugins += {'shared-option': {
    'plugin': false,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="shared-option"'},
  }}
endif

# common sierra support
if plugins_shared['sierra']
  sierra_inc = include_directories('sierra')

  common_c_args = '-DMM_MODULE_NAME="shared-sierra"'

  sources = files(
    'sierra/mm-broadband-bearer-sierra.c',
    'sierra/mm-broadband-modem-sierra.c',
    'sierra/mm-common-sierra.c',
    'sierra/mm-shared.c',
    'sierra/mm-sim-sierra.c',
  )

  plugins += {'shared-sierra': {
    'plugin': false,
    'helper': {'sources': files('sierra/mm-modem-helpers-sierra.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('sierra/tests/test-modem-helpers-sierra.c'), 'include_directories': sierra_inc, 'dependencies': libhelpers_dep},
  }}
endif

# common telit support
if plugins_shared['telit']
  telit_inc = include_directories('telit')

  common_c_args = '-DMM_MODULE_NAME="shared-telit"'

  headers = files('telit/mm-modem-helpers-telit.h')

  sources = files(
    'telit/mm-broadband-modem-telit.c',
    'telit/mm-common-telit.c',
    'telit/mm-shared.c',
    'telit/mm-shared-telit.c',
  )

  enums_types = 'mm-telit-enums-types'

  sources += custom_target(
    enums_types + '.c',
    input: headers,
    output: enums_types + '.c',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-telit-enums-types.h"\n',
      '--template', files(templates_dir / enums_types + '.c.template'),
      '@INPUT@'],
    capture: true,
  )

  sources += custom_target(
    enums_types + '.h',
    input: headers,
    output: enums_types + '.h',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-modem-helpers-telit.h"\n#ifndef __MM_TELIT_ENUMS_TYPES_H__\n#define __MM_TELIT_ENUMS_TYPES_H__\n',
      '--template', files(templates_dir / enums_types + '.h.template'),
      '--ftail', '#endif /* __MM_TELIT_ENUMS_TYPES_H__ */\n',
      '@INPUT@'],
    capture: true,
  )

  if enable_mbim
    sources += files('telit/mm-broadband-modem-mbim-telit.c')
  endif

  plugins += {'shared-telit': {
    'plugin': false,
    'helper': {'sources': files('telit/mm-modem-helpers-telit.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs + [telit_inc], 'c_args': common_c_args},
    'test': {'sources': files('telit/tests/test-mm-modem-helpers-telit.c'), 'include_directories': telit_inc, 'dependencies': plugins_common_test_dep},
  }}
endif

# common xmm support
if plugins_shared['xmm']
  xmm_inc = include_directories('xmm')

  common_c_args = '-DMM_MODULE_NAME="shared-xmm"'

  sources = files(
    'xmm/mm-broadband-modem-xmm.c',
    'xmm/mm-shared.c',
    'xmm/mm-shared-xmm.c',
  )

  if enable_mbim
    sources += files('xmm/mm-broadband-modem-mbim-xmm.c')
  endif

  plugins += {'shared-xmm': {
    'plugin': false,
    'helper': {'sources': files('xmm/mm-modem-helpers-xmm.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('xmm/tests/test-modem-helpers-xmm.c'), 'include_directories': xmm_inc, 'dependencies': libhelpers_dep},
  }}
endif

# plugin: altair lte
if plugins_options['altair-lte']
  common_c_args = '-DMM_MODULE_NAME="altair-lte"'

  sources = files(
    'altair/mm-broadband-bearer-altair-lte.c',
    'altair/mm-broadband-modem-altair-lte.c',
    'altair/mm-plugin-altair-lte.c',
  )

  plugins += {'plugin-altair-lte': {
    'plugin': true,
    'helper': {'sources': files('altair/mm-modem-helpers-altair-lte.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('altair/tests/test-modem-helpers-altair-lte.c'), 'include_directories': include_directories('altair'), 'dependencies': libhelpers_dep},
  }}
endif

# plugin: anydata
if plugins_options['anydata']
  sources = files(
    'anydata/mm-broadband-modem-anydata.c',
    'anydata/mm-plugin-anydata.c',
  )

  plugins += {'plugin-anydata': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="anydata"'},
  }}
endif

# plugin: broadmobi
if plugins_options['broadmobi']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_BROADMOBI="@0@"'.format(plugins_dir / 'broadmobi')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-broadmobi': {
    'plugin': true,
    'module': {'sources': files('broadmobi/mm-plugin-broadmobi.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="broadmobi"']},
  }}

  plugins_udev_rules += files('broadmobi/77-mm-broadmobi-port-types.rules')
endif

# plugin: cinterion (previously siemens)
if plugins_options['cinterion']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_CINTERION="@0@"'.format(plugins_dir / 'cinterion')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="cinterion"']

  sources = files(
    'cinterion/mm-broadband-bearer-cinterion.c',
    'cinterion/mm-broadband-modem-cinterion.c',
    'cinterion/mm-plugin-cinterion.c',
    'cinterion/mm-shared-cinterion.c',
  )

  if enable_qmi
    sources += files('cinterion/mm-broadband-modem-qmi-cinterion.c')
  endif

  if enable_mbim
    sources += files('cinterion/mm-broadband-modem-mbim-cinterion.c')
  endif

  plugins += {'plugin-cinterion': {
    'plugin': true,
    'helper': {'sources': files('cinterion/mm-modem-helpers-cinterion.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('cinterion/tests/test-modem-helpers-cinterion.c'), 'include_directories': plugins_incs + [include_directories('cinterion')], 'dependencies': libport_dep},
  }}

  plugins_udev_rules += files('cinterion/77-mm-cinterion-port-types.rules')
endif

# plugin: dell
if plugins_options['dell']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_DELL="@0@"'.format(plugins_dir / 'dell')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  incs = plugins_incs + [
    novatel_inc,
    sierra_inc,
    telit_inc,
    xmm_inc,
  ]

  if enable_mbim
    incs += [foxconn_inc]
  endif

  plugins += {'plugin-dell': {
    'plugin': true,
    'module': {'sources': files('dell/mm-plugin-dell.c'), 'include_directories': incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="dell"']},
  }}

  plugins_udev_rules += files('dell/77-mm-dell-port-types.rules')
endif

# plugin: dlink
if plugins_options['dlink']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_DLINK="@0@"'.format(plugins_dir / 'dlink')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-dlink': {
    'plugin': true,
    'module': {'sources': files('dlink/mm-plugin-dlink.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="d-link"']},
  }}

  plugins_udev_rules += files('dlink/77-mm-dlink-port-types.rules')
endif

# plugin: fibocom
if plugins_options['fibocom']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_FIBOCOM="@0@"'.format(plugins_dir / 'fibocom')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  incs = plugins_incs + [xmm_inc]

  sources = files(
    'fibocom/mm-broadband-bearer-fibocom-ecm.c',
    'fibocom/mm-broadband-modem-fibocom.c',
    'fibocom/mm-plugin-fibocom.c',
  )
  if enable_mbim
    incs += [fibocom_inc]

    sources += files(
      'fibocom/mm-broadband-modem-mbim-xmm-fibocom.c',
      'fibocom/mm-broadband-modem-mbim-fibocom.c',
    )
  endif
  plugins += {'plugin-fibocom': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="fibocom"']},
  }}

  plugins_udev_rules += files('fibocom/77-mm-fibocom-port-types.rules')
endif

# plugin: foxconn
if plugins_options['foxconn']
  foxconn_dir = plugins_dir / 'foxconn'

  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_FOXCONN="@0@"'.format(foxconn_dir)]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  test_keyfile_c_args = ['-DTESTKEYFILE_FOXCONN_T77W968="@0@"'.format(foxconn_dir / 'mm-foxconn-t77w968-carrier-mapping.conf')]
  plugins_test_keyfile_c_args += test_keyfile_c_args

  plugins += {'plugin-foxconn': {
    'plugin': true,
    'module': {'sources': files('foxconn/mm-plugin-foxconn.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + test_keyfile_c_args + ['-DMM_MODULE_NAME="foxconn"']},
  }}

  plugins_data += files(
    'foxconn/mm-foxconn-t77w968-carrier-mapping.conf',
  )
  plugins_udev_rules += files('foxconn/77-mm-foxconn-port-types.rules')
endif

# plugin: generic
if plugins_options['generic']
  plugins += {'plugin-generic': {
    'plugin': true,
    'module': {'sources': files('generic/mm-plugin-generic.c'), 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="generic"'},
    'test': {'sources': files('generic/tests/test-service-generic.c'), 'include_directories': include_directories('generic'), 'dependencies': plugins_common_test_dep, 'c_args': '-DCOMMON_GSM_PORT_CONF="@0@"'.format(plugins_dir / 'tests/gsm-port.conf')},
  }}
endif

# plugin: gosuncn
if plugins_options['gosuncn']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_GOSUNCN="@0@"'.format(plugins_dir / 'gosuncn')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-gosuncn': {
    'plugin': true,
    'module': {'sources': files('gosuncn/mm-plugin-gosuncn.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="gosuncn"']},
  }}

  plugins_udev_rules += files('gosuncn/77-mm-gosuncn-port-types.rules')
endif

# plugin: haier
if plugins_options['haier']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_HAIER="@0@"'.format(plugins_dir / 'haier')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-haier': {
    'plugin': true,
    'module': {'sources': files('haier/mm-plugin-haier.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="haier"']},
  }}

  plugins_udev_rules += files('haier/77-mm-haier-port-types.rules')
endif

# plugin: huawei
if plugins_options['huawei']
  huawei_inc = include_directories('huawei')

  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_HUAWEI="@0@"'.format(plugins_dir / 'huawei')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="huawei"']

  headers = files('huawei/mm-modem-helpers-huawei.h')

  sources = files(
    'huawei/mm-broadband-bearer-huawei.c',
    'huawei/mm-broadband-modem-huawei.c',
    'huawei/mm-plugin-huawei.c',
    'huawei/mm-sim-huawei.c',
  )

  enums_types = 'mm-huawei-enums-types'

  enums_sources = []
  enums_sources += custom_target(
    enums_types + '.c',
    input: headers,
    output: enums_types + '.c',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-huawei-enums-types.h"\n',
      '--template', files(templates_dir / enums_types + '.c.template'),
      '@INPUT@'],
    capture: true,
  )

  enums_sources += custom_target(
    enums_types + '.h',
    input: headers,
    output: enums_types + '.h',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-modem-helpers-huawei.h"\n#ifndef __MM_HUAWEI_ENUMS_TYPES_H__\n#define __MM_HUAWEI_ENUMS_TYPES_H__\n',
      '--template', files(templates_dir / enums_types + '.h.template'),
      '--ftail', '#endif /* __MM_HUAWEI_ENUMS_TYPES_H__ */\n',
      '@INPUT@'],
    capture: true,
  )

  plugins += {'plugin-huawei': {
    'plugin': true,
    'helper': {'sources': files('huawei/mm-modem-helpers-huawei.c'), 'include_directories': plugins_incs + [huawei_inc], 'c_args': common_c_args},
    'module': {'sources': sources + enums_sources, 'include_directories': plugins_incs + [huawei_inc], 'c_args': common_c_args},
    'test': {'sources': files('huawei/tests/test-modem-helpers-huawei.c') + enums_sources, 'include_directories': huawei_inc, 'dependencies': libhelpers_dep},
  }}

  plugins_udev_rules += files('huawei/77-mm-huawei-net-port-types.rules')
endif

# plugin: intel
if plugins_options['intel']
  sources = files(
    'intel/mm-plugin-intel.c',
  )

  if enable_mbim
    sources += files('intel/mm-broadband-modem-mbim-intel.c')
  endif

  common_c_args = '-DMM_MODULE_NAME="intel"'

  plugins += {'plugin-intel': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs + [xmm_inc], 'c_args': common_c_args},
  }}
endif

# plugin: iridium
if plugins_options['iridium']
  sources = files(
    'iridium/mm-bearer-iridium.c',
    'iridium/mm-broadband-modem-iridium.c',
    'iridium/mm-plugin-iridium.c',
    'iridium/mm-sim-iridium.c',
  )

  plugins += {'plugin-iridium': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="iridium"'},
  }}
endif

# plugin: linktop
if plugins_options['linktop']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_LINKTOP="@0@"'.format(plugins_dir / 'linktop')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="linktop"']

  sources = files(
    'linktop/mm-plugin-linktop.c',
    'linktop/mm-broadband-modem-linktop.c',
  )

  plugins += {'plugin-linktop': {
    'plugin': true,
    'helper': {'sources': files('linktop/mm-modem-helpers-linktop.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('linktop/tests/test-modem-helpers-linktop.c'), 'include_directories': include_directories('linktop'), 'dependencies': libhelpers_dep},
  }}

  plugins_udev_rules += files('linktop/77-mm-linktop-port-types.rules')
endif

# plugin: longcheer (and rebranded dongles)
if plugins_options['longcheer']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_LONGCHEER="@0@"'.format(plugins_dir / 'longcheer')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  sources = files(
    'longcheer/mm-broadband-modem-longcheer.c',
    'longcheer/mm-plugin-longcheer.c',
  )

  plugins += {'plugin-longcheer': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="longcheer"']},
  }}

  plugins_udev_rules += files('longcheer/77-mm-longcheer-port-types.rules')
endif

# plugin: ericsson mbm
if plugins_options['mbm']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_MBM="@0@"'.format(plugins_dir / 'mbm')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="ericsson-mbm"']

  sources = files(
    'mbm/mm-broadband-bearer-mbm.c',
    'mbm/mm-broadband-modem-mbm.c',
    'mbm/mm-plugin-mbm.c',
    'mbm/mm-sim-mbm.c',
  )

  plugins += {'plugin-ericsson-mbm': {
    'plugin': true,
    'helper': {'sources': files('mbm/mm-modem-helpers-mbm.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('mbm/tests/test-modem-helpers-mbm.c'), 'include_directories': plugins_incs + [include_directories('mbm')], 'dependencies': libhelpers_dep},
  }}

  plugins_udev_rules += files('mbm/77-mm-ericsson-mbm.rules')
endif

# plugin: motorola
if plugins_options['motorola']
  sources = files(
    'motorola/mm-broadband-modem-motorola.c',
    'motorola/mm-plugin-motorola.c',
  )

  plugins += {'plugin-motorola': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="motorola"'},
  }}
endif

# plugin: mtk
if plugins_options['mtk']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_MTK="@0@"'.format(plugins_dir / 'mtk')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  sources = files(
    'mtk/mm-broadband-modem-mtk.c',
    'mtk/mm-plugin-mtk.c',
  )

  plugins += {'plugin-mtk': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="motorola"']},
  }}

  plugins_udev_rules += files('mtk/77-mm-mtk-port-types.rules')
endif

# plugin: nokia
if plugins_options['nokia']
  sources = files(
    'nokia/mm-broadband-modem-nokia.c',
    'nokia/mm-plugin-nokia.c',
    'nokia/mm-sim-nokia.c',
  )

  plugins += {'plugin-nokia': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="nokia"'},
  }}
endif

# plugin: nokia (icera)
if plugins_options['nokia-icera']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_NOKIA_ICERA="@0@"'.format(plugins_dir / 'nokia')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-nokia-icera': {
    'plugin': true,
    'module': {'sources': files('nokia/mm-plugin-nokia-icera.c'), 'include_directories': plugins_incs + [icera_inc], 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="nokia-icera"']},
  }}

  plugins_udev_rules += files('nokia/77-mm-nokia-port-types.rules')
endif

# plugin: novatel non-lte
if plugins_options['novatel']
  plugins += {'plugin-novatel': {
    'plugin': true,
    'module': {'sources': files('novatel/mm-plugin-novatel.c'), 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="novatel"'},
  }}
endif

# plugin: novatel lte
if plugins_options['novatel-lte']
  sources = files(
    'novatel/mm-plugin-novatel-lte.c',
    'novatel/mm-broadband-modem-novatel-lte.c',
    'novatel/mm-broadband-bearer-novatel-lte.c',
    'novatel/mm-sim-novatel-lte.c',
  )

  plugins += {'plugin-novatel-lte': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="novatel-lte"'},
  }}
endif

# plugin: option
if plugins_options['option']
  plugins += {'plugin-option': {
    'plugin': true,
    'module': {'sources': files('option/mm-plugin-option.c'), 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="option"'},
  }}
endif

# plugin: option hso
if plugins_options['option-hso']
  sources = files(
    'option/mm-plugin-hso.c',
    'option/mm-broadband-bearer-hso.c',
    'option/mm-broadband-modem-hso.c',
  )

  plugins += {'plugin-option-hso': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="option-hso"'},
  }}
endif

# plugin: pantech
if plugins_options['pantech']
  sources = files(
    'pantech/mm-broadband-modem-pantech.c',
    'pantech/mm-plugin-pantech.c',
    'pantech/mm-sim-pantech.c',
  )

  plugins += {'plugin-pantech': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="pantech"'},
  }}
endif

# plugin: qcom-soc
if plugins_options['qcom-soc']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_QCOM_SOC="@0@"'.format(plugins_dir / 'qcom-soc')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  sources = files(
    'qcom-soc/mm-broadband-modem-qmi-qcom-soc.c',
    'qcom-soc/mm-plugin-qcom-soc.c',
  )

  plugins += {'plugin-qcom-soc': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="qcom-soc"']},
  }}

  plugins_udev_rules += files('qcom-soc/77-mm-qcom-soc.rules')
endif

# plugin: quectel
if plugins_options['quectel']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_QUECTEL="@0@"'.format(plugins_dir / 'quectel')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="quectel"']

  sources = files(
    'quectel/mm-broadband-modem-quectel.c',
    'quectel/mm-plugin-quectel.c',
    'quectel/mm-shared-quectel.c',
  )

  if enable_qmi
    sources += files('quectel/mm-broadband-modem-qmi-quectel.c')
  endif

  if enable_mbim
    sources += files('quectel/mm-broadband-modem-mbim-quectel.c')
  endif

  plugins += {'plugin-quectel': {
    'plugin': true,
    'helper': {'sources': files('quectel/mm-modem-helpers-quectel.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('quectel/tests/test-modem-helpers-quectel.c'), 'include_directories': include_directories('quectel'), 'dependencies': libhelpers_dep},
  }}

  plugins_udev_rules += files('quectel/77-mm-quectel-port-types.rules')
endif

# plugin: samsung
if plugins_options['samsung']
  sources = files(
    'samsung/mm-broadband-modem-samsung.c',
    'samsung/mm-plugin-samsung.c',
  )

  plugins += {'plugin-samsung': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs + [icera_inc], 'c_args': '-DMM_MODULE_NAME="samsung"'},
  }}
endif

# plugin: sierra (legacy)
if plugins_options['sierra-legacy']
  sources = files(
    'sierra/mm-broadband-modem-sierra-icera.c',
    'sierra/mm-plugin-sierra-legacy.c',
  )

  plugins += {'plugin-sierra-legacy': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs + [icera_inc], 'c_args': '-DMM_MODULE_NAME="sierra-legacy"'},
  }}
endif

# plugin: sierra (new QMI or MBIM modems)
if plugins_options['sierra']
  plugins += {'plugin-sierra': {
    'plugin': true,
    'module': {'sources': files('sierra/mm-plugin-sierra.c'), 'include_directories': plugins_incs + [xmm_inc], 'c_args': '-DMM_MODULE_NAME="sierra"'},
  }}

  plugins_udev_rules += files('sierra/77-mm-sierra.rules')
endif

# plugin: simtech
if plugins_options['simtech']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_SIMTECH="@0@"'.format(plugins_dir / 'simtech')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  common_c_args = test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="simtech"']

  sources = files(
    'simtech/mm-broadband-modem-simtech.c',
    'simtech/mm-plugin-simtech.c',
    'simtech/mm-shared-simtech.c',
  )

  if enable_qmi
    sources += files('simtech/mm-broadband-modem-qmi-simtech.c')
  endif

  plugins += {'plugin-simtech': {
    'plugin': true,
    'helper': {'sources': files('simtech/mm-modem-helpers-simtech.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('simtech/tests/test-modem-helpers-simtech.c'), 'include_directories': plugins_incs + [include_directories('simtech')], 'dependencies': libport_dep},
  }}

  plugins_udev_rules += files('simtech/77-mm-simtech-port-types.rules')
endif

# plugin: telit
if plugins_options['telit']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_TELIT="@0@"'.format(plugins_dir / 'telit')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-telit': {
    'plugin': true,
    'module': {'sources': files('telit/mm-plugin-telit.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="telit"']},
  }}

  plugins_udev_rules += files('telit/77-mm-telit-port-types.rules')
endif

# plugin: thuraya xt
if plugins_options['thuraya']
  common_c_args = ['-DMM_MODULE_NAME="thuraya"']

  sources = files(
    'thuraya/mm-broadband-modem-thuraya.c',
    'thuraya/mm-plugin-thuraya.c',
  )

  plugins += {'plugin-thuraya': {
    'plugin': true,
    'helper': {'sources': files('thuraya/mm-modem-helpers-thuraya.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': common_c_args},
    'test': {'sources': files('thuraya/tests/test-mm-modem-helpers-thuraya.c'), 'include_directories': include_directories('thuraya'), 'dependencies': libhelpers_dep},
  }}
endif

# plugin: tplink
if plugins_options['tplink']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_TPLINK="@0@"'.format(plugins_dir / 'tplink')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  plugins += {'plugin-tplink': {
    'plugin': true,
    'module': {'sources': files('tplink/mm-plugin-tplink.c'), 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="tp-link"']},
  }}

  plugins_udev_rules += files('tplink/77-mm-tplink-port-types.rules')
endif

# plugin: u-blox
if plugins_options['ublox']
  ublox_inc = include_directories('ublox')

  common_c_args = '-DMM_MODULE_NAME="u-blox"'

  headers = files('ublox/mm-modem-helpers-ublox.h')

  sources = files(
    'ublox/mm-broadband-bearer-ublox.c',
    'ublox/mm-broadband-modem-ublox.c',
    'ublox/mm-plugin-ublox.c',
    'ublox/mm-sim-ublox.c',
  )

  enums_types = 'mm-ublox-enums-types'

  sources += custom_target(
    enums_types + '.c',
    input: headers,
    output: enums_types + '.c',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-ublox-enums-types.h"\n',
      '--template', files(templates_dir / enums_types + '.c.template'),
      '@INPUT@'],
    capture: true,
  )

  sources += custom_target(
    enums_types + '.h',
    input: headers,
    output: enums_types + '.h',
    command: [
      python,
      mm_mkenums,
      '--fhead', '#include "mm-modem-helpers-ublox.h"\n#ifndef __MM_UBLOX_ENUMS_TYPES_H__\n#define __MM_UBLOX_ENUMS_TYPES_H__\n',
      '--template', files(templates_dir / enums_types + '.h.template'),
      '--ftail', '#endif /* __MM_UBLOX_ENUMS_TYPES_H__ */\n',
      '@INPUT@'],
    capture: true,
  )

  plugins += {'plugin-ublox': {
    'plugin': true,
    'helper': {'sources': files('ublox/mm-modem-helpers-ublox.c'), 'include_directories': plugins_incs, 'c_args': common_c_args},
    'module': {'sources': sources, 'include_directories': plugins_incs + [ublox_inc], 'c_args': common_c_args},
    'test': {'sources': files('ublox/tests/test-modem-helpers-ublox.c'), 'include_directories': ublox_inc, 'dependencies': plugins_common_test_dep},
  }}

  plugins_udev_rules += files('ublox/77-mm-ublox-port-types.rules')
endif

# plugin: via
if plugins_options['via']
  sources = files(
    'via/mm-broadband-modem-via.c',
    'via/mm-plugin-via.c',
  )

  plugins += {'plugin-via': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="via"'},
  }}
endif

# plugin: wavecom (now sierra airlink)
if plugins_options['wavecom']
  sources = files(
    'wavecom/mm-broadband-modem-wavecom.c',
    'wavecom/mm-plugin-wavecom.c',
  )

  plugins += {'plugin-wavecom': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': '-DMM_MODULE_NAME="wavecom"'},
  }}
endif

# plugin: alcatel/TCT/JRD x220D and possibly others
if plugins_options['x22x']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_X22X="@0@"'.format(plugins_dir / 'x22x')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  sources = files(
    'x22x/mm-broadband-modem-x22x.c',
    'x22x/mm-plugin-x22x.c',
  )

  plugins += {'plugin-x22x': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs, 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="x22x"']},
  }}

  plugins_udev_rules += files('x22x/77-mm-x22x-port-types.rules')
endif

# plugin: zte
if plugins_options['zte']
  test_udev_rules_dir_c_args = ['-DTESTUDEVRULESDIR_ZTE="@0@"'.format(plugins_dir / 'zte')]
  plugins_test_udev_rules_dir_c_args += test_udev_rules_dir_c_args

  sources = files(
    'zte/mm-broadband-modem-zte.c',
    'zte/mm-broadband-modem-zte-icera.c',
    'zte/mm-common-zte.c',
    'zte/mm-plugin-zte.c',
  )

  plugins += {'plugin-zte': {
    'plugin': true,
    'module': {'sources': sources, 'include_directories': plugins_incs + [icera_inc], 'c_args': test_udev_rules_dir_c_args + ['-DMM_MODULE_NAME="zte"']},
  }}

  plugins_udev_rules += files('zte/77-mm-zte-port-types.rules')
endif

builtin_sources = []
builtin_plugins = []

if enable_builtin_plugins
   builtin_sources += files('mm-builtin-plugins.c')
endif

foreach plugin_name, plugin_data: plugins
  libpluginhelpers = []
  if plugin_data.has_key('helper')
    libpluginhelpers = static_library(
      'helpers-' + plugin_name,
      dependencies: plugins_deps,
      kwargs: plugin_data['helper'],
    )
  endif

  module_args = plugin_data['module']
  if not enable_builtin_plugins
    if plugin_data['plugin']
      module_args += {
        'link_args': ldflags,
        'link_depends': symbol_map,
      }
    endif

    shared_module(
      'mm-' + plugin_name,
      dependencies: plugins_deps,
      link_with: libpluginhelpers,
      kwargs: module_args,
      install: true,
      install_dir: mm_pkglibdir,
    )
  else
    libplugin = static_library(
      'mm-' + plugin_name,
      dependencies: plugins_deps,
      link_with: libpluginhelpers,
      kwargs: module_args,
    )
    builtin_plugins += libplugin
  endif

  if enable_tests
    if plugin_data.has_key('test')
      test_unit = 'test-' + plugin_name

      exe = executable(
        test_unit,
        link_with: libpluginhelpers,
        kwargs: plugin_data['test'],
      )

      test(test_unit, exe)
    endif
  endif
endforeach

install_data(
  plugins_data,
  install_dir: mm_pkgdatadir,
)

install_data(
  plugins_udev_rules,
  install_dir: udev_rulesdir,
)

# udev-rules and keyfiles tests
test_units = {
  'udev-rules': {'include_directories': top_inc, 'dependencies': libkerneldevice_dep, 'c_args': plugins_test_udev_rules_dir_c_args},
  'keyfiles': {'include_directories': [top_inc, src_inc], 'dependencies': libmm_glib_dep, 'c_args': plugins_test_keyfile_c_args},
}

foreach name, data: test_units
  test_name = 'test-' + name

  exe = executable(
    test_name,
    sources: 'tests/@0@.c'.format(test_name),
    kwargs: data,
  )

  test(test_name, exe)
endforeach
