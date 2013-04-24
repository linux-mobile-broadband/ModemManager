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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-time.h"
#include "mm-base-modem-at.h"
#include "mm-bearer-list.h"
#include "mm-broadband-bearer-icera.h"
#include "mm-broadband-modem-icera.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemIcera, mm_broadband_modem_icera, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init));

enum {
    PROP_0,
    PROP_DEFAULT_IP_METHOD,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandModemIceraPrivate {
    MMBearerIpMethod default_ip_method;

    GRegex *nwstate_regex;
    GRegex *pacsp_regex;
    GRegex *ipdpact_regex;

    /* Cache of the most recent value seen by the unsolicited message handler */
    MMModemAccessTechnology last_act;
};

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
modem_load_allowed_modes_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemMode *allowed,
                                 MMModemMode *preferred,
                                 GError **error)
{
    const gchar *response;
    const gchar *str;
    gint mode, domain;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    str = mm_strip_tag (response, "%IPSYS:");

    if (!sscanf (str, "%d,%d", &mode, &domain)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse %%IPSYS response: '%s'",
                     response);
        return FALSE;
    }

    switch (mode) {
    case 0:
        *allowed = MM_MODEM_MODE_2G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 1:
        *allowed = MM_MODEM_MODE_3G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 2:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_2G;
        return TRUE;
    case 3:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_3G;
        return TRUE;
    case 5: /* any */
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse unexpected %%IPSYS response: '%s'",
                 response);
    return FALSE;
}

static void
modem_load_allowed_modes (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%IPSYS?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
modem_set_allowed_modes_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_mode_update_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
modem_set_allowed_modes (MMIfaceModem *self,
                         MMModemMode allowed,
                         MMModemMode preferred,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *command;
    gint icera_mode = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_set_allowed_modes);

    /*
     * The core has checked the following:
     *  - that 'allowed' are a subset of the 'supported' modes
     *  - that 'preferred' is one mode, and a subset of 'allowed'
     */
    if (allowed == MM_MODEM_MODE_2G)
        icera_mode = 0;
    else if (allowed == MM_MODEM_MODE_3G)
        icera_mode = 1;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (preferred == MM_MODEM_MODE_2G)
            icera_mode = 2;
        else if (preferred == MM_MODEM_MODE_3G)
            icera_mode = 3;
        else /* none preferred, so AUTO */
            icera_mode = 5;
    }

    if (icera_mode < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Requested mode (allowed: '%s', preferred: '%s') not "
                                         "supported by the modem.",
                                         allowed_str,
                                         preferred_str);
        g_free (allowed_str);
        g_free (preferred_str);

        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    command = g_strdup_printf ("%%IPSYS=%d", icera_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        result);
    g_free (command);
}

/*****************************************************************************/
/* Icera-specific unsolicited events handling */

typedef struct {
    guint cid;
    MMBroadbandBearerIceraConnectionStatus status;
} BearerListReportStatusForeachContext;

static void
bearer_list_report_status_foreach (MMBearer *bearer,
                                   BearerListReportStatusForeachContext *ctx)
{
    if (mm_broadband_bearer_get_3gpp_cid (MM_BROADBAND_BEARER (bearer)) != ctx->cid)
        return;

    if (!MM_IS_BROADBAND_BEARER_ICERA (bearer))
        return;

    mm_broadband_bearer_icera_report_connection_status (MM_BROADBAND_BEARER_ICERA (bearer),
                                                        ctx->status);
}

