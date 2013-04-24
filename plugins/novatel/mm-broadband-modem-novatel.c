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
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-time.h"
#include "mm-iface-modem-messaging.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/result.h"
#include "mm-log.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init));

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadAllowedModesResult;

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadAllowedModesResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    /* When a valid result is given, we never complete in idle */
    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
nwrat_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
    GError *error = NULL;
    const gchar *response;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    gint a = -1;
    gint b = -1;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Parse response */
    r = g_regex_new ("\\$NWRAT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &error)) {
        if (error)
            g_simple_async_result_take_error (simple, error);
        else
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't match NWRAT reply: %s",
                                             response);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        g_regex_unref (r);
        return;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &a) ||
        !mm_get_int_from_match_info (match_info, 2, &b) ||
        a < 0 || a > 2 ||
        b < 1 || b > 2) {
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse mode/tech response '%s': invalid modes reported",
            response);
        g_match_info_free (match_info);
        g_regex_unref (r);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    switch (a) {
    case 0:
        result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        result.preferred = MM_MODEM_MODE_NONE;
        break;
    case 1:
        if (b == 1) {
            result.allowed = MM_MODEM_MODE_2G;
            result.preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result.preferred = MM_MODEM_MODE_2G;
        }
        break;
    case 2:
        if (b == 1) {
            result.allowed = MM_MODEM_MODE_3G;
            result.preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result.preferred = MM_MODEM_MODE_3G;
        }
        break;
    default:
        /* We only allow mode 0|1|2 */
        g_assert_not_reached ();
        break;
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    /* When a valid result is given, we never complete in idle */
    g_simple_async_result_set_op_res_gpointer (simple, &result, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

    /* Load allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Loading allowed modes not supported in CDMA-only modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWRAT?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)nwrat_query_ready,
                              result);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_mode_update_ready (MMBroadbandModemNovatel *self,
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
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *command;
    gint a = -1;
    gint b = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    /* Setting allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Setting allowed modes not supported in CDMA-only modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    if (allowed == MM_MODEM_MODE_2G) {
        a = 1;
        b = 1;
    } else if (allowed == MM_MODEM_MODE_3G) {
        a = 2;
        b = 1;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        b = 2;
        if (preferred == MM_MODEM_MODE_NONE)
            a = 0;
        else if (preferred == MM_MODEM_MODE_2G)
            a = 1;
        else if (preferred == MM_MODEM_MODE_3G)
            a = 2;
    }

    if (a < 0 || b < 0) {
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

    command = g_strdup_printf ("AT$NWRAT=%d,%d", a, b);
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
/* Load access technologies (Modem interface) */

typedef struct {
    guint hdr_revision;    /* QCDM_HDR_REV_x */
    MMModemAccessTechnology generic_act;
    guint mask;
} SnapshotResult;

typedef struct {
    MMBaseModem *self;
    MMQcdmSerialPort *port;
    GSimpleAsyncResult *simple;
    MMModemAccessTechnology generic_act;
    guint mask;
} SnapshotContext;

static void
snapshot_result_complete (GSimpleAsyncResult *simple,
                          guint hdr_revision,
                          MMModemAccessTechnology generic_act,
                          guint mask)
{
    SnapshotResult *r;

    r = g_new0 (SnapshotResult, 1);
    r->hdr_revision = hdr_revision;
    r->generic_act = generic_act;
    r->mask = mask;

    g_simple_async_result_set_op_res_gpointer (simple, r, g_free);
    g_simple_async_result_complete (simple);
}

static void
snapshot_result_complete_simple (GSimpleAsyncResult *simple,
                                 MMModemAccessTechnology generic_act,
                                 guint mask)
{
    snapshot_result_complete (simple, QCDM_HDR_REV_UNKNOWN, generic_act, mask);
}

static void
snapshot_context_complete_and_free (SnapshotContext *ctx, guint hdr_revision)
{
    snapshot_result_complete (ctx->simple,
                              hdr_revision,
                              ctx->generic_act,
                              ctx->mask);
    g_object_unref (ctx->simple);
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_free (ctx);
}

static void
nw_snapshot_old_cb (MMQcdmSerialPort *port,
                    GByteArray *response,
                    GError *error,
                    gpointer user_data)
{
    SnapshotContext *ctx = user_data;
    QcdmResult *result;
    guint8 hdr_revision = QCDM_HDR_REV_UNKNOWN;

    if (error) {
        /* Just ignore the error and complete with the input info */
        mm_dbg ("Couldn't run QCDM Novatel Modem MSM6500 snapshot: '%s'", error->message);
        snapshot_context_complete_and_free (ctx, QCDM_HDR_REV_UNKNOWN);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const gchar *) response->data, response->len, NULL);
    if (result) {
        qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &hdr_revision);
        qcdm_result_unref (result);
    } else
        mm_dbg ("Failed to get QCDM Novatel Modem MSM6500 snapshot.");

    snapshot_context_complete_and_free (ctx, hdr_revision);
}

