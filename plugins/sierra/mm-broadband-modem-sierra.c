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
 * Copyright (C) 2012 Lanedo GmbH
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-time.h"
#include "mm-common-sierra.h"
#include "mm-broadband-bearer-sierra.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModemCdma *iface_modem_cdma_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierra, mm_broadband_modem_sierra, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init));

typedef enum {
    TIME_METHOD_UNKNOWN = 0,
    TIME_METHOD_TIME = 1,
    TIME_METHOD_SYSTIME = 2,
} TimeMethod;

struct _MMBroadbandModemSierraPrivate {
    TimeMethod time_method;
};

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMUnlockRetries *unlock_retries;
    const gchar *response;
    gint matched;
    guint a, b, c ,d;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    matched = sscanf (response, "+CPINC: %d,%d,%d,%d",
                      &a, &b, &c, &d);
    if (matched != 4) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse PIN retries results: '%s'",
                     response);
        return NULL;
    }

    if (a > 998) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Invalid PIN attempts left: '%u'",
                     a);
        return NULL;
    }

    unlock_retries = mm_unlock_retries_new ();
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PIN, a);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PIN2, b);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PUK, c);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PUK2, d);
    return unlock_retries;
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading unlock retries (sierra)...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPINC?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Generic AT!STATUS parsing */

typedef enum {
    SYS_MODE_UNKNOWN,
    SYS_MODE_NO_SERVICE,
    SYS_MODE_CDMA_1X,
    SYS_MODE_EVDO_REV0,
    SYS_MODE_EVDO_REVA
} SysMode;

#define MODEM_REG_TAG "Modem has registered"
#define GENERIC_ROAM_TAG "Roaming:"
#define ROAM_1X_TAG "1xRoam:"
#define ROAM_EVDO_TAG "HDRRoam:"
#define SYS_MODE_TAG "Sys Mode:"
#define SYS_MODE_NO_SERVICE_TAG "NO SRV"
#define SYS_MODE_EVDO_TAG "HDR"
#define SYS_MODE_1X_TAG "1x"
#define SYS_MODE_CDMA_TAG "CDMA"
#define EVDO_REV_TAG "HDR Revision:"
#define SID_TAG "SID:"

