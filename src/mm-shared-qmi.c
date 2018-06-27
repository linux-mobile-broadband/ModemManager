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
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-iface-modem.h"
#include "mm-shared-qmi.h"

/*****************************************************************************/

QmiClient *
mm_shared_qmi_peek_client (MMSharedQmi    *self,
                           QmiService      service,
                           MMPortQmiFlag   flag,
                           GError        **error)
{
    g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_client);
    return MM_SHARED_QMI_GET_INTERFACE (self)->peek_client (self, service, flag, error);
}

gboolean
mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                             QmiService            service,
                             QmiClient           **o_client,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
    GError    *error = NULL;
    QmiClient *client;

    client = mm_shared_qmi_peek_client (self, service, MM_PORT_QMI_FLAG_DEFAULT, &error);
    if (!client) {
        g_task_report_error (self, callback, user_data, mm_shared_qmi_ensure_client, error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

static void
shared_qmi_init (gpointer g_iface)
{
}

GType
mm_shared_qmi_get_type (void)
{
    static GType shared_qmi_type = 0;

    if (!G_UNLIKELY (shared_qmi_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQmi),  /* class_size */
            shared_qmi_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_qmi_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQmi", &info, 0);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM);
    }

    return shared_qmi_type;
}