static void
nw_snapshot_new_cb (MMQcdmSerialPort *port,
                    GByteArray *response,
                    GError *error,
                    gpointer user_data)
{
    SnapshotContext *ctx = user_data;
    QcdmResult *result;
    GByteArray *nwsnap;
    guint8 hdr_revision = QCDM_HDR_REV_UNKNOWN;

    if (error) {
        mm_dbg ("Couldn't run QCDM Novatel Modem MSM6800 snapshot: '%s'", error->message);
        snapshot_context_complete_and_free (ctx, QCDM_HDR_REV_UNKNOWN);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const gchar *) response->data, response->len, NULL);
    if (result) {
        qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &hdr_revision);
        qcdm_result_unref (result);
        snapshot_context_complete_and_free (ctx, hdr_revision);
        return;
    }

    mm_dbg ("Failed to get QCDM Novatel Modem MSM6800 snapshot.");

    /* Try for MSM6500 */
    nwsnap = g_byte_array_sized_new (25);
    nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6500);
    g_assert (nwsnap->len);
    mm_qcdm_serial_port_queue_command (port, nwsnap, 3, NULL, nw_snapshot_old_cb, ctx);
}

static gboolean
get_nw_modem_snapshot (MMBaseModem *self,
                       GSimpleAsyncResult *simple,
                       MMModemAccessTechnology generic_act,
                       guint mask)
{
    SnapshotContext *ctx;
    GByteArray *nwsnap;
    MMQcdmSerialPort *port;

    port = mm_base_modem_peek_port_qcdm (self);
    if (!port)
        return FALSE;

    /* Setup context */
    ctx = g_new0 (SnapshotContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->simple = simple;
    ctx->generic_act = generic_act;
    ctx->mask = mask;

    /* Try MSM6800 first since newer cards use that */
    nwsnap = g_byte_array_sized_new (25);
    nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6800);
    g_assert (nwsnap->len);
    mm_qcdm_serial_port_queue_command (port, nwsnap, 3, NULL, nw_snapshot_new_cb, ctx);
    return TRUE;
}

static gboolean
modem_load_access_technologies_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       MMModemAccessTechnology *access_technologies,
                                       guint *mask,
                                       GError **error)
{
    SnapshotResult *r;
    MMModemAccessTechnology act;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    r = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    act = r->generic_act;
    if (act & MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK) {
        /* Update access technology with specific EVDO revision from QCDM */
        if (r->hdr_revision == QCDM_HDR_REV_0) {
            mm_dbg ("Novatel Modem Snapshot EVDO revision: 0");
            act &= ~MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK;
            act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
        } else if (r->hdr_revision == QCDM_HDR_REV_A) {
            mm_dbg ("Novatel Modem Snapshot EVDO revision: A");
            act &= ~MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK;
            act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
        } else
            mm_dbg ("Novatel Modem Snapshot EVDO revision: %d (unknown)", r->hdr_revision);
    }

    *access_technologies = act;
    *mask = r->mask;
    return TRUE;
}

