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
 * Copyright (C) 2024 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-port-at.h"
#include "mm-log-object.h"

G_DEFINE_INTERFACE (MMIfacePortAt, mm_iface_port_at, MM_TYPE_PORT)

/*****************************************************************************/

gboolean
mm_iface_port_at_check_support (MMIfacePortAt  *self,
                                gboolean       *out_supported,
                                GError        **error)
{
    g_assert (out_supported);

    /* If the object implementing the interface doesn't provide a check_support() method,
     * we assume the feature is unconditionally supported. */
    if (!MM_IFACE_PORT_AT_GET_IFACE (self)->check_support) {
        *out_supported = TRUE;
        return TRUE;
    }

    return MM_IFACE_PORT_AT_GET_IFACE (self)->check_support (self, out_supported, error);
}

/*****************************************************************************/

gchar *
mm_iface_port_at_command_finish (MMIfacePortAt  *self,
                                 GAsyncResult   *res,
                                 GError        **error)
{
    return MM_IFACE_PORT_AT_GET_IFACE (self)->command_finish (self, res, error);
}

void
mm_iface_port_at_command (MMIfacePortAt        *self,
                          const gchar          *command,
                          guint32               timeout_seconds,
                          gboolean              is_raw,
                          gboolean              allow_cached,
                          GCancellable         *cancellable,
                          GAsyncReadyCallback   callback,
                          gpointer              user_data)
{
    g_assert (MM_IFACE_PORT_AT_GET_IFACE (self)->command);
    g_assert (MM_IFACE_PORT_AT_GET_IFACE (self)->command_finish);

    MM_IFACE_PORT_AT_GET_IFACE (self)->command (self,
                                                command,
                                                timeout_seconds,
                                                is_raw,
                                                allow_cached,
                                                cancellable,
                                                callback,
                                                user_data);
}

/*****************************************************************************/

static void
mm_iface_port_at_default_init (MMIfacePortAtInterface *iface)
{
}
