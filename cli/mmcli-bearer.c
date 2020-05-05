/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control bearer status & access information from the command line
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
    MMBearer *bearer;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean connect_flag;
static gboolean disconnect_flag;

static GOptionEntry entries[] = {
    { "connect", 'c', 0, G_OPTION_ARG_NONE, &connect_flag,
      "Connect a given bearer.",
      NULL
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
                                "Bearer options:",
                                "Show bearer options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_bearer_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (connect_flag +
                 disconnect_flag);

    if (n_actions == 0 && mmcli_get_common_bearer_string ()) {
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
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_bearer_shutdown (void)
{
    context_free ();
}

static void
print_bearer_info (MMBearer *bearer)
{
    MMBearerIpConfig   *ipv4_config;
    MMBearerIpConfig   *ipv6_config;
    MMBearerProperties *properties;
    MMBearerStats      *stats;

    ipv4_config = mm_bearer_get_ipv4_config (bearer);
    ipv6_config = mm_bearer_get_ipv6_config (bearer);
    properties  = mm_bearer_get_properties (bearer);
    stats       = mm_bearer_get_stats (bearer);

    mmcli_output_string      (MMC_F_BEARER_GENERAL_DBUS_PATH, mm_bearer_get_path (bearer));
    mmcli_output_string      (MMC_F_BEARER_GENERAL_TYPE,      mm_bearer_type_get_string (mm_bearer_get_bearer_type (bearer)));

    mmcli_output_string      (MMC_F_BEARER_STATUS_CONNECTED,  mm_bearer_get_connected (bearer) ? "yes" : "no");
    mmcli_output_string      (MMC_F_BEARER_STATUS_SUSPENDED,  mm_bearer_get_suspended (bearer) ? "yes" : "no");
    mmcli_output_string      (MMC_F_BEARER_STATUS_INTERFACE,  mm_bearer_get_interface (bearer));
    mmcli_output_string_take (MMC_F_BEARER_STATUS_IP_TIMEOUT, g_strdup_printf ("%u", mm_bearer_get_ip_timeout (bearer)));

    /* Properties */
    {
        const gchar *apn = NULL;
        const gchar *roaming = NULL;
        gchar       *ip_family_str = NULL;
        const gchar *user = NULL;
        const gchar *password = NULL;
        const gchar *rm_protocol = NULL;
        gchar       *allowed_auth_str = NULL;

        if (properties) {
            apn              = mm_bearer_properties_get_apn (properties);
            ip_family_str    = (properties ? mm_bearer_ip_family_build_string_from_mask (mm_bearer_properties_get_ip_type (properties)) : NULL);
            allowed_auth_str = (properties ? mm_bearer_allowed_auth_build_string_from_mask (mm_bearer_properties_get_allowed_auth (properties)) : NULL);
            user             = mm_bearer_properties_get_user (properties);
            password         = mm_bearer_properties_get_password (properties);
            if (mm_bearer_get_bearer_type (bearer) != MM_BEARER_TYPE_DEFAULT_ATTACH) {
                roaming     = mm_bearer_properties_get_allow_roaming (properties) ? "allowed" : "forbidden";
                rm_protocol = mm_modem_cdma_rm_protocol_get_string (mm_bearer_properties_get_rm_protocol (properties));
            }
        }

        mmcli_output_string           (MMC_F_BEARER_PROPERTIES_APN,          apn);
        mmcli_output_string           (MMC_F_BEARER_PROPERTIES_ROAMING,      roaming);
        mmcli_output_string_take      (MMC_F_BEARER_PROPERTIES_IP_TYPE,      ip_family_str);
        mmcli_output_string           (MMC_F_BEARER_PROPERTIES_USER,         user);
        mmcli_output_string           (MMC_F_BEARER_PROPERTIES_PASSWORD,     password);
        mmcli_output_string           (MMC_F_BEARER_PROPERTIES_RM_PROTOCOL,  rm_protocol);
        mmcli_output_string_list_take (MMC_F_BEARER_PROPERTIES_ALLOWED_AUTH, allowed_auth_str);
    }

    /* IPv4 config */
    {
        const gchar  *method = NULL;
        const gchar  *address = NULL;
        gchar        *prefix = NULL;
        const gchar  *gateway = NULL;
        const gchar **dns = NULL;
        gchar        *mtu = NULL;

        if (ipv4_config) {
            method = mm_bearer_ip_method_get_string (mm_bearer_ip_config_get_method (ipv4_config));
            if (mm_bearer_ip_config_get_method (ipv4_config) != MM_BEARER_IP_METHOD_UNKNOWN) {
                guint mtu_n;

                address = mm_bearer_ip_config_get_address (ipv4_config);
                prefix  = g_strdup_printf ("%u", mm_bearer_ip_config_get_prefix (ipv4_config));
                gateway = mm_bearer_ip_config_get_gateway (ipv4_config);
                dns     = mm_bearer_ip_config_get_dns (ipv4_config);
                mtu_n   = mm_bearer_ip_config_get_mtu (ipv4_config);
                if (mtu_n)
                    mtu = g_strdup_printf ("%u", mtu_n);
            }
        }

        mmcli_output_string       (MMC_F_BEARER_IPV4_CONFIG_METHOD,  method);
        mmcli_output_string       (MMC_F_BEARER_IPV4_CONFIG_ADDRESS, address);
        mmcli_output_string_take  (MMC_F_BEARER_IPV4_CONFIG_PREFIX,  prefix);
        mmcli_output_string       (MMC_F_BEARER_IPV4_CONFIG_GATEWAY, gateway);
        mmcli_output_string_array (MMC_F_BEARER_IPV4_CONFIG_DNS,     dns, FALSE);
        mmcli_output_string_take  (MMC_F_BEARER_IPV4_CONFIG_MTU,     mtu);
    }

    /* IPv6 config */
    {
        const gchar  *method = NULL;
        const gchar  *address = NULL;
        gchar        *prefix = NULL;
        const gchar  *gateway = NULL;
        const gchar **dns = NULL;
        gchar        *mtu = NULL;

        if (ipv6_config) {
            method = mm_bearer_ip_method_get_string (mm_bearer_ip_config_get_method (ipv6_config));
            if (mm_bearer_ip_config_get_method (ipv6_config) != MM_BEARER_IP_METHOD_UNKNOWN) {
                guint mtu_n;

                address = mm_bearer_ip_config_get_address (ipv6_config);
                prefix  = g_strdup_printf ("%u", mm_bearer_ip_config_get_prefix (ipv6_config));
                gateway = mm_bearer_ip_config_get_gateway (ipv6_config);
                dns     = mm_bearer_ip_config_get_dns (ipv6_config);
                mtu_n   = mm_bearer_ip_config_get_mtu (ipv6_config);
                if (mtu_n)
                    mtu = g_strdup_printf ("%u", mtu_n);
            }
        }

        mmcli_output_string       (MMC_F_BEARER_IPV6_CONFIG_METHOD,  method);
        mmcli_output_string       (MMC_F_BEARER_IPV6_CONFIG_ADDRESS, address);
        mmcli_output_string_take  (MMC_F_BEARER_IPV6_CONFIG_PREFIX,  prefix);
        mmcli_output_string       (MMC_F_BEARER_IPV6_CONFIG_GATEWAY, gateway);
        mmcli_output_string_array (MMC_F_BEARER_IPV6_CONFIG_DNS,     dns, FALSE);
        mmcli_output_string_take  (MMC_F_BEARER_IPV6_CONFIG_MTU,     mtu);
    }

    /* Stats */
    {
        gchar *duration = NULL;
        gchar *bytes_rx = NULL;
        gchar *bytes_tx = NULL;
        gchar *attempts = NULL;
        gchar *failed_attempts = NULL;
        gchar *total_duration = NULL;
        gchar *total_bytes_rx = NULL;
        gchar *total_bytes_tx = NULL;

        if (stats) {
            guint64 val;

            val = mm_bearer_stats_get_duration (stats);
            if (val)
                duration = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_rx_bytes (stats);
            if (val)
                bytes_rx = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_tx_bytes (stats);
            if (val)
                bytes_tx = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_attempts (stats);
            if (val)
                attempts = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_failed_attempts (stats);
            if (val)
                failed_attempts = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_total_duration (stats);
            if (val)
                total_duration = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_total_rx_bytes (stats);
            if (val)
                total_bytes_rx = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
            val = mm_bearer_stats_get_total_tx_bytes (stats);
            if (val)
                total_bytes_tx = g_strdup_printf ("%" G_GUINT64_FORMAT, val);
        }

        mmcli_output_string_take (MMC_F_BEARER_STATS_DURATION,        duration);
        mmcli_output_string_take (MMC_F_BEARER_STATS_BYTES_RX,        bytes_rx);
        mmcli_output_string_take (MMC_F_BEARER_STATS_BYTES_TX,        bytes_tx);
        mmcli_output_string_take (MMC_F_BEARER_STATS_ATTEMPTS,        attempts);
        mmcli_output_string_take (MMC_F_BEARER_STATS_FAILED_ATTEMPTS, failed_attempts);
        mmcli_output_string_take (MMC_F_BEARER_STATS_TOTAL_DURATION,  total_duration);
        mmcli_output_string_take (MMC_F_BEARER_STATS_TOTAL_BYTES_RX,  total_bytes_rx);
        mmcli_output_string_take (MMC_F_BEARER_STATS_TOTAL_BYTES_TX,  total_bytes_tx);
    }

    mmcli_output_dump ();

    g_clear_object (&stats);
    g_clear_object (&properties);
    g_clear_object (&ipv4_config);
    g_clear_object (&ipv6_config);
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
    ctx->bearer = mmcli_get_bearer_finish (result,
                                           &ctx->manager,
                                           &ctx->object);

    if (info_flag)
        g_assert_not_reached ();

    /* Request to connect the bearer? */
    if (connect_flag) {
        g_debug ("Asynchronously connecting bearer...");
        mm_bearer_connect (ctx->bearer,
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
                      mmcli_get_common_bearer_string (),
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
    ctx->bearer = mmcli_get_bearer_sync (connection,
                                         mmcli_get_common_bearer_string (),
                                         &ctx->manager,
                                         &ctx->object);

    /* Request to get info from bearer? */
    if (info_flag) {
        g_debug ("Printing bearer info...");
        print_bearer_info (ctx->bearer);
        return;
    }

    /* Request to connect the bearer? */
    if (connect_flag) {
        gboolean result;

        g_debug ("Synchronously connecting bearer...");
        result = mm_bearer_connect_sync (ctx->bearer,
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
