/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <termio.h>

#include "com.h"
#include "error.h"

gboolean
qcdm_port_setup (int fd, GError **error)
{
    struct termio stbuf;

    g_type_init ();

    errno = 0;
    memset (&stbuf, 0, sizeof (struct termio));
    if (ioctl (fd, TCGETA, &stbuf) != 0) {
        g_set_error (error,
                     QCDM_SERIAL_ERROR, QCDM_SERIAL_CONFIG_FAILED,
                     "TCGETA error: %d", errno);
    }

    stbuf.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | CLOCAL | PARENB);
    stbuf.c_iflag &= ~(HUPCL | IUTF8 | IUCLC | ISTRIP | IXON | ICRNL);
    stbuf.c_oflag &= ~(OPOST | OCRNL | ONLCR | OLCUC | ONLRET);
    stbuf.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHONL);
    stbuf.c_lflag &= ~(NOFLSH | XCASE | TOSTOP | ECHOPRT | ECHOCTL | ECHOKE);
    stbuf.c_cc[VMIN] = 1;
    stbuf.c_cc[VTIME] = 0;
    stbuf.c_cc[VEOF] = 1;
    stbuf.c_cflag |= (B115200 | CS8 | CREAD | 0 | 0);  /* No parity, 1 stop bit */

    errno = 0;
    if (ioctl (fd, TCSETA, &stbuf) < 0) {
        g_set_error (error,
                     QCDM_SERIAL_ERROR, QCDM_SERIAL_CONFIG_FAILED,
                     "TCSETA error: %d", errno);
        return FALSE;
    }

    return TRUE;
}

