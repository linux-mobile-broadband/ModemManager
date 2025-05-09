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
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-port-enums-types.h"
#include "mm-helper-enums-types.h"
#include "mm-bind.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandBearer, mm_broadband_bearer, MM_TYPE_BASE_BEARER, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

typedef enum {
    CONNECTION_TYPE_NONE,
    CONNECTION_TYPE_3GPP,
    CONNECTION_TYPE_CDMA,
} ConnectionType;

enum {
    PROP_0,
    PROP_FLOW_CONTROL,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerPrivate {
    /*-- Common stuff --*/
    /* Data port used when modem is connected */
    MMPort *port;
    /* Current connection type */
    ConnectionType connection_type;

    /* PPP specific */
    MMFlowControl flow_control;

    /*-- 3GPP specific --*/
    /* CID of the PDP context */
    gint profile_id;
};

/*****************************************************************************/
/* Detailed connect context, used in both CDMA and 3GPP sequences */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;

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
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
detailed_connect_context_free (DetailedConnectContext *ctx)
{
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    if (ctx->data) {
        if (ctx->close_data_on_exit)
            mm_port_serial_close (MM_PORT_SERIAL (ctx->data));
        g_object_unref (ctx->data);
    }
    g_object_unref (ctx->modem);
    g_slice_free (DetailedConnectContext, ctx);
}

static DetailedConnectContext *
detailed_connect_context_new (MMBroadbandBearer *self,
                              MMBroadbandModem *modem,
                              MMPortSerialAt *primary,
                              MMPortSerialAt *secondary)
{
    DetailedConnectContext *ctx;

    ctx = g_slice_new0 (DetailedConnectContext);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);

    ctx->ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    mm_3gpp_normalize_ip_family (&ctx->ip_family, TRUE);

    return ctx;
}

/*****************************************************************************/
/* Generic implementations (both 3GPP and CDMA) are always AT-port based */

static MMPortSerialAt *
common_get_at_data_port (MMBroadbandBearer  *self,
                         MMBaseModem        *modem,
                         GError            **error)
{
    MMPort *data;

    /* Look for best data port, NULL if none available. */
    data = mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_AT);
    if (!data) {
        /* It may happen that the desired data port grabbed during probing was
         * actually a 'net' port, which the generic logic cannot handle, so if
         * that is the case, and we have no AT data ports specified, just
         fallback to the primary AT port. */
        data = MM_PORT (mm_base_modem_peek_port_primary (modem));
        if (!data) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't connect: no AT data/primary port found: ");
            return NULL;
        }
    }

    g_assert (MM_IS_PORT_SERIAL_AT (data));

    if (!mm_port_serial_open (MM_PORT_SERIAL (data), error)) {
        g_prefix_error (error, "Couldn't connect: cannot keep data port open.");
        return NULL;
    }

    mm_obj_dbg (self, "connection through a plain serial AT port: %s", mm_port_get_device (data));

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
dial_cdma_ready (MMBaseModem  *modem,
                 GAsyncResult *res,
                 GTask        *task)
{
    MMBroadbandBearer      *self;
    DetailedConnectContext *ctx;
    GError                 *error = NULL;
    MMBearerIpConfig       *config;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't connect: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Configure flow control to use while connected */
    if (self->priv->flow_control != MM_FLOW_CONTROL_NONE) {
        gchar *flow_control_str;

        flow_control_str = mm_flow_control_build_string_from_mask (self->priv->flow_control);
        mm_obj_dbg (self, "setting flow control in %s: %s", mm_port_get_device (ctx->data), flow_control_str);
        g_free (flow_control_str);

        if (!mm_port_serial_set_flow_control (MM_PORT_SERIAL (ctx->data), self->priv->flow_control, &error)) {
            mm_obj_warn (self, "couldn't set flow control settings in %s: %s", mm_port_get_device (ctx->data), error->message);
            g_clear_error (&error);
        }
    }

    /* The ATD command has succeeded, and therefore the TTY is in data mode now.
     * Instead of waiting for setting the port as connected later in
     * connect_succeeded(), we do it right away so that we stop our polling. */
    mm_port_set_connected (ctx->data, TRUE);

    /* Keep port open during connection */
    ctx->close_data_on_exit = FALSE;

    /* Generic CDMA connections are done over PPP always */
    g_assert (MM_IS_PORT_SERIAL_AT (ctx->data));
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Assume only IPv4 is given */
    g_task_return_pointer (
        task,
        mm_bearer_connect_result_new (ctx->data, config, NULL),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);

    g_object_unref (config);
}

static void
cdma_connect_context_dial (GTask *task)
{
    DetailedConnectContext *ctx;

    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_full (ctx->modem,
                                   MM_IFACE_PORT_AT (ctx->data),
                                   "DT#777",
                                   MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)dial_cdma_ready,
                                   task);
}

static void
set_rm_protocol_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       GTask *task)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't set RM protocol: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (task);
}

