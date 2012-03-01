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

#ifndef MM_COMMON_CONNECT_PROPERTIES_H
#define MM_COMMON_CONNECT_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-bearer-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_COMMON_CONNECT_PROPERTIES            (mm_common_connect_properties_get_type ())
#define MM_COMMON_CONNECT_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_CONNECT_PROPERTIES, MMCommonConnectProperties))
#define MM_COMMON_CONNECT_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_CONNECT_PROPERTIES, MMCommonConnectPropertiesClass))
#define MM_IS_COMMON_CONNECT_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_CONNECT_PROPERTIES))
#define MM_IS_COMMON_CONNECT_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_CONNECT_PROPERTIES))
#define MM_COMMON_CONNECT_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_CONNECT_PROPERTIES, MMCommonConnectPropertiesClass))

typedef struct _MMCommonConnectProperties MMCommonConnectProperties;
typedef struct _MMCommonConnectPropertiesClass MMCommonConnectPropertiesClass;
typedef struct _MMCommonConnectPropertiesPrivate MMCommonConnectPropertiesPrivate;

struct _MMCommonConnectProperties {
    GObject parent;
    MMCommonConnectPropertiesPrivate *priv;
};

struct _MMCommonConnectPropertiesClass {
    GObjectClass parent;
};

GType mm_common_connect_properties_get_type (void);

MMCommonConnectProperties *mm_common_connect_properties_new (void);
MMCommonConnectProperties *mm_common_connect_properties_new_from_string (
    const gchar *str,
    GError **error);
MMCommonConnectProperties *mm_common_connect_properties_new_from_dictionary (
    GVariant *dictionary,
    GError **error);

void mm_common_connect_properties_set_pin (
    MMCommonConnectProperties *properties,
    const gchar *pin);
void mm_common_connect_properties_set_operator_id (
    MMCommonConnectProperties *properties,
    const gchar *operator_id);
void mm_common_connect_properties_set_bands (
    MMCommonConnectProperties *properties,
    const MMModemBand *bands,
    guint n_bands);
void mm_common_connect_properties_set_allowed_modes (
    MMCommonConnectProperties *properties,
    MMModemMode allowed,
    MMModemMode preferred);
void mm_common_connect_properties_set_apn (
    MMCommonConnectProperties *properties,
    const gchar *apn);
void mm_common_connect_properties_set_user (
    MMCommonConnectProperties *properties,
    const gchar *user);
void mm_common_connect_properties_set_password (
    MMCommonConnectProperties *properties,
    const gchar *password);
void mm_common_connect_properties_set_ip_type (
    MMCommonConnectProperties *properties,
    const gchar *ip_type);
void mm_common_connect_properties_set_allow_roaming (
    MMCommonConnectProperties *properties,
    gboolean allow_roaming);
void mm_common_connect_properties_set_number (
    MMCommonConnectProperties *properties,
    const gchar *number);

const gchar *mm_common_connect_properties_get_pin (
    MMCommonConnectProperties *properties);
const gchar *mm_common_connect_properties_get_operator_id (
    MMCommonConnectProperties *properties);
void mm_common_connect_properties_get_bands (
    MMCommonConnectProperties *properties,
    const MMModemBand **bands,
    guint *n_bands);
void mm_common_connect_properties_get_allowed_modes (
    MMCommonConnectProperties *properties,
    MMModemMode *allowed,
    MMModemMode *preferred);
const gchar *mm_common_connect_properties_get_apn (
    MMCommonConnectProperties *properties);
const gchar *mm_common_connect_properties_get_user (
    MMCommonConnectProperties *properties);
const gchar *mm_common_connect_properties_get_password (
    MMCommonConnectProperties *properties);
const gchar *mm_common_connect_properties_get_ip_type (
    MMCommonConnectProperties *properties);
gboolean mm_common_connect_properties_get_allow_roaming (
    MMCommonConnectProperties *properties);
const gchar *mm_common_connect_properties_get_number (
    MMCommonConnectProperties *properties);

MMBearerProperties *mm_common_connect_properties_get_bearer_properties (
    MMCommonConnectProperties *properties);

GVariant *mm_common_connect_properties_get_dictionary (MMCommonConnectProperties *self);

G_END_DECLS

#endif /* MM_COMMON_CONNECT_PROPERTIES_H */
