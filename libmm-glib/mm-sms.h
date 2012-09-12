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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_SMS_H_
#define _MM_SMS_H_

#include <ModemManager.h>
#include <libmm-common.h>

G_BEGIN_DECLS

typedef MmGdbusSms     MMSms;
#define MM_TYPE_SMS(o) MM_GDBUS_TYPE_SMS (o)
#define MM_SMS(o)      MM_GDBUS_SMS(o)
#define MM_IS_SMS(o)   MM_GDBUS_IS_SMS(o)

const gchar *mm_sms_get_path (MMSms *self);
gchar       *mm_sms_dup_path (MMSms *self);

void mm_sms_new (GDBusConnection     *connection,
                 const gchar         *object_path,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data);
MMSms *mm_sms_new_finish (GAsyncResult  *res,
                          GError       **error);

MMSms *mm_sms_new_sync (GDBusConnection     *connection,
                        const gchar         *object_path,
                        GCancellable        *cancellable,
                        GError             **error);

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
guint         mm_sms_get_validity                (MMSms *self);
guint         mm_sms_get_class                   (MMSms *self);
guint         mm_sms_get_message_reference       (MMSms *self);
gboolean      mm_sms_get_delivery_report_request (MMSms *self);
guint         mm_sms_get_delivery_state          (MMSms *self);
MMSmsState    mm_sms_get_state                   (MMSms *self);
MMSmsStorage  mm_sms_get_storage                 (MMSms *self);
MMSmsPduType  mm_sms_get_pdu_type                (MMSms *self);

G_END_DECLS

#endif /* _MM_SMS_H_ */