static void
ipdpact_received (MMAtSerialPort *port,
                  GMatchInfo *match_info,
                  MMBroadbandModemIcera *self)
{
    MMBearerList *list = NULL;
    BearerListReportStatusForeachContext ctx;
    guint cid;
    guint status;

    /* Ensure we got proper parsed values */
    if (!mm_get_uint_from_match_info (match_info, 1, &cid) ||
        !mm_get_uint_from_match_info (match_info, 2, &status))
        return;

    /* Setup context */
    ctx.cid = cid;
    ctx.status = MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_UNKNOWN;

    switch (status) {
    case 0:
        ctx.status = MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_DISCONNECTED;
        break;
    case 1:
        ctx.status = MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTED;
        break;
    case 2:
        /* activating */
        break;
    case 3:
        ctx.status = MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTION_FAILED;
        break;
    default:
        mm_warn ("Unknown Icera connect status %d", status);
        break;
    }

    /* If unknown status, don't try to report anything */
    if (ctx.status == MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_UNKNOWN)
        return;

    /* If empty bearer list, nothing else to do */
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list)
        return;

    /* Will report status only in the bearer with the specific CID */
    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_report_status_foreach,
                            &ctx);
    g_object_unref (list);
}

static MMModemAccessTechnology
nwstate_to_act (const gchar *str)
{
    /* small 'g' means CS, big 'G' means PS */
    if (!strcmp (str, "2g"))
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    else if (!strcmp (str, "2G-GPRS"))
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    else if (!strcmp (str, "2G-EDGE"))
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    else if (!strcmp (str, "3G"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "3g"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "R99"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "3G-HSDPA") || !strcmp (str, "HSDPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    else if (!strcmp (str, "3G-HSUPA") || !strcmp (str, "HSUPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA") || !strcmp (str, "HSDPA-HSUPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA-HSPA+") || !strcmp (str, "HSDPA-HSUPA-HSPA+"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
nwstate_changed (MMAtSerialPort *port,
                 GMatchInfo *info,
                 MMBroadbandModemIcera *self)
{
    gchar *str;

    /*
     * %NWSTATE: <rssi>,<mccmnc>,<tech>,<connection state>,<regulation>
     *
     * <connection state> shows the actual access technology in-use when a
     * PS connection is active.
     */

    /* Process signal quality... */
    str = g_match_info_fetch (info, 1);
    if (str) {
        gint rssi;

        rssi = atoi (str);
        rssi = CLAMP (rssi, 0, 5) * 100 / 5;
        g_free (str);

        mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self),
                                              (guint)rssi);
    }

    /* Process access technology... */
    str = g_match_info_fetch (info, 4);
    if (!str || (strcmp (str, "-") == 0)) {
        g_free (str);
        str = g_match_info_fetch (info, 3);
    }
    if (str) {
        MMModemAccessTechnology act;

        act = nwstate_to_act (str);
        g_free (str);

        /* Cache last received value, needed for explicit access technology
         * query handling */
        self->priv->last_act = act;
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   act,
                                                   MM_MODEM_ACCESS_TECHNOLOGY_ANY);
    }
}

static void
set_unsolicited_events_handlers (MMBroadbandModemIcera *self,
                                 gboolean enable)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->nwstate_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)nwstate_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ipdpact_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)ipdpact_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Always to ignore */
        if (!enable) {
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                self->priv->pacsp_regex,
                NULL,
                NULL,
                NULL);
        }
    }
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
modem_load_access_technologies_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       MMModemAccessTechnology *access_technologies,
                                       guint *mask,
                                       GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    *access_technologies = ((MMModemAccessTechnology) GPOINTER_TO_UINT (
                                g_simple_async_result_get_op_res_gpointer (
                                    G_SIMPLE_ASYNC_RESULT (res))));
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
nwstate_query_ready (MMBroadbandModemIcera *self,
                     GAsyncResult *res,
                     GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        mm_dbg ("Couldn't query access technology: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
    } else {
        /*
         * The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we use the result from there.
         */
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   GUINT_TO_POINTER (self->priv->last_act),
                                                   NULL);
    }

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
modem_load_access_technologies (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_access_technologies);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE",
        3,
        FALSE,
        (GAsyncReadyCallback)nwstate_query_ready,
        result);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_setup_unsolicited_events));
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_events);

    /* Our own cleanup first */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Enable/disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
own_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                     GAsyncResult *res,
                                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Our own enable now */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=1",
        3,
        FALSE,
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_enable_unsolicited_events));
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
own_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=0",
        3,
        FALSE,
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_disable_unsolicited_events));
}

