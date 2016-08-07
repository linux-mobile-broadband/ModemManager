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
 * Copyright (C) 2011 - 2012 Google, Inc.
 * Copyright (C) 2011 - 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-port-enums-types.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandBearer, mm_broadband_bearer, MM_TYPE_BASE_BEARER, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

typedef enum {
    CONNECTION_TYPE_NONE,
    CONNECTION_TYPE_3GPP,
    CONNECTION_TYPE_CDMA,
} ConnectionType;

struct _MMBroadbandBearerPrivate {
    /*-- Common stuff --*/
    /* Data port used when modem is connected */
    MMPort *port;
    /* Current connection type */
    ConnectionType connection_type;

    /*-- 3GPP specific --*/
    /* CID of the PDP context */
    guint cid;
};

/*****************************************************************************/

guint
mm_broadband_bearer_get_3gpp_cid (MMBroadbandBearer *self)
{
    return self->priv->cid;
}

/*****************************************************************************/

static MMBearerIpFamily
select_bearer_ip_family (MMBroadbandBearer *self)
{
    MMBearerIpFamily ip_family;

    ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    if (ip_family == MM_BEARER_IP_FAMILY_NONE || ip_family == MM_BEARER_IP_FAMILY_ANY) {
        gchar *default_family;

        ip_family = mm_base_bearer_get_default_ip_family (MM_BASE_BEARER (self));
        default_family = mm_bearer_ip_family_build_string_from_mask (ip_family);
        mm_dbg ("No specific IP family requested, defaulting to %s", default_family);
        g_free (default_family);
    }

    return ip_family;
}

/*****************************************************************************/
/* Detailed connect context, used in both CDMA and 3GPP sequences */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;

    MMPort *data;
    gboolean close_data_on_exit;

    /* 3GPP-specific */
    MMBearerIpFamily ip_family;
} DetailedConnectContext;

static MMBearerConnectResult *
detailed_connect_finish (MMBroadbandBearer *self,
                         GAsyncResult *res,
                         GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
detailed_connect_context_complete_and_free (DetailedConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    if (ctx->data) {
        if (ctx->close_data_on_exit)
            mm_port_serial_close (MM_PORT_SERIAL (ctx->data));
        g_object_unref (ctx->data);
    }
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_slice_free (DetailedConnectContext, ctx);
}

static gboolean
detailed_connect_context_set_error_if_cancelled (DetailedConnectContext *ctx,
                                                 GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Connection setup operation has been cancelled");
    return TRUE;
}

static gboolean
detailed_connect_context_complete_and_free_if_cancelled (DetailedConnectContext *ctx)
{
    GError *error = NULL;

    if (!detailed_connect_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    detailed_connect_context_complete_and_free (ctx);
    return TRUE;
}

static DetailedConnectContext *
detailed_connect_context_new (MMBroadbandBearer *self,
                              MMBroadbandModem *modem,
                              MMPortSerialAt *primary,
                              MMPortSerialAt *secondary,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = g_slice_new0 (DetailedConnectContext);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_connect_context_new);

    ctx->ip_family = select_bearer_ip_family (self);

    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);
    return ctx;
}

/*****************************************************************************/
/* Generic implementations (both 3GPP and CDMA) are always AT-port based */

static MMPortSerialAt *
common_get_at_data_port (MMBaseModem *modem,
                         GError **error)
{
    MMPort *data;

    /* Look for best data port, NULL if none available. */
    data = mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_AT);
    if (!data) {
        /* It may happen that the desired data port grabbed during probing was
         * actually a 'net' port, which the generic logic cannot handle, so if
         * that is the case, and we have no AT data ports specified, just
         fallback to the primary AT port. */
        data = (MMPort *) mm_base_modem_peek_port_primary (modem);
    }

    g_assert (MM_IS_PORT_SERIAL_AT (data));

    if (!mm_port_serial_open (MM_PORT_SERIAL (data), error)) {
        g_prefix_error (error, "Couldn't connect: cannot keep data port open.");
        return NULL;
    }

    mm_dbg ("Connection through a plain serial AT port (%s)", mm_port_get_device (data));

    return MM_PORT_SERIAL_AT (g_object_ref (data));
}

/*****************************************************************************/
/* CDMA CONNECT
 *
 * CDMA connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) If requesting specific RM, load current.
 *  2.1) If current RM different to the requested one, set the new one.
 * 3) Initiate call.
 */

static void
dial_cdma_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    GError *error = NULL;
    MMBearerIpConfig *config;

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_warn ("Couldn't connect: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* else... Yuhu! */

    /* Keep port open during connection */
    ctx->close_data_on_exit = FALSE;

    /* Generic CDMA connections are done over PPP always */
    g_assert (MM_IS_PORT_SERIAL_AT (ctx->data));
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Assume only IPv4 is given */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        mm_bearer_connect_result_new (ctx->data, config, NULL),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    detailed_connect_context_complete_and_free (ctx);

    g_object_unref (config);
}

