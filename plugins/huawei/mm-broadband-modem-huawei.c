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
 * Copyright (C) 2011 - 2012 Google Inc.
 * Copyright (C) 2012 Huawei Technologies Co., Ltd
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-time.h"
#include "mm-iface-modem-cdma.h"
#include "mm-broadband-modem-huawei.h"
#include "mm-broadband-bearer-huawei.h"
#include "mm-broadband-bearer.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_time_init (MMIfaceModemTime *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;
static MMIfaceModemCdma *iface_modem_cdma_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemHuawei, mm_broadband_modem_huawei, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init));

struct _MMBroadbandModemHuaweiPrivate {
    /* Regex for signal quality related notifications */
    GRegex *rssi_regex;
    GRegex *rssilvl_regex;
    GRegex *hrssilvl_regex;

    /* Regex for access-technology related notifications */
    GRegex *mode_regex;

    /* Regex for connection status related notifications */
    GRegex *dsflowrpt_regex;

    /* Regex to ignore */
    GRegex *boot_regex;
    GRegex *csnr_regex;
    GRegex *simst_regex;
    GRegex *srvst_regex;
    GRegex *stin_regex;
};

/*****************************************************************************/

static gboolean
sysinfo_parse (const char *reply,
               guint *out_srv_status,
               guint *out_srv_domain,
               guint *out_roam_status,
               guint *out_sys_mode,
               guint *out_sim_state,
               gboolean *out_sys_submode_valid,
               guint *out_sys_submode,
               GError **error)
{
    gboolean matched;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;

    /* Can't just use \d here since sometimes you get "^SYSINFO:2,1,0,3,1,,3" */
    r = g_regex_new ("\\^SYSINFO:\\s*(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),?(\\d*),?(\\d*)$", 0, 0, NULL);
    g_assert (r != NULL);

    matched = g_regex_match_full (r, reply, -1, 0, 0, &match_info, &match_error);
    if (!matched) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse ^SYSINFO results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match ^SYSINFO reply");
        }
    } else {
        if (out_srv_status)
            mm_get_uint_from_match_info (match_info, 1, out_srv_status);
        if (out_srv_domain)
            mm_get_uint_from_match_info (match_info, 2, out_srv_domain);
        if (out_roam_status)
            mm_get_uint_from_match_info (match_info, 3, out_roam_status);
        if (out_sys_mode)
            mm_get_uint_from_match_info (match_info, 4, out_sys_mode);
        if (out_sim_state)
            mm_get_uint_from_match_info (match_info, 5, out_sim_state);

        /* Remember that g_match_info_get_match_count() includes match #0 */
        if (g_match_info_get_match_count (match_info) >= 8) {
            if (out_sys_submode_valid) {
                *out_sys_submode_valid = TRUE;
                mm_get_uint_from_match_info (match_info, 7, out_sys_submode);
            }
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return matched;
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static MMModemAccessTechnology
huawei_sysinfo_submode_to_act (gint huawei)
{
    /* new more detailed system mode/access technology */
    switch (huawei) {
    case 1:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case 2:
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 3:
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    case 4:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 5:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    case 6:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    case 7:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    case 9:  /* HSPA+ */
    case 17: /* HSPA+ (64QAM) */
    case 18: /* HSPA+ (MIMO) */
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    case 10:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 11:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    case 12:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;
    case 13:  /* 1xRTT */
    case 16:  /* 3xRTT */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 8:
        /* TD-SCDMA */
    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static MMModemAccessTechnology
huawei_sysinfo_sysmode_to_act (gint huawei)
{
    /* Older, less detailed system mode/access technology */
    switch (huawei) {
    case 2:  /* CDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case 3:  /* GSM/GPRS */
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 4:  /* HDR */
    case 8:  /* CDMA/HDR hybrid */
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case 5:  /* WCDMA */
    case 7:  /* GSM/WCDMA */
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case 15:
        /* TD-SCDMA */
    default:
        break;
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *result;
    char *str;
    guint srv_stat = 0;
    gboolean submode_valid = FALSE;
    guint submode = 0;
    guint sysmode = 0;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return FALSE;

    if (!sysinfo_parse (result, &srv_stat, NULL, NULL, &sysmode, NULL, &submode_valid, &submode, error))
        return FALSE;

    if (srv_stat != 0) {
        /* Valid service */
        if (submode_valid)
            act = huawei_sysinfo_submode_to_act (submode);

        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = huawei_sysinfo_sysmode_to_act (sysmode);
    }

    str = mm_modem_access_technology_build_string_from_mask (act);
    mm_dbg ("Access Technology: '%s'", str);
    g_free (str);

    *access_technologies = act;
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_dbg ("loading access technology (huawei)...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSINFO",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMUnlockRetries *unlock_retries;
    const gchar *result;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    guint i;
    MMModemLock locks[4] = {
        MM_MODEM_LOCK_SIM_PUK,
        MM_MODEM_LOCK_SIM_PIN,
        MM_MODEM_LOCK_SIM_PUK2,
        MM_MODEM_LOCK_SIM_PIN2
    };

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return NULL;

    r = g_regex_new ("\\^CPIN:\\s*([^,]+),[^,]*,(\\d+),(\\d+),(\\d+),(\\d+)",
                     G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, result, strlen (result), 0, 0, &match_info, error)) {
        g_prefix_error (error,
                        "Could not parse ^CPIN results: ");
        g_regex_unref (r);
        return NULL;
    }

    unlock_retries = mm_unlock_retries_new ();
    for (i = 0; i <= 3; i++) {
        guint num;

        if (!mm_get_uint_from_match_info (match_info, i + 2, &num) ||
            num > 10) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Could not parse ^CPIN results: "
                         "Missing or invalid match info for lock '%s'",
                         mm_modem_lock_get_string (locks[i]));
            g_object_unref (unlock_retries);
            unlock_retries = NULL;
            break;
        }

        mm_unlock_retries_set (unlock_retries, locks[i], num);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    return unlock_retries;
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading unlock retries (huawei)...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^CPIN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Common band/mode handling code */

typedef struct {
    MMModemBand mm;
    guint32 huawei;
} BandTable;

static BandTable bands[] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_BAND_U2100, 0x00400000 },
    { MM_MODEM_BAND_U1900, 0x00800000 },
    { MM_MODEM_BAND_U850,  0x04000000 },
    { MM_MODEM_BAND_U900,  0x00020000 },
    { MM_MODEM_BAND_G850,  0x00080000 },
    /* 2G second */
    { MM_MODEM_BAND_DCS,   0x00000080 },
    { MM_MODEM_BAND_EGSM,  0x00000100 },
    { MM_MODEM_BAND_PCS,   0x00200000 }
};

static gboolean
bands_array_to_huawei (GArray *bands_array,
                       guint32 *out_huawei)
{
    guint i;

    /* Treat ANY as a special case: All huawei flags enabled */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        *out_huawei = 0x3FFFFFFF;
        return TRUE;
    }

    *out_huawei = 0;
    for (i = 0; i < bands_array->len; i++) {
        guint j;

        for (j = 0; j < G_N_ELEMENTS (bands); j++) {
            if (g_array_index (bands_array, MMModemBand, i) == bands[j].mm)
                *out_huawei |= bands[j].huawei;
        }
    }

    return (*out_huawei > 0 ? TRUE : FALSE);
}

static gboolean
huawei_to_bands_array (guint32 huawei,
                       GArray **bands_array,
                       GError **error)
{
    guint i;

    *bands_array = NULL;
    for (i = 0; i < G_N_ELEMENTS (bands); i++) {
        if (huawei & bands[i].huawei) {
            if (G_UNLIKELY (!*bands_array))
                *bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
            g_array_append_val (*bands_array, bands[i].mm);
        }
    }

    if (!*bands_array) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't build bands array from '%u'",
                     huawei);
        return FALSE;
    }

    return TRUE;
}

