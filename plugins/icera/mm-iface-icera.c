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
 * Copyright (C) 2012 Google Inc.
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
/* Load unlock retries (Modem interface) */

MMUnlockRetries *
mm_iface_icera_modem_load_unlock_retries_finish (MMIfaceModem *self,
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
    GError *error;
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

void
mm_iface_icera_modem_load_unlock_retries (MMIfaceModem *self,
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
                                   mm_iface_icera_modem_load_unlock_retries));
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
    GSList *bands;
    guint32 idx;
} SupportedBandsContext;

static void
supported_bands_context_free (SupportedBandsContext *ctx)
{
    guint i;

    for (i = 0; ctx->cmds[i].command; i++)
        g_free (ctx->cmds[i].command);
    g_free (ctx->cmds);
    g_slist_free_full (ctx->bands, (GDestroyNotify) band_free);
    g_free (ctx);
}

GArray *
mm_iface_icera_modem_load_supported_bands_finish (MMIfaceModem *self,
                                                  GAsyncResult *res,
                                                  GError **error)
{
    /* Never fails */
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
        for (iter = ctx->bands; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            /* 'enabled' here really means supported/unsupported */
            if (b->enabled)
                g_array_append_val (bands, b->band);
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
    Band *b = g_slist_nth_data (ctx->bands, ctx->idx++);

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
    GError *error;
    GSList *iter;
    guint32 len = 0, i;

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
    ctx->bands = parse_bands (response, &len);
    ctx->cmds = g_new0 (MMBaseModemAtCommand, len + 1);

    for (iter = ctx->bands, i = 0; iter; iter = g_slist_next (iter), i++) {
        Band *b = iter->data;

        ctx->cmds[i].command = g_strdup_printf ("%%IPBM=\"%s\",%c",
                                                b->name,
                                                b->enabled ? '1' : '0');
        ctx->cmds[i].timeout = 3;
        ctx->cmds[i].allow_cached = FALSE;
        ctx->cmds[i].response_processor = load_supported_bands_response_processor;
    }

    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               ctx->cmds,
                               ctx,
                               (GDestroyNotify) supported_bands_context_free,
                               (GAsyncReadyCallback) load_supported_bands_ready,
                               operation_result);
}

void
mm_iface_icera_modem_load_supported_bands (MMIfaceModem *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_icera_modem_load_supported_bands);

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
        result);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

GArray *
mm_iface_icera_modem_load_current_bands_finish (MMIfaceModem *self,
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
    GError *error;
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

void
mm_iface_icera_modem_load_current_bands (MMIfaceModem *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_icera_modem_load_current_bands);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_ready,
        result);
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
gboolean
mm_iface_icera_modem_set_bands_finish (MMIfaceModem *self,
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

    bands = mm_iface_icera_modem_load_current_bands_finish (self, res, &error);
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

void
mm_iface_icera_modem_set_bands (MMIfaceModem *self,
                                GArray *bands_array,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    SetBandsContext *ctx;

    ctx = g_new0 (SetBandsContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_icera_modem_set_bands);
    ctx->bandbits = band_array_to_bandbits (bands_array);
    /*
     * For the sake of efficiency, convert "ANY" to the actual set of
     * bands; this matches what we get from load_current_bands and
     * minimizes the number of changes we need to make.
     *
     * This requires that ANY is last in modem_bands and that all the
     * other bits are valid.
     */
    if (ctx->bandbits == (1 << (G_N_ELEMENTS (modem_bands) - 1)))
        ctx->bandbits--; /* clear the top bit, set all lower bits */
    mm_iface_icera_modem_load_current_bands (self,
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
