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

#ifndef MM_BEARER_IP_CONFIG_H
#define MM_BEARER_IP_CONFIG_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_BEARER_IP_CONFIG            (mm_bearer_ip_config_get_type ())
#define MM_BEARER_IP_CONFIG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_IP_CONFIG, MMBearerIpConfig))
#define MM_BEARER_IP_CONFIG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_IP_CONFIG, MMBearerIpConfigClass))
#define MM_IS_BEARER_IP_CONFIG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_IP_CONFIG))
#define MM_IS_BEARER_IP_CONFIG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_IP_CONFIG))
#define MM_BEARER_IP_CONFIG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_IP_CONFIG, MMBearerIpConfigClass))

typedef struct _MMBearerIpConfig MMBearerIpConfig;
typedef struct _MMBearerIpConfigClass MMBearerIpConfigClass;
typedef struct _MMBearerIpConfigPrivate MMBearerIpConfigPrivate;

/**
 * MMBearerIpConfig:
 *
 * The #MMBearerIpConfig structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMBearerIpConfig {
    /*< private >*/
    GObject parent;
    MMBearerIpConfigPrivate *priv;
};

struct _MMBearerIpConfigClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_bearer_ip_config_get_type (void);

MMBearerIpMethod   mm_bearer_ip_config_get_method  (MMBearerIpConfig *self);
const gchar       *mm_bearer_ip_config_get_address (MMBearerIpConfig *self);
guint              mm_bearer_ip_config_get_prefix  (MMBearerIpConfig *self);
const gchar      **mm_bearer_ip_config_get_dns     (MMBearerIpConfig *self);
const gchar       *mm_bearer_ip_config_get_gateway (MMBearerIpConfig *self);
guint              mm_bearer_ip_config_get_mtu     (MMBearerIpConfig *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMBearerIpConfig *mm_bearer_ip_config_new (void);
MMBearerIpConfig *mm_bearer_ip_config_new_from_dictionary (GVariant *dictionary,
                                                           GError **error);

MMBearerIpConfig *mm_bearer_ip_config_dup (MMBearerIpConfig *orig);

void mm_bearer_ip_config_set_method  (MMBearerIpConfig *self,
                                      MMBearerIpMethod ip_method);
void mm_bearer_ip_config_set_address (MMBearerIpConfig *self,
                                      const gchar *address);
void mm_bearer_ip_config_set_prefix  (MMBearerIpConfig *self,
                                      guint prefix);
void mm_bearer_ip_config_set_dns     (MMBearerIpConfig *self,
                                      const gchar **dns);
void mm_bearer_ip_config_set_gateway (MMBearerIpConfig *self,
                                      const gchar *gateway);
void mm_bearer_ip_config_set_mtu     (MMBearerIpConfig *self,
                                      guint mtu);

GVariant *mm_bearer_ip_config_get_dictionary (MMBearerIpConfig *self);

#endif

G_END_DECLS

#endif /* MM_BEARER_IP_CONFIG_H */