static gboolean
huawei_to_modem_mode (guint mode,
                      guint acquisition_order,
                      MMModemMode *allowed,
                      MMModemMode *preferred,
                      GError **error)
{
    switch (mode) {
    case 2:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        switch (acquisition_order) {
        case 1:
            *preferred = MM_MODEM_MODE_2G;
            return TRUE;
        case 2:
            *preferred = MM_MODEM_MODE_3G;
            return TRUE;
        case 0:
            *preferred = MM_MODEM_MODE_NONE;
            return TRUE;
        default:
            break;
        }
        break;

    case 13:
        *allowed = MM_MODEM_MODE_2G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;

    case 14:
        *allowed = MM_MODEM_MODE_3G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;

    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Unexpected system mode reference (%u) or "
                 "acquisition order (%u)",
                 mode,
                 acquisition_order);
    return FALSE;
}

static gboolean
allowed_mode_to_huawei (MMModemMode allowed,
                        MMModemMode preferred,
                        guint *huawei_mode,
                        guint *huawei_acquisition_order,
                        GError **error)
{
    gchar *allowed_str;
    gchar *preferred_str;

    if (allowed == MM_MODEM_MODE_ANY) {
        *huawei_mode = 2;
        *huawei_acquisition_order = 0;
        return TRUE;
    }

    if (allowed == MM_MODEM_MODE_2G) {
        *huawei_mode = 13;
        *huawei_acquisition_order = 1;
        return TRUE;
    }

    if (allowed == MM_MODEM_MODE_3G) {
        *huawei_mode = 14;
        *huawei_acquisition_order = 2;
        return TRUE;
    }

    if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        *huawei_mode = 2;
        if (preferred == MM_MODEM_MODE_2G)
            *huawei_acquisition_order = 1;
        else if (preferred == MM_MODEM_MODE_3G)
            *huawei_acquisition_order = 2;
        else
            *huawei_acquisition_order = 0;
        return TRUE;
    }

    /* Not supported */
    allowed_str = mm_modem_mode_build_string_from_mask (allowed);
    preferred_str = mm_modem_mode_build_string_from_mask (preferred);
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Requested mode (allowed: '%s', preferred: '%s') not "
                 "supported by the modem.",
                 allowed_str,
                 preferred_str);
    g_free (allowed_str);
    g_free (preferred_str);
    return FALSE;
}

