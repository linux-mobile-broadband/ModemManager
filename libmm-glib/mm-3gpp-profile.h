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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_3GPP_PROFILE_H
#define MM_3GPP_PROFILE_H

#include <ModemManager.h>
#include <glib-object.h>

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
# error "Only <libmm-glib.h> can be included directly."
#endif

G_BEGIN_DECLS

/**
 * MM_3GPP_PROFILE_ID_UNKNOWN:
 *
 * This value may be specified in the 'profile-id' property When the user
 * creates a new #MM3gppProfile, to indicate that the real profile id should
 * be assigned by the device.
 */
#define MM_3GPP_PROFILE_ID_UNKNOWN -1

#define MM_TYPE_3GPP_PROFILE            (mm_3gpp_profile_get_type ())
#define MM_3GPP_PROFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_3GPP_PROFILE, MM3gppProfile))
#define MM_3GPP_PROFILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_3GPP_PROFILE, MM3gppProfileClass))
#define MM_IS_3GPP_PROFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_3GPP_PROFILE))
#define MM_IS_3GPP_PROFILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_3GPP_PROFILE))
#define MM_3GPP_PROFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_3GPP_PROFILE, MM3gppProfileClass))

typedef struct _MM3gppProfile MM3gppProfile;
typedef struct _MM3gppProfileClass MM3gppProfileClass;
typedef struct _MM3gppProfilePrivate MM3gppProfilePrivate;

/**
 * MM3gppProfile:
 *
 * The #MM3gppProfile structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MM3gppProfile {
    /*< private >*/
    GObject parent;
    MM3gppProfilePrivate *priv;
};

struct _MM3gppProfileClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_3gpp_profile_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MM3gppProfile, g_object_unref)

MM3gppProfile *mm_3gpp_profile_new (void);

void mm_3gpp_profile_set_profile_id   (MM3gppProfile       *self,
                                       gint                 profile_id);
void mm_3gpp_profile_set_apn          (MM3gppProfile       *self,
                                       const gchar         *apn);
void mm_3gpp_profile_set_allowed_auth (MM3gppProfile       *self,
                                       MMBearerAllowedAuth  allowed_auth);
void mm_3gpp_profile_set_user         (MM3gppProfile       *self,
                                       const gchar         *user);
void mm_3gpp_profile_set_password     (MM3gppProfile       *self,
                                       const gchar         *password);
void mm_3gpp_profile_set_ip_type      (MM3gppProfile       *self,
                                       MMBearerIpFamily     ip_type);
void mm_3gpp_profile_set_apn_type     (MM3gppProfile       *self,
                                       MMBearerApnType      apn_type);

gint                 mm_3gpp_profile_get_profile_id   (MM3gppProfile *self);
const gchar         *mm_3gpp_profile_get_apn          (MM3gppProfile *self);
MMBearerAllowedAuth  mm_3gpp_profile_get_allowed_auth (MM3gppProfile *self);
const gchar         *mm_3gpp_profile_get_user         (MM3gppProfile *self);
const gchar         *mm_3gpp_profile_get_password     (MM3gppProfile *self);
MMBearerIpFamily     mm_3gpp_profile_get_ip_type      (MM3gppProfile *self);
MMBearerApnType      mm_3gpp_profile_get_apn_type     (MM3gppProfile *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MM3gppProfile *mm_3gpp_profile_new_from_string     (const gchar    *str,
                                                    GError        **error);
MM3gppProfile *mm_3gpp_profile_new_from_dictionary (GVariant       *dictionary,
                                                    GError        **error);
GVariant      *mm_3gpp_profile_get_dictionary      (MM3gppProfile  *self);
gboolean       mm_3gpp_profile_consume_string      (MM3gppProfile  *self,
                                                    const gchar    *key,
                                                    const gchar    *value,
                                                    GError        **error);
gboolean       mm_3gpp_profile_consume_variant     (MM3gppProfile  *self,
                                                    const gchar    *key,
                                                    GVariant       *value,
                                                    GError        **error);

typedef enum {
    MM_3GPP_PROFILE_CMP_FLAGS_NONE          = 0,
    MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_ID = 1 << 1,
    MM_3GPP_PROFILE_CMP_FLAGS_NO_AUTH       = 1 << 2,
    MM_3GPP_PROFILE_CMP_FLAGS_NO_APN_TYPE   = 1 << 3,
    MM_3GPP_PROFILE_CMP_FLAGS_NO_IP_TYPE    = 1 << 4,
} MM3gppProfileCmpFlags;

gboolean mm_3gpp_profile_cmp (MM3gppProfile         *a,
                              MM3gppProfile         *b,
                              GEqualFunc             cmp_apn,
                              MM3gppProfileCmpFlags  flags);

#endif

G_END_DECLS

#endif /* MM_3GPP_PROFILE_H */
