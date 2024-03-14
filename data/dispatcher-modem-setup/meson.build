# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 Nero Sinaro <xu.zhang@fibocom.com>

# Shipped but disabled Modem setup tools
mm_modemsetupdiravailable = mm_pkgdatadir / 'modem-setup.available.d'

# Directory for user-enabled tools
mm_modemsetupdiruser = mm_pkgsysconfdir / 'modem-setup.d'

# Directory for package-enabled tools
mm_modemsetupdirpackage = mm_pkglibdir / 'modem-setup.d'

# This is a test file
examples = files(
  '0000:0000'
)

install_data(
  examples,
  install_mode: 'rwx------',
  install_dir: mm_modemsetupdiravailable,
)

mkdir_cmd = 'mkdir -p ${DESTDIR}@0@'
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_modemsetupdiruser))
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_modemsetupdirpackage))
