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
