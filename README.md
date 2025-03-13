<!--
SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2008 Tambet Ingo <tambet@gmail.com>
Copyright (C) 2008-2011 Dan Williams <dcbw@redhat.com>
Copyright (C) 2011-2024 Aleksander Morgado <aleksander@aleksander.es>
-->

# ModemManager

ModemManager provides a unified high level API for communicating with mobile
broadband modems, regardless of the protocol used to communicate with the
actual device (Generic AT, vendor-specific AT, QCDM, QMI, MBIM...).

## Getting Started

ModemManager uses the meson build system. Meson is likely available as a
package from your Linux distribution, but if not please refer to the [Meson
project](https://mesonbuild.com/Quick-guide.html) for installation instructions. Once you have Meson installed you'll
probably want to install [libmbim](https://gitlab.freedesktop.org/mobile-broadband/libmbim) and [libqmi](https://gitlab.freedesktop.org/mobile-broadband/libqmi) which most modems require.

After dependencies are installed you can build ModemManager with:

    $ meson setup build --prefix=/usr --buildtype=release
    $ ninja -C build

And after a successful build, install with:

    $ sudo ninja -C build install

## Using

ModemManager is a system daemon and is not meant to be used directly from
the command line. However, since it provides a DBus API, it is possible to use
'dbus-send' commands or the new 'mmcli' command line interface to control it
from the terminal. The devices are queried from udev and automatically updated
based on hardware events, although a manual re-scan can also be requested to
look for RS232 modems.

## Implementation

ModemManager is a DBus system bus activated service (meaning it's started
automatically when a request arrives). It is written in C, using glib and gio.
Several GInterfaces specify different features that the modems support,
including the generic MMIfaceModem3gpp and MMIfaceModemCdma which provide basic
operations for 3GPP (GSM, UMTS, LTE) or CDMA (CDMA1x, EV-DO) modems. If a given
feature is not available in the modem, the specific interface will not be
exported in DBus.

## Plugins

Plugins are loaded on startup, and must implement the MMPlugin interface. It
consists of a couple of methods which tell the daemon whether the plugin
supports a port and to create custom MMBroadbandModem implementations. It most
likely makes sense to derive custom modem implementations from one of the
generic classes and just add (or override) operations which are not standard.
There are multiple fully working plugins in the plugins/ directory that can be
used as an example for writing new plugins. Writing new plugins is highly
encouraged! The plugin API is open for changes, so if you're writing a plugin
and need to add or change some public method, feel free to suggest it!

## License

The ModemManager and mmcli binaries are both GPLv2+ (See COPYING).
The libmm-glib library and the ModemManager API headers are LGPLv2+ (See
COPYING.LIB).

## Code of Conduct

Please note that this project is released with a Contributor Code of Conduct.
By participating in this project you agree to abide by its terms, which you can
find in the following link:
https://www.freedesktop.org/wiki/CodeOfConduct

CoC issues may be raised to the project maintainers at the following address:
modemmanager-devel-owner@lists.freedesktop.org
