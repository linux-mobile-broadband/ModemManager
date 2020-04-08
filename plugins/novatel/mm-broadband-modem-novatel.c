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
#include "mm-log-object.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init))

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 5);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G, 2G preferred */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_2G;
    g_array_append_val (combinations, mode);
    /* 2G and 3G, 3G preferred */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_3G;
    g_array_append_val (combinations, mode);

    /* Filter out those unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadCurrentModesResult;

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadCurrentModesResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *allowed = result->allowed;
    *preferred = result->preferred;
    g_free (result);

    return TRUE;
}

static void
nwrat_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    LoadCurrentModesResult *result;
    GError *error = NULL;
    const gchar *response;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    gint a = -1;
    gint b = -1;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse response */
    r = g_regex_new ("\\$NWRAT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &error)) {
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't match NWRAT reply: %s",
                                     response);
        g_object_unref (task);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &a) ||
        !mm_get_int_from_match_info (match_info, 2, &b) ||
        a < 0 || a > 2 ||
        b < 1 || b > 2) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse mode/tech response '%s': invalid modes reported",
            response);
        g_object_unref (task);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return;
    }

    result = g_new0 (LoadCurrentModesResult, 1);

    switch (a) {
    case 0:
        result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        result->preferred = MM_MODEM_MODE_NONE;
        break;
    case 1:
        if (b == 1) {
            result->allowed = MM_MODEM_MODE_2G;
            result->preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result->preferred = MM_MODEM_MODE_2G;
        }
        break;
    case 2:
        if (b == 1) {
            result->allowed = MM_MODEM_MODE_3G;
            result->preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result->preferred = MM_MODEM_MODE_3G;
        }
        break;
    default:
        /* We only allow mode 0|1|2 */
        g_assert_not_reached ();
        break;
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Load allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Loading allowed modes not supported in CDMA-only modems");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWRAT?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)nwrat_query_ready,
                              task);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allowed_mode_update_ready (MMBroadbandModemNovatel *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GTask *task;
    gchar *command;
    gint a = -1;
    gint b = -1;

    task = g_task_new (self, NULL, callback, user_data);

    /* Setting allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Setting allowed modes not supported in CDMA-only modems");
        g_object_unref (task);
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
    } else if (allowed == MM_MODEM_MODE_ANY &&
               preferred == MM_MODEM_MODE_NONE) {
        b = 2;
        a = 0;
    }

    if (a < 0 || b < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested mode (allowed: '%s', preferred: '%s') not "
                                 "supported by the modem.",
                                 allowed_str,
                                 preferred_str);
        g_object_unref (task);
        g_free (allowed_str);
        g_free (preferred_str);
        return;
    }

    command = g_strdup_printf ("AT$NWRAT=%d,%d", a, b);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        task);
    g_free (command);
}

/*****************************************************************************/

static void
close_and_unref_port (MMPortSerialQcdm *port)
{
    mm_port_serial_close (MM_PORT_SERIAL (port));
    g_object_unref (port);
}

static gboolean
get_evdo_version_finish (MMBaseModem *self,
                         GAsyncResult *res,
                         guint *hdr_revision,  /* QCDM_HDR_REV_* */
                         GError **error)
{
    gssize result;

    result = g_task_propagate_int (G_TASK (res), error);
    if (result < 0)
        return FALSE;

    *hdr_revision = (guint8) result;
    return TRUE;
}

static void
nw_snapshot_old_ready (MMPortSerialQcdm *port,
                       GAsyncResult *res,
                       GTask *task)
{
    QcdmResult *result;
    GError *error = NULL;
    GByteArray *response;
    guint8 hdr_revision = QCDM_HDR_REV_UNKNOWN;

    response = mm_port_serial_qcdm_command_finish (port, res, &error);
    if (error) {
        /* Just ignore the error and complete with the input info */
        g_prefix_error (&error, "Couldn't run QCDM Novatel Modem MSM6500 snapshot: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const gchar *) response->data, response->len, NULL);
    g_byte_array_unref (response);
    if (!result) {
        g_prefix_error (&error, "Failed to get QCDM Novatel Modem MSM6500 snapshot: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Success */
    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &hdr_revision);
    qcdm_result_unref (result);

    g_task_return_int (task, (gint) hdr_revision);
    g_object_unref (task);
}

static void
nw_snapshot_new_ready (MMPortSerialQcdm *port,
                       GAsyncResult     *res,
                       GTask            *task)
{
    MMBroadbandModemNovatel *self;
    QcdmResult              *result;
    GByteArray              *nwsnap;
    GError                  *error = NULL;
    GByteArray              *response;
    guint8                   hdr_revision = QCDM_HDR_REV_UNKNOWN;

    self = g_task_get_source_object (task);

    response = mm_port_serial_qcdm_command_finish (port, res, &error);
    if (error) {
        g_prefix_error (&error, "couldn't run QCDM Novatel Modem MSM6800 snapshot: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const gchar *) response->data, response->len, NULL);
    g_byte_array_unref (response);
    if (result) {
        /* Success */
        qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &hdr_revision);
        qcdm_result_unref (result);

        g_task_return_int (task, (gint) hdr_revision);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "failed to get QCDM Novatel Modem MSM6800 snapshot");

    /* Try for MSM6500 */
    nwsnap = g_byte_array_sized_new (25);
    nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6500);
    g_assert (nwsnap->len);
    mm_port_serial_qcdm_command (port,
                                 nwsnap,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)nw_snapshot_old_ready,
                                 task);
    g_byte_array_unref (nwsnap);
}

static void
get_evdo_version (MMBaseModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GError *error = NULL;
    GByteArray *nwsnap;
    GTask *task;
    MMPortSerialQcdm *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_base_modem_get_port_qcdm (self);
    if (!port) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "No available QCDM port");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    g_task_set_task_data (task, port, (GDestroyNotify) close_and_unref_port);

    if (!mm_port_serial_open (MM_PORT_SERIAL (port), &error)) {
        g_prefix_error (&error, "couldn't open QCDM port: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Try MSM6800 first since newer cards use that */
    nwsnap = g_byte_array_sized_new (25);
    nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6800);
    g_assert (nwsnap->len);
    mm_port_serial_qcdm_command (port,
                                 nwsnap,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)nw_snapshot_new_ready,
                                 task);
    g_byte_array_unref (nwsnap);
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

typedef struct {
    MMModemAccessTechnology act;
    guint mask;
    guint hdr_revision;  /* QCDM_HDR_REV_* */
} AccessTechContext;

static gboolean
modem_load_access_technologies_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       MMModemAccessTechnology *access_technologies,
                                       guint *mask,
                                       GError **error)
{
    AccessTechContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    /* Update access technology with specific EVDO revision from QCDM if we have them */
    ctx = g_task_get_task_data (G_TASK (res));
    if (ctx->act & MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK) {
        if (ctx->hdr_revision == QCDM_HDR_REV_0) {
            mm_obj_dbg (self, "modem snapshot EVDO revision: 0");
            ctx->act &= ~MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK;
            ctx->act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
        } else if (ctx->hdr_revision == QCDM_HDR_REV_A) {
            mm_obj_dbg (self, "modem snapshot EVDO revision: A");
            ctx->act &= ~MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK;
            ctx->act |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
        } else
            mm_obj_dbg (self, "modem snapshot EVDO revision: %d (unknown)", ctx->hdr_revision);
    }

    *access_technologies = ctx->act;
    *mask = ctx->mask;
    return TRUE;
}

static void
cnti_set_ready (MMBaseModem *self,
                GAsyncResult *res,
                GTask *task)
{
    AccessTechContext *ctx = g_task_get_task_data (task);
    GError *error = NULL;
    const gchar *response;
    const gchar *p;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    p = mm_strip_tag (response, "$CNTI:");
    p = strchr (p, ',');
    if (!p) {
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't parse $CNTI result '%s'",
                             response);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->act = mm_string_to_access_tech (p);
    ctx->mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
evdo_version_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    GTask *task)
{
    GError *error = NULL;
    AccessTechContext *ctx = g_task_get_task_data (task);

    if (!get_evdo_version_finish (self, res, &ctx->hdr_revision, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_load_access_technologies_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask *task)
{
    GError *error = NULL;
    AccessTechContext *ctx = g_task_get_task_data (task);

    if (!iface_modem_parent->load_access_technologies_finish (self,
                                                              res,
                                                              &ctx->act,
                                                              &ctx->mask,
                                                              &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* No point in checking EVDO revision if EVDO isn't being used */
    if (!(ctx->act & MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK)) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Get the EVDO revision from QCDM */
    get_evdo_version (MM_BASE_MODEM (self),
                       (GAsyncReadyCallback) evdo_version_ready,
                       task);
}

static void
modem_load_access_technologies (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    AccessTechContext *ctx;
    GTask *task;

    /* Setup context */
    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new0 (AccessTechContext, 1);
    g_task_set_task_data (task, ctx, g_free);

    /* CDMA-only modems defer to parent for generic access technology
     * checking, but can determine EVDOr0 vs. EVDOrA through proprietary
     * QCDM commands.
     */
    if (mm_iface_modem_is_cdma_only (self)) {
        iface_modem_parent->load_access_technologies (
            self,
            (GAsyncReadyCallback)parent_load_access_technologies_ready,
            task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$CNTI=0",
        3,
        FALSE,
        (GAsyncReadyCallback)cnti_set_ready,
        task);
}

/*****************************************************************************/
/* Signal quality loading (Modem interface) */

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return 0;
    }
    return (guint)value;
}

static void
parent_load_signal_quality_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    GError *error = NULL;
    guint signal_quality;

    signal_quality = iface_modem_parent->load_signal_quality_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, signal_quality);
    g_object_unref (task);
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
        } else if (isdigit (*temp) && (dbm > 0) && (dbm <= 125)) {
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
              GTask *task)
{
    const gchar *response;
    gint quality;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        /* Fallback to parent's method */
        iface_modem_parent->load_signal_quality (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            task);
        return;
    }

    /* Parse the signal quality */
    quality = get_one_quality (response, "RX0=");
    if (quality < 0)
        quality = get_one_quality (response, "1x RSSI=");
    if (quality < 0)
        quality = get_one_quality (response, "RX1=");
    if (quality < 0)
        quality = get_one_quality (response, "HDR RSSI=");

    if (quality >= 0)
        g_task_return_int (task, quality);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't parse $NWRSSI response: '%s'",
                                 response);
    g_object_unref (task);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* 3GPP modems can just run parent's signal quality loading */
    if (mm_iface_modem_is_3gpp (self)) {
        iface_modem_parent->load_signal_quality (
            self,
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            task);
        return;
    }

    /* CDMA modems need custom signal quality loading */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWRSSI",
        3,
        FALSE,
        (GAsyncReadyCallback)nwrssi_ready,
        task);
}