static void
cdma_connect_context_dial (DetailedConnectContext *ctx)
{
    gchar *command;
    const gchar *number;

    number = mm_bearer_properties_get_number (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* If a number was given when creating the bearer, use that one.
     * Otherwise, use the default one, #777
     */
    if (number)
        command = g_strconcat ("DT", number, NULL);
    else
        command = g_strdup ("DT#777");

    mm_base_modem_at_command_full (ctx->modem,
                                   MM_PORT_SERIAL_AT (ctx->data),
                                   command,
                                   90,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)dial_cdma_ready,
                                   ctx);
    g_free (command);
}

static void
set_rm_protocol_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't set RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
current_rm_protocol_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;
    guint current_index;
    MMModemCdmaRmProtocol current_rm;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't query current RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_strip_tag (result, "+CRM:");
    current_index = (guint) atoi (result);
    current_rm = mm_cdma_get_rm_protocol_from_index (current_index, &error);
    if (error) {
        mm_warn ("Couldn't parse RM protocol reply (%s): '%s'",
                 result,
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    if (current_rm != mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (self)))) {
        guint new_index;
        gchar *command;

        mm_dbg ("Setting requested RM protocol...");

        new_index = (mm_cdma_get_index_from_rm_protocol (
                         mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (self))),
                         &error));
        if (error) {
            mm_warn ("Cannot set RM protocol: '%s'",
                     error->message);
            g_simple_async_result_take_error (ctx->result, error);
            detailed_connect_context_complete_and_free (ctx);
            return;
        }

        command = g_strdup_printf ("+CRM=%u", new_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)set_rm_protocol_ready,
                                       ctx);
        g_free (command);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
connect_cdma (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMPortSerialAt *primary,
              MMPortSerialAt *secondary, /* unused by us */
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;
    GError *error = NULL;

    g_assert (primary != NULL);

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        NULL,
                                        cancellable,
                                        callback,
                                        user_data);

    /* Grab dial port. This gets a reference to the dial port and OPENs it.
     * If we fail, we'll need to close it ourselves. */
    ctx->data = (MMPort *)common_get_at_data_port (ctx->modem, &error);
    if (!ctx->data) {
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }
    ctx->close_data_on_exit = TRUE;

    if (mm_bearer_properties_get_rm_protocol (
            mm_base_bearer_peek_config (MM_BASE_BEARER (self))) !=
        MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        /* Need to query current RM protocol */
        mm_dbg ("Querying current RM protocol set...");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CRM?",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)current_rm_protocol_ready,
                                       ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    GError *saved_error;

    MMPortSerialAt *dial_port;
    gboolean close_dial_port_on_exit;
} Dial3gppContext;

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    if (ctx->dial_port)
        g_object_unref (ctx->dial_port);
    g_object_unref (ctx->cancellable);
    g_simple_async_result_complete (ctx->result);
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
extended_error_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      Dial3gppContext *ctx)
{
    const gchar *result;

    /* Close the dialling port as we got an error */
    mm_port_serial_close (MM_PORT_SERIAL (ctx->dial_port));

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        g_simple_async_result_set_error (ctx->result,
                                         ctx->saved_error->domain,
                                         ctx->saved_error->code,
                                         "%s", &result[7]);
        g_error_free (ctx->saved_error);
    } else
        g_simple_async_result_take_error (ctx->result,
                                          ctx->saved_error);

    ctx->saved_error = NULL;

    /* Done with errors */
    dial_3gpp_context_complete_and_free (ctx);
}

static void
atd_ready (MMBaseModem *modem,
           GAsyncResult *res,
           Dial3gppContext *ctx)
{
    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &ctx->saved_error);

    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CEER",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)extended_error_ready,
                                       ctx);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               g_object_ref (ctx->dial_port),
                                               (GDestroyNotify)g_object_unref);
    dial_3gpp_context_complete_and_free (ctx);
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
    gchar *command;
    Dial3gppContext *ctx;
    GError *error = NULL;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp);
    ctx->cancellable = g_object_ref (cancellable);

    /* Grab dial port. This gets a reference to the dial port and OPENs it.
     * If we fail, we'll need to close it ourselves. */
    ctx->dial_port = common_get_at_data_port (ctx->modem, &error);
    if (!ctx->dial_port) {
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Use default *99 to connect */
    command = g_strdup_printf ("ATD*99***%d#", cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->dial_port,
                                   command,
                                   60,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)atd_ready,
                                   ctx);
    g_free (command);
}

/*****************************************************************************/
/* 3GPP cid selection (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem       *modem;
    MMPortSerialAt    *primary;
    GCancellable      *cancellable;
    guint              cid;
    guint              max_cid;
    gboolean           use_existing_cid;
    MMBearerIpFamily   ip_family;
} CidSelection3gppContext;

static void
cid_selection_3gpp_context_free (CidSelection3gppContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->cancellable);
    g_slice_free (CidSelection3gppContext, ctx);
}

static guint
cid_selection_3gpp_finish (MMBroadbandBearer  *self,
                           GAsyncResult       *res,
                           GError            **error)
{
    gssize cid;

    /* We return 0 as an invalid CID, not -1 */
    cid = g_task_propagate_int (G_TASK (res), error);
    return (guint) (cid < 0 ? 0 : cid);
}