static gboolean
parse_syscfg (const gchar *response,
              GArray **bands_array,
              MMModemMode *allowed,
              MMModemMode *preferred,
              GError **error)
{
    gint mode;
    gint acquisition_order;
    guint32 band;
    gint roaming;
    gint srv_domain;

    if (!response ||
        strncmp (response, "^SYSCFG:", 8) != 0 ||
        !sscanf (response + 8, "%d,%d,%x,%d,%d", &mode, &acquisition_order, &band, &roaming, &srv_domain)) {
        /* Dump error to upper layer */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unexpected SYSCFG response: '%s'",
                     response);
        return FALSE;
    }

    /* Build allowed/preferred modes only if requested */
    if (allowed &&
        preferred &&
        !huawei_to_modem_mode (mode, acquisition_order, allowed, preferred, error))
        return FALSE;

    /* Band */
    if (bands_array &&
        !huawei_to_bands_array (band, bands_array, error))
        return FALSE;

    return TRUE;
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    const gchar *response;
    GArray *bands_array;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    if (!parse_syscfg (response, &bands_array, NULL, NULL, error))
        return NULL;

    return bands_array;
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_dbg ("loading current bands (huawei)...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^SYSCFG?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set bands (Modem interface) */

static gboolean
set_bands_finish (MMIfaceModem *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
syscfg_set_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_bands (MMIfaceModem *self,
           GArray *bands_array,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *cmd;
    guint32 huawei_band = 0x3FFFFFFF;
    gchar *bands_string;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_bands);

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array->data,
                                                 bands_array->len);

    if (!bands_array_to_huawei (bands_array, &huawei_band)) {
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Invalid bands requested: '%s'",
                                         bands_string);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        g_free (bands_string);
        return;
    }

    cmd = g_strdup_printf ("AT^SYSCFG=16,3,%X,2,4", huawei_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)syscfg_set_ready,
                              result);
    g_free (cmd);
    g_free (bands_string);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
