#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
REQUIRED_AUTOMAKE_VERSION=1.7
PKG_NAME=ModemManager

(test -f $srcdir/configure.ac \
  && test -f $srcdir/src/mm-modem.c) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

polkit="false"
args=""
while (( "$#" )); do
    if [ "$1" == "--with-polkit=yes" ]; then
        polkit="true"
    fi
    args="$args $1"
    shift
done

(cd $srcdir;
    autoreconf --install --symlink &&
    ((eval "$polkit" && intltoolize --force) || true) &&
    autoreconf &&
    ./configure --enable-maintainer-mode $args
)
