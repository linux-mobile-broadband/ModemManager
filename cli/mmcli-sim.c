/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control sim status & access information from the command line
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
 * Copyright (C) 2011-2018 Aleksander Morgado <aleksander@aleksander.es>
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
    MMManager *manager;
    MMObject *object;
    GCancellable *cancellable;
    MMSim *sim;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gchar *pin_str;
static gchar *puk_str;
static gboolean enable_pin_flag;
static gboolean disable_pin_flag;
static gchar *change_pin_str;
static gchar *set_preferred_networks_str;

static GOptionEntry entries[] = {
    { "pin", 0, 0, G_OPTION_ARG_STRING, &pin_str,
      "Send PIN code to a given SIM.",
      "[PIN]"
    },
    { "puk", 0, 0, G_OPTION_ARG_STRING, &puk_str,
      "Send PUK code to a given SIM (must send the new PIN with --pin).",
      "[PUK]"
    },
    { "enable-pin", 0, 0, G_OPTION_ARG_NONE, &enable_pin_flag,
      "Enable PIN request in a given SIM (must send the current PIN with --pin).",
      NULL
    },
    { "disable-pin", 0, 0, G_OPTION_ARG_NONE, &disable_pin_flag,
      "Disable PIN request in a given SIM (must send the current PIN with --pin).",
      NULL
    },
    { "change-pin", 0, 0, G_OPTION_ARG_STRING, &change_pin_str,
      "Change the PIN in a given SIM (must send the current PIN with --pin).",
      "[New PIN]"
    },
    { "sim-set-preferred-networks", 0, 0, G_OPTION_ARG_STRING, &set_preferred_networks_str,
      "Set preferred network list stored in a given SIM.",
      "[[MCCMNC[,access_tech]],...]"
    },
    { NULL }
};

GOptionGroup *
mmcli_sim_get_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("sim",
                                "SIM options:",
                                "Show SIM options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_sim_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (!!puk_str +
                 enable_pin_flag +
                 disable_pin_flag +
                 !!change_pin_str +
                 !!set_preferred_networks_str);

    if (n_actions == 1) {
        if (!pin_str && !set_preferred_networks_str) {
            g_printerr ("error: action requires also the PIN code\n");
            exit (EXIT_FAILURE);
        }
    } else if (n_actions == 0)
        n_actions += !!pin_str;

    if (n_actions == 0 && mmcli_get_common_sim_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many sim actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (info_flag)
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
    if (ctx->sim)
        g_object_unref (ctx->sim);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_sim_shutdown (void)
{
    context_free ();
}

static void
print_sim_info (MMSim *sim)
{
    GList         *preferred_nets_list;
    const guint8  *gid1bin;
    gsize          gid1bin_size;
    const guint8  *gid2bin;
    gsize          gid2bin_size;

    mmcli_output_string       (MMC_F_SIM_GENERAL_DBUS_PATH,            mm_sim_get_path (sim));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_ACTIVE,            mm_sim_get_active (sim) ? "yes" : "no");
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_IMSI,              mm_sim_get_imsi (sim));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_ICCID,             mm_sim_get_identifier (sim));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_EID,               mm_sim_get_eid (sim));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_OPERATOR_ID,       mm_sim_get_operator_identifier (sim));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_OPERATOR_NAME,     mm_sim_get_operator_name (sim));
    mmcli_output_string_array (MMC_F_SIM_PROPERTIES_EMERGENCY_NUMBERS, (const gchar **) mm_sim_get_emergency_numbers (sim), FALSE);

    preferred_nets_list = mm_sim_get_preferred_networks (sim);
    mmcli_output_preferred_networks (preferred_nets_list);
    g_list_free_full (preferred_nets_list, (GDestroyNotify) mm_sim_preferred_network_free);

    gid1bin = mm_sim_get_gid1 (sim, &gid1bin_size);
    gid2bin = mm_sim_get_gid2 (sim, &gid2bin_size);
    mmcli_output_string_take  (MMC_F_SIM_PROPERTIES_GID1,              gid1bin ? mm_utils_bin2hexstr (gid1bin, gid1bin_size) : NULL);
    mmcli_output_string_take  (MMC_F_SIM_PROPERTIES_GID2,              gid2bin ? mm_utils_bin2hexstr (gid2bin, gid2bin_size) : NULL);

    mmcli_output_string       (MMC_F_SIM_PROPERTIES_SIM_TYPE,          mm_sim_type_get_string (mm_sim_get_sim_type (sim)));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_ESIM_STATUS,       mm_sim_esim_status_get_string (mm_sim_get_esim_status (sim)));
    mmcli_output_string       (MMC_F_SIM_PROPERTIES_REMOVABILITY,      mm_sim_removability_get_string (mm_sim_get_removability (sim)));
    mmcli_output_dump ();
}

