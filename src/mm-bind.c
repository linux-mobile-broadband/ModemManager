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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include "mm-bind.h"
#include "mm-log-object.h"

G_DEFINE_INTERFACE (MMBind, mm_bind, MM_TYPE_LOG_OBJECT)

gboolean
mm_bind_to (MMBind *self, const gchar *local_propname, GObject *other_object)
{
    if (other_object) {
        /* Set log owner ID */
        mm_log_object_set_owner_id (MM_LOG_OBJECT (self),
                                    mm_log_object_get_id (MM_LOG_OBJECT (other_object)));

        if (local_propname) {
            /* Bind the other object's connection property (which is set when it is
             * exported, and unset when unexported) to this object's connection
             * property.
             */
            g_object_bind_property (other_object,
                                    MM_BINDABLE_CONNECTION,
                                    self,
                                    local_propname,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
    }
    return !!other_object;
}

static void
mm_bind_default_init (MMBindInterface *iface)
{
    static gsize initialized = 0;

    if (!g_once_init_enter (&initialized))
        return;

    /* Properties */
    g_object_interface_install_property (
        iface,
        g_param_spec_object (MM_BIND_TO,
                             "Bind to",
                             "Bind to this object",
                             G_TYPE_OBJECT,
                             G_PARAM_READWRITE));

    g_once_init_leave (&initialized, 1);
}
