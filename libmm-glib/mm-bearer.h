/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_BEARER_H_
#define _MM_BEARER_H_

#include <ModemManager.h>
#include <libmm-common.h>

G_BEGIN_DECLS

typedef MmGdbusBearer     MMBearer;
#define MM_TYPE_BEARER(o) MM_GDBUS_TYPE_BEARER (o)
#define MM_BEARER(o)      MM_GDBUS_BEARER(o)
#define MM_IS_BEARER(o)   MM_GDBUS_IS_BEARER(o)

const gchar *mm_bearer_get_path       (MMBearer *self);
gchar       *mm_bearer_dup_path       (MMBearer *self);
const gchar *mm_bearer_get_interface  (MMBearer *self);
gchar       *mm_bearer_dup_interface  (MMBearer *self);
gboolean     mm_bearer_get_connected  (MMBearer *self);
gboolean     mm_bearer_get_suspended  (MMBearer *self);
guint        mm_bearer_get_ip_timeout (MMBearer *self);

void     mm_bearer_connect        (MMBearer *self,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_bearer_connect_finish (MMBearer *self,
                                   GAsyncResult *res,
                                   GError **error);
gboolean mm_bearer_connect_sync   (MMBearer *self,
                                   GCancellable *cancellable,
                                   GError **error);

void     mm_bearer_disconnect        (MMBearer *self,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean mm_bearer_disconnect_finish (MMBearer *self,
                                      GAsyncResult *res,
                                      GError **error);
gboolean mm_bearer_disconnect_sync   (MMBearer *self,
                                      GCancellable *cancellable,
                                      GError **error);

MMBearerProperties *mm_bearer_get_properties  (MMBearer *self);
MMBearerIpConfig   *mm_bearer_get_ipv4_config (MMBearer *self);
MMBearerIpConfig   *mm_bearer_get_ipv6_config (MMBearer *self);

G_END_DECLS

#endif /* _MM_BEARER_H_ */