static gboolean
get_roam_value (const gchar *reply,
                const gchar *tag,
                gboolean is_eri,
                gboolean *out_roaming)
{
    gchar *p;
    gboolean success;
    guint32 ind = 0;

    p = strstr (reply, tag);
    if (!p)
        return FALSE;

    p += strlen (tag);
    while (*p && isspace (*p))
        p++;

    /* Use generic ERI parsing if it's an ERI */
    if (is_eri) {
        success = mm_cdma_parse_eri (p, out_roaming, &ind, NULL);
        if (success) {
            /* Sierra redefines ERI 0, 1, and 2 */
            if (ind == 0)
                *out_roaming = FALSE;  /* home */
            else if (ind == 1 || ind == 2)
                *out_roaming = TRUE;   /* roaming */
        }
        return success;
    }

    /* If it's not an ERI, roaming is just true/false */
    if (*p == '1') {
        *out_roaming = TRUE;
        return TRUE;
    } else if (*p == '0') {
        *out_roaming = FALSE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
sys_mode_has_service (SysMode mode)
{
    return (   mode == SYS_MODE_CDMA_1X
            || mode == SYS_MODE_EVDO_REV0
            || mode == SYS_MODE_EVDO_REVA);
}

static gboolean
sys_mode_is_evdo (SysMode mode)
{
    return (mode == SYS_MODE_EVDO_REV0 || mode == SYS_MODE_EVDO_REVA);
}

static gboolean
parse_status (const char *response,
              MMModemCdmaRegistrationState *out_cdma1x_state,
              MMModemCdmaRegistrationState *out_evdo_state,
              MMModemAccessTechnology *out_act)
{
    gchar **lines;
    gchar **iter;
    gboolean registered = FALSE;
    gboolean have_sid = FALSE;
    SysMode evdo_mode = SYS_MODE_UNKNOWN;
    SysMode sys_mode = SYS_MODE_UNKNOWN;
    gboolean evdo_roam = FALSE, cdma1x_roam = FALSE;

    lines = g_strsplit_set (response, "\n\r", 0);
    if (!lines)
        return FALSE;

    /* Sierra CDMA parts have two general formats depending on whether they
     * support EVDO or not.  EVDO parts report both 1x and EVDO roaming status
     * while of course 1x parts only report 1x status.  Some modems also do not
     * report the Roaming information (MP 555 GPS).
     *
     * AT!STATUS responses:
     *
     * Unregistered MC5725:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0  NID: 0  1xRoam: 0 HDRRoam: 0
     * Temp: 33  State: 100  Sys Mode: NO SRV
     * Pilot NOT acquired
     * Modem has NOT registered
     *
     * Registered MC5725:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 775
     * SID: 30  NID: 2  1xRoam: 0 HDRRoam: 0
     * Temp: 29  State: 200  Sys Mode: HDR
     * Pilot acquired
     * Modem has registered
     * HDR Revision: A
     *
     * Unregistered AC580:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0 NID: 0  Roaming: 0
     * Temp: 39  State: 100  Scan Mode: 0
     * Pilot NOT acquired
     * Modem has NOT registered
     *
     * Registered AC580:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 548
     * SID: 26  NID: 1  Roaming: 1
     * Temp: 39  State: 200  Scan Mode: 0
     * Pilot Acquired
     * Modem has registered
     */

    /* We have to handle the two formats slightly differently; for newer formats
     * with "Sys Mode", we consider the modem registered if the Sys Mode is not
     * "NO SRV".  The explicit registration status is just icing on the cake.
     * For older formats (no "Sys Mode") we treat the modem as registered if
     * the SID is non-zero.
     */

    for (iter = lines; iter && *iter; iter++) {
        gboolean bool_val = FALSE;
        char *p;

        if (!strncmp (*iter, MODEM_REG_TAG, strlen (MODEM_REG_TAG))) {
            registered = TRUE;
            continue;
        }

        /* Roaming */
        get_roam_value (*iter, ROAM_1X_TAG, TRUE, &cdma1x_roam);
        get_roam_value (*iter, ROAM_EVDO_TAG, TRUE, &evdo_roam);
        if (get_roam_value (*iter, GENERIC_ROAM_TAG, FALSE, &bool_val))
            cdma1x_roam = evdo_roam = bool_val;

        /* Current system mode */
        p = strstr (*iter, SYS_MODE_TAG);
        if (p) {
            p += strlen (SYS_MODE_TAG);
            while (*p && isspace (*p))
                p++;
            if (!strncmp (p, SYS_MODE_NO_SERVICE_TAG, strlen (SYS_MODE_NO_SERVICE_TAG)))
                sys_mode = SYS_MODE_NO_SERVICE;
            else if (!strncmp (p, SYS_MODE_EVDO_TAG, strlen (SYS_MODE_EVDO_TAG)))
                sys_mode = SYS_MODE_EVDO_REV0;
            else if (   !strncmp (p, SYS_MODE_1X_TAG, strlen (SYS_MODE_1X_TAG))
                     || !strncmp (p, SYS_MODE_CDMA_TAG, strlen (SYS_MODE_CDMA_TAG)))
                sys_mode = SYS_MODE_CDMA_1X;
        }

        /* Current EVDO revision if system mode is EVDO */
        p = strstr (*iter, EVDO_REV_TAG);
        if (p) {
            p += strlen (EVDO_REV_TAG);
            while (*p && isspace (*p))
                p++;
            if (*p == 'A')
                evdo_mode = SYS_MODE_EVDO_REVA;
            else if (*p == '0')
                evdo_mode = SYS_MODE_EVDO_REV0;
        }

        /* SID */
        p = strstr (*iter, SID_TAG);
        if (p) {
            p += strlen (SID_TAG);
            while (*p && isspace (*p))
                p++;
            if (isdigit (*p) && (*p != '0'))
                have_sid = TRUE;
        }
    }

    /* Update current system mode */
    if (sys_mode_is_evdo (sys_mode)) {
        /* Prefer the explicit EVDO mode from EVDO_REV_TAG */
        if (evdo_mode != SYS_MODE_UNKNOWN)
            sys_mode = evdo_mode;
    }

    /* If the modem didn't report explicit registration with "Modem has
     * registered" then get registration status by looking at either system
     * mode or (for older devices that don't report that) just the SID.
     */
    if (!registered) {
        if (sys_mode != SYS_MODE_UNKNOWN)
            registered = sys_mode_has_service (sys_mode);
        else
            registered = have_sid;
    }

    if (registered) {
        *out_cdma1x_state = (cdma1x_roam ?
                                MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                MM_MODEM_CDMA_REGISTRATION_STATE_HOME);

        if (sys_mode_is_evdo (sys_mode)) {
            *out_evdo_state = (evdo_roam ?
                                  MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                  MM_MODEM_CDMA_REGISTRATION_STATE_HOME);
        } else {
            *out_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        }
    } else {
        /* Not registered */
        *out_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        *out_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    }

    if (out_act) {
        *out_act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        if (registered) {
            if (sys_mode == SYS_MODE_CDMA_1X)
                *out_act = MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
            else if (sys_mode == SYS_MODE_EVDO_REV0)
                *out_act = MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
            else if (sys_mode == SYS_MODE_EVDO_REVA)
                *out_act = MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
        }
    }

    g_strfreev (lines);

    return TRUE;
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

typedef struct {
    MMModemAccessTechnology act;
    guint mask;
} AccessTechInfo;

static void
access_tech_set_result (GSimpleAsyncResult *simple,
                        MMModemAccessTechnology act,
                        guint mask)
{
    AccessTechInfo *info;

    info = g_new (AccessTechInfo, 1);
    info->act = act;
    info->mask = mask;

    g_simple_async_result_set_op_res_gpointer (simple, info, g_free);
}

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    AccessTechInfo *info;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    info = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    g_assert (info);
    *access_technologies = info->act;
    *mask = info->mask;
    return TRUE;
}

static void
access_tech_3gpp_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response)
        g_simple_async_result_take_error (simple, error);
    else {
        MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        const gchar *p;

        p = mm_strip_tag (response, "*CNTI:");
        p = strchr (p, ',');
        if (p)
            act = mm_string_to_access_tech (p + 1);

        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            g_simple_async_result_set_error (
                simple,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Couldn't parse access technologies result: '%s'",
                response);
        else
            access_tech_set_result (simple, act, MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
access_tech_cdma_ready (MMIfaceModemCdma *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response)
        g_simple_async_result_take_error (simple, error);
    else {
        MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        MMModemCdmaRegistrationState cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        MMModemCdmaRegistrationState evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

        if (!parse_status (response, &cdma1x_state, &evdo_state, &act))
            g_simple_async_result_set_error (
                simple,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Couldn't parse access technologies result: '%s'",
                response);
        else
            access_tech_set_result (simple, act, MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_access_technologies);

    if (mm_iface_modem_is_3gpp (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "*CNTI=0",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)access_tech_3gpp_ready,
                                  result);
        return;
    }

    if (mm_iface_modem_is_cdma (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "!STATUS",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)access_tech_cdma_ready,
                                  result);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_array_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* CDMA-only modems don't support changing modes, default to parent's */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_op_res_gpointer (simple, all, (GDestroyNotify) g_array_unref);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* Build list of combinations for 3GPP devices */
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

    /* Non-LTE devices allow 2G/3G preferred modes */
    if (!mm_iface_modem_is_3gpp_lte (self)) {
        /* 2G and 3G, 2G preferred */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_2G;
        g_array_append_val (combinations, mode);
        /* 2G and 3G, 3G preferred */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_3G;
        g_array_append_val (combinations, mode);
    } else {
        /* 4G only */
        mode.allowed = MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 2G, 3G and 4G */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }

    /* Filter out those unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations);
    g_array_unref (all);
    g_array_unref (combinations);

    g_simple_async_result_set_op_res_gpointer (simple, filtered, (GDestroyNotify) g_array_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
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
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_supported_modes));
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

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
selrat_query_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    GSimpleAsyncResult *simple)
{
    LoadCurrentModesResult result;
    const gchar *response;
    GError *error = NULL;
    GRegex *r = NULL;
    GMatchInfo *match_info = NULL;

    response = mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Example response: !SELRAT: 03, UMTS 3G Preferred */
    r = g_regex_new ("!SELRAT:\\s*(\\d+).*$", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &error)) {
        guint mode;

        if (mm_get_uint_from_match_info (match_info, 1, &mode) && mode <= 7) {
            switch (mode) {
            case 0:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_NONE;
                if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
                    result.allowed |=  MM_MODEM_MODE_4G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 1:
                result.allowed = MM_MODEM_MODE_3G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 2:
                result.allowed = MM_MODEM_MODE_2G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 3:
                /* in Sierra LTE devices, mode 3 is automatic, including LTE, no preference */
                if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self))) {
                    result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
                    result.preferred = MM_MODEM_MODE_NONE;
                } else {
                    result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                    result.preferred = MM_MODEM_MODE_3G;
                }
                break;
            case 4:
                /* in Sierra LTE devices, mode 4 is automatic, including LTE, no preference */
                if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self))) {
                    result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
                    result.preferred = MM_MODEM_MODE_NONE;
                } else {
                    result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                    result.preferred = MM_MODEM_MODE_2G;
                }
                break;
            case 5:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 6:
                result.allowed = MM_MODEM_MODE_4G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 7:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            default:
                g_assert_not_reached ();
                break;
            }
        } else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse the allowed mode response: '%s'",
                                 response);
    } else if (!error)
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Could not parse allowed mode response: Response didn't match: '%s'",
                             response);

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple, &result, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMPortSerialAt *primary;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_modes);

    if (!mm_iface_modem_is_3gpp (self)) {
        /* Cannot do this in CDMA modems */
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Cannot load allowed modes in CDMA modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot load allowed modes while connected");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   primary,
                                   "!SELRAT?",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_query_ready,
                                   result);
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
selrat_set_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical. */
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMPortSerialAt *primary;
    gint idx = -1;
    gchar *command;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_current_modes);

    if (!mm_iface_modem_is_3gpp (self)) {
        /* Cannot do this in CDMA modems */
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Cannot set allowed modes in CDMA modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot set allowed modes while connected");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    if (allowed == MM_MODEM_MODE_3G)
        idx = 1;
    else if (allowed == MM_MODEM_MODE_2G)
        idx = 2;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        /* in Sierra LTE devices, modes 3 and 4 are automatic, including LTE, no preference */
        if (mm_iface_modem_is_3gpp_lte (self)) {
            if (preferred == MM_MODEM_MODE_NONE)
                idx = 5; /* GSM and UMTS Only */
        }
        else if (preferred == MM_MODEM_MODE_3G)
            idx = 3;
        else if (preferred == MM_MODEM_MODE_2G)
            idx = 4;
        else if (preferred == MM_MODEM_MODE_NONE)
            idx = 0;
    } else if (allowed == MM_MODEM_MODE_4G)
        idx = 6;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G) &&
             preferred == MM_MODEM_MODE_NONE)
        idx = 7;
    else if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE)
        idx = 0;

    if (idx < 0) {
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

    command = g_strdup_printf ("!SELRAT=%d", idx);
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_set_ready,
                                   result);
    g_free (command);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return TRUE;
}

