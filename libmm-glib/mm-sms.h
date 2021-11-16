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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_SMS_H_
#define _MM_SMS_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-sms.h"
#include "mm-sms-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_SMS            (mm_sms_get_type ())
#define MM_SMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS, MMSms))
#define MM_SMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_SMS, MMSmsClass))
#define MM_IS_SMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS))
#define MM_IS_SMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_SMS))
#define MM_SMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_SMS, MMSmsClass))

typedef struct _MMSms MMSms;
typedef struct _MMSmsClass MMSmsClass;

/**
 * MMSms:
 *
 * The #MMSms structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMSms {
    /*< private >*/
    MmGdbusSmsProxy parent;
    gpointer unused;
};

struct _MMSmsClass {
    /*< private >*/
    MmGdbusSmsProxyClass parent;
};

GType mm_sms_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSms, g_object_unref)

const gchar *mm_sms_get_path                     (MMSms *self);
gchar       *mm_sms_dup_path                     (MMSms *self);

const gchar  *mm_sms_get_text                    (MMSms *self);
gchar        *mm_sms_dup_text                    (MMSms *self);

const guint8 *mm_sms_get_data                    (MMSms *self,
                                                  gsize *data_len);
guint8       *mm_sms_dup_data                    (MMSms *self,
                                                  gsize *data_len);

const gchar  *mm_sms_get_number                  (MMSms *self);
gchar        *mm_sms_dup_number                  (MMSms *self);

const gchar  *mm_sms_get_smsc                    (MMSms *self);
gchar        *mm_sms_dup_smsc                    (MMSms *self);

const gchar  *mm_sms_get_timestamp               (MMSms *self);
gchar        *mm_sms_dup_timestamp               (MMSms *self);

const gchar  *mm_sms_get_discharge_timestamp     (MMSms *self);
gchar        *mm_sms_dup_discharge_timestamp     (MMSms *self);

MMSmsValidityType mm_sms_get_validity_type       (MMSms *self);
guint             mm_sms_get_validity_relative   (MMSms *self);

gint          mm_sms_get_class                   (MMSms *self);

guint         mm_sms_get_message_reference       (MMSms *self);

gboolean      mm_sms_get_delivery_report_request (MMSms *self);

guint         mm_sms_get_delivery_state          (MMSms *self);

MMSmsState    mm_sms_get_state                   (MMSms *self);

MMSmsStorage  mm_sms_get_storage                 (MMSms *self);

MMSmsPduType  mm_sms_get_pdu_type                (MMSms *self);

MMSmsCdmaTeleserviceId mm_sms_get_teleservice_id (MMSms *self);

MMSmsCdmaServiceCategory mm_sms_get_service_category (MMSms *self);

void     mm_sms_send        (MMSms *self,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data);
gboolean mm_sms_send_finish (MMSms *self,
                             GAsyncResult *res,
                             GError **error);
gboolean mm_sms_send_sync   (MMSms *self,
                             GCancellable *cancellable,
                             GError **error);

void     mm_sms_store        (MMSms *self,
                              MMSmsStorage storage,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
gboolean mm_sms_store_finish (MMSms *self,
                              GAsyncResult *res,
                              GError **error);
gboolean mm_sms_store_sync   (MMSms *self,
                              MMSmsStorage storage,
                              GCancellable *cancellable,
                              GError **error);

G_END_DECLS

#endif /* _MM_SMS_H_ */
