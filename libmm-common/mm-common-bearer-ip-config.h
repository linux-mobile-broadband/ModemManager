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

#ifndef MM_COMMON_BEARER_IP_CONFIG_H
#define MM_COMMON_BEARER_IP_CONFIG_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_COMMON_BEARER_IP_CONFIG            (mm_common_bearer_ip_config_get_type ())
#define MM_COMMON_BEARER_IP_CONFIG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_COMMON_BEARER_IP_CONFIG, MMCommonBearerIpConfig))
#define MM_COMMON_BEARER_IP_CONFIG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_COMMON_BEARER_IP_CONFIG, MMCommonBearerIpConfigClass))
#define MM_IS_COMMON_BEARER_IP_CONFIG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_COMMON_BEARER_IP_CONFIG))
#define MM_IS_COMMON_BEARER_IP_CONFIG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_COMMON_BEARER_IP_CONFIG))
#define MM_COMMON_BEARER_IP_CONFIG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_COMMON_BEARER_IP_CONFIG, MMCommonBearerIpConfigClass))

typedef struct _MMCommonBearerIpConfig MMCommonBearerIpConfig;
typedef struct _MMCommonBearerIpConfigClass MMCommonBearerIpConfigClass;
typedef struct _MMCommonBearerIpConfigPrivate MMCommonBearerIpConfigPrivate;

struct _MMCommonBearerIpConfig {
    GObject parent;
    MMCommonBearerIpConfigPrivate *priv;
};

struct _MMCommonBearerIpConfigClass {
    GObjectClass parent;
};

GType mm_common_bearer_ip_config_get_type (void);

MMCommonBearerIpConfig *mm_common_bearer_ip_config_new (void);
MMCommonBearerIpConfig *mm_common_bearer_ip_config_new_from_dictionary (GVariant *dictionary,
                                                                        GError **error);

MMCommonBearerIpConfig *mm_common_bearer_ip_config_dup (MMCommonBearerIpConfig *orig);

MMBearerIpMethod   mm_common_bearer_ip_config_get_method  (MMCommonBearerIpConfig *self);
const gchar       *mm_common_bearer_ip_config_get_address (MMCommonBearerIpConfig *self);
guint              mm_common_bearer_ip_config_get_prefix  (MMCommonBearerIpConfig *self);
const gchar      **mm_common_bearer_ip_config_get_dns     (MMCommonBearerIpConfig *self);
const gchar       *mm_common_bearer_ip_config_get_gateway (MMCommonBearerIpConfig *self);

void mm_common_bearer_ip_config_set_method  (MMCommonBearerIpConfig *self,
                                             MMBearerIpMethod ip_method);
void mm_common_bearer_ip_config_set_address (MMCommonBearerIpConfig *self,
                                             const gchar *address);
void mm_common_bearer_ip_config_set_prefix  (MMCommonBearerIpConfig *self,
                                             guint prefix);
void mm_common_bearer_ip_config_set_dns     (MMCommonBearerIpConfig *self,
                                             const gchar **dns);
void mm_common_bearer_ip_config_set_gateway (MMCommonBearerIpConfig *self,
                                             const gchar *gateway);

GVariant *mm_common_bearer_ip_config_get_dictionary (MMCommonBearerIpConfig *self);

G_END_DECLS

#endif /* MM_COMMON_BEARER_IP_CONFIG_H */
