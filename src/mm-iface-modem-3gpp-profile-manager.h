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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_H
#define MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#define MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER               (mm_iface_modem_3gpp_profile_manager_get_type ())
#define MM_IFACE_MODEM_3GPP_PROFILE_MANAGER(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, MMIfaceModem3gppProfileManager))
#define MM_IS_IFACE_MODEM_3GPP_PROFILE_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER))
#define MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, MMIfaceModem3gppProfileManager))

#define MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON "iface-modem-3gpp-profile-manager-dbus-skeleton"

typedef struct _MMIfaceModem3gppProfileManager MMIfaceModem3gppProfileManager;

struct _MMIfaceModem3gppProfileManager {
    GTypeInterface g_iface;

    /* Check for profile management support (async) */
    void     (* check_support)        (MMIfaceModem3gppProfileManager  *self,
                                       GAsyncReadyCallback              callback,
                                       gpointer                         user_data);
    gboolean (* check_support_finish) (MMIfaceModem3gppProfileManager  *self,
                                       GAsyncResult                    *res,
                                       gchar                          **index_field,
                                       GError                         **error);

    /* Asynchronous setup of unsolicited events */
    void     (* setup_unsolicited_events)        (MMIfaceModem3gppProfileManager  *self,
                                                  GAsyncReadyCallback              callback,
                                                  gpointer                         user_data);
    gboolean (* setup_unsolicited_events_finish) (MMIfaceModem3gppProfileManager  *self,
                                                  GAsyncResult                    *res,
                                                  GError                         **error);

    /* Asynchronous enabling of unsolicited events */
    void     (* enable_unsolicited_events)        (MMIfaceModem3gppProfileManager  *self,
                                                   GAsyncReadyCallback              callback,
                                                   gpointer                         user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModem3gppProfileManager  *self,
                                                   GAsyncResult                    *res,
                                                   GError                         **error);

    /* Asynchronous disabling of unsolicited events */
    void     (* disable_unsolicited_events)        (MMIfaceModem3gppProfileManager  *self,
                                                    GAsyncReadyCallback              callback,
                                                    gpointer                         user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModem3gppProfileManager  *self,
                                                    GAsyncResult                    *res,
                                                    GError                         **error);

    /* Asynchronous cleaning up of unsolicited events */
    void     (* cleanup_unsolicited_events)        (MMIfaceModem3gppProfileManager  *self,
                                                    GAsyncReadyCallback              callback,
                                                    gpointer                         user_data);
    gboolean (* cleanup_unsolicited_events_finish) (MMIfaceModem3gppProfileManager  *self,
                                                    GAsyncResult                    *res,
                                                    GError                         **error);

    /* Get a single profile.
     * This is completely optional, and should be used by implementations that are able
     * to query one single profile settings. For all other implementations, it should be
     * set to NULL so that the generic implementation is used (listing all and lookup by
     * profile id) */
    void            (* get_profile)        (MMIfaceModem3gppProfileManager  *self,
                                            gint                             profile_id,
                                            GAsyncReadyCallback              callback,
                                            gpointer                         user_data);
    MM3gppProfile * (* get_profile_finish) (MMIfaceModem3gppProfileManager  *self,
                                            GAsyncResult                    *res,
                                            GError                         **error);

    /* List */
    void     (* list_profiles)        (MMIfaceModem3gppProfileManager  *self,
                                       GAsyncReadyCallback              callback,
                                       gpointer                         user_data);
    gboolean (* list_profiles_finish) (MMIfaceModem3gppProfileManager  *self,
                                       GAsyncResult                    *res,
                                       GList                          **profiles,
                                       GError                         **error);

    /* Delete */
    void     (* delete_profile)        (MMIfaceModem3gppProfileManager  *self,
                                        MM3gppProfile                   *requested,
                                        const gchar                     *index_field,
                                        GAsyncReadyCallback              callback,
                                        gpointer                         user_data);
    gboolean (* delete_profile_finish) (MMIfaceModem3gppProfileManager  *self,
                                        GAsyncResult                    *res,
                                        GError                         **error);

    /*
     * Check profile format (substep of 'set profiles')
     *
     * Before a profile is set, we would like to check the format of the set
     * operation, if possible.
     *
     * Expected outputs:
     *   - new_id: whether new profiles can be created with a specific known id.
     *   - min_profile_id: minimum supported profile id.
     *   - max_profile_id: maximum supported profile id.
     *   - apn_cmp: method to use when comparing APN strings.
     *   - profile_cmp_flags: flags to use when comparing profile objects.
     *
     * The check is done per IP family, as the ranges may be different for each.
     */
    void     (* check_format)        (MMIfaceModem3gppProfileManager  *self,
                                      MMBearerIpFamily                 family,
                                      GAsyncReadyCallback              callback,
                                      gpointer                         user_data);
    gboolean (* check_format_finish) (MMIfaceModem3gppProfileManager  *self,
                                      GAsyncResult                    *res,
                                      gboolean                        *new_id,
                                      gint                            *min_profile_id,
                                      gint                            *max_profile_id,
                                      GEqualFunc                      *apn_cmp,
                                      MM3gppProfileCmpFlags           *profile_cmp_flags,
                                      GError                         **error);