static void
cnti_set_ready (MMBaseModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;
    const gchar *p;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    p = mm_strip_tag (response, "$CNTI:");
    p = strchr (p, ',');
    if (!p) {
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't parse $CNTI result '%s'",
                             response);
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    snapshot_result_complete_simple (simple,
                                     mm_string_to_access_tech (p),
                                     MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

static void
parent_load_access_technologies_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GSimpleAsyncResult *simple)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    guint mask = 0;
    GError *error = NULL;

    if (!iface_modem_parent->load_access_technologies_finish (self,
                                                              res,
                                                              &act,
                                                              &mask,
                                                              &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* No point in checking EVDO revision if EVDO isn't being used or if for
     * some reason we don't have a QCDM port.
     */
    if (!(act & MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK)) {
        snapshot_result_complete (simple, QCDM_HDR_REV_UNKNOWN, act, mask);
        g_object_unref (simple);
        return;
    }

    /* Pass along the access tech & mask that the parent determined so we
     * can specialize it based on the EVDO revision from QCDM.
     */
    if (!get_nw_modem_snapshot (MM_BASE_MODEM (self), simple, act, mask)) {
        /* If there's any error, use the access tech that the parent interface determined */
        snapshot_result_complete (simple, QCDM_HDR_REV_UNKNOWN, act, mask);
        g_object_unref (simple);
    }
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

    /* CDMA-only modems defer to parent for generic access technology
     * checking, but can determine EVDOr0 vs. EVDOrA through proprietary
     * QCDM commands.
     */
    if (mm_iface_modem_is_cdma_only (self)) {
        iface_modem_parent->load_access_technologies (
            self,
            (GAsyncReadyCallback)parent_load_access_technologies_ready,
            result);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$CNTI=0",
        3,
        FALSE,
        (GAsyncReadyCallback)cnti_set_ready,
        result);
}

/*****************************************************************************/
/* Signal quality loading (Modem interface) */

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_signal_quality_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    guint signal_quality;

    signal_quality = iface_modem_parent->load_signal_quality_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (signal_quality),
                                                   NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gint
get_one_quality (const gchar *reply,
                 const gchar *tag)
{
    gint quality = -1;
    char *temp, *p;
    gint dbm;
    gboolean success = FALSE;

    p = strstr (reply, tag);
    if (!p)
        return -1;

    /* Skip the tag */
    p += strlen (tag);

    /* Skip spaces */
    while (isspace (*p))
        p++;

    p = temp = g_strdup (p);

    /* Cut off the string after the dBm */
    while (isdigit (*p) || (*p == '-'))
        p++;
    *p = '\0';

    /* When registered with EVDO, RX0/RX1 are returned by many cards with
     * negative dBm.  When registered only with 1x, some cards return "1x RSSI"
     * with positive dBm.
     */

    if (mm_get_int_from_str (temp, &dbm)) {
        if (*temp == '-') {
            /* Some cards appear to use RX0/RX1 and output RSSI in negative dBm */
            if (dbm < 0)
                success = TRUE;
        } else if (isdigit (*temp) && (dbm > 0) && (dbm < 115)) {
            /* S720 appears to use "1x RSSI" and print RSSI in dBm without '-' */
            dbm *= -1;
            success = TRUE;
        }
    }

    if (success) {
        dbm = CLAMP (dbm, -113, -51);
        quality = 100 - ((dbm + 51) * 100 / (-113 + 51));
    }

    g_free (temp);
    return quality;
}

static void
nwrssi_ready (MMBaseModem *self,
              GAsyncResult *res,
              GSimpleAsyncResult *simple)
{
    const gchar *response;
    gint quality;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        /* Fallback to parent's method */
        iface_modem_parent->load_signal_quality (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            simple);
        return;
    }

    /* Parse the signal quality */
    quality = get_one_quality (response, "RX0=");
    if (quality < 0)
        quality = get_one_quality (response, "1x RSSI=");
    if (quality < 0)
        quality = get_one_quality (response, "RX1=");

    if (quality >= 0)
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER ((guint)quality),
                                                   NULL);
    else
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse $NWRSSI response: '%s'",
                                         response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("loading signal quality...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_signal_quality);

    /* 3GPP modems can just run parent's signal quality loading */
    if (mm_iface_modem_is_3gpp (self)) {
        iface_modem_parent->load_signal_quality (
            self,
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            result);
        return;
    }

    /* CDMA modems need custom signal quality loading */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWRSSI",
        3,
        FALSE,
        (GAsyncReadyCallback)nwrssi_ready,
        result);
}

