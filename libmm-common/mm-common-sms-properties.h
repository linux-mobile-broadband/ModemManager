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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_COMMON_SMS_PROPERTIES_H
#define MM_COMMON_SMS_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_COMMON_SMS_PROPERTIES            (mm_common_sms_properties_get_type ())
#define MM_COMMON_SMS_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_SMS_PROPERTIES, MMCommonSmsProperties))
#define MM_COMMON_SMS_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_SMS_PROPERTIES, MMCommonSmsPropertiesClass))
#define MM_IS_COMMON_SMS_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_SMS_PROPERTIES))
#define MM_IS_COMMON_SMS_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_SMS_PROPERTIES))
#define MM_COMMON_SMS_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_SMS_PROPERTIES, MMCommonSmsPropertiesClass))

typedef struct _MMCommonSmsProperties MMCommonSmsProperties;
typedef struct _MMCommonSmsPropertiesClass MMCommonSmsPropertiesClass;
typedef struct _MMCommonSmsPropertiesPrivate MMCommonSmsPropertiesPrivate;

struct _MMCommonSmsProperties {
    GObject parent;
    MMCommonSmsPropertiesPrivate *priv;
};

struct _MMCommonSmsPropertiesClass {
    GObjectClass parent;
};

GType mm_common_sms_properties_get_type (void);

MMCommonSmsProperties *mm_common_sms_properties_new (void);
MMCommonSmsProperties *mm_common_sms_properties_new_from_string (
    const gchar *str,
    GError **error);
MMCommonSmsProperties *mm_common_sms_properties_new_from_dictionary (
    GVariant *dictionary,
    GError **error);

MMCommonSmsProperties *mm_common_sms_properties_dup (MMCommonSmsProperties *orig);

void mm_common_sms_properties_set_text (
    MMCommonSmsProperties *properties,
    const gchar *text);
void mm_common_sms_properties_set_number (
    MMCommonSmsProperties *properties,
    const gchar *number);
void mm_common_sms_properties_set_smsc (
    MMCommonSmsProperties *properties,
    const gchar *smsc);
void mm_common_sms_properties_set_validity (
    MMCommonSmsProperties *properties,
    guint validity);
void mm_common_sms_properties_set_class (
    MMCommonSmsProperties *properties,
    guint class);

const gchar *mm_common_sms_properties_get_text (
    MMCommonSmsProperties *properties);
const gchar *mm_common_sms_properties_get_number (
    MMCommonSmsProperties *properties);
const gchar *mm_common_sms_properties_get_smsc (
    MMCommonSmsProperties *properties);
guint mm_common_sms_properties_get_validity (
    MMCommonSmsProperties *properties);
guint mm_common_sms_properties_get_class (
    MMCommonSmsProperties *properties);

GVariant *mm_common_sms_properties_get_dictionary (MMCommonSmsProperties *self);

G_END_DECLS

#endif /* MM_COMMON_SMS_PROPERTIES_H */
