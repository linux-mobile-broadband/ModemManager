/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Google, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"
#include "mmcli-output.h"

/* Context */
typedef struct {
    MMManager                 *manager;
    GCancellable              *cancellable;
    MMObject                  *object;
    MMModem3gppProfileManager *modem_3gpp_profile_manager;
} Context;
static Context *ctx;

/* Options */
static gboolean  status_flag;
static gboolean  list_flag;
static gchar    *set_str;
static gchar    *delete_str;

static GOptionEntry entries[] = {
    { "3gpp-profile-manager-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of the profile management features.",
      NULL
    },
    { "3gpp-profile-manager-list", 0, 0, G_OPTION_ARG_NONE, &list_flag,
      "List available profiles",
      NULL
    },
    { "3gpp-profile-manager-set", 0, 0, G_OPTION_ARG_STRING, &set_str,
      "Create or update (if unique key given) a profile with the given settings.",
      "[\"key=value,...\"]"
    },
    { "3gpp-profile-manager-delete", 0, 0, G_OPTION_ARG_STRING, &delete_str,
      "Delete the profile with the given unique key.",
      "[\"key=value,...\"]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_3gpp_profile_manager_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("3gpp-profile-manager",
                                "3GPP profile management options:",
                                "Show 3GPP profile management related options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_3gpp_profile_manager_options_enabled (void)
{
    static guint    n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 list_flag +
                 !!set_str +
                 !!delete_str);

    if (n_actions > 1) {
        g_printerr ("error: too many 3GPP profile management actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (status_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (void)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem_3gpp_profile_manager)
        g_object_unref (ctx->modem_3gpp_profile_manager);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_3gpp_profile_manager (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_3gpp_profile_manager) {
        g_printerr ("error: modem has no 3GPP profile management capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_3gpp_profile_manager_shutdown (void)
{
    context_free ();
}

static void
print_status (void)
{
    const gchar *index_field;

    index_field = mm_modem_3gpp_profile_manager_get_index_field (ctx->modem_3gpp_profile_manager);
    mmcli_output_string (MMC_F_3GPP_PROFILE_MANAGER_INDEX_FIELD, index_field);
    mmcli_output_dump ();
}

static void
delete_process_reply (gboolean      result,
                      const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't delete profile: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted the profile\n");
}

static void
delete_ready (MMModem3gppProfileManager *modem_3gpp_profile_manager,
              GAsyncResult              *result)
{
    gboolean  operation_result;
    GError   *error = NULL;

    operation_result = mm_modem_3gpp_profile_manager_delete_finish (modem_3gpp_profile_manager, result, &error);
    delete_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static MM3gppProfile *
delete_build_input (const gchar  *str,
                    GError      **error)
{
    /* Legacy command format, expecting just an integer */
    if (!strchr (delete_str, '=')) {
        MM3gppProfile *profile;
        guint          delete_int;

        if (!mm_get_uint_from_str (delete_str, &delete_int)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "Failed parsing string as integer");
            return NULL;
        }

        profile = mm_3gpp_profile_new ();
        mm_3gpp_profile_set_profile_id (profile, delete_int);
        return profile;
    }

    /* New command format, expecting a unique key value */
    return mm_3gpp_profile_new_from_string (delete_str, error);
}

static void
set_process_reply (MM3gppProfile *stored,
                   const GError  *error)
{
    if (error) {
        g_printerr ("error: couldn't set profile: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    mmcli_output_profile_set (stored);
    mmcli_output_dump ();

    g_object_unref (stored);
}

static void
set_ready (MMModem3gppProfileManager *modem_3gpp_profile_manager,
           GAsyncResult              *result)
{
    MM3gppProfile *stored;
    GError        *error = NULL;

    stored = mm_modem_3gpp_profile_manager_set_finish (modem_3gpp_profile_manager, result, &error);
    set_process_reply (stored, error);

    mmcli_async_operation_done ();
}

static void
list_process_reply (GList        *result,
                    const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't list profiles: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    mmcli_output_profile_list (result);
    mmcli_output_dump ();

    g_list_free_full (result, g_object_unref);
}

static void
list_ready (MMModem3gppProfileManager *modem_3gpp_profile_manager,
            GAsyncResult              *result)
{
    GError *error = NULL;
    GList  *profiles = NULL;

    mm_modem_3gpp_profile_manager_list_finish (modem_3gpp_profile_manager, result, &profiles, &error);
    list_process_reply (profiles, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_3gpp_profile_manager = mm_object_get_modem_3gpp_profile_manager (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp_profile_manager)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp_profile_manager));

    ensure_modem_3gpp_profile_manager ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to list? */
    if (list_flag) {
        g_debug ("Asynchronously listing profiles...");
        mm_modem_3gpp_profile_manager_list (ctx->modem_3gpp_profile_manager,
                                            ctx->cancellable,
                                            (GAsyncReadyCallback)list_ready,
                                            NULL);
        return;
    }

    /* Request to set? */
    if (set_str) {
        GError                   *error = NULL;
        g_autoptr(MM3gppProfile)  requested = NULL;

        g_debug ("Asynchronously setting profiles...");
        requested = mm_3gpp_profile_new_from_string (set_str, &error);
        if (!requested) {
            g_printerr ("Error parsing profile string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        mm_modem_3gpp_profile_manager_set (ctx->modem_3gpp_profile_manager,
                                           requested,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)set_ready,
                                           NULL);
        return;
    }

    /* Request to delete? */
    if (delete_str) {
        g_autoptr(MM3gppProfile) profile = NULL;
        g_autoptr(GError)        error = NULL;

        g_debug ("Asynchronously deleting profile...");

        profile = delete_build_input (delete_str, &error);
        if (!profile) {
            g_printerr ("Error parsing profile string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        mm_modem_3gpp_profile_manager_delete (ctx->modem_3gpp_profile_manager,
                                              profile,
                                              ctx->cancellable,
                                              (GAsyncReadyCallback)delete_ready,
                                              NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_3gpp_profile_manager_run_asynchronous (GDBusConnection *connection,
                                                   GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper modem */
    mmcli_get_modem  (connection,
                      mmcli_get_common_modem_string (),
                      cancellable,
                      (GAsyncReadyCallback)get_modem_ready,
                      NULL);
}

void
mmcli_modem_3gpp_profile_manager_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_3gpp_profile_manager = mm_object_get_modem_3gpp_profile_manager (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp_profile_manager)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp_profile_manager));

    ensure_modem_3gpp_profile_manager ();

    /* Request to get location status? */
    if (status_flag) {
        g_debug ("Printing profile management status...");
        print_status ();
        return;
    }

    /* Request to list? */
    if (list_flag) {
        GList *profiles;

        g_debug ("Synchronously listing profiles...");
        mm_modem_3gpp_profile_manager_list_sync (ctx->modem_3gpp_profile_manager,
                                                 ctx->cancellable,
                                                 &profiles,
                                                 &error);
        list_process_reply (profiles, error);
        return;
    }

    /* Request to set? */
    if (set_str) {
        g_autoptr(MM3gppProfile)  requested = NULL;
        MM3gppProfile            *stored;

        g_debug ("Synchronously setting profile...");
        requested = mm_3gpp_profile_new_from_string (set_str, &error);
        if (!requested) {
            g_printerr ("Error parsing profile string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        stored = mm_modem_3gpp_profile_manager_set_sync (ctx->modem_3gpp_profile_manager,
                                                         requested,
                                                         ctx->cancellable,
                                                         &error);
        set_process_reply (stored, error);
        return;
    }

    /* Request to delete? */
    if (delete_str) {
        gboolean                 result;
        g_autoptr(MM3gppProfile) profile = NULL;

        g_debug ("Synchronously deleting profile...");

        profile = delete_build_input (delete_str, &error);
        if (!profile) {
            g_printerr ("Error parsing profile string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        result = mm_modem_3gpp_profile_manager_delete_sync (ctx->modem_3gpp_profile_manager,
                                                            profile,
                                                            ctx->cancellable,
                                                            &error);
        delete_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