/*****************************************************************************/
/* Enable unsolicited events (SMS indications) (Messaging interface) */

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Many Qualcomm chipsets don't support mode=2 */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=1,1,2,1,0",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */

typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    DetailedRegistrationStateResults state;
} DetailedRegistrationStateContext;

static void
detailed_registration_state_context_complete_and_free (DetailedRegistrationStateContext *ctx)
{
    /* Always not in idle! we're passing a struct in stack as result */
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_cdma_get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                                   MMModemCdmaRegistrationState *detailed_evdo_state,
                                                   GError **error)
{
    DetailedRegistrationStateResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *detailed_cdma1x_state = results->detailed_cdma1x_state;
    *detailed_evdo_state = results->detailed_evdo_state;
    return TRUE;
}

static void
parse_modem_eri (DetailedRegistrationStateContext *ctx, QcdmResult *result)
{
    MMModemCdmaRegistrationState new_state;
    guint8 indicator_id = 0, icon_id = 0, icon_mode = 0;

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_INDICATOR_ID, &indicator_id);
    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_ID, &icon_id);
    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_MODE, &icon_mode);

    /* We use the "Icon ID" (also called the "Icon Index") because if it is 1,
     * the device is never roaming.  Any operator-defined IDs (greater than 2)
     * may or may not be roaming, but that's operator-defined and we don't
     * know anything about them.
     *
     * Indicator ID:
     * 0 appears to be "not roaming", contrary to standard ERI values
     * >= 1 appears to be the actual ERI value, which may or may not be
     *      roaming depending on the operator's custom ERI list
     *
     * Icon ID:
     * 0 = roaming indicator on
     * 1 = roaming indicator off
     * 2 = roaming indicator flash
     *
     * Icon Mode:
     * 0 = normal
     * 1 = flash  (only used with Icon ID >= 2)
     *
     * Roaming example:
     *    Roam:         160
     *    Indicator ID: 160
     *    Icon ID:      3
     *    Icon Mode:    0
     *    Call Prompt:  1
     *
     * Home example:
     *    Roam:         0
     *    Indicator ID: 0
     *    Icon ID:      1
     *    Icon Mode:    0
     *    Call Prompt:  1
     */
    if (icon_id == 1)
        new_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
    else
        new_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;

    if (ctx->state.detailed_cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        ctx->state.detailed_cdma1x_state = new_state;
    if (ctx->state.detailed_evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        ctx->state.detailed_evdo_state = new_state;
}

static void
reg_eri_6500_cb (MMQcdmSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 gpointer user_data)
{
    DetailedRegistrationStateContext *ctx = user_data;

    if (error) {
        /* Just ignore the error and complete with the input info */
        mm_dbg ("Couldn't run QCDM MSM6500 ERI: '%s'", error->message);
    } else {
        QcdmResult *result;

        result = qcdm_cmd_nw_subsys_eri_result ((const gchar *) response->data, response->len, NULL);
        if (result) {
            parse_modem_eri (ctx, result);
            qcdm_result_unref (result);
        }
    }

    /* NOTE: always complete NOT in idle here */
    g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
    detailed_registration_state_context_complete_and_free (ctx);
}

static void
reg_eri_6800_cb (MMQcdmSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 gpointer user_data)
{
    DetailedRegistrationStateContext *ctx = user_data;

    if (error) {
        /* Just ignore the error and complete with the input info */
        mm_dbg ("Couldn't run QCDM MSM6800 ERI: '%s'", error->message);
    } else {
        QcdmResult *result;
        GByteArray *nweri;

        /* Parse the response */
        result = qcdm_cmd_nw_subsys_eri_result ((const gchar *) response->data, response->len, NULL);
        if (result) {
            parse_modem_eri (ctx, result);
            qcdm_result_unref (result);
        } else {
            /* Try for MSM6500 */
            nweri = g_byte_array_sized_new (25);
            nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6500);
            g_assert (nweri->len);
            mm_qcdm_serial_port_queue_command (port, nweri, 3, NULL, reg_eri_6500_cb, ctx);
            return;
        }
    }

    /* NOTE: always complete NOT in idle here */
    g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
    detailed_registration_state_context_complete_and_free (ctx);
}