static void
current_rm_protocol_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    const gchar *result;
    GError *error = NULL;
    guint current_index;
    MMModemCdmaRmProtocol current_rm;

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    result = mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't query current RM protocol: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    result = mm_strip_tag (result, "+CRM:");
    current_index = (guint) atoi (result);
    current_rm = mm_cdma_get_rm_protocol_from_index (current_index, &error);
    if (error) {
        mm_obj_warn (self, "couldn't parse RM protocol reply (%s): %s", result, error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (current_rm != mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (self)))) {
        DetailedConnectContext *ctx;
        guint new_index;
        gchar *command;

        mm_obj_dbg (self, "setting requested RM protocol...");

        new_index = (mm_cdma_get_index_from_rm_protocol (
                         mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (self))),
                         &error));
        if (error) {
            mm_obj_warn (self, "cannot set RM protocol: %s", error->message);
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        ctx = g_task_get_task_data (task);
        command = g_strdup_printf ("+CRM=%u", new_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       MM_IFACE_PORT_AT (ctx->primary),
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)set_rm_protocol_ready,
                                       task);
        g_free (command);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (task);
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
    GTask *task;
    GError *error = NULL;

    g_assert (primary != NULL);

    ctx = detailed_connect_context_new (self, modem, primary, NULL);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_connect_context_free);

    /* Grab dial port. This gets a reference to the dial port and OPENs it.
     * If we fail, we'll need to close it ourselves. */
    ctx->data = MM_PORT (common_get_at_data_port (self, ctx->modem, &error));
    if (!ctx->data) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    ctx->close_data_on_exit = TRUE;

    if (mm_bearer_properties_get_rm_protocol (
            mm_base_bearer_peek_config (MM_BASE_BEARER (self))) !=
        MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        /* Need to query current RM protocol */
        mm_obj_dbg (self, "querying current RM protocol set...");
        mm_base_modem_at_command_full (ctx->modem,
                                       MM_IFACE_PORT_AT (ctx->primary),
                                       "+CRM?",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)current_rm_protocol_ready,
                                       task);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (task);
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    GError *saved_error;
    MMPortSerialAt *dial_port;
    gboolean close_dial_port_on_exit;
} Dial3gppContext;

static void
dial_3gpp_context_free (Dial3gppContext *ctx)
{
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    if (ctx->dial_port)
        g_object_unref (ctx->dial_port);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (Dial3gppContext, ctx);
}

static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
extended_error_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      GTask *task)
{
    Dial3gppContext *ctx;
    const gchar *result;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    /* Close the dialling port as we got an error */
    mm_port_serial_close (MM_PORT_SERIAL (ctx->dial_port));

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    result = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        error = g_error_new (ctx->saved_error->domain,
                             ctx->saved_error->code,
                             "%s",
                             &result[7]);
        g_error_free (ctx->saved_error);
    } else
        g_propagate_error (&error, ctx->saved_error);

    ctx->saved_error = NULL;

    /* Done with errors */
    g_task_return_error (task, error);
    g_object_unref (task);
}

static void
atd_ready (MMBaseModem *modem,
           GAsyncResult *res,
           GTask *task)
{
    MMBroadbandBearer *self;
    Dial3gppContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &ctx->saved_error);

    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_full (ctx->modem,
                                       MM_IFACE_PORT_AT (ctx->primary),
                                       "+CEER",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)extended_error_ready,
                                       task);
        return;
    }

    /* Configure flow control to use while connected */
    if (self->priv->flow_control != MM_FLOW_CONTROL_NONE) {
        gchar *flow_control_str;
        GError *error = NULL;

        flow_control_str = mm_flow_control_build_string_from_mask (self->priv->flow_control);
        mm_obj_dbg (self, "setting flow control in %s: %s", mm_port_get_device (MM_PORT (ctx->dial_port)), flow_control_str);
        g_free (flow_control_str);

        if (!mm_port_serial_set_flow_control (MM_PORT_SERIAL (ctx->dial_port), self->priv->flow_control, &error)) {
            mm_obj_warn (self, "couldn't set flow control settings in %s: %s", mm_port_get_device (MM_PORT (ctx->dial_port)), error->message);
            g_clear_error (&error);
        }
    }

    /* The ATD command has succeeded, and therefore the TTY is in data mode now.
     * Instead of waiting for setting the port as connected later in
     * connect_succeeded(), we do it right away so that we stop our polling. */
    mm_port_set_connected (MM_PORT (ctx->dial_port), TRUE);

    g_task_return_pointer (task,
                           g_object_ref (ctx->dial_port),
                           g_object_unref);
    g_object_unref (task);
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
    GTask *task;
    GError *error = NULL;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)dial_3gpp_context_free);

    /* Grab dial port. This gets a reference to the dial port and OPENs it.
     * If we fail, we'll need to close it ourselves. */
    ctx->dial_port = common_get_at_data_port (self, ctx->modem, &error);
    if (!ctx->dial_port) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Use default *99 to connect */
    command = g_strdup_printf ("ATD*99***%d#", cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   MM_IFACE_PORT_AT (ctx->dial_port),
                                   command,
                                   MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)atd_ready,
                                   task);
    g_free (command);
}

