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

#ifndef LIBQCDM_ERROR_H
#define LIBQCDM_ERROR_H

#include <glib.h>
#include <glib-object.h>

enum {
    QCDM_SERIAL_CONFIG_FAILED = 0,
};

#define QCDM_SERIAL_ERROR (qcdm_serial_error_quark ())
#define QCDM_TYPE_SERIAL_ERROR (qcdm_serial_error_get_type ())

GQuark qcdm_serial_error_quark    (void);
GType  qcdm_serial_error_get_type (void);


enum {
    QCDM_COMMAND_MALFORMED_RESPONSE = 0,
    QCDM_COMMAND_UNEXPECTED = 1,
    QCDM_COMMAND_BAD_LENGTH = 2,
    QCDM_COMMAND_BAD_COMMAND = 3,
    QCDM_COMMAND_BAD_PARAMETER = 4,
    QCDM_COMMAND_NOT_ACCEPTED = 5,
    QCDM_COMMAND_BAD_MODE = 6,
    QCDM_COMMAND_NVCMD_FAILED = 7,
    QCDM_COMMAND_SPC_LOCKED = 8,
};

#define QCDM_COMMAND_ERROR (qcdm_command_error_quark ())
#define QCDM_TYPE_COMMAND_ERROR (qcdm_command_error_get_type ())

GQuark qcdm_command_error_quark    (void);
GType  qcdm_command_error_get_type (void);

#endif  /* LIBQCDM_ERROR_H */

