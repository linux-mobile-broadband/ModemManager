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

#ifndef MM_SIMPLE_CONNECT_PROPERTIES_H
#define MM_SIMPLE_CONNECT_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-bearer-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_SIMPLE_CONNECT_PROPERTIES            (mm_simple_connect_properties_get_type ())
#define MM_SIMPLE_CONNECT_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIMPLE_CONNECT_PROPERTIES, MMSimpleConnectProperties))
#define MM_SIMPLE_CONNECT_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIMPLE_CONNECT_PROPERTIES, MMSimpleConnectPropertiesClass))
#define MM_IS_SIMPLE_CONNECT_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIMPLE_CONNECT_PROPERTIES))
#define MM_IS_SIMPLE_CONNECT_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIMPLE_CONNECT_PROPERTIES))
#define MM_SIMPLE_CONNECT_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIMPLE_CONNECT_PROPERTIES, MMSimpleConnectPropertiesClass))

typedef struct _MMSimpleConnectProperties MMSimpleConnectProperties;
typedef struct _MMSimpleConnectPropertiesClass MMSimpleConnectPropertiesClass;
typedef struct _MMSimpleConnectPropertiesPrivate MMSimpleConnectPropertiesPrivate;

struct _MMSimpleConnectProperties {
    GObject parent;
    MMSimpleConnectPropertiesPrivate *priv;
};

struct _MMSimpleConnectPropertiesClass {
    GObjectClass parent;
};

GType mm_simple_connect_properties_get_type (void);

MMSimpleConnectProperties *mm_simple_connect_properties_new (void);
MMSimpleConnectProperties *mm_simple_connect_properties_new_from_string (const gchar *str,
                                                                         GError **error);
MMSimpleConnectProperties *mm_simple_connect_properties_new_from_dictionary (GVariant *dictionary,
                                                                             GError **error);

void mm_simple_connect_properties_set_pin           (MMSimpleConnectProperties *properties,
                                                     const gchar *pin);
void mm_simple_connect_properties_set_operator_id   (MMSimpleConnectProperties *properties,
                                                     const gchar *operator_id);
void mm_simple_connect_properties_set_bands         (MMSimpleConnectProperties *properties,
                                                     const MMModemBand *bands,
                                                     guint n_bands);
void mm_simple_connect_properties_set_allowed_modes (MMSimpleConnectProperties *properties,
                                                     MMModemMode allowed,
                                                     MMModemMode preferred);
void mm_simple_connect_properties_set_apn           (MMSimpleConnectProperties *properties,
                                                     const gchar *apn);
void mm_simple_connect_properties_set_user          (MMSimpleConnectProperties *properties,
                                                     const gchar *user);
void mm_simple_connect_properties_set_password      (MMSimpleConnectProperties *properties,
                                                     const gchar *password);
void mm_simple_connect_properties_set_ip_type       (MMSimpleConnectProperties *properties,
                                                     MMBearerIpFamily ip_type);
void mm_simple_connect_properties_set_allow_roaming (MMSimpleConnectProperties *properties,
                                                     gboolean allow_roaming);
void mm_simple_connect_properties_set_number        (MMSimpleConnectProperties *properties,
                                                     const gchar *number);

const gchar      *mm_simple_connect_properties_get_pin           (MMSimpleConnectProperties *properties);
const gchar      *mm_simple_connect_properties_get_operator_id   (MMSimpleConnectProperties *properties);
void              mm_simple_connect_properties_get_bands         (MMSimpleConnectProperties *properties,
                                                                  const MMModemBand **bands,
                                                                  guint *n_bands);
void              mm_simple_connect_properties_get_allowed_modes (MMSimpleConnectProperties *properties,
                                                                  MMModemMode *allowed,
                                                                  MMModemMode *preferred);
const gchar      *mm_simple_connect_properties_get_apn           (MMSimpleConnectProperties *properties);
const gchar      *mm_simple_connect_properties_get_user          (MMSimpleConnectProperties *properties);
const gchar      *mm_simple_connect_properties_get_password      (MMSimpleConnectProperties *properties);
MMBearerIpFamily  mm_simple_connect_properties_get_ip_type       (MMSimpleConnectProperties *properties);
gboolean          mm_simple_connect_properties_get_allow_roaming (MMSimpleConnectProperties *properties);
const gchar      *mm_simple_connect_properties_get_number        (MMSimpleConnectProperties *properties);

MMBearerProperties *mm_simple_connect_properties_get_bearer_properties (MMSimpleConnectProperties *properties);

GVariant *mm_simple_connect_properties_get_dictionary (MMSimpleConnectProperties *self);

G_END_DECLS

#endif /* MM_SIMPLE_CONNECT_PROPERTIES_H */