static void
modem_cdma_get_detailed_registration_state (MMIfaceModemCdma *self,
                                            MMModemCdmaRegistrationState cdma1x_state,
                                            MMModemCdmaRegistrationState evdo_state,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    DetailedRegistrationStateContext *ctx;
    GByteArray *nweri;
    MMQcdmSerialPort *port;

    /* Setup context */
    ctx = g_new0 (DetailedRegistrationStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_get_detailed_registration_state);
    ctx->state.detailed_cdma1x_state = cdma1x_state;
    ctx->state.detailed_evdo_state = evdo_state;

    port = mm_base_modem_peek_port_qcdm (MM_BASE_MODEM (self));

    /* Try MSM6800 first since newer cards use that */
    nweri = g_byte_array_sized_new (25);
    nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6800);
    g_assert (nweri->len);
    mm_qcdm_serial_port_queue_command (port, nweri, 3, NULL, reg_eri_6800_cb, ctx);
}

/*****************************************************************************/
/* Load network time (Time interface) */

static gboolean
parse_nwltime_reply (const char *response,
                     gchar **out_iso_8601,
                     MMNetworkTimezone **out_tz,
                     GError **error)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;
    guint year, month, day, hour, minute, second;
    gchar *result = NULL;
    gint utc_offset = 0;
    gboolean success = FALSE;

    /* Sample reply: 2013.3.27.15.47.19.2.-5 */
    r = g_regex_new ("(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.([\\-\\+\\d]+)$", 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, -1, 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse $NWLTIME results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match $NWLTIME reply");
        }
    } else {
        /* Remember that g_match_info_get_match_count() includes match #0 */
        g_assert (g_match_info_get_match_count (match_info) >= 9);

        if (mm_get_uint_from_match_info (match_info, 1, &year) &&
            mm_get_uint_from_match_info (match_info, 2, &month) &&
            mm_get_uint_from_match_info (match_info, 3, &day) &&
            mm_get_uint_from_match_info (match_info, 4, &hour) &&
            mm_get_uint_from_match_info (match_info, 5, &minute) &&
            mm_get_uint_from_match_info (match_info, 6, &second) &&
            mm_get_int_from_match_info (match_info, 8, &utc_offset)) {

            result = mm_new_iso8601_time (year, month, day, hour, minute, second,
                                          TRUE, utc_offset * 60);
            if (out_tz) {
                *out_tz = mm_network_timezone_new ();
                mm_network_timezone_set_offset (*out_tz, utc_offset * 60);
            }

            success = TRUE;
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse $NWLTIME reply");
        }
    }

    if (out_iso_8601)
        *out_iso_8601 = result;
    else
        g_free (result);

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return success;
}

static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    const gchar *response;
    gchar *result = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (response)
        parse_nwltime_reply (response, &result, NULL, error);
    return result;
}

static void
modem_time_load_network_time (MMIfaceModemTime *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWLTIME",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load network timezone (Time interface) */

static MMNetworkTimezone *
modem_time_load_network_timezone_finish (MMIfaceModemTime *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    const gchar *response;
    MMNetworkTimezone *tz = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    if (response)
        parse_nwltime_reply (response, NULL, &tz, error);
    return tz;
}

static void
modem_time_load_network_timezone (MMIfaceModemTime *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWLTIME",
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
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_time_check_support (MMIfaceModemTime *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    /* Only CDMA devices support this at the moment */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWLTIME",
                              3,
                              TRUE,
                              callback,
                              user_data);
}

/*****************************************************************************/

MMBroadbandModemNovatel *
mm_broadband_modem_novatel_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_novatel_init (MMBroadbandModemNovatel *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_access_technologies_finish = modem_load_access_technologies_finish;
    iface->load_access_technologies = modem_load_access_technologies;
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface->get_detailed_registration_state = modem_cdma_get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = modem_cdma_get_detailed_registration_state_finish;
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
mm_broadband_modem_novatel_class_init (MMBroadbandModemNovatelClass *klass)
{
}
