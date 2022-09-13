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

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-base-modem.h"
#include "mm-log-object.h"
#include "mm-log-helpers.h"

#define SUPPORT_CHECKED_TAG "3gpp-profile-manager-support-checked-tag"
#define SUPPORTED_TAG       "3gpp-profile-manager-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_profile_manager_bind_simple_status (MMIfaceModem3gppProfileManager *self,
                                                        MMSimpleStatus                 *status)
{
    /* Nothing shown in simple status */
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_profile_manager_updated (MMIfaceModem3gppProfileManager *self)
{
    g_autoptr(MmGdbusModem3gppProfileManagerSkeleton) skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton)
        mm_gdbus_modem3gpp_profile_manager_emit_updated (MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (skeleton));
}

static gboolean
profile_manager_fail_if_connected_bearer (MMIfaceModem3gppProfileManager  *self,
                                          const gchar                     *index_field,
                                          gint                             profile_id,
                                          MMBearerApnType                  apn_type,
                                          GError                         **error)
{
    g_autoptr(MMBearerList) bearer_list = NULL;
    g_autoptr(MMBaseBearer) bearer = NULL;

    g_object_get (self, MM_IFACE_MODEM_BEARER_LIST, &bearer_list, NULL);
    if (bearer_list) {
        if (g_strcmp0 (index_field, "profile-id") == 0)
            bearer = mm_bearer_list_find_by_profile_id (bearer_list, profile_id);
        else if (g_strcmp0 (index_field, "apn-type") == 0)
            bearer = mm_bearer_list_find_by_apn_type (bearer_list, apn_type);
        else
            g_assert_not_reached ();
    }

    /* If a bearer is found reporting the profile id we're targeting to use,
     * it means we have a known connected bearer, and we must abort the
     * operation right away. */
    if (bearer) {
        if (g_strcmp0 (index_field, "profile-id") == 0) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_CONNECTED,
                         "Cannot use profile %d: found an already connected bearer", profile_id);
        } else if (g_strcmp0 (index_field, "apn-type") == 0) {
            g_autofree gchar *apn_type_str = NULL;

            apn_type_str = mm_bearer_apn_type_build_string_from_mask (apn_type);
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_CONNECTED,
                         "Cannot use profile %s: found an already connected bearer", apn_type_str);
        } else
            g_assert_not_reached ();
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
/* Set profile (3GPP profile management interface) */

typedef enum {
    SET_PROFILE_STEP_FIRST,
    SET_PROFILE_STEP_CHECK_FORMAT,
    SET_PROFILE_STEP_LIST_BEFORE,
    SET_PROFILE_STEP_SELECT_PROFILE,
    SET_PROFILE_STEP_CHECK_ACTIVATED_PROFILE,
    SET_PROFILE_STEP_DEACTIVATE_PROFILE,
    SET_PROFILE_STEP_STORE_PROFILE,
    SET_PROFILE_STEP_LIST_AFTER,
    SET_PROFILE_STEP_LAST,
} SetProfileStep;

typedef struct {
    SetProfileStep         step;
    MM3gppProfile         *requested;
    gchar                 *index_field;
    gchar                 *index_field_value_str;
    gboolean               strict;
    gboolean               new_id;
    gint                   min_profile_id;
    gint                   max_profile_id;
    GEqualFunc             profile_apn_cmp;
    MM3gppProfileCmpFlags  profile_cmp_flags;
    gint                   profile_id;
    MMBearerApnType        apn_type;
    gchar                 *apn_type_str;
    GList                 *before_list;
    MM3gppProfile         *stored;
} SetProfileContext;

static void
set_profile_context_free (SetProfileContext *ctx)
{
    mm_3gpp_profile_list_free (ctx->before_list);
    g_clear_object (&ctx->requested);
    g_clear_object (&ctx->stored);
    g_free (ctx->apn_type_str);
    g_free (ctx->index_field);
    g_free (ctx->index_field_value_str);
    g_slice_free (SetProfileContext, ctx);
}

MM3gppProfile *
mm_iface_modem_3gpp_profile_manager_set_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                        GAsyncResult                    *res,
                                                        GError                         **error)
{
    return MM_3GPP_PROFILE (g_task_propagate_pointer (G_TASK (res), error));
}

static void set_profile_step (GTask *task);

