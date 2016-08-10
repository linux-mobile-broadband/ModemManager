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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
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

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModemSignal *modem_signal;
} Context;
static Context *ctx;

/* Options */
static gboolean get_flag;
static gchar *setup_str;

static GOptionEntry entries[] = {
    { "signal-setup", 0, 0, G_OPTION_ARG_STRING, &setup_str,
      "Setup extended signal information retrieval",
      "[Rate]"
    },
    { "signal-get", 0, 0, G_OPTION_ARG_NONE, &get_flag,
      "Get all extended signal quality information",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_signal_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("signal",
                                "Signal options",
                                "Show Signal options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_signal_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (!!setup_str +
                 get_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many Signal actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (get_flag)
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
    if (ctx->modem_signal)
        g_object_unref (ctx->modem_signal);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_signal (void)
{
    if (!ctx->modem_signal) {
        g_printerr ("error: modem has no extended signal capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_signal_shutdown (void)
{
    context_free (ctx);
}

static void
print_signal_info (void)
{
    MMSignal *signal;

    g_print ("\n"
             "%s\n"
             "  -------------------------\n"
             "  Refresh rate: '%u' seconds\n",
             mm_modem_signal_get_path (ctx->modem_signal),
             mm_modem_signal_get_rate (ctx->modem_signal));

    /* CDMA */
    signal = mm_modem_signal_peek_cdma (ctx->modem_signal);
    if (signal)
        g_print ("  -------------------------\n"
                 "  CDMA1x | RSSI: '%.2lf' dBm\n"
                 "         | EcIo: '%.2lf' dBm\n",
                 mm_signal_get_rssi (signal),
                 mm_signal_get_ecio (signal));

    /* EVDO */
    signal = mm_modem_signal_peek_evdo (ctx->modem_signal);
    if (signal)
        g_print ("  -------------------------\n"
                 "  EV-DO  | RSSI: '%.2lf' dBm\n"
                 "         | EcIo: '%.2lf' dBm\n"
                 "         | SINR: '%.2lf' dBm\n"
                 "         |   Io: '%.2lf' dB\n",
                 mm_signal_get_rssi (signal),
                 mm_signal_get_ecio (signal),
                 mm_signal_get_sinr (signal),
                 mm_signal_get_io (signal));

    /* GSM */
    signal = mm_modem_signal_peek_gsm (ctx->modem_signal);
    if (signal)
        g_print ("  -------------------------\n"
                 "  GSM    | RSSI: '%.2lf' dBm\n",
                 mm_signal_get_rssi (signal));

    /* UMTS */
    signal = mm_modem_signal_peek_umts (ctx->modem_signal);
    if (signal)
        g_print ("  -------------------------\n"
                 "  UMTS   | RSSI: '%.2lf' dBm\n"
                 "         | RSCP: '%.2lf' dBm\n"
                 "         | EcIo: '%.2lf' dB\n",
                 mm_signal_get_rssi (signal),
                 mm_signal_get_rscp (signal),
                 mm_signal_get_ecio (signal));

    /* LTE */
    signal = mm_modem_signal_peek_lte (ctx->modem_signal);
    if (signal)
        g_print ("  -------------------------\n"
                 "  LTE    | RSSI: '%.2lf' dBm\n"
                 "         | RSRQ: '%.2lf' dB\n"
                 "         | RSRP: '%.2lf' dBm\n"
                 "         |  SNR: '%.2lf' dB\n",
                 mm_signal_get_rssi (signal),
                 mm_signal_get_rsrq (signal),
                 mm_signal_get_rsrp (signal),
                 mm_signal_get_snr (signal));
}

static void
setup_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't setup extended signal information retrieval: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully setup extended signal information retrieval\n");
}

static void
setup_ready (MMModemSignal *modem,
             GAsyncResult  *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_signal_setup_finish (modem, result, &error);
    setup_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_signal = mm_object_get_modem_signal (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_signal)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_signal));

    ensure_modem_signal ();

    if (get_flag)
        g_assert_not_reached ();

    /* Request to setup? */
    if (setup_str) {
        guint rate;

        if (!mm_get_uint_from_str (setup_str, &rate)) {
            g_printerr ("error: invalid rate value '%s'", setup_str);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting up extended signal quality information retrieval...");
        mm_modem_signal_setup (ctx->modem_signal,
                               rate,
                               ctx->cancellable,
                               (GAsyncReadyCallback)setup_ready,
                               NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_signal_run_asynchronous (GDBusConnection *connection,
                                       GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper modem */
    mmcli_get_modem (connection,
                     mmcli_get_common_modem_string (),
                     cancellable,
                     (GAsyncReadyCallback)get_modem_ready,
                     NULL);
}

void
mmcli_modem_signal_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_signal = mm_object_get_modem_signal (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_signal)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_signal));

    ensure_modem_signal ();

    /* Request to get signal info? */
    if (get_flag) {
        print_signal_info ();
        return;
    }

    /* Request to set rate? */
    if (setup_str) {
        guint rate;
        gboolean result;

        if (!mm_get_uint_from_str (setup_str, &rate)) {
            g_printerr ("error: invalid rate value '%s'", setup_str);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously setting up extended signal quality information retrieval...");
        result = mm_modem_signal_setup_sync (ctx->modem_signal,
                                             rate,
                                             NULL,
                                             &error);
        setup_process_reply (result, error);
        return;
    }


    g_warn_if_reached ();
}
