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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_SIMPLE_H_
#define _MM_MODEM_SIMPLE_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-simple-connect-properties.h"
#include "mm-simple-status.h"
#include "mm-bearer.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_SIMPLE            (mm_modem_simple_get_type ())
#define MM_MODEM_SIMPLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SIMPLE, MMModemSimple))
#define MM_MODEM_SIMPLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_SIMPLE, MMModemSimpleClass))
#define MM_IS_MODEM_SIMPLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SIMPLE))
#define MM_IS_MODEM_SIMPLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_SIMPLE))
#define MM_MODEM_SIMPLE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_SIMPLE, MMModemSimpleClass))

typedef struct _MMModemSimple MMModemSimple;
typedef struct _MMModemSimpleClass MMModemSimpleClass;

/**
 * MMModemSimple:
 *
 * The #MMModemSimple structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemSimple {
    /*< private >*/
    MmGdbusModemSimpleProxy parent;
    gpointer unused;
};

struct _MMModemSimpleClass {
    /*< private >*/
    MmGdbusModemSimpleProxyClass parent;
};

GType mm_modem_simple_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemSimple, g_object_unref)

const gchar *mm_modem_simple_get_path (MMModemSimple *self);
gchar       *mm_modem_simple_dup_path (MMModemSimple *self);

void      mm_modem_simple_connect        (MMModemSimple *self,
                                          MMSimpleConnectProperties *properties,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
MMBearer *mm_modem_simple_connect_finish (MMModemSimple *self,
                                          GAsyncResult *res,
                                          GError **error);
MMBearer *mm_modem_simple_connect_sync   (MMModemSimple *self,
                                          MMSimpleConnectProperties *properties,
                                          GCancellable *cancellable,
                                          GError **error);

void     mm_modem_simple_disconnect        (MMModemSimple *self,
                                            const gchar *bearer,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_modem_simple_disconnect_finish (MMModemSimple *self,
                                            GAsyncResult *res,
                                            GError **error);
gboolean mm_modem_simple_disconnect_sync   (MMModemSimple *self,
                                            const gchar *bearer,
                                            GCancellable *cancellable,
                                            GError **error);

void            mm_modem_simple_get_status        (MMModemSimple *self,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
MMSimpleStatus *mm_modem_simple_get_status_finish (MMModemSimple *self,
                                                   GAsyncResult *res,
                                                   GError **error);
MMSimpleStatus *mm_modem_simple_get_status_sync   (MMModemSimple *self,
                                                   GCancellable *cancellable,
                                                   GError **error);
G_END_DECLS

#endif /* _MM_MODEM_SIMPLE_H_ */