parse_prefmode (const gchar *response, MMModemMode *preferred, GError **error)
{
    int a;

    response = mm_strip_tag (response, "^PREFMODE:");
    a = atoi (response);
    if (a == 2) {
        *preferred = MM_MODEM_MODE_2G;
        return TRUE;
    } else if (a == 4) {
        *preferred = MM_MODEM_MODE_3G;
        return TRUE;
    } else if (a == 8) {
        *preferred = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
        return TRUE;
    }

    g_set_error_literal (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Failed to parse ^PREFMODE response");
    return FALSE;
}

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (mm_iface_modem_is_cdma_only (self)) {
        /* CDMA-only devices always support 2G and 3G if they have 3G, so just
         * use the modes from GCAP as the list of allowed modes.
         */
        *allowed = mm_iface_modem_get_supported_modes (self);
        return parse_prefmode (response, preferred, error);
    }

    return parse_syscfg (response, NULL, allowed, preferred, error);
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    const char *command;

    mm_dbg ("loading allowed_modes (huawei)...");

    command = mm_iface_modem_is_cdma_only (self) ? "^PREFMODE?" : "^SYSCFG?";
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
allowed_mode_to_prefmode (MMModemMode allowed, guint *huawei_mode, GError **error)
{
    char *allowed_str;

    *huawei_mode = 0;
    if (allowed == MM_MODEM_MODE_ANY)
        *huawei_mode = 8;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G))
        *huawei_mode = 8;
    else if (allowed == MM_MODEM_MODE_2G)
        *huawei_mode = 2;
    else if (allowed == MM_MODEM_MODE_3G)
        *huawei_mode = 4;
    else {
        /* Not supported */
        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Requested mode (allowed: '%s') not supported by the modem.",
                     allowed_str);
        g_free (allowed_str);
    }
    return *huawei_mode ? TRUE : FALSE;
}

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_mode_update_ready (MMBroadbandModemHuawei *self,
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
    gchar *command = NULL;
    guint mode = 0;
    guint acquisition_order;
    GError *error = NULL;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    if (mm_iface_modem_is_cdma_only (self)) {
        if (allowed_mode_to_prefmode (allowed, &mode, &error))
            command = g_strdup_printf ("^PREFMODE=%d", mode);
    } else {
        if (allowed_mode_to_huawei (allowed,
                                    preferred,
                                    &mode,
                                    &acquisition_order,
                                    &error))
            command = g_strdup_printf ("AT^SYSCFG=%d,%d,40000000,2,4", mode, acquisition_order);
    }

    if (command) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            command,
            3,
            FALSE,
            (GAsyncReadyCallback)allowed_mode_update_ready,
            result);
    } else {
        g_assert (error);
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
    }
    g_free (command);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
huawei_signal_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    if (quality == 99) {
        /* 99 means unknown */
        quality = 0;
    } else {
        /* Normalize the quality */
        quality = CLAMP (quality, 0, 31) * 100 / 31;
    }

    mm_dbg ("3GPP signal quality: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), (guint)quality);
}