static void
profile_manager_get_profile_after_ready (MMIfaceModem3gppProfileManager *self,
                                         GAsyncResult                   *res,
                                         GTask                          *task)
{
    SetProfileContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->stored = mm_iface_modem_3gpp_profile_manager_get_profile_finish (self, res, &error);
    if (!ctx->stored) {
        g_prefix_error (&error, "Couldn't validate update of profile '%d': ", ctx->profile_id);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_list_after (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_iface_modem_3gpp_profile_manager_get_profile (
        self,
        ctx->profile_id,
        (GAsyncReadyCallback)profile_manager_get_profile_after_ready,
        task);
}

static void
profile_manager_store_profile_ready (MMIfaceModem3gppProfileManager *self,
                                     GAsyncResult                   *res,
                                     GTask                          *task)
{
    SetProfileContext *ctx;
    GError            *error = NULL;
    gint               profile_id;
    MMBearerApnType    apn_type;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->store_profile_finish (self, res, &profile_id, &apn_type, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* when creating a new profile with an unbound input profile id, store the
     * one received after store */
    if (g_strcmp0 (ctx->index_field, "profile-id") == 0) {
        if (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
            ctx->profile_id = profile_id;
            g_free (ctx->index_field_value_str);
            ctx->index_field_value_str = g_strdup_printf ("%d", ctx->profile_id);
        }
        g_assert (ctx->profile_id == profile_id);
    }

    mm_obj_dbg (self, "stored profile '%s'", ctx->index_field_value_str);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_store_profile (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (!ctx->stored);

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->store_profile (
        self,
        ctx->requested,
        ctx->index_field,
        (GAsyncReadyCallback) profile_manager_store_profile_ready,
        task);
}

static void
profile_manager_deactivate_profile_ready (MMIfaceModem3gppProfileManager *self,
                                          GAsyncResult                   *res,
                                          GTask                          *task)
{
    SetProfileContext *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    /* profile deactivation errors aren't fatal per se */
    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->deactivate_profile_finish (self, res, &error))
        mm_obj_dbg (self, "couldn't deactivate profile '%s': %s", ctx->index_field_value_str, error->message);
    else
        mm_obj_dbg (self, "deactivated profile '%s'", ctx->index_field_value_str);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_deactivate_profile (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->deactivate_profile ||
        !MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->deactivate_profile_finish) {
        mm_obj_dbg (self, "skipping profile deactivation");
        ctx->step++;
        set_profile_step (task);
        return;
    }

    /* This profile deactivation is EXCLUSIVELY done for those profiles that
     * are connected in the modem but for which we don't have any connected
     * bearer tracked. This covers e.g. a clean recovery of a previous daemon
     * crash, and is now defined as a supported step in the core logic, instead
     * of doing it differently in the different plugins and protocols. */
    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->deactivate_profile (
        self,
        ctx->requested,
        (GAsyncReadyCallback) profile_manager_deactivate_profile_ready,
        task);
}

static void
profile_manager_check_activated_profile_ready (MMIfaceModem3gppProfileManager *self,
                                               GAsyncResult                   *res,
                                               GTask                          *task)
{
    SetProfileContext *ctx;
    gboolean           activated = TRUE;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_activated_profile_finish (self, res, &activated, &error)) {
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND)) {
            mm_obj_dbg (self, "profile '%s' is not activated: %s", ctx->index_field_value_str, error->message);
            ctx->step = SET_PROFILE_STEP_STORE_PROFILE;
        } else {
            mm_obj_dbg (self, "couldn't check if profile '%s' is activated: %s", ctx->index_field_value_str, error->message);
            ctx->step = SET_PROFILE_STEP_DEACTIVATE_PROFILE;
        }
    } else if (activated) {
        mm_obj_dbg (self, "profile '%s' is activated", ctx->index_field_value_str);
        ctx->step = SET_PROFILE_STEP_DEACTIVATE_PROFILE;
    } else {
        mm_obj_dbg (self, "profile '%s' is not activated", ctx->index_field_value_str);
        ctx->step = SET_PROFILE_STEP_STORE_PROFILE;
    }
    set_profile_step (task);
}