/*****************************************************************************/
/* 3GPP cid selection (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBaseModem  *modem;
    GCancellable *cancellable;
    gint          profile_id;
} SelectProfile3gppContext;

static void
select_profile_3gpp_context_free (SelectProfile3gppContext *ctx)
{
    g_object_unref (ctx->modem);
    g_object_unref (ctx->cancellable);
    g_slice_free (SelectProfile3gppContext, ctx);
}

static gint
select_profile_3gpp_finish (MMBroadbandBearer  *self,
                            GAsyncResult       *res,
                            GError            **error)
{
    gint profile_id;

    /* returns -1 on failure */
    profile_id = (gint) g_task_propagate_int (G_TASK (res), error);
    return (profile_id < 0) ? MM_3GPP_PROFILE_ID_UNKNOWN : profile_id;
}

static void
select_profile_3gpp_set_profile_ready (MMIfaceModem3gppProfileManager *modem,
                                       GAsyncResult                   *res,
                                       GTask                          *task)
{
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;

    profile = mm_iface_modem_3gpp_profile_manager_set_profile_finish (modem, res, &error);
    if (!profile)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, mm_3gpp_profile_get_profile_id (profile));
    g_object_unref (task);
}

static void
select_profile_3gpp_get_profile_ready (MMIfaceModem3gppProfileManager *modem,
                                       GAsyncResult                   *res,
                                       GTask                          *task)
{
    SelectProfile3gppContext *ctx;
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;

    ctx = g_task_get_task_data (task);

    profile = mm_iface_modem_3gpp_profile_manager_get_profile_finish (modem, res, &error);
    if (!profile)
        g_task_return_error (task, error);
    else if (!mm_3gpp_profile_get_enabled (profile)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Profile '%d' is internally disabled",
                                 ctx->profile_id);
    } else
        g_task_return_int (task, ctx->profile_id);
    g_object_unref (task);
}

