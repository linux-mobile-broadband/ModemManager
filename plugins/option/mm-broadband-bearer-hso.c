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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-hso.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerHso, mm_broadband_bearer_hso, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerHsoPrivate {
    guint auth_idx;
    gpointer connect_pending;
    guint connect_pending_id;
    gulong connect_cancellable_id;
    gulong connect_port_closed_id;
};

/*****************************************************************************/
/* 3GPP IP config retrieval (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
} GetIpConfig3gppContext;

static void
get_ip_config_context_free (GetIpConfig3gppContext *ctx)
{
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (GetIpConfig3gppContext, ctx);
}

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer *self,
                           GAsyncResult *res,
                           MMBearerIpConfig **ipv4_config,
                           MMBearerIpConfig **ipv6_config,
                           GError **error)
{
    MMBearerIpConfig *ip_config;

    ip_config = g_task_propagate_pointer (G_TASK (res), error);
    if (!ip_config)
        return FALSE;

    /* No IPv6 for now */
    *ipv4_config = ip_config; /* Transfer ownership */
    *ipv6_config = NULL;
    return TRUE;
}

#define OWANDATA_TAG "_OWANDATA: "

static void
ip_config_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 GTask *task)
{
    GetIpConfig3gppContext *ctx;
    MMBearerIpConfig *ip_config = NULL;
    const gchar *response;
    GError *error = NULL;
    gchar **items;
    gchar *dns[3] = { 0 };
    guint i;
    guint dns_i;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* TODO: use a regex to parse this */

    /* Check result */
    if (!g_str_has_prefix (response, OWANDATA_TAG)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get IP config: invalid response '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    response = mm_strip_tag (response, OWANDATA_TAG);
    items = g_strsplit (response, ", ", 0);

    for (i = 0, dns_i = 0; items[i]; i++) {
        if (i == 0) { /* CID */
            gint num;

            if (!mm_get_int_from_str (items[i], &num) ||
                num != ctx->cid) {
                error = g_error_new (MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unknown CID in OWANDATA response ("
                                     "got %d, expected %d)", (guint) num, ctx->cid);
                break;
            }
        } else if (i == 1) { /* IP address */
            guint32 tmp;

            if (!inet_pton (AF_INET, items[i], &tmp))
                break;

            ip_config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (ip_config, MM_BEARER_IP_METHOD_STATIC);
            mm_bearer_ip_config_set_address (ip_config,  items[i]);
            mm_bearer_ip_config_set_prefix (ip_config, 32);
        } else if (i == 3 || i == 4) { /* DNS entries */
            guint32 tmp;

            if (!inet_pton (AF_INET, items[i], &tmp)) {
                g_clear_object (&ip_config);
                break;
            }

            dns[dns_i++] = items[i];
        }
    }

    if (!ip_config) {
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't get IP config: couldn't parse response '%s'",
                                     response);
    } else {
        /* If we got DNS entries, set them in the IP config */
        if (dns[0])
            mm_bearer_ip_config_set_dns (ip_config, (const gchar **)dns);

        g_task_return_pointer (task, ip_config, g_object_unref);
    }

    g_object_unref (task);
    g_strfreev (items);
}

static void
get_ip_config_3gpp (MMBroadbandBearer *self,
                    MMBroadbandModem *modem,
                    MMPortSerialAt *primary,
                    MMPortSerialAt *secondary,
                    MMPort *data,
                    guint cid,
                    MMBearerIpFamily ip_family,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GetIpConfig3gppContext *ctx;
    GTask *task;
    gchar *command;

    ctx = g_slice_new0 (GetIpConfig3gppContext);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)get_ip_config_context_free);

    command = g_strdup_printf ("AT_OWANDATA=%d", cid);
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (modem),
        primary,
        command,
        3,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)ip_config_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerHso *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    MMPort *data;
    guint auth_idx;
    GError *saved_error;
} Dial3gppContext;

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->data)
        g_object_unref (ctx->data);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_slice_free (Dial3gppContext, ctx);
}

static gboolean
dial_3gpp_context_set_error_if_cancelled (Dial3gppContext *ctx,
                                          GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Dial operation has been cancelled");
    return TRUE;
}

static gboolean
dial_3gpp_context_complete_and_free_if_cancelled (Dial3gppContext *ctx)
{
    GError *error = NULL;

    if (!dial_3gpp_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    dial_3gpp_context_complete_and_free (ctx);
    return TRUE;
}

static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_PORT (g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
connect_reset_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     Dial3gppContext *ctx)
{
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* error should have already been set in the simple async result */
    dial_3gpp_context_complete_and_free (ctx);
}

static void
connect_reset (Dial3gppContext *ctx)
{
    gchar *command;

    /* Need to reset the connection attempt */
    command = g_strdup_printf ("AT_OWANCALL=%d,0,1",
                               ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)connect_reset_ready,
                                   ctx);
    g_free (command);
}