static void
initialize_pdp_context_ready (MMBaseModem  *modem,
                              GAsyncResult *res,
                              GTask        *task)
{
    CidSelection3gppContext *ctx;
    GError                  *error = NULL;

    ctx = (CidSelection3gppContext *) g_task_get_task_data (task);
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_warn ("Couldn't initialize PDP context with our APN: '%s'", error->message);
        g_task_return_error (task, error);
    } else
        g_task_return_int (task, (gssize) ctx->cid);
    g_object_unref (task);
}

static void
find_cid_ready (MMBaseModem  *modem,
                GAsyncResult *res,
                GTask        *task)
{
    gchar                   *apn;
    gchar                   *command;
    GError                  *error = NULL;
    const gchar             *pdp_type;
    CidSelection3gppContext *ctx;

    ctx = (CidSelection3gppContext *) g_task_get_task_data (task);

    mm_base_modem_at_sequence_full_finish (modem, res, NULL, &error);
    if (error) {
        mm_warn ("Couldn't find best CID to use: '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Validate requested PDP type */
    pdp_type = mm_3gpp_get_pdp_type_from_ip_family (ctx->ip_family);
    if (!pdp_type) {
        gchar * str;

        str = mm_bearer_ip_family_build_string_from_mask (ctx->ip_family);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Unsupported IP type requested: '%s'", str);
        g_object_unref (task);
        g_free (str);
        return;
    }

    /* If no error reported, we must have a valid CID to be used */
    g_assert (ctx->cid != 0);

    /* If there's already a PDP context defined, just use it */
    if (ctx->use_existing_cid) {
        g_task_return_int (task, (gssize) ctx->cid);
        g_object_unref (task);
        return;
    }

    /* Otherwise, initialize a new PDP context with our APN */
    apn = mm_port_serial_at_quote_string (mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self))));
    command = g_strdup_printf ("+CGDCONT=%u,\"%s\",%s", ctx->cid, pdp_type, apn);
    g_free (apn);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback) initialize_pdp_context_ready,
                                   task);
    g_free (command);
}

static gboolean
parse_cid_range (MMBaseModem              *modem,
                 CidSelection3gppContext  *ctx,
                 const gchar              *command,
                 const gchar              *response,
                 gboolean                  last_command,
                 const GError             *error,
                 GVariant                **result,
                 GError                  **result_error)
{
    GError *inner_error = NULL;
    GList  *formats, *l;
    guint   cid;

    /* If cancelled, set result error */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_set_error (result_error, MM_CORE_ERROR, MM_CORE_ERROR_CANCELLED,
                     "Connection setup operation has been cancelled");
        return FALSE;
    }

    if (error) {
        mm_dbg ("Unexpected +CGDCONT error: '%s'", error->message);
        mm_dbg ("Defaulting to CID=1");
        ctx->cid = 1;
        return TRUE;
    }

    formats = mm_3gpp_parse_cgdcont_test_response (response, &inner_error);
    if (inner_error) {
        mm_dbg ("Error parsing +CGDCONT test response: '%s'", inner_error->message);
        mm_dbg ("Defaulting to CID=1");
        g_error_free (inner_error);
        ctx->cid = 1;
        return TRUE;
    }

    cid = 0;
    for (l = formats; l; l = g_list_next (l)) {
        MM3gppPdpContextFormat *format = l->data;

        /* Found exact PDP type? */
        if (format->pdp_type == ctx->ip_family) {
            if (ctx->max_cid < format->max_cid)
                cid = ctx->max_cid + 1;
            else
                cid = ctx->max_cid;
            break;
        }
    }

    mm_3gpp_pdp_context_format_list_free (formats);

    if (cid == 0) {
        mm_dbg ("Defaulting to CID=1");
        cid = 1;
    } else
        mm_dbg ("Using CID %u", cid);

    ctx->cid = cid;
    return TRUE;
}

