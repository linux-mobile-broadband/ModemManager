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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_SIMPLE_CONNECT_PROPERTIES_H
#define MM_SIMPLE_CONNECT_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

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

/**
 * MMSimpleConnectProperties:
 *
 * The #MMSimpleConnectProperties structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMSimpleConnectProperties {
    /*< private >*/
    GObject parent;
    MMSimpleConnectPropertiesPrivate *priv;
};

struct _MMSimpleConnectPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_simple_connect_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSimpleConnectProperties, g_object_unref)

MMSimpleConnectProperties *mm_simple_connect_properties_new (void);

void mm_simple_connect_properties_set_pin           (MMSimpleConnectProperties *self,
                                                     const gchar *pin);
void mm_simple_connect_properties_set_operator_id   (MMSimpleConnectProperties *self,
                                                     const gchar *operator_id);
void mm_simple_connect_properties_set_apn           (MMSimpleConnectProperties *self,
                                                     const gchar *apn);
void mm_simple_connect_properties_set_allowed_auth  (MMSimpleConnectProperties *self,
                                                     MMBearerAllowedAuth allowed_auth);
void mm_simple_connect_properties_set_user          (MMSimpleConnectProperties *self,
                                                     const gchar *user);
void mm_simple_connect_properties_set_password      (MMSimpleConnectProperties *self,
                                                     const gchar *password);
void mm_simple_connect_properties_set_ip_type       (MMSimpleConnectProperties *self,
                                                     MMBearerIpFamily ip_type);
void mm_simple_connect_properties_set_apn_type      (MMSimpleConnectProperties *self,
                                                     MMBearerApnType apn_type);
void mm_simple_connect_properties_set_profile_id    (MMSimpleConnectProperties *self,
                                                     gint profile_id);
void mm_simple_connect_properties_set_allow_roaming (MMSimpleConnectProperties *self,
                                                     gboolean allow_roaming);
void mm_simple_connect_properties_set_rm_protocol   (MMSimpleConnectProperties *self,
                                                     MMModemCdmaRmProtocol protocol);
void mm_simple_connect_properties_set_multiplex     (MMSimpleConnectProperties *self,
                                                     MMBearerMultiplexSupport multiplex);

const gchar              *mm_simple_connect_properties_get_pin           (MMSimpleConnectProperties *self);
const gchar              *mm_simple_connect_properties_get_operator_id   (MMSimpleConnectProperties *self);
const gchar              *mm_simple_connect_properties_get_apn           (MMSimpleConnectProperties *self);
MMBearerAllowedAuth       mm_simple_connect_properties_get_allowed_auth  (MMSimpleConnectProperties *self);
const gchar              *mm_simple_connect_properties_get_user          (MMSimpleConnectProperties *self);
const gchar              *mm_simple_connect_properties_get_password      (MMSimpleConnectProperties *self);
MMBearerIpFamily          mm_simple_connect_properties_get_ip_type       (MMSimpleConnectProperties *self);
MMBearerApnType           mm_simple_connect_properties_get_apn_type      (MMSimpleConnectProperties *self);
gint                      mm_simple_connect_properties_get_profile_id    (MMSimpleConnectProperties *self);
gboolean                  mm_simple_connect_properties_get_allow_roaming (MMSimpleConnectProperties *self);
MMModemCdmaRmProtocol     mm_simple_connect_properties_get_rm_protocol   (MMSimpleConnectProperties *self);
MMBearerMultiplexSupport  mm_simple_connect_properties_get_multiplex     (MMSimpleConnectProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMSimpleConnectProperties *mm_simple_connect_properties_new_from_string (const gchar *str,
                                                                         GError **error);
MMSimpleConnectProperties *mm_simple_connect_properties_new_from_dictionary (GVariant *dictionary,
                                                                             GError **error);

MMBearerProperties *mm_simple_connect_properties_get_bearer_properties (MMSimpleConnectProperties *self);

GVariant *mm_simple_connect_properties_get_dictionary (MMSimpleConnectProperties *self);
#endif


G_END_DECLS

#endif /* MM_SIMPLE_CONNECT_PROPERTIES_H */
