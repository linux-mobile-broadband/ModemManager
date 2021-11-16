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
 * Boston, MA 02110-1301 USA.o
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BEARER_PROPERTIES_H
#define MM_BEARER_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>
#include <mm-3gpp-profile.h>

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

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

/**
 * MMBearerProperties:
 *
 * The #MMBearerProperties structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMBearerProperties {
    /*< private >*/
    GObject parent;
    MMBearerPropertiesPrivate *priv;
};

struct _MMBearerPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_bearer_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerProperties, g_object_unref)

MMBearerProperties *mm_bearer_properties_new (void);

void mm_bearer_properties_set_apn           (MMBearerProperties *self,
                                             const gchar *apn);
void mm_bearer_properties_set_allowed_auth  (MMBearerProperties *self,
                                             MMBearerAllowedAuth allowed_auth);
void mm_bearer_properties_set_user          (MMBearerProperties *self,
                                             const gchar *user);
void mm_bearer_properties_set_password      (MMBearerProperties *self,
                                             const gchar *password);
void mm_bearer_properties_set_ip_type       (MMBearerProperties *self,
                                             MMBearerIpFamily ip_type);
void mm_bearer_properties_set_apn_type      (MMBearerProperties *self,
                                             MMBearerApnType apn_type);
void mm_bearer_properties_set_profile_id    (MMBearerProperties *self,
                                             gint profile_id);
void mm_bearer_properties_set_allow_roaming (MMBearerProperties *self,
                                             gboolean allow_roaming);
void mm_bearer_properties_set_rm_protocol   (MMBearerProperties *self,
                                             MMModemCdmaRmProtocol protocol);
void mm_bearer_properties_set_multiplex     (MMBearerProperties       *self,
                                             MMBearerMultiplexSupport  multiplex);

const gchar              *mm_bearer_properties_get_apn           (MMBearerProperties *self);
MMBearerAllowedAuth       mm_bearer_properties_get_allowed_auth  (MMBearerProperties *self);
const gchar              *mm_bearer_properties_get_user          (MMBearerProperties *self);
const gchar              *mm_bearer_properties_get_password      (MMBearerProperties *self);
MMBearerIpFamily          mm_bearer_properties_get_ip_type       (MMBearerProperties *self);
MMBearerApnType           mm_bearer_properties_get_apn_type      (MMBearerProperties *self);
gint                      mm_bearer_properties_get_profile_id    (MMBearerProperties *self);
gboolean                  mm_bearer_properties_get_allow_roaming (MMBearerProperties *self);
MMModemCdmaRmProtocol     mm_bearer_properties_get_rm_protocol   (MMBearerProperties *self);
MMBearerMultiplexSupport  mm_bearer_properties_get_multiplex     (MMBearerProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMBearerProperties *mm_bearer_properties_new_from_string (const gchar *str,
                                                          GError **error);
MMBearerProperties *mm_bearer_properties_new_from_dictionary (GVariant *dictionary,
                                                              GError **error);
MMBearerProperties *mm_bearer_properties_new_from_profile (MM3gppProfile  *profile,
                                                           GError        **error);

gboolean mm_bearer_properties_consume_string (MMBearerProperties *self,
                                              const gchar *key,
                                              const gchar *value,
                                              GError **error);

gboolean mm_bearer_properties_consume_variant (MMBearerProperties *properties,
                                               const gchar *key,
                                               GVariant *value,
                                               GError **error);

GVariant      *mm_bearer_properties_get_dictionary    (MMBearerProperties *self);
MM3gppProfile *mm_bearer_properties_peek_3gpp_profile (MMBearerProperties *self);

typedef enum {
    MM_BEARER_PROPERTIES_CMP_FLAGS_NONE             = 0,
    MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE            = 1 << 0,
    MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PASSWORD      = 1 << 1,
    MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ALLOW_ROAMING = 1 << 2,
    MM_BEARER_PROPERTIES_CMP_FLAGS_NO_RM_PROTOCOL   = 1 << 3,
    MM_BEARER_PROPERTIES_CMP_FLAGS_NO_APN_TYPE      = 1 << 4,
    MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PROFILE_ID    = 1 << 5,
} MMBearerPropertiesCmpFlags;

gboolean mm_bearer_properties_cmp (MMBearerProperties         *a,
                                   MMBearerProperties         *b,
                                   MMBearerPropertiesCmpFlags  flags);

#endif

G_END_DECLS

#endif /* MM_BEARER_PROPERTIES_H */