static gboolean
parse_pdp_list (MMBaseModem             *modem,
                CidSelection3gppContext *ctx,
                const gchar             *command,
                const gchar             *response,
                gboolean                 last_command,
                const GError            *error,
                GVariant               **result,
                GError                 **result_error)
{
    GError *inner_error = NULL;
    GList *pdp_list;
    GList *l;
    guint cid;

    /* If cancelled, set result error */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_set_error (result_error, MM_CORE_ERROR, MM_CORE_ERROR_CANCELLED,
                     "Connection setup operation has been cancelled");
        return FALSE;
    }

    /* Some Android phones don't support querying existing PDP contexts,
     * but will accept setting the APN.  So if CGDCONT? isn't supported,
     * just ignore that error and hope for the best. (bgo #637327)
     */
    if (g_error_matches (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
        mm_dbg ("Querying PDP context list is unsupported");
        return FALSE;
    }

    if (error) {
        mm_dbg ("Unexpected +CGDCONT? error: '%s'", error->message);
        return FALSE;
    }

    pdp_list = mm_3gpp_parse_cgdcont_read_response (response, &inner_error);
    if (!pdp_list) {
        if (inner_error) {
            mm_dbg ("%s", inner_error->message);
            g_error_free (inner_error);
        } else {
            /* No predefined PDP contexts found */
            mm_dbg ("No PDP contexts found");
        }
        return FALSE;
    }

    cid = 0;

    /* Show all found PDP contexts in debug log */
    mm_dbg ("Found '%u' PDP contexts", g_list_length (pdp_list));
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;
        gchar *ip_family_str;

        ip_family_str = mm_bearer_ip_family_build_string_from_mask (pdp->pdp_type);
        mm_dbg ("  PDP context [cid=%u] [type='%s'] [apn='%s']",
                pdp->cid,
                ip_family_str,
                pdp->apn ? pdp->apn : "");
        g_free (ip_family_str);
    }

    /* Look for the exact PDP context we want */
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        if (pdp->pdp_type == ctx->ip_family) {
            /* PDP with no APN set? we may use that one if not exact match found */
            if (!pdp->apn || !pdp->apn[0]) {
                mm_dbg ("Found PDP context with CID %u and no APN",
                        pdp->cid);
                cid = pdp->cid;
            } else {
                const gchar *apn;

                apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
                if (apn && !g_ascii_strcasecmp (pdp->apn, apn)) {
                    gchar *ip_family_str;

                    /* Found a PDP context with the same CID and PDP type, we'll use it. */
                    ip_family_str = mm_bearer_ip_family_build_string_from_mask (pdp->pdp_type);
                    mm_dbg ("Found PDP context with CID %u and PDP type %s for APN '%s'",
                            pdp->cid, ip_family_str, pdp->apn);
                    cid = pdp->cid;
                    ctx->use_existing_cid = TRUE;
                    g_free (ip_family_str);
                    /* In this case, stop searching */
                    break;
                }
            }
        }

        if (ctx->max_cid < pdp->cid)
            ctx->max_cid = pdp->cid;
    }
    mm_3gpp_pdp_context_list_free (pdp_list);

    if (cid > 0) {
        ctx->cid = cid;
        return TRUE;
    }

    return FALSE;
}

static const MMBaseModemAtCommand find_cid_sequence[] = {
    { "+CGDCONT?",  3, FALSE, (MMBaseModemAtResponseProcessor) parse_pdp_list  },
    { "+CGDCONT=?", 3, TRUE,  (MMBaseModemAtResponseProcessor) parse_cid_range },
    { NULL }
};

static void
cid_selection_3gpp (MMBroadbandBearer   *self,
                    MMBaseModem         *modem,
                    MMPortSerialAt      *primary,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask                   *task;
    CidSelection3gppContext *ctx;

    ctx = g_slice_new0 (CidSelection3gppContext);
    ctx->self        = g_object_ref (self);
    ctx->modem       = g_object_ref (modem);
    ctx->primary     = g_object_ref (primary);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->ip_family   = select_bearer_ip_family (self);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) cid_selection_3gpp_context_free);

    mm_dbg ("Looking for best CID...");
    mm_base_modem_at_sequence_full (ctx->modem,
                                    ctx->primary,
                                    find_cid_sequence,
                                    ctx, /* also passed as response processor context */
                                    NULL, /* response_processor_context_free */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) find_cid_ready,
                                    task);
}

/*****************************************************************************/
/* 3GPP CONNECT
 *
 * 3GPP connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) Decide which PDP context to use
 *   2.1) Look for an already existing PDP context with the same APN.
 *   2.2) If none found with the same APN, try to find a PDP context without any
 *        predefined APN.
 *   2.3) If none found, look for the highest available CID, and use that one.
 * 3) Activate PDP context.
 * 4) Initiate call.
 */