static gboolean
after_sim_unlock_wait_cb (GSimpleAsyncResult *result)
{
    g_simple_async_result_complete (result);
    g_object_unref (result);
    return FALSE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;
    guint timeout = 8;
    const gchar **drivers;
    guint i;

    /* A short wait is necessary for SIM to become ready, otherwise some older
     * cards (AC881) crash if asked to connect immediately after sending the
     * PIN.  Assume sierra_net driven devices are better and don't need as long
     * a delay.
     */
    drivers = mm_base_modem_get_drivers (MM_BASE_MODEM (self));
    for (i = 0; drivers[i]; i++) {
        if (g_str_equal (drivers[i], "sierra_net"))
            timeout = 3;
    }

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_after_sim_unlock);

    g_timeout_add_seconds (timeout, (GSourceFunc)after_sim_unlock_wait_cb, result);
}

/*****************************************************************************/
/* Load own numbers (Modem interface) */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
parent_load_own_numbers_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    GStrv numbers;

    numbers = iface_modem_parent->load_own_numbers_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple, numbers, NULL);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

#define MDN_TAG  "MDN: "

static void
own_numbers_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    const gchar *response, *p;
    const gchar *numbers[2] = { NULL, NULL };
    gchar mdn[15];
    guint i;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response)
        goto fallback;

    p = strstr (response, MDN_TAG);
    if (!p)
        goto fallback;

    response = p + strlen (MDN_TAG);
    while (isspace (*response))
        response++;

    for (i = 0; i < (sizeof (mdn) - 1) && isdigit (response[i]); i++)
        mdn[i] = response[i];
    mdn[i] = '\0';
    numbers[0] = &mdn[0];

    /* MDNs are 10 digits in length */
    if (i != 10) {
        mm_warn ("Failed to parse MDN: expected 10 digits, got %d", i);
        goto fallback;
    }

    g_simple_async_result_set_op_res_gpointer (simple,
                                               g_strdupv ((gchar **) numbers),
                                               NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
    return;

fallback:
    /* Fall back to parent method */
    iface_modem_parent->load_own_numbers (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_own_numbers_ready,
        simple);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("loading own numbers (Sierra)...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_own_numbers);

    /* 3GPP modems can just run parent's own number loading */
    if (mm_iface_modem_is_3gpp (self)) {
        iface_modem_parent->load_own_numbers (
            self,
            (GAsyncReadyCallback)parent_load_own_numbers_ready,
            result);
        return;
    }

    /* CDMA modems try AT~NAMVAL?0 first, then fall back to parent for
     * loading own number from NV memory with QCDM.
     */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "~NAMVAL?0",
        3,
        FALSE,
        (GAsyncReadyCallback)own_numbers_ready,
        result);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New Sierra bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_sierra_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_sierra_new_finish (res, &error);
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

    mm_dbg ("Creating Sierra bearer...");
    mm_broadband_bearer_sierra_new (MM_BROADBAND_MODEM (self),
                                    properties,
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)broadband_bearer_sierra_new_ready,
                                    result);
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
                              "!RESET",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_power_down_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       GSimpleAsyncResult *simple)
{
    /* Ignore errors for now; we're not sure if all Sierra CDMA devices support
     * at!pcstate or 3GPP devices support +CFUN=4.
     */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_down);

    /* For CDMA modems, run !pcstate */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "!pcstate=0",
                                  5,
                                  FALSE,
                                  (GAsyncReadyCallback)modem_power_down_ready,
                                  result);
        return;
    }

    /* For GSM modems, run AT+CFUN=4 (power save) */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)modem_power_down_ready,
                              result);
}