static void
report_connection_status (MMBaseBearer *bearer,
                          MMBearerConnectionStatus status)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (bearer);
    Dial3gppContext *ctx;

    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED ||
              status == MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED ||
              status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);

    /* Recover context (if any) and remove both cancellation and timeout (if any)*/
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Connection status reported but no connection attempt? */
    if (!ctx) {
        g_assert (self->priv->connect_pending_id == 0);

        mm_dbg ("Received spontaneous _OWANCALL (%s)",
                mm_bearer_connection_status_get_string (status));

        if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED) {
            /* If no connection attempt on-going, make sure we mark ourselves as
             * disconnected */
            MM_BASE_BEARER_CLASS (mm_broadband_bearer_hso_parent_class)->report_connection_status (
                bearer,
                status);
        }
        return;
    }

    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    if (self->priv->connect_port_closed_id) {
        g_signal_handler_disconnect (ctx->primary, self->priv->connect_port_closed_id);
        self->priv->connect_port_closed_id = 0;
    }

    /* Reporting connected */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED) {
        /* If we wanted to get cancelled before, do it now */
        if (ctx->saved_error) {
            /* Keep error */
            g_simple_async_result_take_error (ctx->result, ctx->saved_error);
            ctx->saved_error = NULL;
            /* Cancel connection */
            connect_reset (ctx);
            return;
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->data),
                                                   g_object_unref);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* If we wanted to get cancelled before and now we couldn't connect,
     * use the cancelled error and return */
    if (ctx->saved_error) {
        g_simple_async_result_take_error (ctx->result, ctx->saved_error);
        ctx->saved_error = NULL;
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Received CONNECTION_FAILED or DISCONNECTED during a connection attempt? */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Call setup failed");
    dial_3gpp_context_complete_and_free (ctx);
}

static gboolean
connect_timed_out_cb (MMBroadbandBearerHso *self)
{
    Dial3gppContext *ctx;

    /* Recover context and remove it from the private info */
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Remove cancellation, if found */
    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    /* Remove closed port watch, if found */
    if (ctx && self->priv->connect_port_closed_id) {
        g_signal_handler_disconnect (ctx->primary, self->priv->connect_port_closed_id);
        self->priv->connect_port_closed_id = 0;
    }

    /* Cleanup timeout ID */
    self->priv->connect_pending_id = 0;

    /* If we were cancelled, prefer that error */
    if (ctx->saved_error) {
        g_simple_async_result_take_error (ctx->result, ctx->saved_error);
        ctx->saved_error = NULL;
    } else
        g_simple_async_result_set_error (ctx->result,
                                         MM_MOBILE_EQUIPMENT_ERROR,
                                         MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                         "Connection attempt timed out");

    /* It's probably pointless to try to reset this here, but anyway... */
    connect_reset (ctx);

    return G_SOURCE_REMOVE;
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerHso *self)
{
    Dial3gppContext *ctx;

    /* Recover context but DON'T remove it from the private info */
    ctx = self->priv->connect_pending;

    /* Remove the cancellable
     * NOTE: we shouldn't remove the timeout yet. We still need to wait
     * to get connected before running the explicit connection reset */
    self->priv->connect_cancellable_id = 0;

    /* Store cancelled error */
    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &ctx->saved_error));

    /* We cannot reset right here, we need to wait for the connection
     * attempt to finish */
}

static void
forced_close_cb (MMPortSerial *port,
                 MMBroadbandBearerHso *self)
{
    /* Just treat the forced close event as any other unsolicited message */
    mm_base_bearer_report_connection_status (MM_BASE_BEARER (self),
                                             MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED);
}

static void
activate_ready (MMBaseModem *modem,
                GAsyncResult *res,
                MMBroadbandBearerHso *self)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    /* Try to recover the connection context. If none found, it means the
     * context was already completed and we have nothing else to do. */
    ctx = self->priv->connect_pending;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    if (!ctx) {
        mm_dbg ("Connection context was finished already by an unsolicited message");

        /* Run _finish() to finalize the async call, even if we don't care
         * the result */
        mm_base_modem_at_command_full_finish (modem, res, NULL);
        return;
    }

    /* From now on, if we get cancelled, we'll need to run the connection
     * reset ourselves just in case */

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout so that we don't wait forever to get the
     * connection on */
    self->priv->connect_pending_id = g_timeout_add_seconds (30,
                                                            (GSourceFunc)connect_timed_out_cb,
                                                            self);
    self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                G_CALLBACK (connect_cancelled_cb),
                                                                self,
                                                                NULL);

    /* If we get the port closed, we treat as a connect error */
    self->priv->connect_port_closed_id = g_signal_connect (ctx->primary,
                                                           "forced-close",
                                                           G_CALLBACK (forced_close_cb),
                                                           self);
}

