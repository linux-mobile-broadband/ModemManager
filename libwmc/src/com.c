/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>

#include "com.h"
#include "errors.h"

wbool
wmc_port_setup (int fd, WmcError **error)
{
    struct termios stbuf;

    errno = 0;
    memset (&stbuf, 0, sizeof (stbuf));
    if (tcgetattr (fd, &stbuf) != 0) {
        wmc_error_set (error, WMC_SERIAL_ERROR, WMC_SERIAL_ERROR_CONFIG_FAILED,
                       "tcgetattr() error: %d", errno);
    }

    stbuf.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | CLOCAL | PARENB);
    stbuf.c_iflag &= ~(HUPCL | IUTF8 | IUCLC | ISTRIP | IXON | IXOFF | IXANY | ICRNL);
    stbuf.c_oflag &= ~(OPOST | OCRNL | ONLCR | OLCUC | ONLRET);
    stbuf.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHONL);
    stbuf.c_lflag &= ~(NOFLSH | XCASE | TOSTOP | ECHOPRT | ECHOCTL | ECHOKE);
    stbuf.c_cc[VMIN] = 1;
    stbuf.c_cc[VTIME] = 0;
    stbuf.c_cc[VEOF] = 1;
    stbuf.c_cflag |= (B115200 | CS8 | CREAD | 0 | 0);  /* No parity, 1 stop bit */

    errno = 0;
    if (tcsetattr (fd, TCSANOW, &stbuf) < 0) {
        wmc_error_set (error, WMC_SERIAL_ERROR, WMC_SERIAL_ERROR_CONFIG_FAILED,
                       "tcsetattr() error: %d", errno);
        return FALSE;
    }

    return TRUE;
}