static void
set_profile_step_check_activated_profile (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;
    GError                         *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (((g_strcmp0 (ctx->index_field, "profile-id") == 0) && (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)) ||
              ((g_strcmp0 (ctx->index_field, "apn-type") == 0) && (ctx->apn_type != MM_BEARER_APN_TYPE_NONE)));

    /* First, a quick check on our own bearer list. If we have a known bearer
     * connected using the same profile id, we fail the operation right away. */
    if (!profile_manager_fail_if_connected_bearer (self,
                                                   ctx->index_field,
                                                   ctx->profile_id,
                                                   ctx->apn_type,
                                                   &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_activated_profile ||
        !MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_activated_profile_finish) {
        ctx->step = SET_PROFILE_STEP_DEACTIVATE_PROFILE;
        set_profile_step (task);
        return;
    }

    /* Second, an actual query to the modem, in order to trigger the profile
     * deactivation before we attempt to activate it again */
    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_activated_profile (
        self,
        ctx->requested,
        (GAsyncReadyCallback) profile_manager_check_activated_profile_ready,
        task);
}

static void
set_profile_step_select_profile_exact (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;
    GError                         *error = NULL;
    g_autoptr(MM3gppProfile)        existing = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Look for the exact profile we want to use */
    if (g_strcmp0 (ctx->index_field, "profile-id") == 0) {
        g_assert (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN);
        existing = mm_3gpp_profile_list_find_by_profile_id (ctx->before_list,
                                                            ctx->profile_id,
                                                            &error);
    } else if (g_strcmp0 (ctx->index_field, "apn-type") == 0) {
        g_assert (ctx->apn_type != MM_BEARER_APN_TYPE_NONE);
        existing = mm_3gpp_profile_list_find_by_apn_type (ctx->before_list,
                                                          ctx->apn_type,
                                                          &error);
    } else
        g_assert_not_reached ();
    if (!existing) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If the profile is 100% equal to what we require, nothing to do */
    if (mm_3gpp_profile_cmp (existing, ctx->requested, ctx->profile_apn_cmp, ctx->profile_cmp_flags)) {
        mm_obj_dbg (self, "reusing profile '%s'", ctx->index_field_value_str);
        ctx->stored = g_object_ref (existing);
    } else
        mm_obj_dbg (self, "overwritting profile '%s'", ctx->index_field_value_str);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_select_profile_new (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;
    GError                         *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (g_strcmp0 (ctx->index_field, "profile-id") == 0);
    g_assert (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN);
    g_assert (ctx->strict);

    /* If strict set required, fail if we cannot find an empty profile id */
    ctx->profile_id = mm_3gpp_profile_list_find_empty (ctx->before_list,
                                                       ctx->min_profile_id,
                                                       ctx->max_profile_id,
                                                       &error);
    if (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* store profile id in the requested profile */
    mm_3gpp_profile_set_profile_id (ctx->requested, ctx->profile_id);

    mm_obj_dbg (self, "creating profile '%d'", ctx->profile_id);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_select_profile_best (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;
    gboolean                        overwritten = FALSE;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (g_strcmp0 (ctx->index_field, "profile-id") == 0);
    g_assert (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN);
    g_assert (!ctx->strict);

    ctx->profile_id = mm_3gpp_profile_list_find_best (ctx->before_list,
                                                      ctx->requested,
                                                      ctx->profile_apn_cmp,
                                                      ctx->profile_cmp_flags,
                                                      ctx->min_profile_id,
                                                      ctx->max_profile_id,
                                                      self,
                                                      &ctx->stored,
                                                      &overwritten);

    /* store profile id in the requested profile */
    mm_3gpp_profile_set_profile_id (ctx->requested, ctx->profile_id);

    /* If we're reusing an already existing profile, we're done at this
     * point, no need to create a new one */
    if (ctx->stored)
        mm_obj_dbg (self, "reusing profile '%d'", ctx->profile_id);
    else if (overwritten)
        mm_obj_dbg (self, "overwriting profile '%d'", ctx->profile_id);

    ctx->step++;
    set_profile_step (task);
}

static void
profile_manager_list_profiles_before_ready (MMIfaceModem3gppProfileManager *self,
                                            GAsyncResult                   *res,
                                            GTask                          *task)
{
    SetProfileContext *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_3gpp_profile_manager_list_profiles_finish (self, res, &ctx->before_list, &error))
        mm_obj_dbg (self, "failed checking currently defined contexts: %s", error->message);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_list_before (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;

    self = g_task_get_source_object (task);
    mm_iface_modem_3gpp_profile_manager_list_profiles (
        self,
        (GAsyncReadyCallback)profile_manager_list_profiles_before_ready,
        task);
}

static void
set_profile_check_format_ready (MMIfaceModem3gppProfileManager *self,
                                GAsyncResult                   *res,
                                GTask                          *task)
{
    SetProfileContext *ctx;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_format_finish (
            self, res,
            &ctx->new_id,
            &ctx->min_profile_id,
            &ctx->max_profile_id,
            &ctx->profile_apn_cmp,
            &ctx->profile_cmp_flags,
            NULL)) {
        ctx->min_profile_id = 1;
        ctx->max_profile_id = G_MAXINT-1;
        mm_obj_dbg (self, "unknown context definition format; using defaults: minimum %d, maximum %d",
                    ctx->min_profile_id, ctx->max_profile_id);
    } else
        mm_obj_dbg (self, "context definition format: minimum %d, maximum %d",
                    ctx->min_profile_id, ctx->max_profile_id);

    ctx->step++;
    set_profile_step (task);
}

static void
set_profile_step_check_format (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_format (
        self,
        mm_3gpp_profile_get_ip_type (ctx->requested),
        (GAsyncReadyCallback)set_profile_check_format_ready,
        task);
}

static void
set_profile_step (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    SetProfileContext              *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case SET_PROFILE_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case SET_PROFILE_STEP_CHECK_FORMAT:
        mm_obj_dbg (self, "set profile state (%d/%d): check format",
                    ctx->step, SET_PROFILE_STEP_LAST);
        set_profile_step_check_format (task);
        return;

    case SET_PROFILE_STEP_LIST_BEFORE:
        mm_obj_dbg (self, "set profile state (%d/%d): list before",
                    ctx->step, SET_PROFILE_STEP_LAST);
        set_profile_step_list_before (task);
        return;

    case SET_PROFILE_STEP_SELECT_PROFILE:
        if (((g_strcmp0 (ctx->index_field, "profile-id") == 0) && (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)) ||
            (g_strcmp0 (ctx->index_field, "apn-type") == 0)) {
            mm_obj_dbg (self, "set profile state (%d/%d): select profile (exact)",
                        ctx->step, SET_PROFILE_STEP_LAST);
            set_profile_step_select_profile_exact (task);
            return;
        }
        /* when using profile-id, allow non-strict and new */
        if (!ctx->strict) {
            mm_obj_dbg (self, "set profile state (%d/%d): select profile (best)",
                        ctx->step, SET_PROFILE_STEP_LAST);
            set_profile_step_select_profile_best (task);
            return;
        }
        if (ctx->new_id) {
            mm_obj_dbg (self, "set profile state (%d/%d): select profile (new)",
                        ctx->step, SET_PROFILE_STEP_LAST);
            set_profile_step_select_profile_new (task);
            return;
        }

        mm_obj_dbg (self, "set profile state (%d/%d): select profile (none)",
                    ctx->step, SET_PROFILE_STEP_LAST);
        ctx->step++;
        /* Fall through */

    case SET_PROFILE_STEP_CHECK_ACTIVATED_PROFILE:
        /* If the modem/protocol doesn't allow preselecting the profile id of
         * a new profile we're going to create, then we won't have a profile id
         * set at this point. If so, just skip this step. */
        if (((g_strcmp0 (ctx->index_field, "profile-id") == 0) && (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)) ||
            (g_strcmp0 (ctx->index_field, "apn-type") == 0)) {
            mm_obj_dbg (self, "set profile state (%d/%d): check activated profile",
                        ctx->step, SET_PROFILE_STEP_LAST);
            set_profile_step_check_activated_profile (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case SET_PROFILE_STEP_DEACTIVATE_PROFILE:
        /* If the modem/protocol doesn't allow preselecting the profile id of
         * a new profile we're going to create, then we won't have a profile id
         * set at this point. If so, just skip this step. */
        if (((g_strcmp0 (ctx->index_field, "profile-id") == 0) && (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)) ||
            (g_strcmp0 (ctx->index_field, "apn-type") == 0)) {
            mm_obj_dbg (self, "set profile state (%d/%d): deactivate profile",
                        ctx->step, SET_PROFILE_STEP_LAST);
            set_profile_step_deactivate_profile (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case SET_PROFILE_STEP_STORE_PROFILE:
        /* if we're reusing an already existing profile, we can jump
         * to the last step now, there is no need to store any update */
        if (ctx->stored) {
            mm_obj_dbg (self, "set profile state (%d/%d): profile already stored",
                        ctx->step, SET_PROFILE_STEP_LAST);
            ctx->step = SET_PROFILE_STEP_LAST;
            set_profile_step (task);
            return;
        }
        mm_obj_dbg (self, "set profile state (%d/%d): store profile",
                    ctx->step, SET_PROFILE_STEP_LAST);
        set_profile_step_store_profile (task);
        return;

    case SET_PROFILE_STEP_LIST_AFTER:
        mm_obj_dbg (self, "set profile state (%d/%d): list after",
                    ctx->step, SET_PROFILE_STEP_LAST);
        set_profile_step_list_after (task);
        return;

    case SET_PROFILE_STEP_LAST:
        mm_obj_dbg (self, "set profile state (%d/%d): all done",
                    ctx->step, SET_PROFILE_STEP_LAST);
        g_assert (ctx->stored);
        g_task_return_pointer (task, g_steal_pointer (&ctx->stored), g_object_unref);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_iface_modem_3gpp_profile_manager_set_profile (MMIfaceModem3gppProfileManager *self,
                                                 MM3gppProfile                  *requested,
                                                 const gchar                    *index_field,
                                                 gboolean                        strict,
                                                 GAsyncReadyCallback             callback,
                                                 gpointer                        user_data)
{
    GError                  *error;
    GTask                   *task;
    SetProfileContext       *ctx;
    MMBearerIpFamily         ip_family;
    g_autoptr(GVariant)      dict = NULL;
    g_autoptr(MM3gppProfile) requested_copy = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* The MM3gppProfile passed to the SetProfileContext is going to
     * be modified, so we make a copy to preserve the original one. */
    dict = mm_3gpp_profile_get_dictionary (requested);
    requested_copy = mm_3gpp_profile_new_from_dictionary (dict, &error);
    if (!requested_copy) {
        g_prefix_error (&error, "Couldn't copy 3GPP profile:");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetProfileContext);
    ctx->step = SET_PROFILE_STEP_FIRST;
    ctx->requested = g_object_ref (requested_copy);
    ctx->index_field = g_strdup (index_field);
    ctx->strict = strict;
    ctx->profile_id = mm_3gpp_profile_get_profile_id (ctx->requested);
    ctx->apn_type = mm_3gpp_profile_get_apn_type (ctx->requested);
    ctx->apn_type_str = mm_bearer_apn_type_build_string_from_mask (ctx->apn_type);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_profile_context_free);

    /* Validate input setup:
     * - allow 'profile-id' as index field, both strict and not strict.
     * - allow 'apn-type' as index field, always strict.
     */
    if (g_strcmp0 (ctx->index_field, "profile-id") == 0)
        ctx->index_field_value_str = g_strdup_printf ("%d", ctx->profile_id);
    else if (g_strcmp0 (ctx->index_field, "apn-type") == 0) {
        g_assert (ctx->strict);
        ctx->index_field_value_str = g_strdup (ctx->apn_type_str);
        /* when using apn-type as index, the field is mandatory because both "create"
         * and "update" are actually the same operation. */
        if (ctx->apn_type == MM_BEARER_APN_TYPE_NONE) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                     "Missing index field ('apn-type') in profile settings");
            g_object_unref (task);
            return;
        }
    } else
        g_assert_not_reached ();

    /* normalize IP family right away */
    ip_family = mm_3gpp_profile_get_ip_type (ctx->requested);
    mm_3gpp_normalize_ip_family (&ip_family);
    mm_3gpp_profile_set_ip_type (ctx->requested, ip_family);

    set_profile_step (task);
}

/*****************************************************************************/

MM3gppProfile *
mm_iface_modem_3gpp_profile_manager_get_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                        GAsyncResult                    *res,
                                                        GError                         **error)
{
    return MM_3GPP_PROFILE (g_task_propagate_pointer (G_TASK (res), error));
}

static void
get_profile_list_ready (MMIfaceModem3gppProfileManager *self,
                        GAsyncResult                   *res,
                        GTask                          *task)
{
    GError        *error = NULL;
    GList         *profiles = NULL;
    gint           profile_id;
    MM3gppProfile *profile;

    profile_id = GPOINTER_TO_INT (g_task_get_task_data (task));

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->list_profiles_finish (self, res, &profiles, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    profile = mm_3gpp_profile_list_find_by_profile_id (profiles, profile_id, &error);
    mm_3gpp_profile_list_free (profiles);

    if (!profile) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, profile, g_object_unref);
    g_object_unref (task);
}

static void
get_profile_single_ready (MMIfaceModem3gppProfileManager *self,
                          GAsyncResult                   *res,
                          GTask                          *task)
{
    GError        *error = NULL;
    MM3gppProfile *profile;

    profile = MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->get_profile_finish (self, res, &error);
    if (!profile)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, profile, g_object_unref);
    g_object_unref (task);
}

void
mm_iface_modem_3gpp_profile_manager_get_profile (MMIfaceModem3gppProfileManager *self,
                                                 gint                            profile_id,
                                                 GAsyncReadyCallback             callback,
                                                 gpointer                        user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->get_profile &&
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->get_profile_finish) {
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->get_profile (self,
                                                                               profile_id,
                                                                               (GAsyncReadyCallback)get_profile_single_ready,
                                                                               task);
        return;
    }

    /* If there is no way to query one single profile, query all and filter */
    g_task_set_task_data (task, GINT_TO_POINTER (profile_id), NULL);

    mm_iface_modem_3gpp_profile_manager_list_profiles (self,
                                                       (GAsyncReadyCallback)get_profile_list_ready,
                                                       task);
}

/*****************************************************************************/

typedef struct {
    GList *profiles;
} ListProfilesContext;

static void
list_profiles_context_free (ListProfilesContext *ctx)
{
    mm_3gpp_profile_list_free (ctx->profiles);
    g_slice_free (ListProfilesContext, ctx);
}

gboolean
mm_iface_modem_3gpp_profile_manager_list_profiles_finish (MMIfaceModem3gppProfileManager  *self,
                                                          GAsyncResult                    *res,
                                                          GList                          **out_profiles,
                                                          GError                         **error)
{
    ListProfilesContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (out_profiles)
        *out_profiles = g_steal_pointer (&ctx->profiles);
    return TRUE;
}

static void
internal_list_profiles_ready (MMIfaceModem3gppProfileManager *self,
                              GAsyncResult                   *res,
                              GTask                          *task)
{
    ListProfilesContext *ctx;
    GError              *error = NULL;

    ctx = g_slice_new0 (ListProfilesContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) list_profiles_context_free);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->list_profiles_finish (self, res, &ctx->profiles, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_3gpp_profile_manager_list_profiles (MMIfaceModem3gppProfileManager *self,
                                                   GAsyncReadyCallback             callback,
                                                   gpointer                        user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Internal calls to the list profile logic may be performed even if the 3GPP Profile Manager
     * interface is not exposed in DBus, therefore, make sure this logic exits cleanly if there
     * is no support for listing profiles */
    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->list_profiles ||
        !MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->list_profiles_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Listing profiles is unsupported");
        g_object_unref (task);
        return;
    }

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->list_profiles (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        (GAsyncReadyCallback)internal_list_profiles_ready,
        task);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppProfileManager *skeleton;
    GDBusMethodInvocation          *invocation;
    MMIfaceModem3gppProfileManager *self;
} HandleListContext;

static void
handle_list_context_free (HandleListContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleListContext, ctx);
}

static GVariant *
build_list_profiles_result (MMIfaceModem3gppProfileManager *self,
                            GList                          *profiles)
{
    GVariantBuilder  builder;
    GList           *l;
    guint            i;

    /* Build array of dicts */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));
    for (l = profiles, i = 0; l; l = g_list_next (l), i++) {
        g_autoptr(GVariant)  dict = NULL;
        MM3gppProfile       *profile;

        profile = MM_3GPP_PROFILE (l->data);

        mm_obj_info (self, "profile %u:", i);
        mm_log_3gpp_profile (self, MM_LOG_LEVEL_INFO, "  ", profile);

        dict = mm_3gpp_profile_get_dictionary (profile);
        g_variant_builder_add_value (&builder, dict);
    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static void
list_profiles_ready (MMIfaceModem3gppProfileManager *self,
                     GAsyncResult                   *res,
                     HandleListContext              *ctx)
{
    GError              *error = NULL;
    GList               *profiles = NULL;
    g_autoptr(GVariant)  dict_array = NULL;

    if (!mm_iface_modem_3gpp_profile_manager_list_profiles_finish (self, res, &profiles, &error)) {
        mm_obj_warn (self, "failed listing 3GPP profiles: %s", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_list_context_free (ctx);
        return;
    }

    mm_obj_info (self, "listed 3GPP profiles: %u found", g_list_length (profiles));
    dict_array = build_list_profiles_result (self, profiles);
    mm_gdbus_modem3gpp_profile_manager_complete_list (ctx->skeleton, ctx->invocation, dict_array);
    mm_3gpp_profile_list_free (profiles);
    handle_list_context_free (ctx);
}

static void
handle_list_auth_ready (MMBaseModem       *self,
                        GAsyncResult      *res,
                        HandleListContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_list_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_list_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to list 3GPP profiles...");

    /* Don't call the class callback directly, use the common helper method
     * that is also used by other internal operations. */
    mm_iface_modem_3gpp_profile_manager_list_profiles (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        (GAsyncReadyCallback)list_profiles_ready,
        ctx);
}

static gboolean
handle_list (MmGdbusModem3gppProfileManager *skeleton,
             GDBusMethodInvocation          *invocation,
             MMIfaceModem3gppProfileManager *self)
{
    HandleListContext *ctx;

    ctx = g_slice_new0 (HandleListContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_list_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppProfileManager *skeleton;
    GDBusMethodInvocation          *invocation;
    GVariant                       *requested_dictionary;
    MMIfaceModem3gppProfileManager *self;
} HandleSetContext;

static void
handle_set_context_free (HandleSetContext *ctx)
{
    g_clear_pointer (&ctx->requested_dictionary, g_variant_unref);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetContext, ctx);
}

static void
set_profile_ready (MMIfaceModem3gppProfileManager *self,
                   GAsyncResult                   *res,
                   HandleSetContext               *ctx)
{
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile_stored = NULL;
    g_autoptr(GVariant)       profile_dictionary = NULL;

    profile_stored = mm_iface_modem_3gpp_profile_manager_set_profile_finish (self, res, &error);
    if (!profile_stored) {
        mm_obj_warn (self, "failed setting 3GPP profile: %s", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_context_free (ctx);
        return;
    }

    mm_obj_info (self, "3GPP profile set:");
    mm_log_3gpp_profile (self, MM_LOG_LEVEL_INFO, "  ", profile_stored);

    profile_dictionary = mm_3gpp_profile_get_dictionary (profile_stored);
    mm_gdbus_modem3gpp_profile_manager_complete_set (ctx->skeleton, ctx->invocation, profile_dictionary);
    handle_set_context_free (ctx);
}

static void
handle_set_auth_ready (MMBaseModem      *self,
                       GAsyncResult     *res,
                       HandleSetContext *ctx)
{
    const gchar              *index_field;
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile_requested = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_set_context_free (ctx);
        return;
    }

    if (!ctx->requested_dictionary) {
        g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                       "Missing requested profile settings");
        handle_set_context_free (ctx);
        return;
    }

    profile_requested = mm_3gpp_profile_new_from_dictionary (ctx->requested_dictionary, &error);
    if (!profile_requested) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set 3GPP profile...");
    mm_log_3gpp_profile (self, MM_LOG_LEVEL_INFO, "  ", profile_requested);

    index_field = mm_gdbus_modem3gpp_profile_manager_get_index_field (ctx->skeleton);

    /* Don't call the class callback directly, use the common helper method
     * that is also used by other internal operations. */
    mm_iface_modem_3gpp_profile_manager_set_profile (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        profile_requested,
        index_field,
        TRUE, /* strict always! */
        (GAsyncReadyCallback)set_profile_ready,
        ctx);
}

static gboolean
handle_set (MmGdbusModem3gppProfileManager *skeleton,
            GDBusMethodInvocation          *invocation,
            GVariant                       *requested_dictionary,
            MMIfaceModem3gppProfileManager *self)
{
    HandleSetContext *ctx;

    ctx = g_slice_new0 (HandleSetContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->requested_dictionary = requested_dictionary ? g_variant_ref (requested_dictionary) : NULL;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppProfileManager *skeleton;
    GDBusMethodInvocation          *invocation;
    GVariant                       *dictionary;
    MMIfaceModem3gppProfileManager *self;
} HandleDeleteContext;

static void
handle_delete_context_free (HandleDeleteContext *ctx)
{
    g_clear_pointer (&ctx->dictionary, g_variant_unref);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleDeleteContext, ctx);
}

static void
delete_profile_ready (MMIfaceModem3gppProfileManager *self,
                      GAsyncResult                   *res,
                      HandleDeleteContext            *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->delete_profile_finish (self, res, &error)) {
        mm_obj_warn (self, "failed deleting 3GPP profile: %s", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (self, "3GPP profile deleted");
        mm_gdbus_modem3gpp_profile_manager_complete_delete (ctx->skeleton, ctx->invocation);
    }
    handle_delete_context_free (ctx);
}

static void
handle_delete_auth_ready (MMBaseModem         *self,
                          GAsyncResult        *res,
                          HandleDeleteContext *ctx)
{
    const gchar              *index_field;
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;
    gint                      profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;
    MMBearerApnType           apn_type = MM_BEARER_APN_TYPE_NONE;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_delete_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->delete_profile ||
        !MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->delete_profile_finish) {
        g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                       "Deleting profiles is not supported");
        handle_delete_context_free (ctx);
        return;
    }

    if (!ctx->dictionary) {
        g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                       "Missing profile settings");
        handle_delete_context_free (ctx);
        return;
    }

    profile = mm_3gpp_profile_new_from_dictionary (ctx->dictionary, &error);
    if (!profile) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    index_field = mm_gdbus_modem3gpp_profile_manager_get_index_field (ctx->skeleton);
    if (g_strcmp0 (index_field, "profile-id") == 0) {
        profile_id = mm_3gpp_profile_get_profile_id (profile);
        if (profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
            g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                           "Missing index field ('profile-id') in profile settings");
            handle_delete_context_free (ctx);
            return;
        }
    } else if (g_strcmp0 (index_field, "apn-type") == 0) {
        apn_type = mm_3gpp_profile_get_apn_type (profile);
        if (apn_type == MM_BEARER_APN_TYPE_NONE) {
            g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                           "Missing index field ('apn-type') in profile settings");
            handle_delete_context_free (ctx);
            return;
        }
    } else
        g_assert_not_reached ();

    if (!profile_manager_fail_if_connected_bearer (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
                                                   index_field,
                                                   profile_id,
                                                   apn_type,
                                                   &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to delete 3GPP profile...");
    mm_log_3gpp_profile (self, MM_LOG_LEVEL_INFO, "  ", profile);

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->delete_profile (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        profile,
        index_field,
        (GAsyncReadyCallback)delete_profile_ready,
        ctx);
}

static gboolean
handle_delete (MmGdbusModem3gppProfileManager *skeleton,
               GDBusMethodInvocation          *invocation,
               GVariant                       *dictionary,
               MMIfaceModem3gppProfileManager *self)
{
    HandleDeleteContext *ctx;

    ctx = g_slice_new0 (HandleDeleteContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_delete_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep                   step;
    MmGdbusModem3gppProfileManager *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (DisablingContext, ctx);
}

gboolean
mm_iface_modem_3gpp_profile_manager_disable_finish (MMIfaceModem3gppProfileManager  *self,
                                                    GAsyncResult                    *res,
                                                    GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModem3gppProfileManager *self,
                                  GAsyncResult                   *res,
                                  GTask                          *task)
{
    DisablingContext  *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't disable unsolicited profile management events: %s", error->message);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModem3gppProfileManager *self,
                                  GAsyncResult                   *res,
                                  GTask                          *task)
{
    DisablingContext  *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't cleanup unsolicited profile management events: %s", error->message);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
interface_disabling_step (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    DisablingContext               *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_profile_manager_disable (MMIfaceModem3gppProfileManager *self,
                                             GAsyncReadyCallback             callback,
                                             gpointer                        user_data)
{
    DisablingContext *ctx;
    GTask            *task;

    ctx = g_slice_new0 (DisablingContext);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_disabling_step (task);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep                    step;
    MmGdbusModem3gppProfileManager *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (EnablingContext, ctx);
}

gboolean
mm_iface_modem_3gpp_profile_manager_enable_finish (MMIfaceModem3gppProfileManager  *self,
                                                   GAsyncResult                    *res,
                                                   GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gppProfileManager *self,
                                GAsyncResult                   *res,
                                GTask                          *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't setup unsolicited profile management events: %s", error->message);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gppProfileManager *self,
                                 GAsyncResult                   *res,
                                 GTask                          *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't enable unsolicited profile management events: %s", error->message);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    EnablingContext                *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_profile_manager_enable (MMIfaceModem3gppProfileManager *self,
                                            GAsyncReadyCallback             callback,
                                            gpointer                        user_data)
{
    EnablingContext *ctx;
    GTask           *task;

    ctx = g_slice_new0 (EnablingContext);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModem3gppProfileManager *skeleton;
    InitializationStep              step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (InitializationContext, ctx);
}

gboolean
mm_iface_modem_3gpp_profile_manager_initialize_finish (MMIfaceModem3gppProfileManager  *self,
                                                       GAsyncResult                    *res,
                                                       GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
check_support_ready (MMIfaceModem3gppProfileManager *self,
                     GAsyncResult                   *res,
                     GTask                          *task)
{
    InitializationContext *ctx;
    g_autoptr(GError)      error = NULL;
    g_autofree gchar      *index_field = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_support_finish (self, res, &index_field, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "profile management support check failed: %s", error->message);
        }
    } else {
        /* profile management is supported! */
        mm_gdbus_modem3gpp_profile_manager_set_index_field (ctx->skeleton, index_field);
        g_object_set_qdata (G_OBJECT (self), supported_quark, GUINT_TO_POINTER (TRUE));
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
profile_manager_list_profiles_check_ready (MMIfaceModem3gppProfileManager *self,
                                           GAsyncResult                   *res,
                                           GTask                          *task)
{
    InitializationContext *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_3gpp_profile_manager_list_profiles_finish (self, res, NULL, &error))
        mm_obj_dbg (self, "profile management support check failed: couldn't load profile list: %s", error->message);
    else {
        /* profile management is supported!
         * We are here because the modem type did not define the check_support functions,
         * but we need anyway to set index_field, so let's use "profile-id" as default */
        mm_gdbus_modem3gpp_profile_manager_set_index_field (ctx->skeleton, "profile-id");
        g_object_set_qdata (G_OBJECT (self), supported_quark, GUINT_TO_POINTER (TRUE));
    }

    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    InitializationContext          *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (SUPPORTED_TAG));
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self), support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (self), support_checked_quark, GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (self), supported_quark, GUINT_TO_POINTER (FALSE));
            if (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_GET_INTERFACE (self)->check_support (
                    self,
                    (GAsyncReadyCallback)check_support_ready,
                    task);
                return;
            }

            /* If there is no implementation to check support, try to query the list
             * explicitly; it may be the case that there is no other way to check for
             * support. */
            mm_iface_modem_3gpp_profile_manager_list_profiles (
                self,
                (GAsyncReadyCallback)profile_manager_list_profiles_check_ready,
                task);
            return;
        }

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self), supported_quark))) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                     "Profile management not supported");
            g_object_unref (task);
            return;
        }

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_object_connect (ctx->skeleton,
                          "signal::handle-list",   G_CALLBACK (handle_list),   self,
                          "signal::handle-set",    G_CALLBACK (handle_set),    self,
                          "signal::handle-delete", G_CALLBACK (handle_delete), self,
                          NULL);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp_profile_manager (MM_GDBUS_OBJECT_SKELETON (self),
                                                                MM_GDBUS_MODEM3GPP_PROFILE_MANAGER (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_profile_manager_initialize (MMIfaceModem3gppProfileManager *self,
                                                GAsyncReadyCallback             callback,
                                                gpointer                        user_data)
{
    InitializationContext          *ctx;
    MmGdbusModem3gppProfileManager *skeleton = NULL;
    GTask                          *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem3gpp_profile_manager_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_slice_new0 (InitializationContext);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_3gpp_profile_manager_shutdown (MMIfaceModem3gppProfileManager *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem3gpp_profile_manager (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_3gpp_profile_manager_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER_DBUS_SKELETON,
                              "3GPP Profile Manager DBus skeleton",
                              "DBus skeleton for the 3GPP Profile Manager interface",
                              MM_GDBUS_TYPE_MODEM3GPP_PROFILE_MANAGER_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_profile_manager_get_type (void)
{
    static GType iface_modem_3gpp_profile_manager_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_profile_manager_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gppProfileManager), /* class_size */
            iface_modem_3gpp_profile_manager_init,   /* base_init */
            NULL,                                    /* base_finalize */
        };

        iface_modem_3gpp_profile_manager_type = g_type_register_static (G_TYPE_INTERFACE,
                                                                        "MMIfaceModem3gppProfileManager",
                                                                        &info, 0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_profile_manager_type, MM_TYPE_IFACE_MODEM_3GPP);
    }

    return iface_modem_3gpp_profile_manager_type;
}