/*****************************************************************************/
/* Automatic activation (CDMA interface) */

static gboolean
modem_cdma_activate_finish (MMIfaceModemCdma  *self,
                            GAsyncResult      *res,
                            GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
qcmipgetp_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    GError      *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response)
        g_task_return_error (task, error);
    else {
        mm_obj_dbg (self, "current profile information retrieved: %s", response);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
activate_cdv_ready (MMBaseModem  *self,
                    GAsyncResult *res,
                    GTask        *task)
{
    GError      *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Let's query the MIP profile */
    mm_base_modem_at_command (self,
                              "$QCMIPGETP",
                              20,
                              FALSE,
                              (GAsyncReadyCallback)qcmipgetp_ready,
                              task);
}

static void
modem_cdma_activate (MMIfaceModemCdma    *self,
                     const gchar         *carrier_code,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    GTask *task;
    gchar *cmd;

    task = g_task_new (self, NULL, callback, user_data);

    cmd = g_strdup_printf ("+CDV=%s", carrier_code);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              20,
                              FALSE,
                              (GAsyncReadyCallback)activate_cdv_ready,
                              task);
    g_free (cmd);
}

/*****************************************************************************/
/* Manual activation (CDMA interface) */

/* Wait up to 2 minutes */
#define MAX_IOTA_QUERY_RETRIES    24
#define MAX_IOTA_QUERY_RETRY_TIME  5