/*****************************************************************************/
/* Setup registration checks (CDMA interface) */

typedef struct {
    gboolean skip_qcdm_call_manager_step;
    gboolean skip_qcdm_hdr_step;
    gboolean skip_at_cdma_service_status_step;
    gboolean skip_at_cdma1x_serving_system_step;
    gboolean skip_detailed_registration_state;
} SetupRegistrationChecksResults;

static gboolean
setup_registration_checks_finish (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  gboolean *skip_qcdm_call_manager_step,
                                  gboolean *skip_qcdm_hdr_step,
                                  gboolean *skip_at_cdma_service_status_step,
                                  gboolean *skip_at_cdma1x_serving_system_step,
                                  gboolean *skip_detailed_registration_state,
                                  GError **error)
{
    SetupRegistrationChecksResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *skip_qcdm_call_manager_step = results->skip_qcdm_call_manager_step;
    *skip_qcdm_hdr_step = results->skip_qcdm_hdr_step;
    *skip_at_cdma_service_status_step = results->skip_at_cdma_service_status_step;
    *skip_at_cdma1x_serving_system_step = results->skip_at_cdma1x_serving_system_step;
    *skip_detailed_registration_state = results->skip_detailed_registration_state;
    return TRUE;
}

static void
parent_setup_registration_checks_ready (MMIfaceModemCdma *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    SetupRegistrationChecksResults results = { 0 };

    if (!iface_modem_cdma_parent->setup_registration_checks_finish (self,
                                                                    res,
                                                                    &results.skip_qcdm_call_manager_step,
                                                                    &results.skip_qcdm_hdr_step,
                                                                    &results.skip_at_cdma_service_status_step,
                                                                    &results.skip_at_cdma1x_serving_system_step,
                                                                    &results.skip_detailed_registration_state,
                                                                    &error)) {
        g_simple_async_result_take_error (simple, error);
    } else {
        /* Skip +CSS */
        results.skip_at_cdma1x_serving_system_step = TRUE;
        /* Skip +CAD */
        results.skip_at_cdma_service_status_step = TRUE;

        /* Force to always use the detailed registration checks, as we have
         * !STATUS for that */
        results.skip_detailed_registration_state = FALSE;

        g_simple_async_result_set_op_res_gpointer (simple, &results, NULL);
    }

    /* All done. NOTE: complete NOT in idle! */
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
setup_registration_checks (MMIfaceModemCdma *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_registration_checks);

    /* Run parent's checks first */
    iface_modem_cdma_parent->setup_registration_checks (self,
                                                        (GAsyncReadyCallback)parent_setup_registration_checks_ready,
                                                        result);
}

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */

typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

typedef struct {
    MMBroadbandModemSierra *self;
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
get_detailed_registration_state_finish (MMIfaceModemCdma *self,
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
status_ready (MMIfaceModemCdma *self,
              GAsyncResult *res,
              DetailedRegistrationStateContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    /* If error, leave superclass' reg state alone if AT!STATUS isn't supported. */
    if (error) {
        g_error_free (error);

        /* NOTE: always complete NOT in idle here */
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    parse_status (response,
                  &(ctx->state.detailed_cdma1x_state),
                  &(ctx->state.detailed_evdo_state),
                  NULL);

    /* NOTE: always complete NOT in idle here */
    g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
    detailed_registration_state_context_complete_and_free (ctx);
}

static void
get_detailed_registration_state (MMIfaceModemCdma *self,
                                 MMModemCdmaRegistrationState cdma1x_state,
                                 MMModemCdmaRegistrationState evdo_state,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedRegistrationStateContext *ctx;

    /* Setup context */
    ctx = g_new0 (DetailedRegistrationStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             get_detailed_registration_state);
    ctx->state.detailed_cdma1x_state = cdma1x_state;
    ctx->state.detailed_evdo_state = evdo_state;

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "!STATUS",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)status_ready,
                              ctx);
}

/*****************************************************************************/
/* Load network time (Time interface) */

static gchar *
parse_time (const gchar *response,
            const gchar *regex,
            const gchar *tag,
            GError **error)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;
    guint year, month, day, hour, minute, second;
    gchar *result = NULL;

    r = g_regex_new (regex, 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, -1, 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse %s results: ", tag);
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Couldn't match %s reply", tag);
        }
    } else {
        if (mm_get_uint_from_match_info (match_info, 1, &year) &&
            mm_get_uint_from_match_info (match_info, 2, &month) &&
            mm_get_uint_from_match_info (match_info, 3, &day) &&
            mm_get_uint_from_match_info (match_info, 4, &hour) &&
            mm_get_uint_from_match_info (match_info, 5, &minute) &&
            mm_get_uint_from_match_info (match_info, 6, &second)) {
            result = mm_new_iso8601_time (year, month, day, hour, minute, second, FALSE, 0);
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Failed to parse %s reply", tag);
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return result;
}


static gchar *
parse_3gpp_time (const gchar *response, GError **error)
{
    /* Returns both local time and UTC time, but we have no good way to
     * determine the timezone from all of that, so just report local time.
     */
    return parse_time (response,
                       "\\s*!TIME:\\s+"
                       "(\\d+)/(\\d+)/(\\d+)\\s+"
                       "(\\d+):(\\d+):(\\d+)\\s*\\(local\\)\\s+"
                       "(\\d+)/(\\d+)/(\\d+)\\s+"
                       "(\\d+):(\\d+):(\\d+)\\s*\\(UTC\\)\\s*",
                       "!TIME",
                       error);
}

static gchar *
parse_cdma_time (const gchar *response, GError **error)
{
    /* YYYYMMDDWHHMMSS */
    return parse_time (response,
                       "\\s*(\\d{4})(\\d{2})(\\d{2})\\d(\\d{2})(\\d{2})(\\d{2})\\s*",
                       "!SYSTIME",
                       error);
}

static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    const gchar *response = NULL;
    char *iso8601 = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (response) {
        if (strstr (response, "!TIME:"))
            iso8601 = parse_3gpp_time (response, error);
        else
            iso8601 = parse_cdma_time (response, error);
    }
    return iso8601;
}