static void
select_profile_3gpp (MMBroadbandBearer   *self,
                     MMBaseModem         *modem,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    GTask                    *task;
    SelectProfile3gppContext *ctx;
    MMBearerProperties       *bearer_properties;

    task = g_task_new (self, cancellable, callback, user_data);

    ctx = g_slice_new0 (SelectProfile3gppContext);
    ctx->modem       = g_object_ref (modem);
    ctx->cancellable = g_object_ref (cancellable);

    bearer_properties = mm_base_bearer_peek_config (MM_BASE_BEARER (self));
    ctx->profile_id = mm_bearer_properties_get_profile_id (bearer_properties);

    g_task_set_task_data (task, ctx, (GDestroyNotify)select_profile_3gpp_context_free);

    if (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
        mm_iface_modem_3gpp_profile_manager_set_profile (
            MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (modem),
            mm_bearer_properties_peek_3gpp_profile (bearer_properties),
            "profile-id",
            FALSE, /* not strict! */
            (GAsyncReadyCallback)select_profile_3gpp_set_profile_ready,
            task);
        return;
    }

    mm_iface_modem_3gpp_profile_manager_get_profile (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (modem),
        ctx->profile_id,
        (GAsyncReadyCallback)select_profile_3gpp_get_profile_ready,
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
get_ip_config_3gpp_ready (MMBroadbandBearer *self,
                          GAsyncResult      *res,
                          GTask             *task)
{
    DetailedConnectContext           *ctx;
    GError                           *error = NULL;
    g_autoptr(MMBearerConnectResult)  result = NULL;
    g_autoptr(MMBearerIpConfig)       ipv4_config = NULL;
    g_autoptr(MMBearerIpConfig)       ipv6_config = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->get_ip_config_3gpp_finish (self, res,
                                                                          &ipv4_config, &ipv6_config,
                                                                          &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep port open during connection */
    if (MM_IS_PORT_SERIAL_AT (ctx->data))
        ctx->close_data_on_exit = FALSE;

    result = mm_bearer_connect_result_new (ctx->data, ipv4_config, ipv6_config);
    mm_bearer_connect_result_set_profile_id (result, self->priv->profile_id);

    g_task_return_pointer (task, g_steal_pointer (&result), (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);
}

static void
dial_3gpp_ready (MMBroadbandBearer *self,
                 GAsyncResult      *res,
                 GTask             *task)
{
    DetailedConnectContext           *ctx;
    MMBearerIpMethod                  ip_method = MM_BEARER_IP_METHOD_UNKNOWN;
    GError                           *error = NULL;
    g_autoptr(MMBearerConnectResult)  result = NULL;
    g_autoptr(MMBearerIpConfig)       ipv4_config = NULL;
    g_autoptr(MMBearerIpConfig)       ipv6_config = NULL;

    ctx = g_task_get_task_data (task);

    ctx->data = MM_BROADBAND_BEARER_GET_CLASS (self)->dial_3gpp_finish (self, res, &error);
    if (!ctx->data) {
        /* Clear CID when it failed to connect. */
        self->priv->profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If the dialling operation used an AT port, it is assumed to have an extra
     * open() count. */
    if (MM_IS_PORT_SERIAL_AT (ctx->data))
        ctx->close_data_on_exit = TRUE;

    if (MM_BROADBAND_BEARER_GET_CLASS (self)->get_ip_config_3gpp &&
        MM_BROADBAND_BEARER_GET_CLASS (self)->get_ip_config_3gpp_finish) {
        /* Launch specific IP config retrieval */
        MM_BROADBAND_BEARER_GET_CLASS (self)->get_ip_config_3gpp (
            self,
            MM_BROADBAND_MODEM (ctx->modem),
            ctx->primary,
            ctx->secondary,
            ctx->data,
            (guint)self->priv->profile_id,
            ctx->ip_family,
            (GAsyncReadyCallback)get_ip_config_3gpp_ready,
            task);
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

    result = mm_bearer_connect_result_new (ctx->data, ipv4_config, ipv6_config);
    mm_bearer_connect_result_set_profile_id (result, self->priv->profile_id);
    g_task_return_pointer (task, g_steal_pointer (&result), (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);
}

static void
select_profile_3gpp_ready (MMBroadbandBearer *self,
                           GAsyncResult      *res,
                           GTask             *task)
{
    DetailedConnectContext *ctx;
    GError                 *error = NULL;

    ctx = g_task_get_task_data (task);

    /* Keep CID around after initializing the PDP context in order to
     * handle corresponding unsolicited PDP activation responses. */
    self->priv->profile_id = select_profile_3gpp_finish (self, res, &error);
    if (self->priv->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    MM_BROADBAND_BEARER_GET_CLASS (self)->dial_3gpp (self,
                                                     ctx->modem,
                                                     ctx->primary,
                                                     (guint) self->priv->profile_id,
                                                     g_task_get_cancellable (task),
                                                     (GAsyncReadyCallback) dial_3gpp_ready,
                                                     task);
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
    GTask *task;

    g_assert (primary != NULL);

    /* Clear CID on every connection attempt */
    self->priv->profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;

    ctx = detailed_connect_context_new (self, modem, primary, secondary);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_connect_context_free);

    select_profile_3gpp (self,
                         ctx->modem,
                         cancellable,
                         (GAsyncReadyCallback)select_profile_3gpp_ready,
                         task);
}

/*****************************************************************************/
/* CONNECT */

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
connect_succeeded (GTask *task,
                   ConnectionType connection_type,
                   MMBearerConnectResult *result)
{
    MMBroadbandBearer *self;

    self = g_task_get_source_object (task);

    /* Keep connected port and type of connection */
    self->priv->port = g_object_ref (mm_bearer_connect_result_peek_data (result));
    self->priv->connection_type = connection_type;

    /* Save profile ID for 3GPP connections. This is required for cases where
     * the standard profile selection of this class is not executed during
     * connection, e.g. if a derived class implements their own connect_3gpp. */
    self->priv->profile_id = mm_bearer_connect_result_get_profile_id (result);

    /* Port is connected; update the state. For ATD based connections, the port
     * may already be set as connected, but no big deal. */
    mm_port_set_connected (self->priv->port, TRUE);

    /* Set operation result */
    g_task_return_pointer (task,
                           result,
                           (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);
}

static void
connect_cdma_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    GTask *task)
{
    MMBearerConnectResult *result;
    GError *error = NULL;

    result = MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma_finish (self, res, &error);
    if (!result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* take result */
    connect_succeeded (task, CONNECTION_TYPE_CDMA, result);
}

static void
connect_3gpp_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    GTask *task)
{
    MMBearerConnectResult *result;
    GError *error = NULL;

    result = MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp_finish (self, res, &error);
    if (!result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* take result */
    connect_succeeded (task, CONNECTION_TYPE_3GPP, result);
}

static void
connect (MMBaseBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    MMPortSerialAt           *primary;
    const gchar              *apn;
    gint                      profile_id;
    MMBearerMultiplexSupport  multiplex;
    GTask                    *task;
    g_autoptr(MMBaseModem)    modem = NULL;

    task = g_task_new (self, cancellable, callback, user_data);

    /* Don't try to connect if already connected */
    if (MM_BROADBAND_BEARER (self)->priv->port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_CONNECTED,
                                 "Couldn't connect: this bearer is already connected");
        g_object_unref (task);
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
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_CONNECTED,
                                 "Couldn't connect: couldn't get primary port");
        g_object_unref (task);
        return;
    }

    /* ...only if not already connected */
    if (mm_port_get_connected (MM_PORT (primary))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_CONNECTED,
                                 "Couldn't connect: primary AT port is already connected");
        g_object_unref (task);
        return;
    }

    /* Default bearer connection logic
     *
     * 1) 3GPP-only modem:
     *     1a) If no profile id or APN given, error.
     *     1b) If APN or profile id given, run 3GPP connection logic.
     *     1c) If APN given, but empty (""), run 3GPP connection logic and try
     *         to use default subscription APN.
     *
     * 2) 3GPP2-only modem:
     *     2a) If no APN or profile id given, run CDMA connection logic.
     *     2b) If APN or profile id given, error.
     *
     * 3) 3GPP and 3GPP2 modem:
     *     3a) If no profile id or APN given, run CDMA connection logic.
     *     3b) If APN given, run 3GPP connection logic.
     *     3c) If APN given, but empty (""), run 3GPP connection logic and try
     *         to use default subscription APN.
     */

    /* Check whether we have an APN */
    apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    profile_id = mm_bearer_properties_get_profile_id (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

    /* Is this a 3GPP only modem and no APN or profile id was given? If so, error */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (modem)) &&
        !apn && (profile_id == MM_3GPP_PROFILE_ID_UNKNOWN)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "3GPP connection logic requires APN or profile id setting");
        g_object_unref (task);
        return;
    }

    /* Is this a 3GPP2 only modem and APN or profile id was given? If so, error */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (modem)) &&
        (apn || (profile_id != MM_3GPP_PROFILE_ID_UNKNOWN))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "3GPP2 doesn't support APN or profile id setting");
        g_object_unref (task);
        return;
    }

    /* If no multiplex setting given by the user, assume none (which is the only
     * supported mode anyway)  */
    multiplex = mm_bearer_properties_get_multiplex (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN)
        multiplex = MM_BEARER_MULTIPLEX_SUPPORT_NONE;

    /* The generic broadband bearer doesn't support multiplexing */
    if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Multiplexing required but not supported");
        g_object_unref (task);
        return;
    }

    /* If the modem has 3GPP capabilities and an APN, launch 3GPP-based connection */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem)) &&
        (apn || (profile_id != MM_3GPP_PROFILE_ID_UNKNOWN))) {
        mm_obj_dbg (self, "launching 3GPP connection attempt");
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            cancellable,
            (GAsyncReadyCallback) connect_3gpp_ready,
            task);
        return;
    }

    /* Otherwise, launch CDMA-specific connection. */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (modem)) &&
        !apn && (profile_id == MM_3GPP_PROFILE_ID_UNKNOWN)) {
        mm_obj_dbg (self, "launching 3GPP2 connection attempt");
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            cancellable,
            (GAsyncReadyCallback) connect_cdma_ready,
            task);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* Detailed disconnect context, used in both CDMA and 3GPP sequences */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    MMPort *data;

    /* 3GPP-specific */
    gchar *cgact_command;
    gboolean cgact_sent;
} DetailedDisconnectContext;