static void
get_ip_config_3gpp_ready (MMBroadbandModem *modem,
                          GAsyncResult *res,
                          DetailedConnectContext *ctx)
{
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp_finish (ctx->self,
                                                                               res,
                                                                               &ipv4_config,
                                                                               &ipv6_config,
                                                                               &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Keep port open during connection */
    if (MM_IS_PORT_SERIAL_AT (ctx->data))
        ctx->close_data_on_exit = FALSE;

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        mm_bearer_connect_result_new (ctx->data, ipv4_config, ipv6_config),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    detailed_connect_context_complete_and_free (ctx);

    if (ipv4_config)
        g_object_unref (ipv4_config);
    if (ipv6_config)
        g_object_unref (ipv6_config);
}

static void
dial_3gpp_ready (MMBroadbandModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    MMBearerIpMethod ip_method = MM_BEARER_IP_METHOD_UNKNOWN;
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    GError *error = NULL;

    ctx->data = MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->dial_3gpp_finish (ctx->self, res, &error);
    if (!ctx->data) {
        /* Clear CID when it failed to connect. */
        ctx->self->priv->cid = 0;
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* If the dialling operation used an AT port, it is assumed to have an extra
     * open() count. */
    if (MM_IS_PORT_SERIAL_AT (ctx->data))
        ctx->close_data_on_exit = TRUE;

    if (MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp &&
        MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp_finish) {
        /* Launch specific IP config retrieval */
        MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp (
            ctx->self,
            MM_BROADBAND_MODEM (ctx->modem),
            ctx->primary,
            ctx->secondary,
            ctx->data,
            ctx->self->priv->cid,
            ctx->ip_family,
            (GAsyncReadyCallback)get_ip_config_3gpp_ready,
            ctx);
        return;
    }

    /* Yuhu! */

    /* Keep port open during connection */
    if (MM_IS_PORT_SERIAL_AT (ctx->data))
        ctx->close_data_on_exit = FALSE;

    /* If no specific IP retrieval requested, set the default implementation
     * (PPP if data port is AT, DHCP otherwise) */
    ip_method = MM_IS_PORT_SERIAL_AT (ctx->data) ?
                    MM_BEARER_IP_METHOD_PPP :
                    MM_BEARER_IP_METHOD_DHCP;

    if (ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4 ||
            ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
        ipv4_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ipv4_config, ip_method);
    }
    if (ctx->ip_family & MM_BEARER_IP_FAMILY_IPV6 ||
            ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
        ipv6_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ipv6_config, ip_method);
    }
    g_assert (ipv4_config || ipv6_config);

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        mm_bearer_connect_result_new (ctx->data, ipv4_config, ipv6_config),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    detailed_connect_context_complete_and_free (ctx);

    g_clear_object (&ipv4_config);
    g_clear_object (&ipv6_config);
}

static void
cid_selection_3gpp_ready (MMBroadbandModem       *modem,
                          GAsyncResult           *res,
                          DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* Keep CID around after initializing the PDP context in order to
     * handle corresponding unsolicited PDP activation responses. */
    ctx->self->priv->cid = MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->cid_selection_3gpp_finish (ctx->self, res, &error);
    if (!ctx->self->priv->cid) {
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->dial_3gpp (ctx->self,
                                                          ctx->modem,
                                                          ctx->primary,
                                                          ctx->self->priv->cid,
                                                          ctx->cancellable,
                                                          (GAsyncReadyCallback) dial_3gpp_ready,
                                                          ctx);
}

static void
connect_3gpp (MMBroadbandBearer   *self,
              MMBroadbandModem    *modem,
              MMPortSerialAt      *primary,
              MMPortSerialAt      *secondary,
              GCancellable        *cancellable,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    DetailedConnectContext *ctx;

    g_assert (primary != NULL);

    /* Clear CID on every connection attempt */
    self->priv->cid = 0;

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        secondary,
                                        cancellable,
                                        callback,
                                        user_data);

    MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->cid_selection_3gpp (ctx->self,
                                                                   ctx->modem,
                                                                   ctx->primary,
                                                                   ctx->cancellable,
                                                                   (GAsyncReadyCallback)cid_selection_3gpp_ready,
                                                                   ctx);
}

/*****************************************************************************/
/* CONNECT */

typedef struct {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
connect_succeeded (ConnectContext *ctx,
                   ConnectionType connection_type,
                   MMBearerConnectResult *result)
{
    /* Keep connected port and type of connection */
    ctx->self->priv->port = g_object_ref (mm_bearer_connect_result_peek_data (result));
    ctx->self->priv->connection_type = connection_type;

    /* Port is connected; update the state */
    mm_port_set_connected (ctx->self->priv->port, TRUE);

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               result,
                                               (GDestroyNotify)mm_bearer_connect_result_unref);
    connect_context_complete_and_free (ctx);
}

static void
connect_cdma_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    MMBearerConnectResult *result;
    GError *error = NULL;

    result = MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma_finish (self, res, &error);
    if (!result) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* take result */
    connect_succeeded (ctx, CONNECTION_TYPE_CDMA, result);
}

static void
connect_3gpp_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    MMBearerConnectResult *result;
    GError *error = NULL;

    result = MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp_finish (self, res, &error);
    if (!result) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* take result */
    connect_succeeded (ctx, CONNECTION_TYPE_3GPP, result);
}