static void
modem_time_load_network_time (MMIfaceModemTime *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    const char *command;

    switch (MM_BROADBAND_MODEM_SIERRA (self)->priv->time_method) {
    case TIME_METHOD_TIME:
        command = "!TIME?";
        break;
    case TIME_METHOD_SYSTIME:
        command = "!SYSTIME?";
        break;
    default:
        g_assert_not_reached ();
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Check support (Time interface) */

enum {
    TIME_SUPPORTED = 1,
    SYSTIME_SUPPORTED = 2,
};

static gboolean
modem_time_check_support_finish (MMIfaceModemTime *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res));
}

static void
modem_time_check_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    GVariant *result;

    g_simple_async_result_set_op_res_gboolean (simple, FALSE);

    result = mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (!error && result) {
        MMBroadbandModemSierra *sierra = MM_BROADBAND_MODEM_SIERRA (self);

        sierra->priv->time_method = g_variant_get_uint32 (result);
        if (sierra->priv->time_method != TIME_METHOD_UNKNOWN)
            g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }
    g_clear_error (&error);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gboolean
parse_time_reply (MMBaseModem *self,
                  gpointer none,
                  const gchar *command,
                  const gchar *response,
                  gboolean last_command,
                  const GError *error,
                  GVariant **result,
                  GError **result_error)
{
    /* If error, try next command */
    if (!error) {
        if (strstr (command, "!TIME"))
            *result = g_variant_new_uint32 (TIME_METHOD_TIME);
        else if (strstr (command, "!SYSTIME"))
            *result = g_variant_new_uint32 (TIME_METHOD_SYSTIME);
    }

    /* Stop sequence if we get a result, but not on errors */
    return *result ? TRUE : FALSE;
}

