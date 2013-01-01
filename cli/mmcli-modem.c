/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModem *modem;
    MMModem3gpp *modem_3gpp;
    MMModemCdma *modem_cdma;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean monitor_state_flag;
static gboolean enable_flag;
static gboolean disable_flag;
static gboolean set_power_state_on_flag;
static gboolean set_power_state_low_flag;
static gboolean reset_flag;
static gchar *factory_reset_str;
static gchar *command_str;
static gboolean list_bearers_flag;
static gchar *create_bearer_str;
static gchar *delete_bearer_str;
static gchar *set_allowed_modes_str;
static gchar *set_preferred_mode_str;
static gchar *set_bands_str;

static GOptionEntry entries[] = {
    { "monitor-state", 'w', 0, G_OPTION_ARG_NONE, &monitor_state_flag,
      "Monitor state of a given modem",
      NULL
    },
    { "enable", 'e', 0, G_OPTION_ARG_NONE, &enable_flag,
      "Enable a given modem",
      NULL
    },
    { "disable", 'd', 0, G_OPTION_ARG_NONE, &disable_flag,
      "Disable a given modem",
      NULL
    },
    { "set-power-state-on", 0, 0, G_OPTION_ARG_NONE, &set_power_state_on_flag,
      "Set full power state in the modem",
      NULL
    },
    { "set-power-state-low", 0, 0, G_OPTION_ARG_NONE, &set_power_state_low_flag,
      "Set low power state in the modem",
      NULL
    },
    { "reset", 'r', 0, G_OPTION_ARG_NONE, &reset_flag,
      "Reset a given modem",
      NULL
    },
    { "factory-reset", 0, 0, G_OPTION_ARG_STRING, &factory_reset_str,
      "Reset a given modem to its factory state",
      "[CODE]"
    },
    { "command", 0, 0, G_OPTION_ARG_STRING, &command_str,
      "Send an AT command to the modem",
      "[COMMAND]"
    },
    { "list-bearers", 0, 0, G_OPTION_ARG_NONE, &list_bearers_flag,
      "List packet data bearers available in a given modem",
      NULL
    },
    { "create-bearer", 0, 0, G_OPTION_ARG_STRING, &create_bearer_str,
      "Create a new packet data bearer in a given modem",
      "[\"key=value,...\"]"
    },
    { "delete-bearer", 0, 0, G_OPTION_ARG_STRING, &delete_bearer_str,
      "Delete a data bearer from a given modem",
      "[PATH]"
    },
    { "set-allowed-modes", 0, 0, G_OPTION_ARG_STRING, &set_allowed_modes_str,
      "Set allowed modes in a given modem.",
      "[MODE1|MODE2...]"
    },
    { "set-bands", 0, 0, G_OPTION_ARG_STRING, &set_bands_str,
      "Set bands to be used by a given modem.",
      "[BAND1|BAND2...]"
    },
    { "set-preferred-mode", 0, 0, G_OPTION_ARG_STRING, &set_preferred_mode_str,
      "Set preferred mode in a given modem (Must give allowed modes with --set-allowed-modes)",
      "[MODE]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("modem",
	                            "Modem options",
	                            "Show modem options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_modem_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (monitor_state_flag +
                 enable_flag +
                 disable_flag +
                 set_power_state_on_flag +
                 set_power_state_low_flag +
                 reset_flag +
                 list_bearers_flag +
                 !!create_bearer_str +
                 !!delete_bearer_str +
                 !!factory_reset_str +
                 !!command_str +
                 !!set_allowed_modes_str +
                 !!set_preferred_mode_str +
                 !!set_bands_str);

    if (n_actions == 0 && mmcli_get_common_modem_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (set_preferred_mode_str) {
        if (!set_allowed_modes_str) {
            g_printerr ("error: setting preferred mode requires list of allowed modes\n");
            exit (EXIT_FAILURE);
        }
        n_actions--;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many modem actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (monitor_state_flag)
        mmcli_force_async_operation ();

    if (info_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem)
        g_object_unref (ctx->modem);
    if (ctx->modem_3gpp)
        g_object_unref (ctx->modem_3gpp);
    if (ctx->modem_cdma)
        g_object_unref (ctx->modem_cdma);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_modem_shutdown (void)
{
    context_free (ctx);
}

static void
cancelled (GCancellable *cancellable)
{
    mmcli_async_operation_done ();
}

static void
print_bearer_short_info (MMBearer *bearer)
{
    g_print ("\t%s\n",
             mm_bearer_get_path (bearer));
}

static void
print_modem_info (void)
{
    gchar *drivers_string;
    gchar *prefixed_revision;
    gchar *modem_capabilities_string;
    gchar *current_capabilities_string;
    gchar *access_technologies_string;
    gchar *supported_modes_string;
    gchar *allowed_modes_string;
    gchar *preferred_mode_string;
    gchar *supported_bands_string;
    gchar *bands_string;
    gchar *unlock_retries_string;
    gchar *own_numbers_string;
    MMModemBand *bands = NULL;
    MMUnlockRetries *unlock_retries;
    guint n_bands = 0;
    guint signal_quality = 0;
    gboolean signal_quality_recent = FALSE;

    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE_UNKNOWN
#define VALIDATE_UNKNOWN(str) (str ? str : "unknown")
#undef VALIDATE_PATH
#define VALIDATE_PATH(str) ((str && !g_str_equal (str, "/")) ? str : "none")

    /* Strings in heap */
    modem_capabilities_string = mm_modem_capability_build_string_from_mask (
        mm_modem_get_modem_capabilities (ctx->modem));
    current_capabilities_string = mm_modem_capability_build_string_from_mask (
        mm_modem_get_current_capabilities (ctx->modem));
    access_technologies_string = mm_modem_access_technology_build_string_from_mask (
        mm_modem_get_access_technologies (ctx->modem));
    mm_modem_get_bands (ctx->modem, &bands, &n_bands);
    bands_string = mm_common_build_bands_string (bands, n_bands);
    g_free (bands);
    mm_modem_get_supported_bands (ctx->modem, &bands, &n_bands);
    supported_bands_string = mm_common_build_bands_string (bands, n_bands);
    g_free (bands);
    allowed_modes_string = mm_modem_mode_build_string_from_mask (
        mm_modem_get_allowed_modes (ctx->modem));
    preferred_mode_string = mm_modem_mode_build_string_from_mask (
        mm_modem_get_preferred_mode (ctx->modem));
    supported_modes_string = mm_modem_mode_build_string_from_mask (
        mm_modem_get_supported_modes (ctx->modem));

    unlock_retries = mm_modem_get_unlock_retries (ctx->modem);
    unlock_retries_string = mm_unlock_retries_build_string (unlock_retries);
    g_object_unref (unlock_retries);

    if (mm_modem_get_own_numbers (ctx->modem)) {
        own_numbers_string = g_strjoinv (", ", (gchar **)mm_modem_get_own_numbers (ctx->modem));
        if (!own_numbers_string[0]) {
            g_free (own_numbers_string);
            own_numbers_string = NULL;
        }
    } else
        own_numbers_string = NULL;

    if (mm_modem_get_drivers (ctx->modem)) {
        drivers_string = g_strjoinv (", ", (gchar **)mm_modem_get_drivers (ctx->modem));
        if (!drivers_string[0]) {
            g_free (drivers_string);
            drivers_string = NULL;
        }
    } else
        drivers_string = NULL;

    /* Rework possible multiline strings */
    if (mm_modem_get_revision (ctx->modem))
        prefixed_revision = mmcli_prefix_newlines ("           |                  ",
                                                   mm_modem_get_revision (ctx->modem));
    else
        prefixed_revision = NULL;

    /* Get signal quality info */
    signal_quality = mm_modem_get_signal_quality (ctx->modem, &signal_quality_recent);

    /* Global IDs */
    g_print ("\n"
             "%s (device id '%s')\n",
             VALIDATE_UNKNOWN (mm_modem_get_path (ctx->modem)),
             VALIDATE_UNKNOWN (mm_modem_get_device_identifier (ctx->modem)));

    /* Hardware related stuff */
    g_print ("  -------------------------\n"
             "  Hardware |   manufacturer: '%s'\n"
             "           |          model: '%s'\n"
             "           |       revision: '%s'\n"
             "           |   capabilities: '%s'\n"
             "           |        current: '%s'\n"
             "           |   equipment id: '%s'\n",
             VALIDATE_UNKNOWN (mm_modem_get_manufacturer (ctx->modem)),
             VALIDATE_UNKNOWN (mm_modem_get_model (ctx->modem)),
             VALIDATE_UNKNOWN (prefixed_revision),
             VALIDATE_UNKNOWN (modem_capabilities_string),
             VALIDATE_UNKNOWN (current_capabilities_string),
             VALIDATE_UNKNOWN (mm_modem_get_equipment_identifier (ctx->modem)));

    /* System related stuff */
    g_print ("  -------------------------\n"
             "  System   |         device: '%s'\n"
             "           |        drivers: '%s'\n"
             "           |         plugin: '%s'\n"
             "           |   primary port: '%s'\n",
             VALIDATE_UNKNOWN (mm_modem_get_device (ctx->modem)),
             VALIDATE_UNKNOWN (drivers_string),
             VALIDATE_UNKNOWN (mm_modem_get_plugin (ctx->modem)),
             VALIDATE_UNKNOWN (mm_modem_get_primary_port (ctx->modem)));

    /* Numbers related stuff */
    g_print ("  -------------------------\n"
             "  Numbers  |           own : '%s'\n",
             VALIDATE_UNKNOWN (own_numbers_string));

    /* Status related stuff */
    g_print ("  -------------------------\n"
             "  Status   |           lock: '%s'\n"
             "           | unlock retries: '%s'\n"
             "           |          state: '%s'\n"
             "           |    power state: '%s'\n"
             "           |    access tech: '%s'\n"
             "           | signal quality: '%u' (%s)\n",
             mm_modem_lock_get_string (mm_modem_get_unlock_required (ctx->modem)),
             VALIDATE_UNKNOWN (unlock_retries_string),
             VALIDATE_UNKNOWN (mm_modem_state_get_string (mm_modem_get_state (ctx->modem))),
             VALIDATE_UNKNOWN (mm_modem_power_state_get_string (mm_modem_get_power_state (ctx->modem))),
             VALIDATE_UNKNOWN (access_technologies_string),
             signal_quality, signal_quality_recent ? "recent" : "cached");

    /* Modes */
    g_print ("  -------------------------\n"
             "  Modes    |      supported: '%s'\n"
             "           |        allowed: '%s'\n"
             "           |      preferred: '%s'\n",
             VALIDATE_UNKNOWN (supported_modes_string),
             VALIDATE_UNKNOWN (allowed_modes_string),
             VALIDATE_UNKNOWN (preferred_mode_string));

    /* Band related stuff */
    g_print ("  -------------------------\n"
             "  Bands    |      supported: '%s'\n"
             "           |        current: '%s'\n",
             VALIDATE_UNKNOWN (supported_bands_string),
             VALIDATE_UNKNOWN (bands_string));

    /* If available, 3GPP related stuff */
    if (ctx->modem_3gpp) {
        gchar *facility_locks;

        facility_locks = (mm_modem_3gpp_facility_build_string_from_mask (
                              mm_modem_3gpp_get_enabled_facility_locks (ctx->modem_3gpp)));
        g_print ("  -------------------------\n"
                 "  3GPP     |           imei: '%s'\n"
                 "           |  enabled locks: '%s'\n"
                 "           |    operator id: '%s'\n"
                 "           |  operator name: '%s'\n"
                 "           |   registration: '%s'\n",
                 VALIDATE_UNKNOWN (mm_modem_3gpp_get_imei (ctx->modem_3gpp)),
                 facility_locks,
                 VALIDATE_UNKNOWN (mm_modem_3gpp_get_operator_code (ctx->modem_3gpp)),
                 VALIDATE_UNKNOWN (mm_modem_3gpp_get_operator_name (ctx->modem_3gpp)),
                 mm_modem_3gpp_registration_state_get_string (
                     mm_modem_3gpp_get_registration_state ((ctx->modem_3gpp))));

        g_free (facility_locks);
    }

    /* If available, CDMA related stuff */
    if (ctx->modem_cdma) {
        guint sid;
        guint nid;
        gchar *sid_str;
        gchar *nid_str;

        sid = mm_modem_cdma_get_sid (ctx->modem_cdma);
        sid_str = (sid != MM_MODEM_CDMA_SID_UNKNOWN ?
                   g_strdup_printf ("%u", sid) :
                   NULL);
        nid = mm_modem_cdma_get_nid (ctx->modem_cdma);
        nid_str = (nid != MM_MODEM_CDMA_NID_UNKNOWN ?
                   g_strdup_printf ("%u", nid) :
                   NULL);

        g_print ("  -------------------------\n"
                 "  CDMA     |           meid: '%s'\n"
                 "           |            esn: '%s'\n"
                 "           |            sid: '%s'\n"
                 "           |            nid: '%s'\n"
                 "           |   registration: CDMA1x '%s'\n"
                 "           |                 EV-DO  '%s'\n",
                 VALIDATE_UNKNOWN (mm_modem_cdma_get_meid (ctx->modem_cdma)),
                 VALIDATE_UNKNOWN (mm_modem_cdma_get_esn (ctx->modem_cdma)),
                 VALIDATE_UNKNOWN (sid_str),
                 VALIDATE_UNKNOWN (nid_str),
                 mm_modem_cdma_registration_state_get_string (
                     mm_modem_cdma_get_cdma1x_registration_state ((ctx->modem_cdma))),
                 mm_modem_cdma_registration_state_get_string (
                     mm_modem_cdma_get_evdo_registration_state ((ctx->modem_cdma))));

        g_free (sid_str);
        g_free (nid_str);
    }

    /* SIM */
    g_print ("  -------------------------\n"
             "  SIM      |           path: '%s'\n",
             VALIDATE_PATH (mm_modem_get_sim_path (ctx->modem)));
    g_print ("\n");

    g_free (bands_string);
    g_free (supported_bands_string);
    g_free (access_technologies_string);
    g_free (modem_capabilities_string);
    g_free (current_capabilities_string);
    g_free (prefixed_revision);
    g_free (allowed_modes_string);
    g_free (preferred_mode_string);
    g_free (supported_modes_string);
    g_free (unlock_retries_string);
    g_free (own_numbers_string);
    g_free (drivers_string);
}

static void
enable_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled the modem\n");
}

static void
enable_ready (MMModem      *modem,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_enable_finish (modem, result, &error);
    enable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled the modem\n");
}

static void
disable_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_disable_finish (modem, result, &error);
    disable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_power_state_process_reply (gboolean      result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set new power state in the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set new power state in the modem\n");
}

static void
set_power_state_ready (MMModem      *modem,
                       GAsyncResult *result,
                       gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_power_state_finish (modem, result, &error);
    set_power_state_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
reset_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem\n");
}

static void
reset_ready (MMModem      *modem,
             GAsyncResult *result,
             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_reset_finish (modem, result, &error);
    reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
factory_reset_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem to factory state: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem to factory state\n");
}

static void
factory_reset_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_factory_reset_finish (modem, result, &error);
    factory_reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}


static void
command_process_reply (gchar  *result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: command failed: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("response: '%s'\n", result);
    g_free (result);
}

static void
command_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gchar * operation_result;
    GError *error = NULL;

    operation_result = mm_modem_command_finish (modem, result, &error);
    command_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static guint
command_get_timeout (MMModem *modem)
{
    gint timeout;

    /* If --timeout was given, it should already have been set in the proxy */
    timeout = (g_dbus_proxy_get_default_timeout (G_DBUS_PROXY (modem)) / 1000) - 1;
    if (timeout <= 0) {
        g_printerr ("error: timeout is too short (%d)\n", timeout);
        exit (EXIT_FAILURE);
    }

    return (guint)timeout;
}

static void
list_bearers_process_reply (GList        *result,
                            const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't list bearers: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("\n");
    if (!result) {
        g_print ("No bearers were found\n");
    } else {
        GList *l;

        g_print ("Found %u bearers:\n", g_list_length (result));
        for (l = result; l; l = g_list_next (l)) {
            MMBearer *bearer = MM_BEARER (l->data);

            g_print ("\n");
            print_bearer_short_info (bearer);
            g_object_unref (bearer);
        }
        g_list_free (result);
    }
}

static void
list_bearers_ready (MMModem      *modem,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_list_bearers_finish (modem, result, &error);
    list_bearers_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
create_bearer_process_reply (MMBearer     *bearer,
                             const GError *error)
{
    if (!bearer) {
        g_printerr ("error: couldn't create new bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully created new bearer in modem:\n");
    print_bearer_short_info (bearer);
    g_object_unref (bearer);
}

static void
create_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = mm_modem_create_bearer_finish (modem, result, &error);
    create_bearer_process_reply (bearer, error);

    mmcli_async_operation_done ();
}

static void
delete_bearer_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted bearer from modem\n");
}

static void
delete_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_delete_bearer_finish (modem, result, &error);
    delete_bearer_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_allowed_modes_process_reply (gboolean      result,
                                 const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set allowed modes: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set allowed modes in the modem\n");
}

static void
set_allowed_modes_ready (MMModem      *modem,
                         GAsyncResult *result,
                         gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_allowed_modes_finish (modem, result, &error);
    set_allowed_modes_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_modes (MMModemMode *allowed,
             MMModemMode *preferred)
{
    GError *error = NULL;

    *allowed = mm_common_get_modes_from_string (set_allowed_modes_str, &error);
    if (error) {
        g_printerr ("error: couldn't parse list of allowed modes: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    *preferred = (set_preferred_mode_str ?
                  mm_common_get_modes_from_string (set_preferred_mode_str, &error) :
                  MM_MODEM_MODE_NONE);
    if (error) {
        g_printerr ("error: couldn't parse preferred mode: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}

static void
set_bands_process_reply (gboolean      result,
                         const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set bands: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set bands in the modem\n");
}

static void
set_bands_ready (MMModem      *modem,
                 GAsyncResult *result,
                 gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_set_bands_finish (modem, result, &error);
    set_bands_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_bands (MMModemBand **bands,
             guint *n_bands)
{
    GError *error = NULL;

    mm_common_get_bands_from_string (set_bands_str,
                                     bands,
                                     n_bands,
                                     &error);
    if (error) {
        g_printerr ("error: couldn't parse list of bands: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }
}


static void
state_changed (MMModem                  *modem,
               MMModemState              old_state,
               MMModemState              new_state,
               MMModemStateChangeReason  reason)
{
    g_print ("\t%s: State changed, '%s' --> '%s' (Reason: %s)\n",
             mm_modem_get_path (modem),
             mm_modem_state_get_string (old_state),
             mm_modem_state_get_string (new_state),
             mmcli_get_state_reason_string (reason));
    fflush (stdout);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem = mm_object_get_modem (ctx->object);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem));
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    if (info_flag)
        g_assert_not_reached ();

    /* Request to monitor modems? */
    if (monitor_state_flag) {
        MMModemState current;

        g_signal_connect (ctx->modem,
                          "state-changed",
                          G_CALLBACK (state_changed),
                          NULL);

        current = mm_modem_get_state (ctx->modem);
        g_print ("\t%s: Initial state, '%s'\n",
                 mm_object_get_path (ctx->object),
                 mm_modem_state_get_string (current));

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        g_debug ("Asynchronously enabling modem...");
        mm_modem_enable (ctx->modem,
                         ctx->cancellable,
                         (GAsyncReadyCallback)enable_ready,
                         NULL);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        g_debug ("Asynchronously disabling modem...");
        mm_modem_disable (ctx->modem,
                          ctx->cancellable,
                          (GAsyncReadyCallback)disable_ready,
                          NULL);
        return;
    }

    /* Request to full power the modem? */
    if (set_power_state_on_flag) {
        g_debug ("Asynchronously setting full power...");
        mm_modem_set_power_state (ctx->modem,
                                  MM_MODEM_POWER_STATE_ON,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)set_power_state_ready,
                                  NULL);
        return;
    }

    /* Request to low power the modem? */
    if (set_power_state_low_flag) {
        g_debug ("Asynchronously setting low power...");
        mm_modem_set_power_state (ctx->modem,
                                  MM_MODEM_POWER_STATE_LOW,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)set_power_state_ready,
                                  NULL);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        g_debug ("Asynchronously reseting modem...");
        mm_modem_reset (ctx->modem,
                        ctx->cancellable,
                        (GAsyncReadyCallback)reset_ready,
                        NULL);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        g_debug ("Asynchronously factory-reseting modem...");
        mm_modem_factory_reset (ctx->modem,
                                factory_reset_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)factory_reset_ready,
                                NULL);
        return;
    }

    /* Request to send a command to the modem? */
    if (command_str) {
        guint timeout;

        timeout = command_get_timeout (ctx->modem);

        g_debug ("Asynchronously sending a command to the modem (%u seconds timeout)...",
                 timeout);

        mm_modem_command (ctx->modem,
                          command_str,
                          timeout,
                          ctx->cancellable,
                          (GAsyncReadyCallback)command_ready,
                          NULL);
        return;
    }

    /* Request to list bearers? */
    if (list_bearers_flag) {
        g_debug ("Asynchronously listing bearers in modem...");
        mm_modem_list_bearers (ctx->modem,
                               ctx->cancellable,
                               (GAsyncReadyCallback)list_bearers_ready,
                               NULL);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        GError *error = NULL;
        MMBearerProperties *properties;

        properties = mm_bearer_properties_new_from_string (create_bearer_str, &error);
        if (!properties) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously creating new bearer in modem...");
        mm_modem_create_bearer (ctx->modem,
                                properties,
                                ctx->cancellable,
                                (GAsyncReadyCallback)create_bearer_ready,
                                NULL);
        g_object_unref (properties);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        mm_modem_delete_bearer (ctx->modem,
                                delete_bearer_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)delete_bearer_ready,
                                NULL);
        return;
    }

    /* Request to set allowed modes in a given modem? */
    if (set_allowed_modes_str) {
        MMModemMode allowed;
        MMModemMode preferred;

        parse_modes (&allowed, &preferred);
        mm_modem_set_allowed_modes (ctx->modem,
                                    allowed,
                                    preferred,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)set_allowed_modes_ready,
                                    NULL);
        return;
    }

    /* Request to set allowed bands in a given modem? */
    if (set_bands_str) {
        MMModemBand *bands;
        guint n_bands;

        parse_bands (&bands, &n_bands);
        mm_modem_set_bands (ctx->modem,
                            bands,
                            n_bands,
                            ctx->cancellable,
                            (GAsyncReadyCallback)set_bands_ready,
                            NULL);
        g_free (bands);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (monitor_state_flag)
        g_assert_not_reached ();

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem = mm_object_get_modem (ctx->object);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem));
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    /* Request to get info from modem? */
    if (info_flag) {
        g_debug ("Printing modem info...");
        print_modem_info ();
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        gboolean result;

        g_debug ("Synchronously enabling modem...");
        result = mm_modem_enable_sync (ctx->modem, NULL, &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        gboolean result;

        g_debug ("Synchronously disabling modem...");
        result = mm_modem_disable_sync (ctx->modem, NULL, &error);
        disable_process_reply (result, error);
        return;
    }

    /* Request to set full power state? */
    if (set_power_state_on_flag) {
        gboolean result;

        g_debug ("Synchronously setting full power...");
        result = mm_modem_set_power_state_sync (ctx->modem, MM_MODEM_POWER_STATE_ON, NULL, &error);
        set_power_state_process_reply (result, error);
        return;
    }

    /* Request to set low power state? */
    if (set_power_state_low_flag) {
        gboolean result;

        g_debug ("Synchronously setting low power...");
        result = mm_modem_set_power_state_sync (ctx->modem, MM_MODEM_POWER_STATE_LOW, NULL, &error);
        set_power_state_process_reply (result, error);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        gboolean result;

        g_debug ("Synchronously reseting modem...");
        result = mm_modem_reset_sync (ctx->modem, NULL, &error);
        reset_process_reply (result, error);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        gboolean result;

        g_debug ("Synchronously factory-reseting modem...");
        result = mm_modem_factory_reset_sync (ctx->modem,
                                              factory_reset_str,
                                              NULL,
                                              &error);
        factory_reset_process_reply (result, error);
        return;
    }


    /* Request to send a command to the modem? */
    if (command_str) {
        gchar *result;
        guint timeout;

        timeout = command_get_timeout (ctx->modem);

        g_debug ("Synchronously sending command to modem (%u seconds timeout)...",
                 timeout);

        result = mm_modem_command_sync (ctx->modem,
                                        command_str,
                                        timeout,
                                        NULL,
                                        &error);
        command_process_reply (result, error);
        return;
    }

    /* Request to list the bearers? */
    if (list_bearers_flag) {
        GList *result;

        g_debug ("Synchronously listing bearers...");
        result = mm_modem_list_bearers_sync (ctx->modem, NULL, &error);
        list_bearers_process_reply (result, error);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        MMBearer *bearer;
        GError *error = NULL;
        MMBearerProperties *properties;

        properties = mm_bearer_properties_new_from_string (create_bearer_str, &error);
        if (!properties) {
            g_printerr ("Error parsing properties string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously creating new bearer in modem...");
        bearer = mm_modem_create_bearer_sync (ctx->modem,
                                              properties,
                                              NULL,
                                              &error);
        g_object_unref (properties);

        create_bearer_process_reply (bearer, error);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        gboolean result;

        result = mm_modem_delete_bearer_sync (ctx->modem,
                                              delete_bearer_str,
                                              NULL,
                                              &error);

        delete_bearer_process_reply (result, error);
        return;
    }

    /* Request to set allowed modes in a given modem? */
    if (set_allowed_modes_str) {
        MMModemMode allowed;
        MMModemMode preferred;
        gboolean result;

        parse_modes (&allowed, &preferred);
        result = mm_modem_set_allowed_modes_sync (ctx->modem,
                                                  allowed,
                                                  preferred,
                                                  NULL,
                                                  &error);

        set_allowed_modes_process_reply (result, error);
        return;
    }

    /* Request to set allowed bands in a given modem? */
    if (set_bands_str) {
        gboolean result;
        MMModemBand *bands;
        guint n_bands;

        parse_bands (&bands, &n_bands);
        result = mm_modem_set_bands_sync (ctx->modem,
                                          bands,
                                          n_bands,
                                          NULL,
                                          &error);
        g_free (bands);
        set_bands_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