/*****************************************************************************/
/* Create bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_BEARER (g_object_ref (
                          g_simple_async_result_get_op_res_gpointer (
                              G_SIMPLE_ASYNC_RESULT (res))));
}

static void
broadband_bearer_icera_new_ready (GObject *source,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_icera_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* If we get a NET port, create Icera bearer */
    if (mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self), MM_PORT_TYPE_NET)) {
        mm_broadband_bearer_icera_new (
            MM_BROADBAND_MODEM (self),
            MM_BROADBAND_MODEM_ICERA (self)->priv->default_ip_method,
            properties,
            NULL, /* cancellable */
            (GAsyncReadyCallback)broadband_bearer_icera_new_ready,
            result);
        return;
    }

    /* Otherwise, plain generic broadband bearer */
    mm_broadband_bearer_new (
        MM_BROADBAND_MODEM (self),
        properties,
        NULL, /* cancellable */
        (GAsyncReadyCallback)broadband_bearer_new_ready,
        result);
}

/*****************************************************************************/
/* Modem power up (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cfun_enable_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        /* Ignore all errors except NOT_ALLOWED, which means Airplane Mode */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED))
            g_simple_async_result_take_error (simple, error);
        else
            g_error_free (error);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_up);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1",
                              10,
                              FALSE,
                              (GAsyncReadyCallback)cfun_enable_ready,
                              result);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    /* Use AT+CFUN=4 for power down. It will stop the RF (IMSI detach), and
     * keeps access to the SIM */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              /* The modem usually completes +CFUN=4 within 1-2 seconds,
                               * but sometimes takes a ridiculously long time (~30-35 seconds).
                               * It's better to have a long timeout here than to have the
                               * modem not responding to subsequent AT commands until +CFUN=4
                               * completes. */
                              40,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
modem_reset_finish (MMIfaceModem *self,
                    GAsyncResult *res,
                    GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%IRESET",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return (MMUnlockRetries *) g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    int pin1, puk1, pin2, puk2;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query unlock retries: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    response = mm_strip_tag (response, "%PINNUM:");
    if (sscanf (response, " %d, %d, %d, %d", &pin1, &puk1, &pin2, &puk2) == 4) {
        MMUnlockRetries *retries;
        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2);
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   retries,
                                                   (GDestroyNotify)g_object_unref);
    } else {
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Invalid unlock retries response: '%s'",
                                         response);
    }
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%PINNUM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_unlock_retries_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_load_unlock_retries));
}

/*****************************************************************************/
/* Generic band handling utilities */

typedef struct {
    MMModemBand band;
    char *name;
    gboolean enabled;
} Band;

static void
band_free (Band *b)
{
    g_free (b->name);
    g_free (b);
}

static const Band modem_bands[] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_BAND_U2100, "FDD_BAND_I",    FALSE },
    { MM_MODEM_BAND_U1900, "FDD_BAND_II",   FALSE },
    { MM_MODEM_BAND_U1800, "FDD_BAND_III",  FALSE },
    { MM_MODEM_BAND_U17IV, "FDD_BAND_IV",   FALSE },
    { MM_MODEM_BAND_U800,  "FDD_BAND_VI",   FALSE },
    { MM_MODEM_BAND_U850,  "FDD_BAND_V",    FALSE },
    { MM_MODEM_BAND_U900,  "FDD_BAND_VIII", FALSE },
    /* 2G second */
    { MM_MODEM_BAND_G850,  "G850",          FALSE },
    { MM_MODEM_BAND_DCS,   "DCS",           FALSE },
    { MM_MODEM_BAND_EGSM,  "EGSM",          FALSE },
    { MM_MODEM_BAND_PCS,   "PCS",           FALSE },
    /* And ANY last since it's most inclusive */
    { MM_MODEM_BAND_ANY,   "ANY",           FALSE },
};

static const guint modem_band_any_bit = 1 << (G_N_ELEMENTS (modem_bands) - 1);

