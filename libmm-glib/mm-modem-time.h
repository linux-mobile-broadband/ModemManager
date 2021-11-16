/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_TIME_H_
#define _MM_MODEM_TIME_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-network-timezone.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_TIME            (mm_modem_time_get_type ())
#define MM_MODEM_TIME(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_TIME, MMModemTime))
#define MM_MODEM_TIME_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_TIME, MMModemTimeClass))
#define MM_IS_MODEM_TIME(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_TIME))
#define MM_IS_MODEM_TIME_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_TIME))
#define MM_MODEM_TIME_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_TIME, MMModemTimeClass))

typedef struct _MMModemTime MMModemTime;
typedef struct _MMModemTimeClass MMModemTimeClass;
typedef struct _MMModemTimePrivate MMModemTimePrivate;

/**
 * MMModemTime:
 *
 * The #MMModemTime structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemTime {
    /*< private >*/
    MmGdbusModemTimeProxy parent;
    MMModemTimePrivate *priv;
};

struct _MMModemTimeClass {
    /*< private >*/
    MmGdbusModemTimeProxyClass parent;
};

GType mm_modem_time_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemTime, g_object_unref)

const gchar *mm_modem_time_get_path (MMModemTime *self);
gchar       *mm_modem_time_dup_path (MMModemTime *self);

void   mm_modem_time_get_network_time        (MMModemTime *self,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gchar *mm_modem_time_get_network_time_finish (MMModemTime *self,
                                              GAsyncResult *res,
                                              GError **error);
gchar *mm_modem_time_get_network_time_sync   (MMModemTime *self,
                                              GCancellable *cancellable,
                                              GError **error);

MMNetworkTimezone *mm_modem_time_peek_network_timezone (MMModemTime *self);
MMNetworkTimezone *mm_modem_time_get_network_timezone  (MMModemTime *self);

G_END_DECLS

#endif /* _MM_MODEM_TIME_H_ */
