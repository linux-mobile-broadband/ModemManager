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
 * Copyright (C) 2021 - Aleksander Morgado <aleksander@aleksander.es>
 */

#include <net/if.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-net.h"
#include "mm-log-object.h"
#include "mm-netlink.h"

G_DEFINE_TYPE (MMPortNet, mm_port_net, MM_TYPE_PORT)

struct _MMPortNetPrivate {
    guint ifindex;
};

static void
ensure_ifindex (MMPortNet *self)
{
    if (!self->priv->ifindex) {
        self->priv->ifindex = if_nametoindex (mm_port_get_device (MM_PORT (self)));
        if (!self->priv->ifindex)
            mm_obj_warn (self, "couldn't get interface index");
        else
            mm_obj_dbg (self, "interface index: %u", self->priv->ifindex);
    }
}

/*****************************************************************************/

gboolean
mm_port_net_link_setup_finish (MMPortNet     *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
netlink_setlink_ready (MMNetlink    *netlink,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError *error = NULL;

    if (!mm_netlink_setlink_finish (netlink, res, &error)) {
        g_prefix_error (&error, "netlink operation failed: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_port_net_link_setup (MMPortNet            *self,
                        gboolean              up,
                        guint                 mtu,
                        GCancellable         *cancellable,
                        GAsyncReadyCallback   callback,
                        gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    ensure_ifindex (self);
    if (!self->priv->ifindex) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "no valid interface index found for %s",
                                 mm_port_get_device (MM_PORT (self)));
        g_object_unref (task);
        return;
    }

    mm_netlink_setlink (mm_netlink_get (), /* singleton */
                        self->priv->ifindex,
                        up,
                        mtu,
                        cancellable,
                        (GAsyncReadyCallback) netlink_setlink_ready,
                        task);
}

/*****************************************************************************/

MMPortNet *
mm_port_net_new (const gchar *name)
{
    return MM_PORT_NET (g_object_new (MM_TYPE_PORT_NET,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE,   MM_PORT_TYPE_NET,
                                      NULL));
}

static void
mm_port_net_init (MMPortNet *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_NET, MMPortNetPrivate);
}

static void
mm_port_net_class_init (MMPortNetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortNetPrivate));
}