    /* Check activated profile (substep of 'set profiles')
     *
     * Before a profile is set, we may attempt to deactivate it first, but only
     * if there is no known bearer using it already and only if this check for
     * activation really reports the profile being already activated.
     *
     * The given profile MUST have profile-id set, so the set_profile()
     * implementation should only use it once the profile-id is known, never
     * before.
     *
     * This step is optional (method pointers can be initialized to NULL), so
     * that the deactivate profile step is done always.
     */
    void     (* check_activated_profile)        (MMIfaceModem3gppProfileManager  *self,
                                                 MM3gppProfile                   *requested,
                                                 GAsyncReadyCallback              callback,
                                                 gpointer                         user_data);
    gboolean (* check_activated_profile_finish) (MMIfaceModem3gppProfileManager  *self,
                                                 GAsyncResult                    *res,
                                                 gboolean                        *out_activated,
                                                 GError                         **error);

    /* Deactivate profile (substep of 'set profiles')
     *
     * Before a profile is set, we may attempt to deactivate it first, but only
     * if there is no known bearer using it already.
     *
     * The given profile MUST have profile-id set, so the set_profile()
     * implementation should only use it once the profile-id is known, never
     * before.
     */
    void     (* deactivate_profile)        (MMIfaceModem3gppProfileManager  *self,
                                            MM3gppProfile                   *requested,
                                            GAsyncReadyCallback              callback,
                                            gpointer                         user_data);
    gboolean (* deactivate_profile_finish) (MMIfaceModem3gppProfileManager  *self,
                                            GAsyncResult                    *res,
                                            GError                         **error);

    /* Store profile (substep of 'set profiles') */
    void     (* store_profile)        (MMIfaceModem3gppProfileManager  *self,
                                       MM3gppProfile                   *requested,
                                       const gchar                     *index_field,
                                       GAsyncReadyCallback              callback,
                                       gpointer                         user_data);
    gboolean (* store_profile_finish) (MMIfaceModem3gppProfileManager  *self,
                                       GAsyncResult                    *res,
                                       gint                            *out_profile_id,
                                       MMBearerApnType                 *out_apn_type,
                                       GError                         **error);
};

GType mm_iface_modem_3gpp_profile_manager_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModem3gppProfileManager, g_object_unref)

/* Initialize profile manager interface (async) */
void     mm_iface_modem_3gpp_profile_manager_initialize        (MMIfaceModem3gppProfileManager  *self,
                                                                GAsyncReadyCallback              callback,
                                                                gpointer                         user_data);
gboolean mm_iface_modem_3gpp_profile_manager_initialize_finish (MMIfaceModem3gppProfileManager  *self,
                                                                GAsyncResult                    *res,
                                                                GError                         **error);

/* Enable profile manager interface (async) */
void     mm_iface_modem_3gpp_profile_manager_enable        (MMIfaceModem3gppProfileManager  *self,
                                                            GAsyncReadyCallback              callback,
                                                            gpointer                         user_data);
gboolean mm_iface_modem_3gpp_profile_manager_enable_finish (MMIfaceModem3gppProfileManager  *self,
                                                            GAsyncResult                    *res,
                                                            GError                         **error);

/* Disable profile manager interface (async) */
void     mm_iface_modem_3gpp_profile_manager_disable        (MMIfaceModem3gppProfileManager  *self,
                                                             GAsyncReadyCallback              callback,
                                                             gpointer                         user_data);
gboolean mm_iface_modem_3gpp_profile_manager_disable_finish (MMIfaceModem3gppProfileManager  *self,
                                                             GAsyncResult                    *res,
                                                             GError                         **error);

/* Shutdown profile manager interface */
void mm_iface_modem_3gpp_profile_manager_shutdown (MMIfaceModem3gppProfileManager *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_3gpp_profile_manager_bind_simple_status (MMIfaceModem3gppProfileManager *self,
                                                             MMSimpleStatus                 *status);

/* Helper to emit the Updated signal by implementations */
void mm_iface_modem_3gpp_profile_manager_updated (MMIfaceModem3gppProfileManager *self);

/* Internal list profile management */
void           mm_iface_modem_3gpp_profile_manager_get_profile          (MMIfaceModem3gppProfileManager  *self,
                                                                         gint                             profile_id,
                                                                         GAsyncReadyCallback              callback,
                                                                         gpointer                         user_data);
MM3gppProfile *mm_iface_modem_3gpp_profile_manager_get_profile_finish   (MMIfaceModem3gppProfileManager  *self,
                                                                         GAsyncResult                    *res,
                                                                         GError                         **error);
void           mm_iface_modem_3gpp_profile_manager_list_profiles        (MMIfaceModem3gppProfileManager  *self,
                                                                         GAsyncReadyCallback              callback,
                                                                         gpointer                         user_data);
gboolean       mm_iface_modem_3gpp_profile_manager_list_profiles_finish (MMIfaceModem3gppProfileManager  *self,
                                                                         GAsyncResult                    *res,
                                                                         GList                          **profiles,
                                                                         GError                         **error);
void           mm_iface_modem_3gpp_profile_manager_set_profile          (MMIfaceModem3gppProfileManager  *self,
                                                                         MM3gppProfile                   *requested,
                                                                         const gchar                     *index_field,
                                                                         gboolean                         strict,
                                                                         GAsyncReadyCallback              callback,
                                                                         gpointer                         user_data);
MM3gppProfile *mm_iface_modem_3gpp_profile_manager_set_profile_finish   (MMIfaceModem3gppProfileManager  *self,
                                                                         GAsyncResult                    *res,
                                                                         GError                         **error);


#endif /* MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_H */