static const MMBaseModemAtCommand time_check_sequence[] = {
    { "!TIME?", 3, FALSE, parse_time_reply },    /* 3GPP */
    { "!SYSTIME?", 3, FALSE, parse_time_reply }, /* CDMA */
    { NULL }
};

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

    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        time_check_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback)modem_time_check_ready,
        result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_sierra_parent_class)->setup_ports (self);

    mm_common_sierra_setup_ports (self);
}

/*****************************************************************************/

MMBroadbandModemSierra *
mm_broadband_modem_sierra_new (const gchar *device,
                               const gchar **drivers,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_sierra_init (MMBroadbandModemSierra *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_SIERRA,
                                              MMBroadbandModemSierraPrivate);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    mm_common_sierra_peek_parent_interfaces (iface);

    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->load_power_state = mm_common_sierra_load_power_state;
    iface->load_power_state_finish = mm_common_sierra_load_power_state_finish;
    iface->modem_power_up = mm_common_sierra_modem_power_up;
    iface->modem_power_up_finish = mm_common_sierra_modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->create_sim = mm_common_sierra_create_sim;
    iface->create_sim_finish = mm_common_sierra_create_sim_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface_modem_cdma_parent = g_type_interface_peek_parent (iface);

    iface->setup_registration_checks = setup_registration_checks;
    iface->setup_registration_checks_finish = setup_registration_checks_finish;
    iface->get_detailed_registration_state = get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = get_detailed_registration_state_finish;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
    iface->check_support = modem_time_check_support;
    iface->check_support_finish = modem_time_check_support_finish;
    iface->load_network_time = modem_time_load_network_time;
    iface->load_network_time_finish = modem_time_load_network_time_finish;
}

static void
mm_broadband_modem_sierra_class_init (MMBroadbandModemSierraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemSierraPrivate));

    broadband_modem_class->setup_ports = setup_ports;
}