static MMModemBand
icera_band_to_mm (const char *icera)
{
    int i;

    for (i = 0 ; i < G_N_ELEMENTS (modem_bands); i++) {
        if (g_strcmp0 (icera, modem_bands[i].name) == 0)
            return modem_bands[i].band;
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static GSList *
parse_bands (const gchar *response, guint32 *out_len)
{
    GRegex *r;
    GMatchInfo *info;
    GSList *bands = NULL;

    g_return_val_if_fail (out_len != NULL, NULL);

    /*
     * Response is a number of lines of the form:
     *   "EGSM": 0
     *   "FDD_BAND_I": 1
     *   ...
     * with 1 and 0 indicating whether the particular band is enabled or not.
     */
    r = g_regex_new ("^\"(\\w+)\": (\\d)",
                     G_REGEX_MULTILINE, G_REGEX_MATCH_NEWLINE_ANY,
                     NULL);
    g_assert (r != NULL);

    g_regex_match (r, response, 0, &info);
    while (g_match_info_matches (info)) {
        gchar *name, *enabled;
        Band *b;
        MMModemBand band;

        name = g_match_info_fetch (info, 1);
        enabled = g_match_info_fetch (info, 2);
        band = icera_band_to_mm (name);
        if (band != MM_MODEM_BAND_UNKNOWN) {
            b = g_malloc0 (sizeof (Band));
            b->band = band;
            b->name = g_strdup (name);
            b->enabled = (enabled[0] == '1' ? TRUE : FALSE);
            bands = g_slist_append (bands, b);
            *out_len = *out_len + 1;
        }
        g_free (name);
        g_free (enabled);
        g_match_info_next (info, NULL);
    }
    g_match_info_free (info);
    g_regex_unref (r);

    return bands;
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

typedef struct {
    MMBaseModemAtCommand *cmds;
    GSList *check_bands;
    GSList *enabled_bands;
    guint32 idx;
} SupportedBandsContext;

static void
supported_bands_context_free (SupportedBandsContext *ctx)
{
    guint i;

    for (i = 0; ctx->cmds[i].command; i++)
        g_free (ctx->cmds[i].command);
    g_free (ctx->cmds);
    g_slist_free_full (ctx->check_bands, (GDestroyNotify) band_free);
    g_slist_free_full (ctx->enabled_bands, (GDestroyNotify) band_free);
    g_free (ctx);
}

static GArray *
modem_load_supported_bands_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_bands_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    SupportedBandsContext *ctx = NULL;
    GArray *bands;
    GSList *iter;

    mm_base_modem_at_sequence_finish (self, res, (gpointer) &ctx, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), ctx->idx);

        /* Add already enabled bands */
        for (iter = ctx->enabled_bands; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            g_array_prepend_val (bands, b->band);
        }

        /* Add any checked bands that are supported */
        for (iter = ctx->check_bands; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            /* 'enabled' here really means supported/unsupported */
            if (b->enabled)
                g_array_prepend_val (bands, b->band);
        }

        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bands,
                                                   (GDestroyNotify) g_array_unref);
    }
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gboolean
load_supported_bands_response_processor (MMBaseModem *self,
                                         gpointer context,
                                         const gchar *command,
                                         const gchar *response,
                                         gboolean last_command,
                                         const GError *error,
                                         GVariant **result,
                                         GError **result_error)
{
    SupportedBandsContext *ctx = context;
    Band *b = g_slist_nth_data (ctx->check_bands, ctx->idx++);

    /* If there was no error setting the band, that band is supported.  We
     * abuse the 'enabled' item to mean supported/unsupported.
     */
    b->enabled = !error;

    /* Continue to next band */
    return FALSE;
}

