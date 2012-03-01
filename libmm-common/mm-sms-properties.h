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

#ifndef MM_SMS_PROPERTIES_H
#define MM_SMS_PROPERTIES_H

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

struct _MMSmsProperties {
    GObject parent;
    MMSmsPropertiesPrivate *priv;
};

struct _MMSmsPropertiesClass {
    GObjectClass parent;
};

GType mm_sms_properties_get_type (void);

MMSmsProperties *mm_sms_properties_new (void);
MMSmsProperties *mm_sms_properties_new_from_string (const gchar *str,
                                                    GError **error);
MMSmsProperties *mm_sms_properties_new_from_dictionary (GVariant *dictionary,
                                                        GError **error);

MMSmsProperties *mm_sms_properties_dup (MMSmsProperties *orig);

void mm_sms_properties_set_text     (MMSmsProperties *properties,
                                     const gchar *text);
void mm_sms_properties_set_number   (MMSmsProperties *properties,
                                     const gchar *number);
void mm_sms_properties_set_smsc     (MMSmsProperties *properties,
                                     const gchar *smsc);
void mm_sms_properties_set_validity (MMSmsProperties *properties,
                                     guint validity);
void mm_sms_properties_set_class    (MMSmsProperties *properties,
                                     guint class);

const gchar *mm_sms_properties_get_text     (MMSmsProperties *properties);
const gchar *mm_sms_properties_get_number   (MMSmsProperties *properties);
const gchar *mm_sms_properties_get_smsc     (MMSmsProperties *properties);
guint        mm_sms_properties_get_validity (MMSmsProperties *properties);
guint        mm_sms_properties_get_class    (MMSmsProperties *properties);

GVariant *mm_sms_properties_get_dictionary (MMSmsProperties *self);

G_END_DECLS

#endif /* MM_SMS_PROPERTIES_H */