static gboolean
detailed_disconnect_finish (MMBroadbandBearer *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
detailed_disconnect_context_free (DetailedDisconnectContext *ctx)
{
    g_free (ctx->cgact_command);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static DetailedDisconnectContext *
detailed_disconnect_context_new (MMBroadbandModem *modem,
                                 MMPortSerialAt *primary,
                                 MMPortSerialAt *secondary,
                                 MMPort *data)
{
    DetailedDisconnectContext *ctx;

    ctx = g_new0 (DetailedDisconnectContext, 1);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->data = g_object_ref (data);

    return ctx;
}

/*****************************************************************************/
/* CDMA disconnect */

static void
data_flash_cdma_ready (MMPortSerial *data,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMBroadbandBearer *self;
    GError            *error = NULL;

    self = g_task_get_source_object (task);

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
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "port flashing failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    /* Run init port sequence in the data port */
    mm_port_serial_at_run_init_sequence (MM_PORT_SERIAL_AT (data));

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
data_reopen_cdma_ready (MMPortSerial *data,
                        GAsyncResult *res,
                        GTask *task)
{
    MMBroadbandBearer         *self;
    DetailedDisconnectContext *ctx;
    GError                    *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_object_set (data, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, TRUE, NULL);

    if (!mm_port_serial_reopen_finish (data, res, &error)) {
        /* Fatal */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Just flash the data port */
    mm_obj_dbg (self, "flashing data port %s...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_flash (MM_PORT_SERIAL (ctx->data),
                          1000,
                          TRUE,
                          (GAsyncReadyCallback)data_flash_cdma_ready,
                          task);
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
    GTask *task;

    g_assert (primary != NULL);

    /* Generic CDMA plays only with SERIAL data ports */
    g_assert (MM_IS_PORT_SERIAL (data));

    ctx = detailed_disconnect_context_new (modem, primary, secondary, data);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_disconnect_context_free);

    /* We don't want to run init sequence right away during the reopen, as we're
     * going to flash afterwards. */
    g_object_set (data, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, FALSE, NULL);

    /* Fully reopen the port before flashing */
    mm_obj_dbg (self, "reopening data port %s...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_reopen (MM_PORT_SERIAL (ctx->data),
                           1000,
                           (GAsyncReadyCallback)data_reopen_cdma_ready,
                           task);
}

/*****************************************************************************/
/* 3GPP disconnect */

static void
cgact_data_ready (MMBaseModem  *modem,
                  GAsyncResult *res,
                  GTask        *task)
{
    MMBroadbandBearer *self;
    GError            *error = NULL;

    self = g_task_get_source_object (task);

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_obj_dbg (self, "PDP context deactivation failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
data_flash_3gpp_ready (MMPortSerial *data,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMBroadbandBearer         *self;
    DetailedDisconnectContext *ctx;
    GError                    *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

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
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "port flashing failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    /* Run init port sequence in the data port */
    mm_port_serial_at_run_init_sequence (MM_PORT_SERIAL_AT (data));

    /* Don't bother doing the CGACT again if it was already done on the
     * primary or secondary port */
    if (ctx->cgact_sent) {
        mm_obj_dbg (self, "PDP disconnection already sent");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Send another CGACT on the primary port (also the data port when the modem
     * only has one serial port) if the previous one failed.  Some modems, like
     * the Huawei E173 (fw 11.126.15.00.445) stop responding on their primary
     * port when the CGACT is sent on the separate data port.
     */
    if (MM_PORT_SERIAL (ctx->primary) == data)
        mm_obj_dbg (self, "sending PDP context deactivation in primary/data port...");
    else
        mm_obj_dbg (self, "sending PDP context deactivation in primary port again...");

    mm_base_modem_at_command_full (ctx->modem,
                                   MM_IFACE_PORT_AT (ctx->primary),
                                   ctx->cgact_command,
                                   10,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)cgact_data_ready,
                                   task);
}

static void
data_reopen_3gpp_ready (MMPortSerial *data,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMBroadbandBearer         *self;
    DetailedDisconnectContext *ctx;
    GError                    *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_object_set (data, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, TRUE, NULL);

    if (!mm_port_serial_reopen_finish (data, res, &error)) {
        /* Fatal */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Just flash the data port */
    mm_obj_dbg (self, "flashing data port %s...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_flash (MM_PORT_SERIAL (ctx->data),
                          1000,
                          TRUE,
                          (GAsyncReadyCallback)data_flash_3gpp_ready,
                          task);
}

static void
data_reopen_3gpp (GTask *task)
{
    MMBroadbandBearer         *self;
    DetailedDisconnectContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* We don't want to run init sequence right away during the reopen, as we're
     * going to flash afterwards. */
    g_object_set (ctx->data, MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED, FALSE, NULL);

    /* Fully reopen the port before flashing */
    mm_obj_dbg (self, "reopening data port %s...", mm_port_get_device (MM_PORT (ctx->data)));
    mm_port_serial_reopen (MM_PORT_SERIAL (ctx->data),
                           1000,
                           (GAsyncReadyCallback)data_reopen_3gpp_ready,
                           task);
}

static void
cgact_ready (MMBaseModem  *modem,
             GAsyncResult *res,
             GTask        *task)
{
    MMBroadbandBearer         *self;
    DetailedDisconnectContext *ctx;
    GError                    *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!error)
        ctx->cgact_sent = TRUE;
    else {
        mm_obj_dbg (self, "PDP context deactivation failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    data_reopen_3gpp (task);
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
    GTask *task;

    g_assert (primary != NULL);

    /* Generic 3GPP plays only with SERIAL data ports */
    g_assert (MM_IS_PORT_SERIAL (data));

    ctx = detailed_disconnect_context_new (modem, primary, secondary, data);

    /* If no specific CID was used, disable all PDP contexts */
    ctx->cgact_command = (cid > 0 ?
                          g_strdup_printf ("+CGACT=0,%d", cid) :
                          g_strdup_printf ("+CGACT=0"));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_disconnect_context_free);

    /* If the primary port is NOT connected (doesn't have to be the data port),
     * we'll send CGACT there */
    if (!mm_port_get_connected (MM_PORT (ctx->primary))) {
        mm_obj_dbg (self, "sending PDP context deactivation in primary port...");
        mm_base_modem_at_command_full (ctx->modem,
                                       MM_IFACE_PORT_AT (ctx->primary),
                                       ctx->cgact_command,
                                       45,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgact_ready,
                                       task);
        return;
    }

    /* If the primary port is connected, then try sending the PDP
     * context deactivation on the secondary port because not all modems will
     * respond to flashing (since either the modem or the kernel's serial
     * driver doesn't support it).
     */
    if (ctx->secondary) {
        mm_obj_dbg (self, "sending PDP context deactivation in secondary port...");
        mm_base_modem_at_command_full (ctx->modem,
                                       MM_IFACE_PORT_AT (ctx->secondary),
                                       ctx->cgact_command,
                                       45,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgact_ready,
                                       task);
        return;
    }

    /* If no secondary port, go on to reopen & flash the data/primary port */
    data_reopen_3gpp (task);
}

/*****************************************************************************/
/* DISCONNECT */

static gboolean
disconnect_finish (MMBaseBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
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
disconnect_cdma_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       GTask *task)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma_finish (self,
                                                                       res,
                                                                       &error))
        g_task_return_error (task, error);
    else {
        /* Cleanup all connection related data */
        reset_bearer_connection (self);

        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
disconnect_3gpp_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       GTask *task)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp_finish (self,
                                                                       res,
                                                                       &error))
        g_task_return_error (task, error);
    else {
        /* Clear CID if we got any set */
        self->priv->profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;

        /* Cleanup all connection related data */
        reset_bearer_connection (self);

        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
disconnect (MMBaseBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMPortSerialAt *primary;
    MMBaseModem *modem = NULL;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!MM_BROADBAND_BEARER (self)->priv->port) {
        mm_obj_dbg (self, "no need to disconnect: bearer is already disconnected");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We need the primary port to disconnect... */
    primary = mm_base_modem_peek_port_primary (modem);
    if (!primary) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't disconnect: couldn't get primary port");
        g_object_unref (task);
        g_object_unref (modem);
        return;
    }

    switch (MM_BROADBAND_BEARER (self)->priv->connection_type) {
    case CONNECTION_TYPE_3GPP:
            MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp (
                MM_BROADBAND_BEARER (self),
                MM_BROADBAND_MODEM (modem),
                primary,
                mm_base_modem_peek_port_secondary (modem),
                MM_BROADBAND_BEARER (self)->priv->port,
                (guint)MM_BROADBAND_BEARER (self)->priv->profile_id,
                (GAsyncReadyCallback) disconnect_3gpp_ready,
                task);
        break;

    case CONNECTION_TYPE_CDMA:
        MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            MM_BROADBAND_BEARER (self)->priv->port,
            (GAsyncReadyCallback) disconnect_cdma_ready,
            task);
        break;

    case CONNECTION_TYPE_NONE:
    default:
        g_assert_not_reached ();
    }

    g_object_unref (modem);
}

/*****************************************************************************/
/* Connection status monitoring */

static MMBearerConnectionStatus
load_connection_status_finish (MMBaseBearer  *bearer,
                               GAsyncResult  *res,
                               GError       **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_CONNECTION_STATUS_UNKNOWN;
    }
    return (MMBearerConnectionStatus)value;
}

static void
cgact_periodic_query_ready (MMBaseModem  *modem,
                            GAsyncResult *res,
                            GTask        *task)
{
    MMBroadbandBearer        *self;
    const gchar              *response;
    GError                   *error = NULL;
    GList                    *pdp_active_list = NULL;
    GList                    *l;
    MMBearerConnectionStatus  status = MM_BEARER_CONNECTION_STATUS_UNKNOWN;

    self = MM_BROADBAND_BEARER (g_task_get_source_object (task));

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (response)
        pdp_active_list = mm_3gpp_parse_cgact_read_response (response, &error);

    if (error) {
        g_assert (!pdp_active_list);
        g_prefix_error (&error, "Couldn't check current list of active PDP contexts: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (l = pdp_active_list; l; l = g_list_next (l)) {
        MM3gppPdpContextActive *pdp_active;

        /* Just assume the first active PDP context found is the one we're looking for. */
        pdp_active = (MM3gppPdpContextActive *)(l->data);
        if (pdp_active->cid == (guint)self->priv->profile_id) {
            status = (pdp_active->active ? MM_BEARER_CONNECTION_STATUS_CONNECTED : MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
            break;
        }
    }
    mm_3gpp_pdp_context_active_list_free (pdp_active_list);

    /* PDP context not found? This shouldn't happen, error out */
    if (status == MM_BEARER_CONNECTION_STATUS_UNKNOWN)
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "PDP context not found in the known contexts list");
    else
        g_task_return_int (task, (gssize) status);
    g_object_unref (task);
}

static void
load_connection_status (MMBaseBearer        *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GTask                  *task;
    g_autoptr(MMBaseModem)  modem = NULL;
    MMIfacePortAt          *port;

    task = g_task_new (self, NULL, callback, user_data);

    g_object_get (MM_BASE_BEARER (self),
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);

    /* No connection status checks on CDMA-only */
    if (MM_BROADBAND_BEARER (self)->priv->connection_type == CONNECTION_TYPE_CDMA) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Couldn't load connection status: unsupported in CDMA");
        g_object_unref (task);
        return;
    }

    /* If CID not defined, error out */
    if (MM_BROADBAND_BEARER (self)->priv->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't load connection status: cid not defined");
        g_object_unref (task);
        return;
    }

    /* If no control port available, error out */
    port = mm_base_modem_peek_best_at_port (modem, NULL);
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Couldn't load connection status: no control port available");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                   port,
                                   "+CGACT?",
                                   3,
                                   FALSE, /* allow cached */
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback) cgact_periodic_query_ready,
                                   task);
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer             *self,
                          MMBearerConnectionStatus  status,
                          const GError             *connection_error)
{
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED)
        /* Cleanup all connection related data */
        reset_bearer_connection (MM_BROADBAND_BEARER (self));

    /* Chain up parent's report_connection_status() */
    MM_BASE_BEARER_CLASS (mm_broadband_bearer_parent_class)->report_connection_status (
        self,
        status,
        connection_error);
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CDMA_RM_PROTOCOL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    MMBaseModem *modem;
    InitializationStep step;
    MMPortSerialAt *primary;
};