static void
load_supported_bands_get_bands_ready (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *operation_result)
{
    SupportedBandsContext *ctx;
    const gchar *response;
    GError *error = NULL;
    GSList *iter, *new;
    guint32 len = 0, i = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query current bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    ctx = g_new0 (SupportedBandsContext, 1);

    /* For each reported band, build up an AT command to set that band
     * to its current enabled/disabled state.
     */
    iter = ctx->check_bands = parse_bands (response, &len);
    ctx->cmds = g_new0 (MMBaseModemAtCommand, len + 1);

    while (iter) {
        Band *b = iter->data;

        if (b->enabled || b->band == MM_MODEM_BAND_ANY) {
            /* Move known-supported band to the enabled list */
            new = g_slist_next (iter);
            ctx->check_bands = g_slist_remove_link (ctx->check_bands, iter);
            ctx->enabled_bands = g_slist_prepend (ctx->enabled_bands, iter->data);
            g_slist_free (iter);
            iter = new;
        } else {
            /* Check support for disabled band */
            ctx->cmds[i].command = g_strdup_printf ("%%IPBM=\"%s\",0", b->name);
            ctx->cmds[i].timeout = 10;
            ctx->cmds[i].allow_cached = FALSE;
            ctx->cmds[i].response_processor = load_supported_bands_response_processor;
            i++;
            iter = g_slist_next (iter);
        }
    }

    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               ctx->cmds,
                               ctx,
                               (GDestroyNotify) supported_bands_context_free,
                               (GAsyncReadyCallback) load_supported_bands_ready,
                               operation_result);
}

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    /* The modems report some bands as disabled that they don't actually
     * support enabling.  Thanks Icera!  So we have to try setting each
     * band to it's current enabled/disabled value, and the modem will
     * return an error if it doesn't support that band at all.
     */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_supported_bands_get_bands_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_load_supported_bands));
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
modem_load_current_bands_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_current_bands_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GSimpleAsyncResult *operation_result)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;
    GSList *parsed, *iter;
    guint32 len = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_dbg ("Couldn't query current bands: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
    } else {
        /* Parse bands from Icera response into MM band numbers */
        parsed = parse_bands (response, &len);
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), len);
        for (iter = parsed; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            if (b->enabled)
                g_array_append_val (bands, b->band);
        }
        g_slist_free_full (parsed, (GDestroyNotify) band_free);

        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   bands,
                                                   (GDestroyNotify)g_array_unref);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
    }
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_load_current_bands));
}

/*****************************************************************************/
/* Set bands (Modem interface) */

typedef struct {
    GSimpleAsyncResult *result;
    guint bandbits;
    guint enablebits;
    guint disablebits;
} SetBandsContext;

/*
 * The modem's band-setting command (%IPBM=) enables or disables one
 * band at a time, and one band must always be enabled. Here, we first
 * get the set of enabled bands, compute the difference between that
 * set and the requested set, enable any added bands, and finally
 * disable any removed bands.
 */
