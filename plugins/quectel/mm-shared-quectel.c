/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem-firmware.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-quectel.h"

/*****************************************************************************/
/* Firmware update settings loading (Firmware interface) */

MMFirmwareUpdateSettings *
mm_shared_quectel_firmware_load_update_settings_finish (MMIfaceModemFirmware  *self,
                                                        GAsyncResult          *res,
                                                        GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
qfastboot_test_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMFirmwareUpdateSettings *update_settings;

    if (!mm_base_modem_at_command_finish (self, res, NULL))
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);
    else {
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT);
        mm_firmware_update_settings_set_fastboot_at (update_settings, "AT+QFASTBOOT");
    }

    g_task_return_pointer (task, update_settings, g_object_unref);
    g_object_unref (task);
}

void
mm_shared_quectel_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT+QFASTBOOT=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)qfastboot_test_ready,
                              task);
}

/*****************************************************************************/
/* "+QUSIM: 1" URC is emitted by Quectel modems after the USIM has been
 * (re)initialized. We register a handler for this URC and perform a check
 * for SIM swap when it is encountered. The motivation for this is to detect
 * M2M eUICC profile switches. According to SGP.02 chapter 3.2.1, the eUICC
 * shall trigger a REFRESH operation with eUICC reset when a new profile is
 * enabled. The +QUSIM URC appears after the eUICC has restarted and can act
 * as a trigger for profile switch check. This should basically be handled
 * the same as a physical SIM swap, so the existing SIM hot swap mechanism
 * is used.
 */

static void
quectel_qusim_check_for_sim_swap_ready (MMIfaceModem *self,
                                        GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't check SIM swap: %s", error->message);
        g_error_free (error);
    } else
        mm_obj_dbg (self, "check SIM swap completed");
}

static void
quectel_qusim_unsolicited_handler (MMPortSerialAt *port,
                                   GMatchInfo *match_info,
                                   MMIfaceModem* self)
{
    if (MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish) {
        mm_obj_dbg (self, "checking SIM swap");
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap (
            self,
            (GAsyncReadyCallback)quectel_qusim_check_for_sim_swap_ready,
            NULL);
    }
}

gboolean
mm_shared_quectel_setup_sim_hot_swap_finish (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_shared_quectel_setup_sim_hot_swap (MMIfaceModem *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMPortSerialAt *port_primary;
    MMPortSerialAt *port_secondary;
    GTask *task;
    GRegex *pattern;

    task = g_task_new (self, NULL, callback, user_data);

    port_primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    port_secondary = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    pattern = g_regex_new ("\\+QUSIM:\\s*1\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);

    if (port_primary)
        mm_port_serial_at_add_unsolicited_msg_handler (
            port_primary,
            pattern,
            (MMPortSerialAtUnsolicitedMsgFn)quectel_qusim_unsolicited_handler,
            self,
            NULL);

    if (port_secondary)
        mm_port_serial_at_add_unsolicited_msg_handler (
            port_secondary,
            pattern,
            (MMPortSerialAtUnsolicitedMsgFn)quectel_qusim_unsolicited_handler,
            self,
            NULL);

    g_regex_unref (pattern);
    mm_obj_dbg (self, "+QUSIM detection set up");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

static void
shared_quectel_init (gpointer g_iface)
{
}

GType
mm_shared_quectel_get_type (void)
{
    static GType shared_quectel_type = 0;

    if (!G_UNLIKELY (shared_quectel_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQuectel),  /* class_size */
            shared_quectel_init,       /* base_init */
            NULL,                      /* base_finalize */
        };

        shared_quectel_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQuectel", &info, 0);
        g_type_interface_add_prerequisite (shared_quectel_type, MM_TYPE_IFACE_MODEM_FIRMWARE);
    }

    return shared_quectel_type;
}
