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

#ifndef MM_BEARER_PROPERTIES_H
#define MM_BEARER_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_BEARER_PROPERTIES            (mm_bearer_properties_get_type ())
#define MM_BEARER_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_PROPERTIES, MMBearerProperties))
#define MM_BEARER_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_PROPERTIES, MMBearerPropertiesClass))
#define MM_IS_BEARER_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_PROPERTIES))
#define MM_IS_BEARER_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_PROPERTIES))
#define MM_BEARER_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_PROPERTIES, MMBearerPropertiesClass))

typedef struct _MMBearerProperties MMBearerProperties;
typedef struct _MMBearerPropertiesClass MMBearerPropertiesClass;
typedef struct _MMBearerPropertiesPrivate MMBearerPropertiesPrivate;

struct _MMBearerProperties {
    GObject parent;
    MMBearerPropertiesPrivate *priv;
};

struct _MMBearerPropertiesClass {
    GObjectClass parent;
};

GType mm_bearer_properties_get_type (void);

MMBearerProperties *mm_bearer_properties_new (void);
MMBearerProperties *mm_bearer_properties_new_from_string (const gchar *str,
                                                          GError **error);
MMBearerProperties *mm_bearer_properties_new_from_dictionary (GVariant *dictionary,
                                                              GError **error);

MMBearerProperties *mm_bearer_properties_dup (MMBearerProperties *orig);

void mm_bearer_properties_set_apn           (MMBearerProperties *properties,
                                             const gchar *apn);
void mm_bearer_properties_set_user          (MMBearerProperties *properties,
                                             const gchar *user);
void mm_bearer_properties_set_password      (MMBearerProperties *properties,
                                             const gchar *password);
void mm_bearer_properties_set_ip_type       (MMBearerProperties *properties,
                                             MMBearerIpFamily ip_type);
void mm_bearer_properties_set_allow_roaming (MMBearerProperties *properties,
                                             gboolean allow_roaming);
void mm_bearer_properties_set_number        (MMBearerProperties *properties,
                                             const gchar *number);
void mm_bearer_properties_set_rm_protocol   (MMBearerProperties *properties,
                                             MMModemCdmaRmProtocol protocol);

const gchar           *mm_bearer_properties_get_apn           (MMBearerProperties *properties);
const gchar           *mm_bearer_properties_get_user          (MMBearerProperties *properties);
const gchar           *mm_bearer_properties_get_password      (MMBearerProperties *properties);
MMBearerIpFamily       mm_bearer_properties_get_ip_type       (MMBearerProperties *properties);
gboolean               mm_bearer_properties_get_allow_roaming (MMBearerProperties *properties);
const gchar           *mm_bearer_properties_get_number        (MMBearerProperties *properties);
MMModemCdmaRmProtocol  mm_bearer_properties_get_rm_protocol   (MMBearerProperties *properties);

gboolean mm_bearer_properties_consume_string (MMBearerProperties *self,
                                              const gchar *key,
                                              const gchar *value,
                                              GError **error);

gboolean mm_bearer_properties_consume_variant (MMBearerProperties *self,
                                               const gchar *key,
                                               GVariant *value,
                                               GError **error);

GVariant *mm_bearer_properties_get_dictionary (MMBearerProperties *self);

gboolean mm_bearer_properties_cmp (MMBearerProperties *a,
                                   MMBearerProperties *b);

G_END_DECLS

#endif /* MM_BEARER_PROPERTIES_H */