static void
connect (MMBaseBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    MMBaseModem *modem = NULL;
    MMPortSerialAt *primary;
    ConnectContext *ctx;
    const gchar *apn;

    /* Don't try to connect if already connected */
    if (MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: this bearer is already connected");
        return;
    }

    /* Get the owner modem object */
    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We will launch the ATD call in the primary port... */
    primary = mm_base_modem_peek_port_primary (modem);
    if (!primary) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: couldn't get primary port");
        g_object_unref (modem);
        return;
    }

    /* ...only if not already connected */
    if (mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: primary AT port is already connected");
        g_object_unref (modem);
        return;
    }

    /* Default bearer connection logic
     *
     * 1) 3GPP-only modem:
     *     1a) If no APN given, error.
     *     1b) If APN given, run 3GPP connection logic.
     *     1c) If APN given, but empty (""), run 3GPP connection logic and try
     *         to use default subscription APN.
     *
     * 2) 3GPP2-only modem:
     *     2a) If no APN given, run CDMA connection logic.
     *     2b) If APN given, error.
     *
     * 3) 3GPP and 3GPP2 modem:
     *     3a) If no APN given, run CDMA connection logic.
     *     3b) If APN given, run 3GPP connection logic.
     *     1c) If APN given, but empty (""), run 3GPP connection logic and try
     *         to use default subscription APN.
     */

    /* Check whether we have an APN */
    apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

    /* Is this a 3GPP only modem and no APN was given? If so, error */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (modem)) && !apn) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "3GPP connection logic requires APN setting");
        g_object_unref (modem);
        return;
    }

    /* Is this a 3GPP2 only modem and APN was given? If so, error */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (modem)) && apn) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "3GPP2 doesn't support APN setting");
        g_object_unref (modem);
        return;
    }

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_slice_new0 (ConnectContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* If the modem has 3GPP capabilities and an APN, launch 3GPP-based connection */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem)) && apn) {
        mm_dbg ("Launching 3GPP connection attempt with APN '%s'", apn);
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            cancellable,
            (GAsyncReadyCallback) connect_3gpp_ready,
            ctx);
        g_object_unref (modem);
        return;
    }

    /* Otherwise, launch CDMA-specific connection. */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (modem)) && !apn) {
        mm_dbg ("Launching 3GPP2 connection attempt");
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            cancellable,
            (GAsyncReadyCallback) connect_cdma_ready,
            ctx);
        g_object_unref (modem);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* Detailed disconnect context, used in both CDMA and 3GPP sequences */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    MMPort *data;
    GSimpleAsyncResult *result;

    /* 3GPP-specific */
    gchar *cgact_command;
    gboolean cgact_sent;
} DetailedDisconnectContext;

static gboolean
detailed_disconnect_finish (MMBroadbandBearer *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
detailed_disconnect_context_complete_and_free (DetailedDisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cgact_command)
        g_free (ctx->cgact_command);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static DetailedDisconnectContext *
detailed_disconnect_context_new (MMBroadbandBearer *self,
                                 MMBroadbandModem *modem,
                                 MMPortSerialAt *primary,
                                 MMPortSerialAt *secondary,
                                 MMPort *data,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    ctx = g_new0 (DetailedDisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_disconnect_context_new);
    return ctx;
}

/*****************************************************************************/
/* CDMA disconnect */

static void
data_flash_cdma_ready (MMPortSerial *data,
                       GAsyncResult *res,
                       DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    mm_port_serial_flash_finish (data, res, &error);

    /* We kept the serial port open during connection, now we close that open
     * count */
    mm_port_serial_close (data);

    /* Port is disconnected; update the state */
    mm_port_set_connected (MM_PORT (data), FALSE);

    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            g_simple_async_result_take_error (ctx->result, error);
            detailed_disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    detailed_disconnect_context_complete_and_free (ctx);
}

static void
data_reopen_cdma_ready (MMPortSerial *data,
                        GAsyncResult *res,
                        DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_port_serial_reopen_finish (data, res, &error)) {
        /* Fatal */
        g_simple_async_result_take_error (ctx->result, error);
        detailed_disconnect_context_complete_and_free (ctx);
        return;
    }

    /* Just flash the data port */
    mm_dbg ("Flashing data port (%s)...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_flash (MM_PORT_SERIAL (ctx->data),
                          1000,
                          TRUE,
                          (GAsyncReadyCallback)data_flash_cdma_ready,
                          ctx);
}

static void
disconnect_cdma (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    g_assert (primary != NULL);

    /* Generic CDMA plays only with SERIAL data ports */
    g_assert (MM_IS_PORT_SERIAL (data));

    ctx = detailed_disconnect_context_new (self,
                                           modem,
                                           primary,
                                           secondary,
                                           data,
                                           callback,
                                           user_data);

    /* Fully reopen the port before flashing */
    mm_dbg ("Reopening data port (%s)...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_reopen (MM_PORT_SERIAL (ctx->data),
                           1000,
                           (GAsyncReadyCallback)data_reopen_cdma_ready,
                           ctx);
}

/*****************************************************************************/
/* 3GPP disconnect */

static void
cgact_data_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  DetailedDisconnectContext *ctx)
{

    GError *error = NULL;

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_dbg ("PDP context deactivation failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    detailed_disconnect_context_complete_and_free (ctx);
}

static void
data_flash_3gpp_ready (MMPortSerial *data,
                       GAsyncResult *res,
                       DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    mm_port_serial_flash_finish (data, res, &error);

    /* We kept the serial port open during connection, now we close that open
     * count */
    mm_port_serial_close (data);

    /* Port is disconnected; update the state */
    mm_port_set_connected (MM_PORT (data), FALSE);

    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            g_simple_async_result_take_error (ctx->result, error);
            detailed_disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    /* Don't bother doing the CGACT again if it was already done on the
     * primary or secondary port */
    if (ctx->cgact_sent) {
        mm_dbg ("PDP disconnection already sent");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        detailed_disconnect_context_complete_and_free (ctx);
        return;
    }

    /* Send another CGACT on the primary port (also the data port when the modem
     * only has one serial port) if the previous one failed.  Some modems, like
     * the Huawei E173 (fw 11.126.15.00.445) stop responding on their primary
     * port when the CGACT is sent on the separte data port.
     */
    if (MM_PORT_SERIAL (ctx->primary) == data)
        mm_dbg ("Sending PDP context deactivation in primary/data port...");
    else
        mm_dbg ("Sending PDP context deactivation in primary port again...");

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   ctx->cgact_command,
                                   10,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)cgact_data_ready,
                                   ctx);
}