static void
init_async_context_free (InitAsyncContext *ctx)
{
    if (ctx->primary) {
        mm_port_serial_close (MM_PORT_SERIAL (ctx->primary));
        g_object_unref (ctx->primary);
    }
    g_object_unref (ctx->modem);
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
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
crm_range_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 GTask *task)
{
    MMBroadbandModem *self;
    InitAsyncContext *ctx;
    GError *error = NULL;
    const gchar *response;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        /* We should possibly take this error as fatal. If we were told to use a
         * specific Rm protocol, we must be able to check if it is supported. */
    } else {
        MMModemCdmaRmProtocol min = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
        MMModemCdmaRmProtocol max = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;

        if (mm_cdma_parse_crm_test_response (response,
                                             &min, &max,
                                             &error)) {
            MMModemCdmaRmProtocol current;

            current = mm_bearer_properties_get_rm_protocol (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
            /* Check if value within the range */
            if (current >= min &&
                current <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (task);
                return;
            }

            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 mm_modem_cdma_rm_protocol_get_string (current));
        }
        /* Failed, set as fatal as well */
    }

    g_task_return_error (task, error);
    g_object_unref (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMBroadbandModem *self;
    InitAsyncContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_CDMA_RM_PROTOCOL:
        /* If a specific RM protocol is given, we need to check whether it is
         * supported. */
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->modem)) &&
            mm_bearer_properties_get_rm_protocol (
                mm_base_bearer_peek_config (MM_BASE_BEARER (self))) != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
            mm_base_modem_at_command_full (ctx->modem,
                                           MM_IFACE_PORT_AT (ctx->primary),
                                           "+CRM=?",
                                           3,
                                           TRUE, /* getting range, so reply can be cached */
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)crm_range_ready,
                                           task);
            return;
        }

        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
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
    GTask *task;
    GError *error = NULL;

    ctx = g_new0 (InitAsyncContext, 1);
    g_object_get (initable,
                  MM_BASE_BEARER_MODEM, &ctx->modem,
                  NULL);

    task = g_task_new (MM_BROADBAND_BEARER (initable),
                       cancellable,
                       callback,
                       user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)init_async_context_free);

    ctx->primary = mm_base_modem_get_port_primary (ctx->modem);
    if (!ctx->primary) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get primary port");
        g_object_unref (task);
        return;
    }

    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->primary), &error)) {
        g_clear_object (&ctx->primary);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    interface_initialization_step (task);
}

