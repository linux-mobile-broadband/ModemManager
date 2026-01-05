# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_env = environment()
test_env.set('ASAN_OPTIONS', 'fast_unwind_on_malloc=0')

test_units = {
  'at-serial-port': libport_dep,
  'cbm-part': libhelpers_dep,
  'charsets': libhelpers_dep,
  'error-helpers': libhelpers_dep,
  'kernel-device-helpers': libkerneldevice_dep,
  'location-cache': libhelpers_dep,
  'modem-helpers': libhelpers_dep,
  'port-scheduler': libport_dep,
  'sms-part-3gpp': libhelpers_dep,
  'sms-part-cdma': libhelpers_dep,
  'sms-list': libsms_dep,
  'udev-rules': libkerneldevice_dep,
}

deps = [
  libport_dep,
  libqcdm_dep,
  util_dep,
]

test_units += {'qcdm-serial-port': deps}

if enable_qmi
  test_units += {'modem-helpers-qmi': libkerneldevice_dep}
endif

if enable_mbim
  test_units += {'modem-helpers-mbim': libkerneldevice_dep}
endif

c_args = [
  '-DTEST_SERVICES="@0@"'.format(build_root / 'data/tests'),
  '-DTESTUDEVRULESDIR="@0@"'.format(src_dir),
]

foreach test_unit, test_deps: test_units
  test_name = 'test-' + test_unit

  exe = executable(
    test_name,
    sources: test_name + '.c',
    include_directories: top_inc,
    dependencies: test_deps,
    c_args: c_args,
  )

  test(test_name, exe, suite: 'daemon', env: test_env)
endforeach

# base call test
exe = executable(
  'test-base-call',
  sources: [ 'test-base-call.c', 'fake-modem.c', 'fake-call.c' ],
  include_directories: top_inc,
  dependencies: libmmbase_dep,
  c_args: c_args,
)

test('test-base-call', exe, suite: 'daemon', env: test_env)


if get_option('fuzzer')
  fuzzer_tests = ['test-sms-part-3gpp-fuzzer',
                  'test-sms-part-3gpp-tr-fuzzer',
                  'test-sms-part-cdma-fuzzer']
  foreach fuzzer_test: fuzzer_tests
    exe = executable(
      fuzzer_test,
      sources: fuzzer_test + '.c',
      include_directories: top_inc,
      dependencies: libhelpers_dep,
      link_args : '-fsanitize=fuzzer',
    )
  endforeach
endif