static void
data_reopen_3gpp_ready (MMPortSerial *data,
                        GAsyncResult *res,
                        DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_port_serial_reopen_finish (data, res, &error)) {
        /* Fatal */
        g_simple_async_result_take_error (ctx->result, error);
        detailed_disconnect_context_complete_and_free (ctx);
        return;
    }

    /* Just flash the data port */
    mm_dbg ("Flashing data port (%s)...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_flash (MM_PORT_SERIAL (ctx->data),
                          1000,
                          TRUE,
                          (GAsyncReadyCallback)data_flash_3gpp_ready,
                          ctx);
}

static void
data_reopen_3gpp (DetailedDisconnectContext *ctx)
{
    /* Fully reopen the port before flashing */
    mm_dbg ("Reopening data port (%s)...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_reopen (MM_PORT_SERIAL (ctx->data),
                           1000,
                           (GAsyncReadyCallback)data_reopen_3gpp_ready,
                           ctx);
}

static void
cgact_ready (MMBaseModem *modem,
             GAsyncResult *res,
             DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!error)
        ctx->cgact_sent = TRUE;
    else {
        mm_dbg ("PDP context deactivation failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    data_reopen_3gpp (ctx);
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
    DetailedDisconnectContext *ctx;

    g_assert (primary != NULL);

    /* Generic 3GPP plays only with SERIAL data ports */
    g_assert (MM_IS_PORT_SERIAL (data));

    ctx = detailed_disconnect_context_new (self,
                                           modem,
                                           primary,
                                           secondary,
                                           data,
                                           callback,
                                           user_data);

    /* If no specific CID was used, disable all PDP contexts */
    ctx->cgact_command = (cid > 0 ?
                          g_strdup_printf ("+CGACT=0,%d", cid) :
                          g_strdup_printf ("+CGACT=0"));

    /* If the primary port is NOT connected (doesn't have to be the data port),
     * we'll send CGACT there */
    if (!mm_port_get_connected (MM_PORT (ctx->primary))) {
        mm_dbg ("Sending PDP context deactivation in primary port...");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       ctx->cgact_command,
                                       10,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgact_ready,
                                       ctx);
        return;
    }

    /* If the primary port is connected, then try sending the PDP
     * context deactivation on the secondary port because not all modems will
     * respond to flashing (since either the modem or the kernel's serial
     * driver doesn't support it).
     */
    if (ctx->secondary) {
        mm_dbg ("Sending PDP context deactivation in secondary port...");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->secondary,
                                       ctx->cgact_command,
                                       10,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgact_ready,
                                       ctx);
        return;
    }

    /* If no secondary port, go on to reopen & flash the data/primary port */
    data_reopen_3gpp (ctx);
}

/*****************************************************************************/
/* DISCONNECT */

typedef struct {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    MMPort *data;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
disconnect_finish (MMBaseBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
reset_bearer_connection (MMBroadbandBearer *self)
{
    if (self->priv->port) {
        /* Port is disconnected; update the state. Note: implementations may
         * already have set the port as disconnected (e.g the 3GPP one) */
        mm_port_set_connected (self->priv->port, FALSE);

        /* Clear data port */
        g_clear_object (&self->priv->port);
    }

    /* Reset current connection type */
    self->priv->connection_type = CONNECTION_TYPE_NONE;
}

static void
disconnect_succeeded (DisconnectContext *ctx)
{
    /* Cleanup all connection related data */
    reset_bearer_connection (ctx->self);

    /* Set operation result */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disconnect_context_complete_and_free (ctx);
}

static void
disconnect_failed (DisconnectContext *ctx,
                   GError *error)
{
    g_simple_async_result_take_error (ctx->result, error);
    disconnect_context_complete_and_free (ctx);
}

static void
disconnect_cdma_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma_finish (self,
                                                                       res,
                                                                       &error))
        disconnect_failed (ctx, error);
    else
        disconnect_succeeded (ctx);
}

static void
disconnect_3gpp_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp_finish (self,
                                                                       res,
                                                                       &error))
        disconnect_failed (ctx, error);
    else {
        /* Clear CID if we got any set */
        if (ctx->self->priv->cid)
            ctx->self->priv->cid = 0;
        disconnect_succeeded (ctx);
    }
}

