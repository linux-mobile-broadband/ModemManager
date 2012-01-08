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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_COMMON_BEARER_PROPERTIES_H
#define MM_COMMON_BEARER_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_COMMON_BEARER_PROPERTIES            (mm_common_bearer_properties_get_type ())
#define MM_COMMON_BEARER_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_BEARER_PROPERTIES, MMCommonBearerProperties))
#define MM_COMMON_BEARER_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_BEARER_PROPERTIES, MMCommonBearerPropertiesClass))
#define MM_IS_COMMON_BEARER_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_BEARER_PROPERTIES))
#define MM_IS_COMMON_BEARER_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_BEARER_PROPERTIES))
#define MM_COMMON_BEARER_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_BEARER_PROPERTIES, MMCommonBearerPropertiesClass))

typedef struct _MMCommonBearerProperties MMCommonBearerProperties;
typedef struct _MMCommonBearerPropertiesClass MMCommonBearerPropertiesClass;
typedef struct _MMCommonBearerPropertiesPrivate MMCommonBearerPropertiesPrivate;

struct _MMCommonBearerProperties {
    GObject parent;
    MMCommonBearerPropertiesPrivate *priv;
};

struct _MMCommonBearerPropertiesClass {
    GObjectClass parent;
};

GType mm_common_bearer_properties_get_type (void);

MMCommonBearerProperties *mm_common_bearer_properties_new (void);
MMCommonBearerProperties *mm_common_bearer_properties_new_from_string (
    const gchar *str,
    GError **error);
MMCommonBearerProperties *mm_common_bearer_properties_new_from_dictionary (
    GVariant *dictionary,
    GError **error);

void mm_common_bearer_properties_set_apn (
    MMCommonBearerProperties *properties,
    const gchar *apn);
void mm_common_bearer_properties_set_user (
    MMCommonBearerProperties *properties,
    const gchar *user);
void mm_common_bearer_properties_set_password (
    MMCommonBearerProperties *properties,
    const gchar *password);
void mm_common_bearer_properties_set_ip_type (
    MMCommonBearerProperties *properties,
    const gchar *ip_type);
void mm_common_bearer_properties_set_allow_roaming (
    MMCommonBearerProperties *properties,
    gboolean allow_roaming);
void mm_common_bearer_properties_set_number (
    MMCommonBearerProperties *properties,
    const gchar *number);
void mm_common_bearer_properties_set_rm_protocol (
    MMCommonBearerProperties *properties,
    MMModemCdmaRmProtocol protocol);

const gchar *mm_common_bearer_properties_get_apn (
    MMCommonBearerProperties *properties);
const gchar *mm_common_bearer_properties_get_user (
    MMCommonBearerProperties *properties);
const gchar *mm_common_bearer_properties_get_password (
    MMCommonBearerProperties *properties);
const gchar *mm_common_bearer_properties_get_ip_type (
    MMCommonBearerProperties *properties);
gboolean mm_common_bearer_properties_get_allow_roaming (
    MMCommonBearerProperties *properties);
const gchar *mm_common_bearer_properties_get_number (
    MMCommonBearerProperties *properties);
MMModemCdmaRmProtocol mm_common_bearer_properties_get_rm_protocol (
    MMCommonBearerProperties *properties);

gboolean mm_common_bearer_properties_consume_string (MMCommonBearerProperties *self,
                                                     const gchar *key,
                                                     const gchar *value,
                                                     GError **error);

gboolean mm_common_bearer_properties_consume_variant (MMCommonBearerProperties *self,
                                                      const gchar *key,
                                                      GVariant *value,
                                                      GError **error);

GVariant *mm_common_bearer_properties_get_dictionary (MMCommonBearerProperties *self);

G_END_DECLS

#endif /* MM_COMMON_BEARER_PROPERTIES_H */
