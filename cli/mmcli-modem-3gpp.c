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
 * Copyright (C) 2011 - 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2012 - 2021 Google, Inc.
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
    MMManager    *manager;
    GCancellable *cancellable;
    MMObject     *object;
    MMModem3gpp  *modem_3gpp;
} Context;
static Context *ctx;

/* Options */
static gboolean  scan_flag;
static gboolean  register_home_flag;
static gchar    *register_in_operator_str;
static gchar    *set_eps_ue_mode_operation_str;
static gchar    *set_initial_eps_bearer_settings_str;
static gchar    *disable_facility_lock_str;
static gchar    *set_packet_service_state_str;
static gchar    *set_nr5g_registration_settings_str;
static gchar    *set_carrier_lock_str;

static GOptionEntry entries[] = {
    { "3gpp-scan", 0, 0, G_OPTION_ARG_NONE, &scan_flag,
      "Scan for available networks in a given modem.",
      NULL
    },
    { "3gpp-register-home", 0, 0, G_OPTION_ARG_NONE, &register_home_flag,
      "Request a given modem to register in its home network",
      NULL
    },
    { "3gpp-register-in-operator", 0, 0, G_OPTION_ARG_STRING, &register_in_operator_str,
      "Request a given modem to register in the network of the given operator",
      "[MCCMNC]"
    },
    { "3gpp-set-eps-ue-mode-operation", 0, 0, G_OPTION_ARG_STRING, &set_eps_ue_mode_operation_str,
      "Set the UE mode of operation for EPS",
      "[ps-1|ps-2|csps-1|csps-2]"
    },
    { "3gpp-set-initial-eps-bearer-settings", 0, 0, G_OPTION_ARG_STRING, &set_initial_eps_bearer_settings_str,
      "Set the initial EPS bearer settings",
      "[\"key=value,...\"]"
    },
    { "3gpp-disable-facility-lock", 0, 0, G_OPTION_ARG_STRING, &disable_facility_lock_str,
      "Disable facility personalization",
      "[facility,key]"
    },
    { "3gpp-set-packet-service-state", 0, 0, G_OPTION_ARG_STRING, &set_packet_service_state_str,
      "Set packet service state",
      "[attached|detached]"
    },
    { "3gpp-set-nr5g-registration-settings", 0, 0, G_OPTION_ARG_STRING, &set_nr5g_registration_settings_str,
      "Set 5GNR registration settings",
      "[\"key=value,...\"]"
    },
    { "3gpp-set-carrier-lock", 0, 0, G_OPTION_ARG_STRING, &set_carrier_lock_str,
      "Carrier Lock",
      "[(Data)]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_3gpp_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("3gpp",
                                "3GPP options:",
                                "Show 3GPP related options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_3gpp_options_enabled (void)
{
    static guint    n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (scan_flag +
                 register_home_flag +
                 !!register_in_operator_str +
                 !!set_eps_ue_mode_operation_str +
                 !!set_initial_eps_bearer_settings_str +
                 !!disable_facility_lock_str +
                 !!set_packet_service_state_str +
                 !!set_nr5g_registration_settings_str +
                 !!set_carrier_lock_str);

    if (n_actions > 1) {
        g_printerr ("error: too many 3GPP actions requested\n");
        exit (EXIT_FAILURE);
    }

    /* Scanning networks takes really a long time, so we do it asynchronously
     * always to avoid DBus timeouts */
    if (scan_flag)
        mmcli_force_async_operation ();

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
    if (ctx->modem_3gpp)
        g_object_unref (ctx->modem_3gpp);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_enabled (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

static void
ensure_modem_3gpp (void)
{
    if (!ctx->modem_3gpp) {
        g_printerr ("error: modem has no 3GPP capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_3gpp_shutdown (void)
{
    context_free ();
}

static void
scan_process_reply (GList        *result,
                    const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't scan networks in the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    mmcli_output_scan_networks (result);
    mmcli_output_dump ();

    g_list_free_full (result, (GDestroyNotify) mm_modem_3gpp_network_free);
}

static void
scan_ready (MMModem3gpp  *modem_3gpp,
            GAsyncResult *result)
{
    GList  *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_scan_finish (modem_3gpp, result, &error);
    scan_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
register_process_reply (gboolean      result,
                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't register the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully registered the modem\n");
}

static void
register_ready (MMModem3gpp  *modem_3gpp,
                GAsyncResult *result)
{
    gboolean  operation_result;
    GError   *error = NULL;

    operation_result = mm_modem_3gpp_register_finish (modem_3gpp, result, &error);
    register_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_initial_eps_bearer_settings_process_reply (gboolean      result,
                                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set initial EPS bearer properties: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully set initial EPS bearer properties\n");
}

static void
set_initial_eps_bearer_settings_ready (MMModem3gpp  *modem,
                                       GAsyncResult *res)
{
    gboolean  result;
    GError   *error = NULL;

    result = mm_modem_3gpp_set_initial_eps_bearer_settings_finish (modem, res, &error);
    set_initial_eps_bearer_settings_process_reply (result, error);

    mmcli_async_operation_done ();
}

static void
set_eps_ue_mode_operation_process_reply (gboolean      result,
                                         const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set UE mode of operation for EPS: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set UE mode of operation for EPS\n");
}

static void
set_eps_ue_mode_operation_ready (MMModem3gpp  *modem,
                                 GAsyncResult *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_set_eps_ue_mode_operation_finish (modem, result, &error);
    set_eps_ue_mode_operation_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_eps_ue_mode_operation (MMModem3gppEpsUeModeOperation *uemode)
{
    GError *error = NULL;

    *uemode = mm_common_get_eps_ue_mode_operation_from_string (set_eps_ue_mode_operation_str, &error);
    if (error) {
        g_printerr ("error: couldn't parse UE mode of operation for EPS: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}

static void
disable_facility_lock_process_reply (gboolean      result,
                                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable facility lock: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled facility lock\n");
}

static gboolean
disable_facility_lock_parse_input (const gchar          *str,
                                   MMModem3gppFacility  *out_facility,
                                   gchar               **out_control_key)
{
    g_auto(GStrv)       properties = NULL;
    MMModem3gppFacility facility;

    properties = g_strsplit (str, ",", -1);
    if (!properties || !properties[0] || !properties[1])
        return FALSE;

    /* Facilities is a bitmask, if 0 is returned we failed parsing */
    facility = mm_common_get_3gpp_facility_from_string (properties[0], NULL);
    if (!facility)
        return FALSE;

    *out_facility = facility;
    *out_control_key = g_strdup (properties[1]);
    return TRUE;
}

static void
disable_facility_lock_ready (MMModem3gpp  *modem_3gpp,
                             GAsyncResult *result,
                             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_disable_facility_lock_finish (modem_3gpp, result, &error);
    disable_facility_lock_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_packet_service_state_process_reply (gboolean      result,
                                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set packet service state: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set packet service state\n");
}

static void
set_packet_service_state_ready (MMModem3gpp  *modem_3gpp,
                                GAsyncResult *result,
                                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_set_packet_service_state_finish (modem_3gpp, result, &error);
    set_packet_service_state_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static gboolean
set_packet_service_state_parse_input (const gchar                   *str,
                                      MMModem3gppPacketServiceState *out_state)
{
    MMModem3gppPacketServiceState state;

    state = mm_common_get_3gpp_packet_service_state_from_string (str, NULL);
    if (state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN)
        return FALSE;

    *out_state = state;
    return TRUE;
}

static void
set_nr5g_registration_settings_process_reply (gboolean      result,
                                              const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set 5GNR registration settings: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set 5GNR registration settings\n");
}

static void
set_nr5g_registration_settings_ready (MMModem3gpp  *modem_3gpp,
                                      GAsyncResult *result,
                                      gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_set_nr5g_registration_settings_finish (modem_3gpp, result, &error);
    set_nr5g_registration_settings_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_carrier_lock_process_reply (gboolean     result,
                                const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send carrier lock information: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully sent carrier lock information to modem\n");
}

static void
set_carrier_lock_ready (MMModem3gpp  *modem_3gpp,
                        GAsyncResult *result)
{
    gboolean          operation_result;
    g_autoptr(GError) error = NULL;

    operation_result = mm_modem_3gpp_set_carrier_lock_finish (modem_3gpp, result, &error);
    set_carrier_lock_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));

    ensure_modem_3gpp ();

    /* Request to disable facility lock */
    if (disable_facility_lock_str) {
        g_autofree gchar    *control_key = NULL;
        MMModem3gppFacility  facility;

        if (!disable_facility_lock_parse_input (disable_facility_lock_str,
                                                &facility,
                                                &control_key)) {
            g_printerr ("Error parsing properties string.\n");
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously disabling facility lock...");
        mm_modem_3gpp_disable_facility_lock (ctx->modem_3gpp,
                                             facility,
                                             control_key,
                                             ctx->cancellable,
                                             (GAsyncReadyCallback)disable_facility_lock_ready,
                                             NULL);
        return;
    }

    /* Request to set carrier Lock */
    if (set_carrier_lock_str) {
        gsize              data_size  = 0;
        g_autofree guint8 *data = NULL;
        GError            *error = NULL;

        data = mm_utils_hexstr2bin (set_carrier_lock_str, -1, &data_size, &error);
        if (!data) {
            g_printerr ("Failed to read data from the input: %s\n", error->message);
            exit (EXIT_FAILURE);
            return;
        }

        mm_modem_3gpp_set_carrier_lock (ctx->modem_3gpp,
                                        data,
                                        (guint32)data_size,
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)set_carrier_lock_ready,
                                        NULL);
        return;
    }

    ensure_modem_enabled ();

    /* Request to scan networks? */
    if (scan_flag) {
        g_debug ("Asynchronously scanning for networks...");
        mm_modem_3gpp_scan (ctx->modem_3gpp,
                            ctx->cancellable,
                            (GAsyncReadyCallback)scan_ready,
                            NULL);
        return;
    }

    /* Request to register the modem? */
    if (register_in_operator_str || register_home_flag) {
        g_debug ("Asynchronously registering the modem...");
        mm_modem_3gpp_register (ctx->modem_3gpp,
                                (register_in_operator_str ? register_in_operator_str : ""),
                                ctx->cancellable,
                                (GAsyncReadyCallback)register_ready,
                                NULL);
        return;
    }

    /* Request to set UE mode of operation for EPS? */
    if (set_eps_ue_mode_operation_str) {
        MMModem3gppEpsUeModeOperation uemode;

        parse_eps_ue_mode_operation (&uemode);

        g_debug ("Asynchronously setting UE mode of operation for EPS...");
        mm_modem_3gpp_set_eps_ue_mode_operation (ctx->modem_3gpp,
                                                 uemode,
                                                 ctx->cancellable,
                                                 (GAsyncReadyCallback)set_eps_ue_mode_operation_ready,
                                                 NULL);
        return;
    }

    /* Request to set initial EPS bearer properties? */
    if (set_initial_eps_bearer_settings_str) {
        GError             *error = NULL;
        MMBearerProperties *config;

        config = mm_bearer_properties_new_from_string (set_initial_eps_bearer_settings_str, &error);
        if (!config) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting initial EPS bearer properties...");
        mm_modem_3gpp_set_initial_eps_bearer_settings (ctx->modem_3gpp,
                                                       config,
                                                       ctx->cancellable,
                                                       (GAsyncReadyCallback)set_initial_eps_bearer_settings_ready,
                                                       NULL);
        g_object_unref (config);
        return;

    }

    /* Request to set packet service state */
    if (set_packet_service_state_str) {
        MMModem3gppPacketServiceState  state;

        if (!set_packet_service_state_parse_input (set_packet_service_state_str, &state)) {
            g_printerr ("Error parsing packet service state string.\n");
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting packet service state...");
        mm_modem_3gpp_set_packet_service_state (ctx->modem_3gpp,
                                                state,
                                                ctx->cancellable,
                                                (GAsyncReadyCallback)set_packet_service_state_ready,
                                                NULL);
        return;
    }

    /* Request to set packet service state */
    if (set_nr5g_registration_settings_str) {
        g_autoptr(MMNr5gRegistrationSettings)  settings = NULL;
        GError                                *error = NULL;

        settings = mm_nr5g_registration_settings_new_from_string (set_nr5g_registration_settings_str, &error);
        if (!settings) {
            g_printerr ("Error parsing 5GNR registration settings string: %s\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting 5GNR registration settings...");
        mm_modem_3gpp_set_nr5g_registration_settings (ctx->modem_3gpp,
                                                      settings,
                                                      ctx->cancellable,
                                                      (GAsyncReadyCallback)set_nr5g_registration_settings_ready,
                                                      NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_3gpp_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_3gpp_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));

    ensure_modem_3gpp ();

    if (scan_flag)
        g_assert_not_reached ();

    /* Request to remove carrier lock */
    if (disable_facility_lock_str) {
        g_autofree gchar    *control_key = NULL;
        MMModem3gppFacility  facility;
        gboolean             result;

        if (!disable_facility_lock_parse_input (disable_facility_lock_str,
                                                &facility,
                                                &control_key)) {
            g_printerr ("Error parsing properties string.\n");
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously disabling facility lock...");
        result = mm_modem_3gpp_disable_facility_lock_sync (ctx->modem_3gpp,
                                                           facility,
                                                           control_key,
                                                           NULL,
                                                           &error);
        disable_facility_lock_process_reply (result, error);
        return;
    }

    /* Request to set carrier Lock */
    if (set_carrier_lock_str) {
        gsize              data_size  = 0;
        g_autofree guint8 *data = NULL;
        gboolean           result;

        data = mm_utils_hexstr2bin (set_carrier_lock_str, -1, &data_size, &error);
        if (!data) {
            g_printerr ("Failed to read data from the input: %s\n", error->message);
            exit (EXIT_FAILURE);
            return;
        }

        result = mm_modem_3gpp_set_carrier_lock_sync (ctx->modem_3gpp,
                                                      data,
                                                      (guint32)data_size,
                                                      NULL,
                                                      &error);
        set_carrier_lock_process_reply (result, error);
        return;
    }

    ensure_modem_enabled ();

    /* Request to register the modem? */
    if (register_in_operator_str || register_home_flag) {
        gboolean result;

        g_debug ("Synchronously registering the modem...");
        result = mm_modem_3gpp_register_sync (
            ctx->modem_3gpp,
            (register_in_operator_str ? register_in_operator_str : ""),
            NULL,
            &error);
        register_process_reply (result, error);
        return;
    }

    /* Request to set UE mode of operation for EPS? */
    if (set_eps_ue_mode_operation_str) {
        MMModem3gppEpsUeModeOperation uemode;
        gboolean                      result;

        parse_eps_ue_mode_operation (&uemode);

        g_debug ("Synchronously setting UE mode of operation for EPS...");
        result = mm_modem_3gpp_set_eps_ue_mode_operation_sync (ctx->modem_3gpp,
                                                               uemode,
                                                               NULL,
                                                               &error);
        set_eps_ue_mode_operation_process_reply (result, error);
        return;
    }

    /* Request to set initial EPS bearer properties? */
    if (set_initial_eps_bearer_settings_str) {
        gboolean            result;
        MMBearerProperties *config;

        config = mm_bearer_properties_new_from_string (set_initial_eps_bearer_settings_str, &error);
        if (!config) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously setting initial EPS bearer properties...");
        result = mm_modem_3gpp_set_initial_eps_bearer_settings_sync (ctx->modem_3gpp,
                                                                     config,
                                                                     NULL,
                                                                     &error);
        set_initial_eps_bearer_settings_process_reply (result, error);
        g_object_unref (config);
        return;
    }

    /* Request to set packet service state */
    if (set_packet_service_state_str) {
        gboolean                       result;
        MMModem3gppPacketServiceState  state;

        if (!set_packet_service_state_parse_input (set_packet_service_state_str, &state)) {
            g_printerr ("Error parsing packet service state string.\n");
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting packet service state...");
        result = mm_modem_3gpp_set_packet_service_state_sync (ctx->modem_3gpp,
                                                              state,
                                                              NULL,
                                                              &error);
        set_packet_service_state_process_reply (result, error);
        return;
    }

    if (set_nr5g_registration_settings_str) {
        g_autoptr(MMNr5gRegistrationSettings) settings = NULL;
        gboolean                              result;

        settings = mm_nr5g_registration_settings_new_from_string (set_nr5g_registration_settings_str, &error);
        if (!settings) {
            g_printerr ("Error parsing 5GNR registration settings string: %s\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting 5GNR registration settings...");
        result = mm_modem_3gpp_set_nr5g_registration_settings_sync (ctx->modem_3gpp, settings, NULL, &error);
        set_nr5g_registration_settings_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
