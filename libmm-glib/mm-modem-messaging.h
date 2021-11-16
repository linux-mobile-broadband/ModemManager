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

#ifndef _MM_MODEM_MESSAGING_H_
#define _MM_MODEM_MESSAGING_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-sms.h"
#include "mm-sms-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_MESSAGING            (mm_modem_messaging_get_type ())
#define MM_MODEM_MESSAGING(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_MESSAGING, MMModemMessaging))
#define MM_MODEM_MESSAGING_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_MESSAGING, MMModemMessagingClass))
#define MM_IS_MODEM_MESSAGING(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_MESSAGING))
#define MM_IS_MODEM_MESSAGING_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_MESSAGING))
#define MM_MODEM_MESSAGING_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_MESSAGING, MMModemMessagingClass))

typedef struct _MMModemMessaging MMModemMessaging;
typedef struct _MMModemMessagingClass MMModemMessagingClass;
typedef struct _MMModemMessagingPrivate MMModemMessagingPrivate;

/**
 * MMModemMessaging:
 *
 * The #MMModemMessaging structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemMessaging {
    /*< private >*/
    MmGdbusModemMessagingProxy parent;
    MMModemMessagingPrivate *priv;
};

struct _MMModemMessagingClass {
    /*< private >*/
    MmGdbusModemMessagingProxyClass parent;
};

GType mm_modem_messaging_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemMessaging, g_object_unref)

const gchar *mm_modem_messaging_get_path (MMModemMessaging *self);
gchar       *mm_modem_messaging_dup_path (MMModemMessaging *self);

gboolean     mm_modem_messaging_get_supported_storages  (MMModemMessaging *self,
                                                         MMSmsStorage **storages,
                                                         guint *n_storages);
gboolean     mm_modem_messaging_peek_supported_storages (MMModemMessaging *self,
                                                         const MMSmsStorage **storages,
                                                         guint *n_storages);

MMSmsStorage mm_modem_messaging_get_default_storage    (MMModemMessaging *self);

void   mm_modem_messaging_create        (MMModemMessaging *self,
                                         MMSmsProperties *properties,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
MMSms *mm_modem_messaging_create_finish (MMModemMessaging *self,
                                         GAsyncResult *res,
                                         GError **error);
MMSms *mm_modem_messaging_create_sync   (MMModemMessaging *self,
                                         MMSmsProperties *properties,
                                         GCancellable *cancellable,
                                         GError **error);

void   mm_modem_messaging_list        (MMModemMessaging *self,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GList *mm_modem_messaging_list_finish (MMModemMessaging *self,
                                       GAsyncResult *res,
                                       GError **error);
GList *mm_modem_messaging_list_sync   (MMModemMessaging *self,
                                       GCancellable *cancellable,
                                       GError **error);

void     mm_modem_messaging_delete        (MMModemMessaging *self,
                                           const gchar *sms,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_modem_messaging_delete_finish (MMModemMessaging *self,
                                           GAsyncResult *res,
                                           GError **error);
gboolean mm_modem_messaging_delete_sync   (MMModemMessaging *self,
                                           const gchar *sms,
                                           GCancellable *cancellable,
                                           GError **error);

G_END_DECLS

#endif /* _MM_MODEM_MESSAGING_H_ */