void
mm_broadband_bearer_new (MMBroadbandModem *modem,
                         MMBearerProperties *bearer_properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMFlowControl flow_control;

    /* Inherit flow control from modem object directly */
    g_object_get (modem,
                  MM_BROADBAND_MODEM_FLOW_CONTROL, &flow_control,
                  NULL);

    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM,             modem,
        MM_BIND_TO,                       G_OBJECT (modem),
        MM_BASE_BEARER_CONFIG,            bearer_properties,
        MM_BROADBAND_BEARER_FLOW_CONTROL, flow_control,
        NULL);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    switch (prop_id) {
    case PROP_FLOW_CONTROL:
        self->priv->flow_control = g_value_get_flags (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    switch (prop_id) {
    case PROP_FLOW_CONTROL:
        g_value_set_flags (value, self->priv->flow_control);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
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
    self->priv->flow_control    = MM_FLOW_CONTROL_NONE;
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

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose      = dispose;

    base_bearer_class->connect = connect;
    base_bearer_class->connect_finish = connect_finish;
    base_bearer_class->disconnect = disconnect;
    base_bearer_class->disconnect_finish = disconnect_finish;
    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->load_connection_status = load_connection_status;
    base_bearer_class->load_connection_status_finish = load_connection_status_finish;
#if defined WITH_SUSPEND_RESUME
    base_bearer_class->reload_connection_status = load_connection_status;
    base_bearer_class->reload_connection_status_finish = load_connection_status_finish;
#endif

    klass->connect_3gpp = connect_3gpp;
    klass->connect_3gpp_finish = detailed_connect_finish;
    klass->dial_3gpp = dial_3gpp;
    klass->dial_3gpp_finish = dial_3gpp_finish;

    klass->connect_cdma = connect_cdma;
    klass->connect_cdma_finish = detailed_connect_finish;

    klass->disconnect_3gpp = disconnect_3gpp;
    klass->disconnect_3gpp_finish = detailed_disconnect_finish;
    klass->disconnect_cdma = disconnect_cdma;
    klass->disconnect_cdma_finish = detailed_disconnect_finish;

    properties[PROP_FLOW_CONTROL] =
        g_param_spec_flags (MM_BROADBAND_BEARER_FLOW_CONTROL,
                            "Flow control",
                            "Flow control settings to use during connection",
                            MM_TYPE_FLOW_CONTROL,
                            MM_FLOW_CONTROL_NONE,
                            G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_FLOW_CONTROL, properties[PROP_FLOW_CONTROL]);
}