static void authenticate (Dial3gppContext *ctx);

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    Dial3gppContext *ctx)
{
    gchar *command;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    if (!mm_base_modem_at_command_full_finish (modem, res, NULL)) {
        /* Try the next auth command */
        ctx->auth_idx++;
        authenticate (ctx);
        return;
    }

    /* Store which auth command worked, for next attempts */
    ctx->self->priv->auth_idx = ctx->auth_idx;

    /* The unsolicited response to AT_OWANCALL may come before the OK does.
     * We will keep the connection context in the bearer private data so
     * that it is accessible from the unsolicited message handler. Note
     * also that we do NOT pass the ctx to the GAsyncReadyCallback, as it
     * may not be valid any more when the callback is called (it may be
     * already completed in the unsolicited handling) */
    g_assert (ctx->self->priv->connect_pending == NULL);
    ctx->self->priv->connect_pending = ctx;

    /* Success, activate the PDP context and start the data session */
    command = g_strdup_printf ("AT_OWANCALL=%d,1,1",
                               ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)activate_ready,
                                   g_object_ref (ctx->self)); /* we pass the bearer object! */
    g_free (command);
}

const gchar *auth_commands[] = {
    "$QCPDPP",
    /* Icera-based devices (GI0322/Quicksilver, iCON 505) don't implement
     * $QCPDPP, but instead use _OPDPP with the same arguments.
     */
    "_OPDPP",
    NULL
};

static void
authenticate (Dial3gppContext *ctx)
{
    gchar *command;
    const gchar *user;
    const gchar *password;
    MMBearerAllowedAuth allowed_auth;

    if (!auth_commands[ctx->auth_idx]) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't run HSO authentication");
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    allowed_auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* Both user and password are required; otherwise firmware returns an error */
    if (!user || !password || allowed_auth == MM_BEARER_ALLOWED_AUTH_NONE) {
        mm_dbg ("Not using authentication");
        command = g_strdup_printf ("%s=%d,0",
                                   auth_commands[ctx->auth_idx],
                                   ctx->cid);
    } else {
        gchar *quoted_user;
        gchar *quoted_password;
        guint hso_auth;

        if (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
            mm_dbg ("Using default (PAP) authentication method");
            hso_auth = 1;
        } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
            mm_dbg ("Using PAP authentication method");
            hso_auth = 1;
        } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
            mm_dbg ("Using CHAP authentication method");
            hso_auth = 2;
        } else {
            gchar *str;

            str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_UNSUPPORTED,
                "Cannot use any of the specified authentication methods (%s)",
                str);
            g_free (str);
            dial_3gpp_context_complete_and_free (ctx);
            return;
        }

        quoted_user = mm_port_serial_at_quote_string (user);
        quoted_password = mm_port_serial_at_quote_string (password);
        command = g_strdup_printf ("%s=%d,%u,%s,%s",
                                   auth_commands[ctx->auth_idx],
                                   ctx->cid,
                                   hso_auth,
                                   quoted_password,
                                   quoted_user);
        g_free (quoted_user);
        g_free (quoted_password);
    }

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)authenticate_ready,
                                   ctx);
    g_free (command);
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMPortSerialAt *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    Dial3gppContext *ctx;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp);
    ctx->cancellable = g_object_ref (cancellable);

    /* Always start with the index that worked last time
     * (will be 0 the first time)*/
    ctx->auth_idx = ctx->self->priv->auth_idx;

    /* We need a net data port */
    ctx->data = mm_base_modem_get_best_data_port (modem, MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "No valid data port found to launch connection");
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    authenticate (ctx);
}

/*****************************************************************************/
/* 3GPP disconnect */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
} DisconnectContext;

static void
disconnect_context_free (DisconnectContext *ctx)
{
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disconnect_owancall_ready (MMBaseModem *modem,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_dbg ("Disconnection failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disconnect_3gpp (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    gchar *command;
    DisconnectContext *ctx;
    GTask *task;

    g_assert (primary != NULL);

    ctx = g_new0 (DisconnectContext, 1);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disconnect_context_free);

    /* Use specific CID */
    command = g_strdup_printf ("AT_OWANCALL=%d,0,0", cid);
    mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                   primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)disconnect_owancall_ready,
                                   task);
    g_free (command);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_hso_new_finish (GAsyncResult *res,
                                    GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_hso_new (MMBroadbandModemHso *modem,
                             MMBearerProperties *config,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_HSO,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_hso_init (MMBroadbandBearerHso *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_HSO,
                                              MMBroadbandBearerHsoPrivate);
}

static void
mm_broadband_bearer_hso_class_init (MMBroadbandBearerHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerHsoPrivate));

    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