typedef enum {
    CDMA_ACTIVATION_STEP_FIRST,
    CDMA_ACTIVATION_STEP_REQUEST_ACTIVATION,
    CDMA_ACTIVATION_STEP_OTA_UPDATE,
    CDMA_ACTIVATION_STEP_PRL_UPDATE,
    CDMA_ACTIVATION_STEP_WAIT_UNTIL_FINISHED,
    CDMA_ACTIVATION_STEP_LAST
} CdmaActivationStep;

typedef struct {
    CdmaActivationStep                step;
    MMCdmaManualActivationProperties *properties;
    guint                             wait_timeout_id;
    guint                             wait_retries;
} CdmaActivationContext;

static void
cdma_activation_context_free (CdmaActivationContext *ctx)
{
    g_assert (ctx->wait_timeout_id == 0);
    g_object_unref (ctx->properties);
    g_slice_free (CdmaActivationContext, ctx);
}

static gboolean
modem_cdma_activate_manual_finish (MMIfaceModemCdma  *self,
                                   GAsyncResult      *res,
                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void cdma_activation_step (GTask *task);

static gboolean
cdma_activation_step_retry (GTask *task)
{
    CdmaActivationContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->wait_timeout_id = 0;
    cdma_activation_step (task);
    return G_SOURCE_REMOVE;
}

static void
iota_query_ready (MMBaseModem  *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    GError                *error = NULL;
    const gchar           *response;
    CdmaActivationContext *ctx;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Finished? */
    if (strstr (response, "IOTA Enabled")) {
        ctx->step++;
        cdma_activation_step (task);
        return;
    }

    /* Too many retries? */
    if (ctx->wait_retries == MAX_IOTA_QUERY_RETRIES) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Too much time waiting to finish the IOTA activation");
        g_object_unref (task);
        return;
    }

    /* Otherwise, schedule retry in some secs */
    g_assert (ctx->wait_timeout_id == 0);
    ctx->wait_retries++;
    ctx->wait_timeout_id = g_timeout_add_seconds (MAX_IOTA_QUERY_RETRY_TIME,
                                                  (GSourceFunc)cdma_activation_step_retry,
                                                  task);
}