static void
huawei_mode_changed (MMAtSerialPort *port,
                     GMatchInfo *match_info,
                     MMBroadbandModemHuawei *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;
    gint a;
    guint32 mask = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    str = g_match_info_fetch (match_info, 1);
    a = atoi (str);
    g_free (str);

    /* CDMA/EVDO devices may not send this */
    str = g_match_info_fetch (match_info, 2);
    if (str[0])
        act = huawei_sysinfo_submode_to_act (atoi (str));
    g_free (str);

    switch (a) {
    case 3:
        /* GSM/GPRS mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_GSM ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_EDGE)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_warn ("Unexpected access technology (%s) in GSM/GPRS mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 5:
        /* WCDMA mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_UMTS ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_warn ("Unexpected access technology (%s) in WCDMA mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 2:
        /* CDMA mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            act != MM_MODEM_ACCESS_TECHNOLOGY_1XRTT) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_warn ("Unexpected access technology (%s) in CDMA mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
        mask = MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 4:  /* HDR mode */
    case 8:  /* CDMA/HDR hybrid mode */
        if (act != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN &&
            (act < MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 ||
             act > MM_MODEM_ACCESS_TECHNOLOGY_EVDOB)) {
            str = mm_modem_access_technology_build_string_from_mask (act);
            mm_warn ("Unexpected access technology (%s) in EVDO mode", str);
            g_free (str);
            act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        }
        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            act = MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
        mask = MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK;
        break;

    case 0:
        act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        break;

    default:
        mm_warn ("Unexpected mode change value reported: '%d'", a);
        return;
    }

    str = mm_modem_access_technology_build_string_from_mask (act);
    mm_dbg ("Access Technology: '%s'", str);
    g_free (str);

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self), act, mask);
}

static void
huawei_status_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemHuawei *self)
{
    gchar *str;
    gint n1, n2, n3, n4, n5, n6, n7;

    str = g_match_info_fetch (match_info, 1);
    if (sscanf (str, "%x,%x,%x,%x,%x,%x,%x", &n1, &n2, &n3, &n4, &n5, &n6, &n7)) {
        mm_dbg ("Duration: %d Up: %d Kbps Down: %d Kbps Total: %d Total: %d\n",
                n1, n2 * 8 / 1000, n3  * 8 / 1000, n4 / 1024, n5 / 1024);
    }
    g_free (str);
}

static void
set_3gpp_unsolicited_events_handlers (MMBroadbandModemHuawei *self,
                                      gboolean enable)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Signal quality related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->rssi_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->mode_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_mode_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->dsflowrpt_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_status_changed : NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_3gpp_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                            GAsyncResult *res,
                                            GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
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
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_events);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_setup_unsolicited_events_ready,
        result);
}

static void
parent_3gpp_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
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
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_3gpp_cleanup_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    /* With ^PORTSEL we specify whether we want the PCUI port (0) or the
     * modem port (1) to receive the unsolicited messages */
    { "^PORTSEL=0", 5, FALSE, NULL },
    { "^CURC=1",    3, FALSE, NULL },
    { NULL }
};

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
    }

    /* Our own enable now */
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        unsolicited_enable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_enable_unsolicited_events);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Disabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
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
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_disable_unsolicited_events);

    /* Our own disable first */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        "^CURC=0",
        5,
        FALSE, /* allow_cached */
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
huawei_modem_create_bearer_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMBearer *bearer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New huawei bearer created at DBus path '%s'", mm_bearer_get_path (bearer));
    return g_object_ref (bearer);
}

static void
broadband_bearer_huawei_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = ((GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (res), "huawei-bearer"))) ?
              mm_broadband_bearer_huawei_new_finish (res, &error) :
              mm_broadband_bearer_new_finish (res, &error));

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
huawei_modem_create_bearer (MMIfaceModem *self,
                            MMBearerProperties *properties,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMPort *data_port;

    data_port = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self));
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        huawei_modem_create_bearer);

    if (mm_port_get_port_type (data_port) == MM_PORT_TYPE_NET) {
        mm_dbg ("Creating huawei bearer...");
        g_object_set_data (G_OBJECT (result), "huawei-bearer", GUINT_TO_POINTER (TRUE));
        mm_broadband_bearer_huawei_new (MM_BROADBAND_MODEM_HUAWEI (self),
                                        properties,
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback)broadband_bearer_huawei_new_ready,
                                        result);
    } else {
        mm_dbg ("Creating default bearer...");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM(self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_huawei_new_ready,
                                 result);
   }
}


/*****************************************************************************/
/* USSD encode/decode (3GPP-USSD interface) */