static void
send_pin_process_reply (gboolean      result,
                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send PIN code to the SIM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully sent PIN code to the SIM\n");
}

static void
send_pin_ready (MMSim        *sim,
                GAsyncResult *result,
                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_send_pin_finish (sim, result, &error);
    send_pin_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
send_puk_process_reply (gboolean      result,
                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send PUK code to the SIM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully sent PUK code to the SIM\n");
}

static void
send_puk_ready (MMSim        *sim,
                GAsyncResult *result,
                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_send_puk_finish (sim, result, &error);
    send_puk_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
enable_pin_process_reply (gboolean      result,
                          const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable PIN code request in the SIM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled PIN code request in the SIM\n");
}

static void
enable_pin_ready (MMSim        *sim,
                  GAsyncResult *result,
                  gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_enable_pin_finish (sim, result, &error);
    enable_pin_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_pin_process_reply (gboolean      result,
                           const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable PIN code request in the SIM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled PIN code request in the SIM\n");
}

static void
disable_pin_ready (MMSim        *sim,
                   GAsyncResult *result,
                   gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_disable_pin_finish (sim, result, &error);
    disable_pin_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
change_pin_process_reply (gboolean      result,
                          const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't change PIN code in the SIM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully changed PIN code in the SIM\n");
}

static void
change_pin_ready (MMSim        *sim,
                  GAsyncResult *result,
                  gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_change_pin_finish (sim, result, &error);
    change_pin_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
parse_preferred_networks (GList **preferred_networks)
{
    gchar **parts;
    GList  *preferred_nets_list = NULL;
    GError *error = NULL;

    parts = g_strsplit (set_preferred_networks_str, ",", -1);
    if (parts) {
        guint i;

        for (i = 0; parts[i]; i++) {
            MMModemAccessTechnology access_tech = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
            MMSimPreferredNetwork *preferred_net;
            const gchar *mccmnc;

            mccmnc = parts[i];
            if (!mm_is_string_mccmnc (mccmnc)) {
                g_printerr ("error: couldn't parse MCCMNC for preferred network: '%s'\n",
                            mccmnc);
                exit (EXIT_FAILURE);
            }
            /* if the next item is MCCMNC or is missing, omit the access technology */
            if (parts[i + 1] && !mm_is_string_mccmnc (parts[i + 1])) {
                i++;
                access_tech = mm_common_get_access_technology_from_string (parts[i], &error);
                if (error) {
                    g_printerr ("error: %s\n", error->message);
                    g_error_free (error);
                    exit (EXIT_FAILURE);
                }
            }

            preferred_net = mm_sim_preferred_network_new ();
            mm_sim_preferred_network_set_operator_code (preferred_net, mccmnc);
            mm_sim_preferred_network_set_access_technology (preferred_net, access_tech);
            preferred_nets_list = g_list_append (preferred_nets_list, preferred_net);
        }
    }
    g_strfreev (parts);

    *preferred_networks = preferred_nets_list;
}

static void
set_preferred_networks_process_reply (gboolean      result,
                                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set preferred networks: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set preferred networks\n");
}

static void
set_preferred_networks_ready (MMSim        *sim,
                              GAsyncResult *result,
                              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sim_set_preferred_networks_finish (sim, result, &error);
    set_preferred_networks_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_sim_ready (GObject      *source,
               GAsyncResult *result,
               gpointer      none)
{
    ctx->sim = mmcli_get_sim_finish (result,
                                     &ctx->manager,
                                     &ctx->object);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->sim));

    if (info_flag)
        g_assert_not_reached ();

    /* Requesting to enable PIN? */
    if (enable_pin_flag) {
        mm_sim_enable_pin (ctx->sim,
                           pin_str,
                           ctx->cancellable,
                           (GAsyncReadyCallback)enable_pin_ready,
                           NULL);
        return;
    }

    /* Requesting to disable PIN? */
    if (disable_pin_flag) {
        mm_sim_disable_pin (ctx->sim,
                            pin_str,
                            ctx->cancellable,
                            (GAsyncReadyCallback)disable_pin_ready,
                            NULL);
        return;
    }

    /* Requesting to change PIN? */
    if (change_pin_str) {
        mm_sim_change_pin (ctx->sim,
                           pin_str, /* current */
                           change_pin_str, /* new */
                           ctx->cancellable,
                           (GAsyncReadyCallback)change_pin_ready,
                           NULL);
        return;
    }

    /* Requesting to send PUK? */
    if (puk_str) {
        mm_sim_send_puk (ctx->sim,
                         puk_str,
                         pin_str,
                         ctx->cancellable,
                         (GAsyncReadyCallback)send_puk_ready,
                         NULL);
        return;
    }

    /* Requesting to set preferred networks? */
    if (set_preferred_networks_str) {
        GList *preferred_networks = NULL;

        parse_preferred_networks (&preferred_networks);
        mm_sim_set_preferred_networks (ctx->sim,
                                       preferred_networks,
                                       ctx->cancellable,
                                       (GAsyncReadyCallback)set_preferred_networks_ready,
                                       NULL);
        g_list_free_full (preferred_networks, (GDestroyNotify) mm_sim_preferred_network_free);
        return;
    }

    /* Requesting to send PIN? (always LAST check!) */
    if (pin_str) {
        mm_sim_send_pin (ctx->sim,
                         pin_str,
                         ctx->cancellable,
                         (GAsyncReadyCallback)send_pin_ready,
                         NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_sim_run_asynchronous (GDBusConnection *connection,
                            GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper sim */
    mmcli_get_sim (connection,
                   mmcli_get_common_sim_string (),
                   cancellable,
                   (GAsyncReadyCallback)get_sim_ready,
                   NULL);
}

void
mmcli_sim_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->sim = mmcli_get_sim_sync (connection,
                                   mmcli_get_common_sim_string (),
                                   &ctx->manager,
                                   &ctx->object);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->sim));

    /* Request to get info from SIM? */
    if (info_flag) {
        g_debug ("Printing sim info...");
        print_sim_info (ctx->sim);
        return;
    }

    /* Requesting to enable PIN? */
    if (enable_pin_flag) {
        gboolean operation_result;

        operation_result = mm_sim_enable_pin_sync (ctx->sim,
                                                   pin_str,
                                                   NULL,
                                                   &error);
        enable_pin_process_reply (operation_result, error);
        return;
    }

    /* Requesting to disable PIN? */
    if (disable_pin_flag) {
        gboolean operation_result;

        operation_result = mm_sim_disable_pin_sync (ctx->sim,
                                                    pin_str,
                                                    NULL,
                                                    &error);
        disable_pin_process_reply (operation_result, error);
        return;
    }

    /* Requesting to change PIN? */
    if (change_pin_str) {
        gboolean operation_result;

        operation_result = mm_sim_change_pin_sync (ctx->sim,
                                                   pin_str, /* current */
                                                   change_pin_str, /* new */
                                                   NULL,
                                                   &error);
        change_pin_process_reply (operation_result, error);
        return;
    }

    /* Requesting to send PUK? */
    if (puk_str) {
        gboolean operation_result;

        operation_result = mm_sim_send_puk_sync (ctx->sim,
                                                 puk_str,
                                                 pin_str,
                                                 NULL,
                                                 &error);
        send_puk_process_reply (operation_result, error);
        return;
    }

    /* Requesting to set preferred networks? */
    if (set_preferred_networks_str) {
        gboolean  operation_result;
        GList    *preferred_networks = NULL;

        parse_preferred_networks (&preferred_networks);
        operation_result = mm_sim_set_preferred_networks_sync (ctx->sim,
                                                               preferred_networks,
                                                               NULL,
                                                               &error);
        g_list_free_full (preferred_networks, (GDestroyNotify) mm_sim_preferred_network_free);
        set_preferred_networks_process_reply (operation_result, error);
        return;
    }

    /* Requesting to send PIN? (always LAST check!) */
    if (pin_str) {
        gboolean operation_result;

        operation_result = mm_sim_send_pin_sync (ctx->sim,
                                                 pin_str,
                                                 NULL,
                                                 &error);
        send_pin_process_reply (operation_result, error);
        return;
    }

    g_warn_if_reached ();
}
