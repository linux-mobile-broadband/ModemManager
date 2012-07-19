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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-iface-icera.h"
#include "mm-broadband-bearer-icera.h"
#include "mm-base-modem-at.h"
#include "mm-bearer-list.h"

/*****************************************************************************/
/* Icera context */

#define ICERA_CONTEXT_TAG "icera-context"
static GQuark icera_context_quark;

typedef struct {
    GRegex *nwstate_regex;
    GRegex *pacsp_regex;
    GRegex *ipdpact_regex;

    /* Cache of the most recent value seen by the unsolicited message handler */
    MMModemAccessTechnology last_act;
} IceraContext;

static void
icera_context_free (IceraContext *ctx)
{
    g_regex_unref (ctx->nwstate_regex);
    g_regex_unref (ctx->pacsp_regex);
    g_regex_unref (ctx->ipdpact_regex);
    g_slice_free (IceraContext, ctx);
}

static IceraContext *
get_icera_context (MMBroadbandModem *self)
{
    IceraContext *ctx;

    if (G_UNLIKELY (!icera_context_quark))
        icera_context_quark = (g_quark_from_static_string (
                                   ICERA_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), icera_context_quark);
    if (!ctx) {
        ctx = g_slice_new (IceraContext);
        ctx->nwstate_regex = (g_regex_new (
                                  "%NWSTATE:\\s*(-?\\d+),(\\d+),([^,]*),([^,]*),(\\d+)",
                                  G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL));
        ctx->pacsp_regex = (g_regex_new (
                                "\\r\\n\\+PACSP(\\d)\\r\\n",
                                G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL));
        ctx->ipdpact_regex = (g_regex_new (
                                  "\\r\\n%IPDPACT:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\r\\n",
                                  G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL));
        ctx->last_act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        g_object_set_qdata_full (G_OBJECT (self),
                                 icera_context_quark,
                                 ctx,
                                 (GDestroyNotify)icera_context_free);
    }

    return ctx;
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

gboolean
mm_iface_icera_modem_load_allowed_modes_finish (MMIfaceModem *self,
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
        *allowed = (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
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

void
mm_iface_icera_modem_load_allowed_modes (MMIfaceModem *self,
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

gboolean
mm_iface_icera_modem_set_allowed_modes_finish (MMIfaceModem *self,
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

void
mm_iface_icera_modem_set_allowed_modes (MMIfaceModem *self,
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
                                        mm_iface_icera_modem_set_allowed_modes);

    /* There is no explicit config for CS connections, we just assume we may
     * have them as part of 2G when no GPRS is available */
    if (allowed & MM_MODEM_MODE_CS) {
        allowed |= MM_MODEM_MODE_2G;
        allowed &= ~MM_MODEM_MODE_CS;
    }

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
                  MMBroadbandModem *self)
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
    ctx.cid = 0;
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

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
nwstate_changed (MMAtSerialPort *port,
                 GMatchInfo *info,
                 MMBroadbandModem *self)
{
    IceraContext *ctx;
    gchar *str;

    /*
     * %NWSTATE: <rssi>,<mccmnc>,<tech>,<connection state>,<regulation>
     *
     * <connection state> shows the actual access technology in-use when a
     * PS connection is active.
     */

    ctx = get_icera_context (self);

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
        ctx->last_act = act;
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   act,
                                                   MM_MODEM_ACCESS_TECHNOLOGY_ANY);
    }
}

void
mm_iface_icera_modem_set_unsolicited_events_handlers (MMBroadbandModem *self,
                                                      gboolean enable)
{
    IceraContext *ctx;
    MMAtSerialPort *ports[2];
    guint i;

    ctx = get_icera_context (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            ctx->nwstate_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)nwstate_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            ctx->ipdpact_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)ipdpact_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Always to ignore */
        if (!enable) {
            mm_at_serial_port_add_unsolicited_msg_handler (
                ports[i],
                ctx->pacsp_regex,
                NULL,
                NULL,
                NULL);
        }
    }
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

gboolean
mm_iface_icera_modem_load_access_technologies_finish (MMIfaceModem *self,
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
nwstate_query_ready (MMBaseModem *self,
                     GAsyncResult *res,
                     GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_dbg ("Couldn't query access technology: '%s'", error->message);
        g_simple_async_result_take_error (operation_result, error);
    } else {
        IceraContext *ctx;

        /*
         * The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we use the result from there.
         */
        ctx = get_icera_context (MM_BROADBAND_MODEM (self));
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   GUINT_TO_POINTER (ctx->last_act),
                                                   NULL);
    }

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

void
mm_iface_icera_modem_load_access_technologies (MMIfaceModem *self,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_icera_modem_load_access_technologies);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE",
        3,
        FALSE,
        (GAsyncReadyCallback)nwstate_query_ready,
        result);
}