static gchar *
encode (MMIfaceModem3gppUssd *self,
        const gchar *command,
        guint *scheme,
        GError **error)
{
    gchar *hex;
    guint8 *gsm, *packed;
    guint32 len = 0, packed_len = 0;

    *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;
    gsm = mm_charset_utf8_to_unpacked_gsm (command, &len);

    /* If command is a multiple of 7 characters long, Huawei firmwares
     * apparently want that padded.  Maybe all modems?
     */
    if (len % 7 == 0) {
        gsm = g_realloc (gsm, len + 1);
        gsm[len] = 0x0d;
        len++;
    }

    packed = gsm_pack (gsm, len, 0, &packed_len);
    hex = mm_utils_bin2hexstr (packed, packed_len);
    g_free (packed);
    g_free (gsm);

    return hex;
}

static gchar *
decode (MMIfaceModem3gppUssd *self,
        const gchar *reply,
        GError **error)
{
    gchar *bin, *utf8;
    guint8 *unpacked;
    gsize bin_len;
    guint32 unpacked_len;

    bin = mm_utils_hexstr2bin (reply, &bin_len);
    unpacked = gsm_unpack ((guint8*) bin, (bin_len * 8) / 7, 0, &unpacked_len);
    /* if the last character in a 7-byte block is padding, then drop it */
    if ((bin_len % 7 == 0) && (unpacked[unpacked_len - 1] == 0x0d))
        unpacked_len--;
    utf8 = (char*) mm_charset_gsm_unpacked_to_utf8 (unpacked, unpacked_len);

    g_free (bin);
    g_free (unpacked);
    return utf8;
}

/*****************************************************************************/

static void
huawei_1x_signal_changed (MMAtSerialPort *port,
                          GMatchInfo *match_info,
                          MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = CLAMP (quality, 0, 100);
    mm_dbg ("1X signal quality: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), (guint)quality);
}

static void
huawei_evdo_signal_changed (MMAtSerialPort *port,
                            GMatchInfo *match_info,
                            MMBroadbandModemHuawei *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = CLAMP (quality, 0, 100);
    mm_dbg ("EVDO signal quality: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), (guint)quality);
}

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

static void
signal_ready (MMBaseModem *self,
              GAsyncResult *res,
              GSimpleAsyncResult *simple)
{
    const gchar *response, *command;
    gchar buf[5];
    guint quality = 0, i = 0;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        /* Fallback to parent's method */
        iface_modem_parent->load_signal_quality (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            simple);
        return;
    }

    command = g_object_get_data (G_OBJECT (simple), "command");
    g_assert (command);
    response = mm_strip_tag (response, command);
    /* 'command' won't include the trailing ':' in the response, so strip that */
    while ((*response == ':') || isspace (*response))
        response++;

    /* Sanitize response for mm_get_uint_from_str() which wants only digits */
    memset (buf, 0, sizeof (buf));
    while (i < (sizeof (buf) - 1) && isdigit (*response))
        buf[i++] = *response++;

    if (mm_get_uint_from_str (buf, &quality)) {
        quality = CLAMP (quality, 0, 100);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (quality),
                                                   NULL);
    } else {
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse %s response: '%s'",
                                         command, response);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMModemCdmaRegistrationState evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    const char *command = "^CSQLVL";

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

    g_object_get (G_OBJECT (self),
                  MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, &evdo_state,
                  NULL);
    if (evdo_state > MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        command = "^HDRCSQLVL";
    g_object_set_data (G_OBJECT (result), "command", (gpointer) command);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)signal_ready,
        result);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (CDMA interface) */

static void
set_cdma_unsolicited_events_handlers (MMBroadbandModemHuawei *self,
                                      gboolean enable)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Signal quality related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->rssilvl_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_1x_signal_changed : NULL,
            enable ? self : NULL,
            NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->hrssilvl_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_evdo_signal_changed : NULL,
            enable ? self : NULL,
            NULL);
        /* Access technology related */
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->mode_regex,
            enable ? (MMAtSerialUnsolicitedMsgFn)huawei_mode_changed : NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_cdma_setup_cleanup_unsolicited_events_finish (MMIfaceModemCdma *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_cdma_setup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_cdma_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_cdma_setup_unsolicited_events (MMIfaceModemCdma *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_cdma_setup_unsolicited_events);

    /* Chain up parent's setup if needed */
    if (iface_modem_cdma_parent->setup_unsolicited_events &&
        iface_modem_cdma_parent->setup_unsolicited_events_finish) {
        iface_modem_cdma_parent->setup_unsolicited_events (
            self,
            (GAsyncReadyCallback)parent_cdma_setup_unsolicited_events_ready,
            result);
        return;
    }

    /* Otherwise just run our setup and complete */
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), TRUE);
    g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result), TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