static gboolean
modem_set_bands_finish (MMIfaceModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void set_one_band (MMIfaceModem *self, SetBandsContext *ctx);

static void
set_bands_context_complete_and_free (SetBandsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_free (ctx);
}

static void
set_bands_next (MMIfaceModem *self,
                GAsyncResult *res,
                SetBandsContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        mm_dbg ("Couldn't set bands: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    set_one_band (self, ctx);
}

static void
set_one_band (MMIfaceModem *self,
              SetBandsContext *ctx)
{
    guint enable, band;
    gchar *command;

    /* Find the next band to enable or disable, always doing enables first */
    enable = 1;
    band = ffs (ctx->enablebits);
    if (band == 0) {
        enable = 0;
        band = ffs (ctx->disablebits);
    }
    if (band == 0) {
        /* Both enabling and disabling are done */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    /* Note that ffs() returning 2 corresponds to 1 << 1, not 1 << 2 */
    band--;
    mm_dbg("1. enablebits %x disablebits %x band %d enable %d",
           ctx->enablebits, ctx->disablebits, band, enable);

    if (enable)
        ctx->enablebits &= ~(1 << band);
    else
        ctx->disablebits &= ~(1 << band);
    mm_dbg("2. enablebits %x disablebits %x",
           ctx->enablebits, ctx->disablebits);

    command = g_strdup_printf ("%%IPBM=\"%s\",%d",
                               modem_bands[band].name,
                               enable);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)set_bands_next,
        ctx);
    g_free (command);
}

static guint
band_array_to_bandbits (GArray *bands)
{
    MMModemBand band;
    guint i, j, bandbits;

    bandbits = 0;
    for (i = 0 ; i < bands->len ; i++) {
        band = g_array_index (bands, MMModemBand, i);
        for (j = 0 ; j < G_N_ELEMENTS (modem_bands) ; j++) {
            if (modem_bands[j].band == band) {
                bandbits |= 1 << j;
                break;
            }
        }
        g_assert (j <  G_N_ELEMENTS (modem_bands));
    }

    return bandbits;
}

static void
set_bands_got_current_bands (MMIfaceModem *self,
                             GAsyncResult *res,
                             SetBandsContext *ctx)
{
    GArray *bands;
    GError *error = NULL;
    guint currentbits;

    bands = modem_load_current_bands_finish (self, res, &error);
    if (!bands) {
        g_simple_async_result_take_error (ctx->result, error);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    currentbits = band_array_to_bandbits (bands);
    ctx->enablebits = ctx->bandbits & ~currentbits;
    ctx->disablebits = currentbits & ~ctx->bandbits;

    set_one_band (self, ctx);
}

static void
modem_set_bands (MMIfaceModem *self,
                 GArray *bands_array,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    SetBandsContext *ctx;

    ctx = g_new0 (SetBandsContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_set_bands);
    ctx->bandbits = band_array_to_bandbits (bands_array);

    /*
     * If ANY is requested, simply enable ANY to activate all bands except for
     * those forbidden. */
    if (ctx->bandbits & modem_band_any_bit) {
        ctx->enablebits = modem_band_any_bit;
        ctx->disablebits = 0;
        set_one_band (self, ctx);
        return;
    }

    modem_load_current_bands (self,
                              (GAsyncReadyCallback)set_bands_got_current_bands,
                              ctx);
}

/*****************************************************************************/
/* Load network timezone (Time interface) */

static gboolean
parse_tlts_query_reply (const gchar *response,
                        gchar **iso8601,
                        MMNetworkTimezone **tz,
                        GError **error)
{
    gint year;
    gint month;
    gint day;
    gint hour;
    gint minute;
    gint second;
    gchar sign;
    gint offset;
    GDateTime *utc, *adjusted;

    /* TLTS reports UTC time with the TZ offset to *local* time */
    response = mm_strip_tag (response, "*TLTS: ");
    if (sscanf (response,
                "\"%02d/%02d/%02d,%02d:%02d:%02d%c%02d\"",
                &year,
                &month,
                &day,
                &hour,
                &minute,
                &second,
                &sign,
                &offset) != 8) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unknown *TLTS response: %s",
                     response);
        return FALSE;
    }

    /* Icera modems only report a 2-digit year, while ISO-8601 requires
     * a 4-digit year.  Assume 2000.
     */
    if (year < 100)
        year += 2000;

    /* Offset comes in 15-min units */
    offset *= 15;
    /* Apply sign to offset;  */
    if (sign == '-')
        offset *= -1;

    utc = g_date_time_new_utc (year, month, day, hour, minute, second);
    if (!utc) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Invalid *TLTS date/time: %s",
                     response);
        return FALSE;
    }

    /* Convert UTC time to local time by adjusting by the timezone offset */
    adjusted = g_date_time_add_minutes (utc, offset);
    g_date_time_unref (utc);
    if (!adjusted) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to convert modem time to local time (offset %d)",
                     offset);
        return FALSE;
    }

    /* Convert offset from minutes-to-UTC to minutes-from-UTC */
    offset *= -1;

    if (tz) {
        *tz = mm_network_timezone_new ();
        mm_network_timezone_set_offset (*tz, offset);
    }

    if (iso8601) {
        *iso8601 = mm_new_iso8601_time (g_date_time_get_year (adjusted),
                                        g_date_time_get_month (adjusted),
                                        g_date_time_get_day_of_month (adjusted),
                                        g_date_time_get_hour (adjusted),
                                        g_date_time_get_minute (adjusted),
                                        g_date_time_get_second (adjusted),
                                        TRUE,
                                        offset);
    }

    g_date_time_unref (adjusted);
    return TRUE;
}

