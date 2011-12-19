/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control bearer status & access information from the command line
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
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    GCancellable *cancellable;
    MMBearer *bearer;
} Context;
static Context *ctx;

/* Options */
static gchar *bearer_str;
static gboolean info_flag; /* set when no action found */
static gchar *connect_with_number_str;
static gboolean connect_flag;
static gboolean disconnect_flag;

static GOptionEntry entries[] = {
    { "bearer", 'b', 0, G_OPTION_ARG_STRING, &bearer_str,
      "Specify bearer by path. Shows bearer information if no action specified.",
      NULL
    },
    { "connect", 'c', 0, G_OPTION_ARG_NONE, &connect_flag,
      "Connect a given bearer using the default number, if any.",
      NULL
    },
    { "connect-with-number", 0, 0, G_OPTION_ARG_STRING, &connect_with_number_str,
      "Connect a given bearer using the specified number.",
      "[NUMBER]"
    },
    { "disconnect", 'x', 0, G_OPTION_ARG_NONE, &disconnect_flag,
      "Disconnect a given bearer.",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_bearer_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("bearer",
	                            "Bearer options",
	                            "Show bearer options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_bearer_options_enabled (void)
{
    guint n_actions;

    n_actions = (!!connect_with_number_str +
                 connect_flag +
                 disconnect_flag);

    if (n_actions == 0 && bearer_str) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many bearer actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (info_flag)
        mmcli_force_sync_operation ();

    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_free (ctx);
}

void
mmcli_bearer_shutdown (void)
{
    context_free (ctx);
}

static void
print_bearer_info (MMBearer *bearer)
{
    const MMBearerIpConfig *ipv4_config;
    const MMBearerIpConfig *ipv6_config;

    ipv4_config = mm_bearer_get_ipv4_config (bearer);
    ipv6_config = mm_bearer_get_ipv6_config (bearer);

    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE
#define VALIDATE(str) (str ? str : "unknown")

    g_print ("Bearer '%s'\n",
             mm_bearer_get_path (bearer));
    g_print ("  -------------------------\n"
             "  Status             |   connected: '%s'\n"
             "                     |   suspended: '%s'\n"
             "                     |   interface: '%s'\n",
             mm_bearer_get_connected (bearer) ? "yes" : "no",
             mm_bearer_get_suspended (bearer) ? "yes" : "no",
             VALIDATE (mm_bearer_get_interface (bearer)));

    /* IPv4 */
    g_print ("  -------------------------\n"
             "  IPv4 configuration |   method: '%s'\n",
             (ipv4_config ?
              mmcli_get_bearer_ip_method_string (mm_bearer_ip_config_get_method (ipv4_config)) :
              "none"));
    if (ipv4_config &&
        mm_bearer_ip_config_get_method (ipv4_config) == MM_BEARER_IP_METHOD_STATIC) {
        const gchar **dns;
        guint i;

        dns = mm_bearer_ip_config_get_dns (ipv4_config);
        g_print ("                   |  address: '%s'\n"
                 "                   |   prefix: '%u'\n"
                 "                   |  gateway: '%s'\n"
                 "                   |      DNS: '%s'",
                 VALIDATE (mm_bearer_ip_config_get_address (ipv4_config)),
                 mm_bearer_ip_config_get_prefix (ipv4_config),
                 VALIDATE (mm_bearer_ip_config_get_gateway (ipv4_config)),
                 VALIDATE (dns[0]));
        /* Additional DNS addresses */
        for (i = 1; dns[i]; i++)
            g_print (", '%s'", dns[i]);
        g_print ("\n");
    }

    /* IPv6 */
    g_print ("  -------------------------\n"
             "  IPv6 configuration |   method: '%s'\n",
             (ipv6_config ?
              mmcli_get_bearer_ip_method_string (mm_bearer_ip_config_get_method (ipv6_config)) :
              "none"));
    if (ipv6_config &&
        mm_bearer_ip_config_get_method (ipv6_config) == MM_BEARER_IP_METHOD_STATIC) {
        const gchar **dns;
        guint i;

        dns = mm_bearer_ip_config_get_dns (ipv6_config);
        g_print ("                   |  address: '%s'\n"
                 "                   |   prefix: '%u'\n"
                 "                   |  gateway: '%s'\n"
                 "                   |      DNS: '%s'",
                 VALIDATE(mm_bearer_ip_config_get_address (ipv6_config)),
                 mm_bearer_ip_config_get_prefix (ipv6_config),
                 VALIDATE(mm_bearer_ip_config_get_gateway (ipv6_config)),
                 VALIDATE(dns[0]));
        /* Additional DNS addresses */
        for (i = 1; dns[i]; i++)
            g_print (", '%s'", dns[i]);
        g_print ("\n");
    }
}

static void
connect_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't connect the bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully connected the bearer\n");
}

static void
connect_ready (MMBearer      *bearer,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_bearer_connect_finish (bearer, result, &error);
    connect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disconnect_process_reply (gboolean      result,
                          const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disconnect the bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disconnected the bearer\n");
}

static void
disconnect_ready (MMBearer      *bearer,
                  GAsyncResult *result,
                  gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_bearer_disconnect_finish (bearer, result, &error);
    disconnect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_bearer_ready (GObject      *source,
                  GAsyncResult *result,
                  gpointer      none)
{
    ctx->bearer = mmcli_get_bearer_finish (result);

    if (info_flag)
        g_assert_not_reached ();

    /* Request to connect the bearer? */
    if (connect_flag || connect_with_number_str) {
        g_debug ("Asynchronously connecting bearer...");
        mm_bearer_connect (ctx->bearer,
                           connect_flag ? "" : connect_with_number_str,
                           ctx->cancellable,
                           (GAsyncReadyCallback)connect_ready,
                           NULL);
        return;
    }

    /* Request to disconnect the bearer? */
    if (disconnect_flag) {
        g_debug ("Asynchronously disconnecting bearer...");
        mm_bearer_disconnect (ctx->bearer,
                              ctx->cancellable,
                              (GAsyncReadyCallback)disconnect_ready,
                              NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_bearer_run_asynchronous (GDBusConnection *connection,
                               GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper bearer */
    mmcli_get_bearer (connection,
                      bearer_str,
                      cancellable,
                      (GAsyncReadyCallback)get_bearer_ready,
                      NULL);
}

void
mmcli_bearer_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->bearer = mmcli_get_bearer_sync (connection, bearer_str);

    /* Request to get info from bearer? */
    if (info_flag) {
        g_debug ("Printing bearer info...");
        print_bearer_info (ctx->bearer);
        return;
    }

    /* Request to connect the bearer? */
    if (connect_flag || connect_with_number_str) {
        gboolean result;

        g_debug ("Synchronously connecting bearer...");
        result = mm_bearer_connect_sync (ctx->bearer,
                                         connect_flag ? "" : connect_with_number_str,
                                         NULL,
                                         &error);
        connect_process_reply (result, error);
        return;
    }

    /* Request to disconnect the bearer? */
    if (disconnect_flag) {
        gboolean result;

        g_debug ("Synchronously disconnecting bearer...");
        result = mm_bearer_disconnect_sync (ctx->bearer, NULL, &error);
        disconnect_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