parent_cdma_cleanup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                              GAsyncResult *res,
                                              GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_cdma_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_cdma_cleanup_unsolicited_events (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_cdma_cleanup_unsolicited_events);

    /* Our own cleanup first */
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);

    /* Chain up parent's setup if needed */
    if (iface_modem_cdma_parent->cleanup_unsolicited_events &&
        iface_modem_cdma_parent->cleanup_unsolicited_events_finish) {
        iface_modem_cdma_parent->cleanup_unsolicited_events (
            self,
            (GAsyncReadyCallback)parent_cdma_cleanup_unsolicited_events_ready,
            result);
        return;
    }

    /* Otherwise we're done */
    g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result), TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
        gboolean evdo_supported = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED, &evdo_supported,
                      NULL);

        /* Don't use AT+CSS on EVDO-capable hardware for determining registration
         * status, because often the device will have only an EVDO connection and
         * AT+CSS won't necessarily report EVDO registration status, only 1X.
         */
        if (evdo_supported)
            results.skip_at_cdma1x_serving_system_step = TRUE;

        /* Force to always use the detailed registration checks, as we have
         * ^SYSINFO for that */
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
sysinfo_ready (MMIfaceModemCdma *self,
               GAsyncResult *res,
               DetailedRegistrationStateContext *ctx)
{
    GError *error = NULL;
    const gchar *response;
    guint srv_stat = 0;
    guint sysmode = 0;
    guint roaming = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    /* If error, leave superclass' reg state alone if AT^SYSINFO isn't supported. */
    if (error) {
        g_error_free (error);

        /* NOTE: always complete NOT in idle here */
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    if (!sysinfo_parse (response, &srv_stat, NULL, &roaming, &sysmode, NULL, NULL, NULL, NULL)) {
        /* NOTE: always complete NOT in idle here */
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    if (srv_stat == 2) {
        MMModemCdmaRegistrationState reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;

        /* Service available, check roaming state */
        if (roaming == 0)
            reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
        else if (roaming == 1)
            reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;

        /* Check service type */
        if (sysmode == 2)
            ctx->state.detailed_cdma1x_state = reg_state;
        else if (sysmode == 4)
            ctx->state.detailed_evdo_state = reg_state;
        else if (sysmode == 8) {
            ctx->state.detailed_cdma1x_state = reg_state;
            ctx->state.detailed_evdo_state = reg_state;
        } else {
            /* Say we're registered to something even though sysmode parsing failed */
            mm_dbg ("SYSMODE parsing failed: assuming registered at least in CDMA1x");
            ctx->state.detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        }
    }

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
                              "^SYSINFO",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)sysinfo_ready,
                              ctx);
}

/*****************************************************************************/
/* Load network time (Time interface) */