static MMNetworkTimezone *
modem_time_load_network_timezone_finish (MMIfaceModemTime *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    const gchar *response;
    MMNetworkTimezone *tz;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    if (!response) {
        /* We'll assume we can retry a bit later */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_RETRY,
                     "Retry");
        return NULL;
    }

    return (parse_tlts_query_reply (response, NULL, &tz, error) ? tz : NULL);
}

static void
modem_time_load_network_timezone (MMIfaceModemTime *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*TLTS",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load network time (Time interface) */

static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    const gchar *response;
    gchar *iso8601;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    return (parse_tlts_query_reply (response, &iso8601, NULL, error) ? iso8601 : NULL);
}

static void
modem_time_load_network_time (MMIfaceModemTime *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*TLTS",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Check support (Time interface) */

static gboolean
modem_time_check_support_finish (MMIfaceModemTime *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    /* We assume Icera devices always support *TLTS, since they appear
     * to return ERROR if the modem is not powered up, and thus we cannot
     * check for *TLTS support during modem initialization.
     */
    return TRUE;
}

static void
modem_time_check_support (MMIfaceModemTime *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_time_check_support);

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_icera_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemIcera *
mm_broadband_modem_icera_new (const gchar *device,
                              const gchar **drivers,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_ICERA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        self->priv->default_ip_method = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        g_value_set_enum (value, self->priv->default_ip_method);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_modem_icera_init (MMBroadbandModemIcera *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_ICERA,
                                              MMBroadbandModemIceraPrivate);

    self->priv->nwstate_regex = g_regex_new ("%NWSTATE:\\s*(-?\\d+),(\\d+),([^,]*),([^,]*),(\\d+)",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->pacsp_regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ipdpact_regex = g_regex_new ("\\r\\n%IPDPACT:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->default_ip_method = MM_BEARER_IP_METHOD_STATIC;
    self->priv->last_act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    g_regex_unref (self->priv->nwstate_regex);
    g_regex_unref (self->priv->pacsp_regex);
    g_regex_unref (self->priv->ipdpact_regex);

    G_OBJECT_CLASS (mm_broadband_modem_icera_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_allowed_modes = modem_load_allowed_modes;
    iface->load_allowed_modes_finish = modem_load_allowed_modes_finish;
    iface->set_allowed_modes = modem_set_allowed_modes;
    iface->set_allowed_modes_finish = modem_set_allowed_modes_finish;
    iface->load_access_technologies = modem_load_access_technologies;
    iface->load_access_technologies_finish = modem_load_access_technologies_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_supported_bands = modem_load_supported_bands;
    iface->load_supported_bands_finish = modem_load_supported_bands_finish;
    iface->load_current_bands = modem_load_current_bands;
    iface->load_current_bands_finish = modem_load_current_bands_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    /* Note: don't implement modem_init_power_down, as CFUN=4 here may take
     * looong to reply */
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->set_bands = modem_set_bands;
    iface->set_bands_finish = modem_set_bands_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
    iface->check_support = modem_time_check_support;
    iface->check_support_finish = modem_time_check_support_finish;
    iface->load_network_time = modem_time_load_network_time;
    iface->load_network_time_finish = modem_time_load_network_time_finish;
    iface->load_network_timezone = modem_time_load_network_timezone;
    iface->load_network_timezone_finish = modem_time_load_network_timezone_finish;
}

static void
mm_broadband_modem_icera_class_init (MMBroadbandModemIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemIceraPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;

    properties[PROP_DEFAULT_IP_METHOD] =
        g_param_spec_enum (MM_BROADBAND_MODEM_ICERA_DEFAULT_IP_METHOD,
                           "Default IP method",
                           "Default IP Method (static or DHCP) to use.",
                           MM_TYPE_BEARER_IP_METHOD,
                           MM_BEARER_IP_METHOD_STATIC,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEFAULT_IP_METHOD, properties[PROP_DEFAULT_IP_METHOD]);
}