static void
activation_command_ready (MMBaseModem  *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    GError                *error = NULL;
    const gchar           *response;
    CdmaActivationContext *ctx;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    cdma_activation_step (task);
}

static void
cdma_activation_step (GTask *task)
{
    MMBroadbandModemNovatel *self;
    CdmaActivationContext   *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case CDMA_ACTIVATION_STEP_FIRST:
        mm_obj_dbg (self, "launching manual activation...");
        ctx->step++;
        /* fall-through */

    case CDMA_ACTIVATION_STEP_REQUEST_ACTIVATION: {
        gchar *command;

        mm_obj_info (self, "activation step [1/5]: setting up activation details");
        command = g_strdup_printf ("$NWACTIVATION=%s,%s,%s",
                                   mm_cdma_manual_activation_properties_get_spc (ctx->properties),
                                   mm_cdma_manual_activation_properties_get_mdn (ctx->properties),
                                   mm_cdma_manual_activation_properties_get_min (ctx->properties));
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  command,
                                  20,
                                  FALSE,
                                  (GAsyncReadyCallback)activation_command_ready,
                                  task);
        g_free (command);
        return;
    }

    case CDMA_ACTIVATION_STEP_OTA_UPDATE:
        mm_obj_info (self, "activation step [2/5]: starting OTA activation");
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+IOTA=1",
                                  20,
                                  FALSE,
                                  (GAsyncReadyCallback)activation_command_ready,
                                  task);
        return;

    case CDMA_ACTIVATION_STEP_PRL_UPDATE:
        mm_obj_info (self, "activation step [3/5]: starting PRL update");
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+IOTA=2",
                                  20,
                                  FALSE,
                                  (GAsyncReadyCallback)activation_command_ready,
                                  task);
        return;

    case CDMA_ACTIVATION_STEP_WAIT_UNTIL_FINISHED:
        mm_obj_info (self, "activation step [4/5]: checking activation process status");
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+IOTA?",
                                  20,
                                  FALSE,
                                  (GAsyncReadyCallback)iota_query_ready,
                                  task);
        return;

    case CDMA_ACTIVATION_STEP_LAST:
        mm_obj_info (self, "activation step [5/5]: activation process finished");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_cdma_activate_manual (MMIfaceModemCdma                 *self,
                            MMCdmaManualActivationProperties *properties,
                            GAsyncReadyCallback               callback,
                            gpointer                          user_data)
{
    GTask                 *task;
    CdmaActivationContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    /* Setup context */
    ctx = g_slice_new0 (CdmaActivationContext);
    ctx->properties = g_object_ref (properties);
    ctx->step = CDMA_ACTIVATION_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify) cdma_activation_context_free);

    /* And start it */
    cdma_activation_step (task);
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
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;
} DetailedRegistrationStateResult;

typedef struct {
    MMPortSerialQcdm *port;
    gboolean close_port;
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;
} DetailedRegistrationStateContext;

static void
detailed_registration_state_context_free (DetailedRegistrationStateContext *ctx)
{
    if (ctx->port) {
        if (ctx->close_port)
            mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
        g_object_unref (ctx->port);
    }
    g_free (ctx);
}

