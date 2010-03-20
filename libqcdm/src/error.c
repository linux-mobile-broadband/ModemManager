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

#include "error.h"

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GQuark
qcdm_serial_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("qcdm-serial-error");

    return ret;
}

GType
qcdm_serial_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (QCDM_SERIAL_CONFIG_FAILED, "SerialConfigFailed"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("QcdmSerialError", values);
    }

    return etype;
}

/***************************************************************/

GQuark
qcdm_command_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("qcdm-command-error");

    return ret;
}

GType
qcdm_command_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (QCDM_COMMAND_MALFORMED_RESPONSE, "QcdmCommandMalformedResponse"),
            ENUM_ENTRY (QCDM_COMMAND_UNEXPECTED, "QcdmCommandUnexpected"),
            ENUM_ENTRY (QCDM_COMMAND_BAD_LENGTH, "QcdmCommandBadLength"),
            ENUM_ENTRY (QCDM_COMMAND_BAD_COMMAND, "QcdmCommandBadCommand"),
            ENUM_ENTRY (QCDM_COMMAND_BAD_PARAMETER, "QcdmCommandBadParameter"),
            ENUM_ENTRY (QCDM_COMMAND_NOT_ACCEPTED, "QcdmCommandNotAccepted"),
            ENUM_ENTRY (QCDM_COMMAND_BAD_MODE, "QcdmCommandBadMode"),
            ENUM_ENTRY (QCDM_COMMAND_NVCMD_FAILED, "QcdmCommandNvCmdFailed"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("QcdmCommandError", values);
    }

    return etype;
}


