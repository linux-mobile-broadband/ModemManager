# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>

# Shipped but disabled FCC unlock tools
mm_fccunlockdiravailable = mm_pkgdatadir / 'fcc-unlock.available.d'

# Directory for user-enabled tools
mm_fccunlockdiruser = mm_pkgsysconfdir / 'fcc-unlock.d'

# Directory for package-enabled tools
mm_fccunlockdirpackage = mm_pkglibdir / 'fcc-unlock.d'

examples = files(
  '105b',
  '1199',
  '1eac',
  '2c7c',
)

install_data(
  examples,
  install_mode: 'rwx------',
  install_dir: mm_fccunlockdiravailable,
)

vidpids = {
  '105b:e0ab': '105b',
  '105b:e0c3': '105b',
  '03f0:4e1d': '1199',
  '1199:9079': '1199',
  '413c:81a3': '1199',
  '413c:81a8': '1199',
  '1eac:1001': '1eac',
  '2c7c:030a': '2c7c',
}

ln_cmd = 'ln -fs @0@ ${DESTDIR}@1@'
foreach output, input: vidpids
  meson.add_install_script('sh', '-c', ln_cmd.format(input, mm_prefix / mm_fccunlockdiravailable / output))
endforeach

mkdir_cmd = 'mkdir -p ${DESTDIR}@0@'
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_fccunlockdiruser))
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_fccunlockdirpackage))