static gboolean
modem_cdma_get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                                   GAsyncResult *res,
                                                   MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                                   MMModemCdmaRegistrationState *detailed_evdo_state,
                                                   GError **error)
{
    GTask *task = G_TASK (res);
    DetailedRegistrationStateContext *ctx = g_task_get_task_data (task);;

    if (!g_task_propagate_boolean (task, error))
        return FALSE;

    *detailed_cdma1x_state = ctx->cdma1x_state;
    *detailed_evdo_state = ctx->evdo_state;
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

    if (ctx->cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        ctx->cdma1x_state = new_state;
    if (ctx->evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        ctx->evdo_state = new_state;
}

static void
reg_eri_6500_cb (MMPortSerialQcdm *port,
                 GAsyncResult     *res,
                 GTask            *task)
{
    MMBroadbandModemNovatel          *self;
    DetailedRegistrationStateContext *ctx;
    GError                           *error = NULL;
    GByteArray                       *response;
    QcdmResult                       *result;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mm_port_serial_qcdm_command_finish (port, res, &error);
    if (error) {
        /* Just ignore the error and complete with the input info */
        mm_obj_dbg (self, "couldn't run QCDM MSM6500 ERI: %s", error->message);
        g_error_free (error);
        goto done;
    }

    result = qcdm_cmd_nw_subsys_eri_result ((const gchar *) response->data, response->len, NULL);
    g_byte_array_unref (response);
    if (result) {
        parse_modem_eri (ctx, result);
        qcdm_result_unref (result);
    }

done:
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
reg_eri_6800_cb (MMPortSerialQcdm *port,
                 GAsyncResult *res,
                 GTask *task)
{
    MMBroadbandModemNovatel          *self;
    DetailedRegistrationStateContext *ctx;
    GError                           *error = NULL;
    GByteArray                       *response;
    GByteArray                       *nweri;
    QcdmResult                       *result;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mm_port_serial_qcdm_command_finish (port, res, &error);
    if (error) {
        /* Just ignore the error and complete with the input info */
        mm_obj_dbg (self, "couldn't run QCDM MSM6800 ERI: %s", error->message);
        g_error_free (error);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_eri_result ((const gchar *) response->data, response->len, NULL);
    g_byte_array_unref (response);
    if (result) {
        /* Success */
        parse_modem_eri (ctx, result);
        qcdm_result_unref (result);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Try for MSM6500 */
    nweri = g_byte_array_sized_new (25);
    nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6500);
    g_assert (nweri->len);
    mm_port_serial_qcdm_command (port,
                                 nweri,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)reg_eri_6500_cb,
                                 task);
    g_byte_array_unref (nweri);
}

static void
modem_cdma_get_detailed_registration_state (MMIfaceModemCdma *self,
                                            MMModemCdmaRegistrationState cdma1x_state,
                                            MMModemCdmaRegistrationState evdo_state,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    DetailedRegistrationStateContext *ctx;
    GTask *task;
    GByteArray *nweri;
    GError *error = NULL;

    /* Setup context */
    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_new0 (DetailedRegistrationStateContext, 1);
    g_task_set_task_data (task, ctx, (GDestroyNotify) detailed_registration_state_context_free);

    ctx->cdma1x_state = cdma1x_state;
    ctx->evdo_state = evdo_state;

    ctx->port = mm_base_modem_get_port_qcdm (MM_BASE_MODEM (self));
    if (!ctx->port) {
        /* Ignore errors and use non-detailed registration state */
        mm_obj_dbg (self, "no available QCDM port");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
        /* Ignore errors and use non-detailed registration state */
        mm_obj_dbg (self, "couldn't open QCDM port: %s", error->message);
        g_error_free (error);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->close_port = TRUE;

    /* Try MSM6800 first since newer cards use that */
    nweri = g_byte_array_sized_new (25);
    nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6800);
    g_assert (nweri->len);
    mm_port_serial_qcdm_command (ctx->port,
                                 nweri,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)reg_eri_6800_cb,
                                 task);
    g_byte_array_unref (nweri);
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
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
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

    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
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
    iface->activate = modem_cdma_activate;
    iface->activate_finish = modem_cdma_activate_finish;
    iface->activate_manual = modem_cdma_activate_manual;
    iface->activate_manual_finish = modem_cdma_activate_manual_finish;
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
