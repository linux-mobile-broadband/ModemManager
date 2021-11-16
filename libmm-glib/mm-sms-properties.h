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

#ifndef MM_SMS_PROPERTIES_H
#define MM_SMS_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_SMS_PROPERTIES            (mm_sms_properties_get_type ())
#define MM_SMS_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SMS_PROPERTIES, MMSmsProperties))
#define MM_SMS_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SMS_PROPERTIES, MMSmsPropertiesClass))
#define MM_IS_SMS_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SMS_PROPERTIES))
#define MM_IS_SMS_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SMS_PROPERTIES))
#define MM_SMS_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SMS_PROPERTIES, MMSmsPropertiesClass))

typedef struct _MMSmsProperties MMSmsProperties;
typedef struct _MMSmsPropertiesClass MMSmsPropertiesClass;
typedef struct _MMSmsPropertiesPrivate MMSmsPropertiesPrivate;

/**
 * MMSmsProperties:
 *
 * The #MMSmsProperties structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMSmsProperties {
    /*< private >*/
    GObject parent;
    MMSmsPropertiesPrivate *priv;
};

struct _MMSmsPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_sms_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSmsProperties, g_object_unref)

MMSmsProperties *mm_sms_properties_new (void);

void mm_sms_properties_set_text                    (MMSmsProperties *self,
                                                    const gchar *text);
void mm_sms_properties_set_data                    (MMSmsProperties *self,
                                                    const guint8 *data,
                                                    gsize data_length);
void mm_sms_properties_set_data_bytearray          (MMSmsProperties *self,
                                                    GByteArray *data);
void mm_sms_properties_set_number                  (MMSmsProperties *self,
                                                    const gchar *number);
void mm_sms_properties_set_smsc                    (MMSmsProperties *self,
                                                    const gchar *smsc);
void mm_sms_properties_set_validity_relative       (MMSmsProperties *self,
                                                    guint validity);
void mm_sms_properties_set_class                   (MMSmsProperties *self,
                                                    gint message_class);
void mm_sms_properties_set_delivery_report_request (MMSmsProperties *self,
                                                    gboolean request);
void mm_sms_properties_set_teleservice_id          (MMSmsProperties *self,
                                                    MMSmsCdmaTeleserviceId teleservice_id);
void mm_sms_properties_set_service_category        (MMSmsProperties *self,
                                                    MMSmsCdmaServiceCategory service_category);

const gchar  *mm_sms_properties_get_text                    (MMSmsProperties *self);
const guint8 *mm_sms_properties_get_data                    (MMSmsProperties *self,
                                                             gsize *data_len);
GByteArray   *mm_sms_properties_peek_data_bytearray         (MMSmsProperties *self);
GByteArray   *mm_sms_properties_get_data_bytearray          (MMSmsProperties *self);
const gchar  *mm_sms_properties_get_number                  (MMSmsProperties *self);
const gchar  *mm_sms_properties_get_smsc                    (MMSmsProperties *self);
MMSmsValidityType mm_sms_properties_get_validity_type       (MMSmsProperties *self);
guint         mm_sms_properties_get_validity_relative       (MMSmsProperties *self);
gint          mm_sms_properties_get_class                   (MMSmsProperties *self);
gboolean      mm_sms_properties_get_delivery_report_request (MMSmsProperties *self);
MMSmsCdmaTeleserviceId   mm_sms_properties_get_teleservice_id   (MMSmsProperties *self);
MMSmsCdmaServiceCategory mm_sms_properties_get_service_category (MMSmsProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMSmsProperties *mm_sms_properties_new_from_string (const gchar *str,
                                                    GError **error);
MMSmsProperties *mm_sms_properties_new_from_dictionary (GVariant *dictionary,
                                                        GError **error);

GVariant *mm_sms_properties_get_dictionary (MMSmsProperties *self);

#endif

G_END_DECLS

#endif /* MM_SMS_PROPERTIES_H */