static void
disconnect (MMBaseBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMPortSerialAt *primary;
    MMBaseModem *modem = NULL;
    DisconnectContext *ctx;

    if (!MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect: this bearer is not connected");
        return;
    }

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We need the primary port to disconnect... */
    primary = mm_base_modem_peek_port_primary (modem);
    if (!primary) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect: couldn't get primary port");
        g_object_unref (modem);
        return;
    }

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (DisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->data = g_object_ref (MM_BROADBAND_BEARER (self)->priv->port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);

    switch (MM_BROADBAND_BEARER (self)->priv->connection_type) {
    case CONNECTION_TYPE_3GPP:
            MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp (
                MM_BROADBAND_BEARER (self),
                MM_BROADBAND_MODEM (modem),
                primary,
                mm_base_modem_peek_port_secondary (modem),
                MM_BROADBAND_BEARER (self)->priv->port,
                MM_BROADBAND_BEARER (self)->priv->cid,
                (GAsyncReadyCallback) disconnect_3gpp_ready,
                ctx);
        break;

    case CONNECTION_TYPE_CDMA:
        MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            MM_BROADBAND_BEARER (self)->priv->port,
            (GAsyncReadyCallback) disconnect_cdma_ready,
            ctx);
        break;

    case CONNECTION_TYPE_NONE:
        g_assert_not_reached ();
    }

    g_object_unref (modem);
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer *self,
                          MMBearerConnectionStatus status)
{
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED)
        /* Cleanup all connection related data */
        reset_bearer_connection (MM_BROADBAND_BEARER (self));

    /* Chain up parent's report_connection_status() */
    MM_BASE_BEARER_CLASS (mm_broadband_bearer_parent_class)->report_connection_status (
        self,
        status);
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CDMA_RM_PROTOCOL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBaseModem *modem;
    InitializationStep step;
    MMPortSerialAt *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (ctx->port) {
        if (close_port)
            mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
        g_object_unref (ctx->port);
    }
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMBaseBearer *
mm_broadband_bearer_new_finish (GAsyncResult *res,
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

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
crm_range_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 InitAsyncContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        /* We should possibly take this error as fatal. If we were told to use a
         * specific Rm protocol, we must be able to check if it is supported. */
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        MMModemCdmaRmProtocol min = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
        MMModemCdmaRmProtocol max = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;

        if (mm_cdma_parse_crm_test_response (response,
                                             &min, &max,
                                             &error)) {
            MMModemCdmaRmProtocol current;

            current = mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
            /* Check if value within the range */
            if (current >= min &&
                current <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (ctx);
            }

            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 mm_modem_cdma_rm_protocol_get_string (current));
        }

        /* Failed, set as fatal as well */
        g_simple_async_result_take_error (ctx->result, error);
    }

    g_simple_async_result_complete (ctx->result);
    init_async_context_free (ctx, TRUE);
}

static void
interface_initialization_step (InitAsyncContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CDMA_RM_PROTOCOL:
        /* If a specific RM protocol is given, we need to check whether it is
         * supported. */
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->modem)) &&
            mm_bearer_properties_get_rm_protocol (
                mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self))) != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->port,
                                           "+CRM=?",
                                           3,
                                           TRUE, /* getting range, so reply can be cached */
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)crm_range_ready,
                                           ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, TRUE);
        return;
    }

    g_assert_not_reached ();
}

static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    InitAsyncContext *ctx;
    GError *error = NULL;

    ctx = g_new0 (InitAsyncContext, 1);
    ctx->self = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             initable_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);

    g_object_get (initable,
                  MM_BASE_BEARER_MODEM, &ctx->modem,
                  NULL);

    ctx->port = mm_base_modem_get_port_primary (ctx->modem);
    if (!ctx->port) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get primary port");
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

void
mm_broadband_bearer_new (MMBroadbandModem *modem,
                         MMBearerProperties *properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM,  modem,
        MM_BASE_BEARER_CONFIG, properties,
        NULL);
}

static void
mm_broadband_bearer_init (MMBroadbandBearer *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER,
                                              MMBroadbandBearerPrivate);

    /* Set defaults */
    self->priv->connection_type = CONNECTION_TYPE_NONE;
}

static void
dispose (GObject *object)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    reset_bearer_connection (self);

    G_OBJECT_CLASS (mm_broadband_bearer_parent_class)->dispose (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_broadband_bearer_class_init (MMBroadbandBearerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    base_bearer_class->connect = connect;
    base_bearer_class->connect_finish = connect_finish;
    base_bearer_class->disconnect = disconnect;
    base_bearer_class->disconnect_finish = disconnect_finish;
    base_bearer_class->report_connection_status = report_connection_status;

    klass->connect_3gpp = connect_3gpp;
    klass->connect_3gpp_finish = detailed_connect_finish;
    klass->dial_3gpp = dial_3gpp;
    klass->dial_3gpp_finish = dial_3gpp_finish;
    klass->cid_selection_3gpp = cid_selection_3gpp;
    klass->cid_selection_3gpp_finish = cid_selection_3gpp_finish;

    klass->connect_cdma = connect_cdma;
    klass->connect_cdma_finish = detailed_connect_finish;

    klass->disconnect_3gpp = disconnect_3gpp;
    klass->disconnect_3gpp_finish = detailed_disconnect_finish;
    klass->disconnect_cdma = disconnect_cdma;
    klass->disconnect_cdma_finish = detailed_disconnect_finish;
}
