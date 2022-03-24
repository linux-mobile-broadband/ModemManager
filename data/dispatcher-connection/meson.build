# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>

# Shipped example connection info dispatcher
mm_connectiondiravailable = mm_pkgdatadir / 'connection.available.d'

# Directory for user-enabled scripts
mm_connectiondiruser = mm_pkgsysconfdir / 'connection.d'

# Directory for package-enabled tools
mm_connectiondirpackage = mm_pkglibdir / 'connection.d'

examples = files(
  '99-log-event',
)

install_data(
  examples,
  install_mode: 'rwx------',
  install_dir: mm_connectiondiravailable,
)

mkdir_cmd = 'mkdir -p ${DESTDIR}@0@'
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_connectiondiruser))
meson.add_install_script('sh', '-c', mkdir_cmd.format(mm_prefix / mm_connectiondirpackage))
