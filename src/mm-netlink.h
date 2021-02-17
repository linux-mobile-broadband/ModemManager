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
 * Basic netlink support from libqmi:
 *   Copyright (C) 2020 Eric Caruso <ejcaruso@chromium.org>
 *   Copyright (C) 2020 Andrew Lassalle <andrewlassalle@chromium.org>
 *
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_NETLINK_H
#define MM_NETLINK_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define MM_TYPE_NETLINK         (mm_netlink_get_type ())
#define MM_NETLINK(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MM_TYPE_NETLINK, MMNetlink))
#define MM_NETLINK_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), MM_TYPE_NETLINK, MMNetlinkClass))
#define MM_NETLINK_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MM_TYPE_NETLINK, MMNetlinkClass))
#define MM_IS_NETLINK(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MM_TYPE_NETLINK))
#define MM_IS_NETLINK_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MM_TYPE_NETLINK))

typedef struct _MMNetlink         MMNetlink;
typedef struct _MMNetlinkClass    MMNetlinkClass;

GType      mm_netlink_get_type     (void) G_GNUC_CONST;
MMNetlink *mm_netlink_get          (void);

void     mm_netlink_setlink        (MMNetlink           *self,
                                    guint                ifindex,
                                    gboolean             up,
                                    guint                mtu,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);
gboolean mm_netlink_setlink_finish (MMNetlink            *self,
                                    GAsyncResult         *res,
                                    GError              **error);

G_END_DECLS

#endif  /* MM_MODEM_HELPERS_NETLINK_H */