/*****************************************************************************/
/* Disable unsolicited events (3GPP interface) */

gboolean
mm_iface_icera_modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                             GAsyncResult *res,
                                                             GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

void
mm_iface_icera_modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=0",
        3,
        FALSE,
        callback,
        user_data);
}

/*****************************************************************************/
/* Enable unsolicited events (3GPP interface) */

gboolean
mm_iface_icera_modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                            GAsyncResult *res,
                                                            GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

void
mm_iface_icera_modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=1",
        3,
        FALSE,
        callback,
        user_data);
}

/*****************************************************************************/
/* Create bearer (Modem interface) */

MMBearer *
mm_iface_icera_modem_create_bearer_finish (MMIfaceModem *self,
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

void
mm_iface_icera_modem_create_bearer (MMIfaceModem *self,
                                    MMBearerProperties *properties,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_icera_modem_create_bearer);

    mm_broadband_bearer_icera_new (MM_BROADBAND_MODEM (self),
                                   properties,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)broadband_bearer_icera_new_ready,
                                   result);
}

/*****************************************************************************/
/* Reset (Modem interface) */

gboolean
mm_iface_icera_modem_reset_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

void mm_iface_icera_modem_reset (MMIfaceModem *self,
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
                &offset) == 8) {
        /* Offset comes in 15-min intervals */
        offset *= 15;
        /* Apply sign to offset */
        if (sign == '-')
            offset *= -1;

        /* If asked for it, build timezone information */
        if (tz) {
            *tz = mm_network_timezone_new ();
            mm_network_timezone_set_offset (*tz, offset);
        }

        if (iso8601) {
            /* don't give tz info in the date/time string, we have another
             * property for that */
            *iso8601 = g_strdup_printf ("%02d/%02d/%02d %02d:%02d:%02d",
                                        year, month, day,
                                        hour, minute, second);
        }

        return TRUE;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Unknown *TLTS response: %s",
                 response);
    return FALSE;
}

MMNetworkTimezone *
mm_iface_icera_modem_time_load_network_timezone_finish (MMIfaceModemTime *self,
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

void
mm_iface_icera_modem_time_load_network_timezone (MMIfaceModemTime *self,
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

gchar *
mm_iface_icera_modem_time_load_network_time_finish (MMIfaceModemTime *self,
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

void
mm_iface_icera_modem_time_load_network_time (MMIfaceModemTime *self,
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
/* Check Icera support */

gboolean
mm_iface_icera_check_support_finish (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

void
mm_iface_icera_check_support (MMBroadbandModem *self,
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

static void
iface_icera_init (gpointer g_iface)
{
}

GType
mm_iface_icera_get_type (void)
{
    static GType iface_icera_type = 0;

    if (!G_UNLIKELY (iface_icera_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceIcera), /* class_size */
            iface_icera_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_icera_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "MMIfaceIcera",
                                                   &info,
                                                   0);

        g_type_interface_add_prerequisite (iface_icera_type, MM_TYPE_BROADBAND_MODEM);
    }

    return iface_icera_type;
}