static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    const gchar *response;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;
    guint year, month, day, hour, minute, second;
    gchar *result = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    /* Already in ISO-8601 format, but verify just to be sure */
    r = g_regex_new ("\\^TIME:\\s*(\\d+)/(\\d+)/(\\d+)\\s*(\\d+):(\\d+):(\\d*)$", 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, -1, 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse ^TIME results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match ^TIME reply");
        }
    } else {
        /* Remember that g_match_info_get_match_count() includes match #0 */
        g_assert (g_match_info_get_match_count (match_info) >= 7);

        if (mm_get_uint_from_match_info (match_info, 1, &year) &&
            mm_get_uint_from_match_info (match_info, 2, &month) &&
            mm_get_uint_from_match_info (match_info, 3, &day) &&
            mm_get_uint_from_match_info (match_info, 4, &hour) &&
            mm_get_uint_from_match_info (match_info, 5, &minute) &&
            mm_get_uint_from_match_info (match_info, 6, &second)) {
            /* Return ISO-8601 format date/time string */
            result = g_strdup_printf ("%04d/%02d/%02d %02d:%02d:%02d",
                                      year, month, day, hour, minute, second);
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse ^TIME reply");
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return result;
}

static void
modem_time_load_network_time (MMIfaceModemTime *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^TIME",
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
modem_time_check_ready (MMBroadbandModem *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
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

    /* Only CDMA devices support this at the moment */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "^TIME",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)modem_time_check_ready,
                              result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
set_ignored_unsolicited_events_handlers (MMBroadbandModemHuawei *self)
{
    MMAtSerialPort *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->boot_regex,
            NULL, NULL, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->csnr_regex,
            NULL, NULL, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->simst_regex,
            NULL, NULL, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->srvst_regex,
            NULL, NULL, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            self->priv->stin_regex,
            NULL, NULL, NULL);
    }
}

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_huawei_parent_class)->setup_ports (self);

    /* Unsolicited messages to always ignore */
    set_ignored_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self));

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_3gpp_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);
    set_cdma_unsolicited_events_handlers (MM_BROADBAND_MODEM_HUAWEI (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemHuawei *
mm_broadband_modem_huawei_new (const gchar *device,
                               const gchar **drivers,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_HUAWEI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_huawei_init (MMBroadbandModemHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_HUAWEI,
                                              MMBroadbandModemHuaweiPrivate);
    /* Prepare regular expressions to setup */
    self->priv->rssi_regex = g_regex_new ("\\r\\n\\^RSSI:(\\d+)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->rssilvl_regex = g_regex_new ("\\r\\n\\^RSSILVL:(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->hrssilvl_regex = g_regex_new ("\\r\\n\\^HRSSILVL:(\\d+)\\r\\n",
                                              G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    /* 3GPP: <cr><lf>^MODE:5<cr><lf>
     * CDMA: <cr><lf>^MODE: 2<cr><cr><lf>
     */
    self->priv->mode_regex = g_regex_new ("\\r\\n\\^MODE:\\s*(\\d*),?(\\d*)\\r+\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->dsflowrpt_regex = g_regex_new ("\\r\\n\\^DSFLOWRPT:(.+)\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->boot_regex = g_regex_new ("\\r\\n\\^BOOT:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->csnr_regex = g_regex_new ("\\r\\n\\^CSNR:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->simst_regex = g_regex_new ("\\r\\n\\^SIMST:.+\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->srvst_regex = g_regex_new ("\\r\\n\\^SRVST:.+\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->stin_regex = g_regex_new ("\\r\\n\\^STIN:.+\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemHuawei *self = MM_BROADBAND_MODEM_HUAWEI (object);

    g_regex_unref (self->priv->rssi_regex);
    g_regex_unref (self->priv->rssilvl_regex);
    g_regex_unref (self->priv->hrssilvl_regex);
    g_regex_unref (self->priv->mode_regex);
    g_regex_unref (self->priv->dsflowrpt_regex);
    g_regex_unref (self->priv->boot_regex);
    g_regex_unref (self->priv->csnr_regex);
    g_regex_unref (self->priv->simst_regex);
    g_regex_unref (self->priv->srvst_regex);
    g_regex_unref (self->priv->stin_regex);

    G_OBJECT_CLASS (mm_broadband_modem_huawei_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_bands = set_bands;
    iface->set_bands_finish = set_bands_finish;
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
    iface->create_bearer = huawei_modem_create_bearer;
    iface->create_bearer_finish = huawei_modem_create_bearer_finish;
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
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;
}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface)
{
    iface->encode = encode;
    iface->decode = decode;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface_modem_cdma_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_cdma_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_cdma_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
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
mm_broadband_modem_huawei_class_init (MMBroadbandModemHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemHuaweiPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;
}
