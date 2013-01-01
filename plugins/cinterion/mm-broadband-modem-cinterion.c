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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2011 Google Inc.
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-cinterion.h"

static void iface_modem_init      (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemCinterion, mm_broadband_modem_cinterion, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init));

struct _MMBroadbandModemCinterionPrivate {
    /* Flag to know if we should try AT^SIND or not to get psinfo */
    gboolean sind_psinfo;

    /* Command to go into sleep mode */
    gchar *sleep_mode_cmd;
};

/* Setup relationship between the band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    gchar *cinterion_band;
    guint n_mm_bands;
    MMModemBand mm_bands [4];
} CinterionBand2G;

/* Table checked in both MC75i (GPRS/EDGE) and EGS5 (GPRS) references.
 * Note that the modem's configuration is also based on a bitmask, but as we
 * will just support some of the combinations, we just use strings for them.
 */
static const CinterionBand2G bands_2g[] = {
    { "1",  1, { MM_MODEM_BAND_EGSM, 0, 0, 0 }},
    { "2",  1, { MM_MODEM_BAND_DCS,  0, 0, 0 }},
    { "4",  1, { MM_MODEM_BAND_PCS,  0, 0, 0 }},
    { "8",  1, { MM_MODEM_BAND_G850, 0, 0, 0 }},
    { "3",  2, { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, 0, 0 }},
    { "5",  2, { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_PCS, 0, 0 }},
    { "10", 2, { MM_MODEM_BAND_G850, MM_MODEM_BAND_DCS, 0, 0 }},
    { "12", 2, { MM_MODEM_BAND_G850, MM_MODEM_BAND_PCS, 0, 0 }},
    { "15", 4, { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS, MM_MODEM_BAND_G850 }}
};

/* Setup relationship between the 3G band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    guint32 cinterion_band_flag;
    MMModemBand mm_band;
} CinterionBand3G;

/* Table checked in HC25 (3G) reference. This table includes both 2G and 3G
 * frequencies. Depending on which one is configured, one access technology or
 * the other will be used. This may conflict with the allowed mode configuration
 * set, so you shouldn't for example set 3G frequency bands, and then use a
 * 2G-only allowed mode. */
static const CinterionBand3G bands_3g[] = {
    { (1 << 0), MM_MODEM_BAND_EGSM  },
    { (1 << 1), MM_MODEM_BAND_DCS   },
    { (1 << 2), MM_MODEM_BAND_PCS   },
    { (1 << 3), MM_MODEM_BAND_G850  },
    { (1 << 4), MM_MODEM_BAND_U2100 },
    { (1 << 5), MM_MODEM_BAND_U1900 },
    { (1 << 6), MM_MODEM_BAND_U850  }
};

/*****************************************************************************/
/* Unsolicited events enabling */

static gboolean
enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
enable_unsolicited_events (MMIfaceModem3gpp *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    /* AT=CMER=[<mode>[,<keyp>[,<disp>[,<ind>[,<bfr>]]]]]
     *  but <ind> should be either not set, or equal to 0 or 2.
     * Enabled with 2.
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CMER=3,0,0,2",
                              3,
                              FALSE,
                              callback,
                              user_data);
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
    /* AT+CNMI=<mode>,[<mt>[,<bm>[,<ds>[,<bfr>]]]]
     *  but <bfr> should be either not set, or equal to 1;
     *  and <ds> can be only either 0 or 2 (EGS5)
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=2,1,2,2,1",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* MODEM POWER DOWN */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
sleep_ready (MMBaseModem *self,
             GAsyncResult *res,
             GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    /* Ignore errors */
    if (error) {
        mm_dbg ("Couldn't send power down command: '%s'", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
send_sleep_mode_command (MMBroadbandModemCinterion *self,
                         GSimpleAsyncResult *operation_result)
{
    if (self->priv->sleep_mode_cmd &&
        self->priv->sleep_mode_cmd[0]) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  self->priv->sleep_mode_cmd,
                                  5,
                                  FALSE,
                                  (GAsyncReadyCallback)sleep_ready,
                                  operation_result);
        return;
    }

    /* No default command; just finish without sending anything */
    g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete_in_idle (operation_result);
    g_object_unref (operation_result);
}

static void
supported_functionality_status_query_ready (MMBroadbandModemCinterion *self,
                                            GAsyncResult *res,
                                            GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;

    g_assert (self->priv->sleep_mode_cmd == NULL);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_warn ("Couldn't query supported functionality status: '%s'",
                 error->message);
        g_error_free (error);
        self->priv->sleep_mode_cmd = g_strdup ("");
    } else {
        /* We need to get which power-off command to use to put the modem in low
         * power mode (with serial port open for AT commands, but with RF switched
         * off). According to the documentation of various Cinterion modems, some
         * support AT+CFUN=4 (HC25) and those which don't support it can use
         * AT+CFUN=7 (CYCLIC SLEEP mode with 2s timeout after last character
         * received in the serial port).
         *
         * So, just look for '4' in the reply; if not found, look for '7', and if
         * not found, report warning and don't use any.
         */
        if (strstr (response, "4") != NULL) {
            mm_dbg ("Device supports CFUN=4 sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("+CFUN=4");
        } else if (strstr (response, "7") != NULL) {
            mm_dbg ("Device supports CFUN=7 sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("+CFUN=7");
        } else {
            mm_warn ("Unknown functionality mode to go into sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("");
        }
    }

    send_sleep_mode_command (self, operation_result);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MMBroadbandModemCinterion *cinterion = MM_BROADBAND_MODEM_CINTERION (self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_down);

    /* If sleep command already decided, use it. */
    if (cinterion->priv->sleep_mode_cmd)
        send_sleep_mode_command (MM_BROADBAND_MODEM_CINTERION (self),
                                 result);
    else
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "+CFUN=?",
            3,
            FALSE,
            (GAsyncReadyCallback)supported_functionality_status_query_ready,
            result);
}

/*****************************************************************************/
/* ACCESS TECHNOLOGIES */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    *access_technologies = (MMModemAccessTechnology) GPOINTER_TO_UINT (
        g_simple_async_result_get_op_res_gpointer (
            G_SIMPLE_ASYNC_RESULT (res)));
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static MMModemAccessTechnology
get_access_technology_from_smong_gprs_status (const gchar *gprs_status,
                                              GError **error)
{
    if (strlen (gprs_status) == 1) {
        switch (gprs_status[0]) {
        case '0':
            return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        case '1':
        case '2':
            return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        case '3':
        case '4':
            return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't get network capabilities, "
                 "invalid GPRS status value: '%s'",
                 gprs_status);
    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
smong_query_ready (MMBroadbandModemCinterion *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* The AT^SMONG command returns a cell info table, where the second
     * column identifies the "GPRS status", which is exactly what we want.
     * So we'll try to read that second number in the values row.
     *
     * AT^SMONG
     * GPRS Monitor
     * BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC    # Cell #
     * 0776  1  -      -   214   03  2    00      01
     * OK
     */
    regex = g_regex_new (".*GPRS Monitor\\r\\n"
                         "BCCH\\s*G.*\\r\\n"
                         "(\\d*)\\s*(\\d*)\\s*", 0, 0, NULL);
    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, NULL)) {
        gchar *gprs_status;
        MMModemAccessTechnology act;

        gprs_status = g_match_info_fetch (match_info, 2);
        act = get_access_technology_from_smong_gprs_status (gprs_status, &error);
        g_free (gprs_status);

        if (error)
            g_simple_async_result_take_error (operation_result, error);
        else {
            /* We'll default to use SMONG then */
            self->priv->sind_psinfo = FALSE;
            g_simple_async_result_set_op_res_gpointer (operation_result,
                                                       GUINT_TO_POINTER (act),
                                                       NULL);
        }
    } else {
        /* We'll reset here the flag to try to use SIND/psinfo the next time */
        self->priv->sind_psinfo = TRUE;

        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_INVALID_ARGS,
                                         "Couldn't get network capabilities, "
                                         "invalid SMONG reply: '%s'",
                                         response);
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static MMModemAccessTechnology
get_access_technology_from_psinfo (const gchar *psinfo,
                                   GError **error)
{
    if (strlen (psinfo) == 1) {
        switch (psinfo[0]) {
        case '0':
            return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        case '1':
        case '2':
            return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        case '3':
        case '4':
            return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
        case '5':
        case '6':
            return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        case '7':
        case '8':
            return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't get network capabilities, "
                 "invalid psinfo value: '%s'",
                 psinfo);
    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
sind_query_ready (MMBroadbandModemCinterion *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* The AT^SIND? command replies a list of several different indicators.
     * We will only look for 'psinfo' which is the one which may tell us
     * the available network access technology. Note that only 3G-enabled
     * devices seem to have this indicator.
     *
     * AT+SIND?
     * ^SIND: battchg,1,1
     * ^SIND: signal,1,99
     * ...
     */
    regex = g_regex_new ("\\r\\n\\^SIND:\\s*psinfo,\\s*(\\d*),\\s*(\\d*)", 0, 0, NULL);
    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, NULL)) {
        MMModemAccessTechnology act;
        gchar *ind_value;

        ind_value = g_match_info_fetch (match_info, 2);
        act = get_access_technology_from_psinfo (ind_value, &error);
        g_free (ind_value);
        g_simple_async_result_set_op_res_gpointer (operation_result, GUINT_TO_POINTER (act), NULL);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
    } else {
        /* If there was no 'psinfo' indicator, we'll try AT^SMONG and read the cell
         * info table. */
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SMONG",
            3,
            FALSE,
            (GAsyncReadyCallback)smong_query_ready,
            operation_result);
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MMBroadbandModemCinterion *broadband = MM_BROADBAND_MODEM_CINTERION (self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_access_technologies);

    if (broadband->priv->sind_psinfo) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SIND?",
            3,
            FALSE,
            (GAsyncReadyCallback)sind_query_ready,
            result);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "^SMONG",
        3,
        FALSE,
        (GAsyncReadyCallback)smong_query_ready,
        result);
}

/*****************************************************************************/
/* ALLOWED MODES */

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_access_technology_update_ready (MMBroadbandModemCinterion *self,
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

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    /* For dual 2G/3G devices... */
    if (mm_iface_modem_is_2g (self) &&
        mm_iface_modem_is_3g (self)) {
        GString *cmd;

        /* We will try to simulate the possible allowed modes here. The
         * Cinterion devices do not seem to allow setting preferred access
         * technology in 3G devices, but they allow restricting to a given
         * one:
         * - 2G-only is forced by forcing GERAN RAT (AcT=0)
         * - 3G-only is forced by forcing UTRAN RAT (AcT=2)
         * - for the remaining ones, we default to automatic selection of RAT,
         *   which is based on the quality of the connection.
         */
        cmd = g_string_new ("+COPS=,,,");
        if (allowed == MM_MODEM_MODE_3G &&
            preferred == MM_MODEM_MODE_NONE) {
            g_string_append (cmd, "2");
        } else if (allowed == MM_MODEM_MODE_2G &&
                   preferred == MM_MODEM_MODE_NONE) {
            g_string_append (cmd, "0");
        } else {
            gchar *allowed_str;
            gchar *preferred_str;

            /* no AcT given, defaults to Auto */
            allowed_str = mm_modem_mode_build_string_from_mask (allowed);
            preferred_str = mm_modem_mode_build_string_from_mask (preferred);
            mm_warn ("Requested mode (allowed: '%s', preferred: '%s') not "
                     "supported by the modem. Defaulting to automatic mode.",
                     allowed_str,
                     preferred_str);
            g_free (allowed_str);
            g_free (preferred_str);
        }

        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            cmd->str,
            3,
            FALSE,
            (GAsyncReadyCallback)allowed_access_technology_update_ready,
            result);
        g_string_free (cmd, TRUE);
        return;
    }

    /* For 3G-only devices, allow only 3G-related allowed modes.
     * For 2G-only devices, allow only 2G-related allowed modes.
     *
     * Note that the common logic of the interface already limits the
     * allowed/preferred modes that can be tried in these cases. */
    if (mm_iface_modem_is_2g_only (self) ||
        mm_iface_modem_is_3g_only (self)) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        mm_dbg ("Not doing anything. Assuming requested mode "
                "(allowed: '%s', preferred: '%s') is supported by "
                "%s-only modem.",
                allowed_str,
                preferred_str,
                mm_iface_modem_is_3g_only (self) ? "3G" : "2G");
        g_free (allowed_str);
        g_free (preferred_str);
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* SUPPORTED BANDS */

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    /* Never fails */
    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;
    GArray *bands;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_bands);

    /* We do assume that we already know if the modem is 2G-only, 3G-only or
     * 2G+3G. This is checked quite before trying to load supported bands. */

#define _g_array_insert_enum(array,index,type,val) do { \
        type aux = (type)val;                           \
        g_array_insert_val (array, index, aux);         \
    } while (0)

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
    _g_array_insert_enum (bands, 0, MMModemBand, MM_MODEM_BAND_EGSM);
    _g_array_insert_enum (bands, 1, MMModemBand, MM_MODEM_BAND_DCS);
    _g_array_insert_enum (bands, 2, MMModemBand, MM_MODEM_BAND_PCS);
    _g_array_insert_enum (bands, 3, MMModemBand, MM_MODEM_BAND_G850);

    /* Add 3G-specific bands */
    if (mm_iface_modem_is_3g (self)) {
        g_array_set_size (bands, 7);
        _g_array_insert_enum (bands, 4, MMModemBand, MM_MODEM_BAND_U2100);
        _g_array_insert_enum (bands, 5, MMModemBand, MM_MODEM_BAND_U1900);
        _g_array_insert_enum (bands, 6, MMModemBand, MM_MODEM_BAND_U850);
    }

    g_simple_async_result_set_op_res_gpointer (result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* CURRENT BANDS */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
get_2g_band_ready (MMBroadbandModemCinterion *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    GArray *bands_array = NULL;
    GRegex *regex;
    GMatchInfo *match_info = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* The AT^SCFG? command replies a list of several different config
     * values. We will only look for 'Radio/Band".
     *
     * AT+SCFG="Radio/Band"
     * ^SCFG: "Radio/Band","0031","0031"
     *
     * Note that "0031" is a UCS2-encoded string, as we configured UCS2 as
     * character set to use.
     */
    regex = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\s*\"(.*)\",\\s*\"(.*)\"", 0, 0, NULL);
    g_assert (regex != NULL);

    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, NULL)) {
        gchar *current;

        /* The first number given is the current band configuration, the
         * second number given is the allowed band configuration, which we
         * don't really need to get here. */
        current = g_match_info_fetch (match_info, 1);
        if (current) {
            guint i;

            /* If in UCS2, convert to UTF-8 */
            current = mm_broadband_modem_take_and_convert_to_utf8 (MM_BROADBAND_MODEM (self),
                                                                   current);

            for (i = 0; i < G_N_ELEMENTS (bands_2g); i++) {
                if (strcmp (bands_2g[i].cinterion_band, current) == 0) {
                    guint j;

                    if (G_UNLIKELY (!bands_array))
                        bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

                    for (j = 0; j < bands_2g[i].n_mm_bands; j++)
                        g_array_append_val (bands_array, bands_2g[i].mm_bands[j]);

                    break;
                }
            }

            g_free (current);
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (regex);

    if (!bands_array)
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse current bands reply");
    else
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   bands_array,
                                                   (GDestroyNotify)g_array_unref);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
get_3g_band_ready (MMBroadbandModemCinterion *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    GArray *bands_array = NULL;
    GRegex *regex;
    GMatchInfo *match_info = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* The AT^SCFG? command replies a list of several different config
     * values. We will only look for 'Radio/Band".
     *
     * AT+SCFG="Radio/Band"
     * ^SCFG: "Radio/Band",127
     *
     * Note that in this case, the <rba> replied is a number, not a string.
     */
    regex = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\s*(\\d*)", 0, 0, NULL);
    g_assert (regex != NULL);

    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, NULL)) {
        gchar *current;

        current = g_match_info_fetch (match_info, 1);
        if (current) {
            guint32 current_int;
            guint i;

            current_int = (guint32) atoi (current);

            for (i = 0; i < G_N_ELEMENTS (bands_3g); i++) {
                if (current_int & bands_3g[i].cinterion_band_flag) {
                    if (G_UNLIKELY (!bands_array))
                        bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
                    g_array_append_val (bands_array, bands_3g[i].mm_band);
                }
            }

            g_free (current);
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (regex);

    if (!bands_array)
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse current bands reply");
    else
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   bands_array,
                                                   (GDestroyNotify)g_array_unref);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_bands);

    /* Query the currently used Radio/Band. The query command is the same for
     * both 2G and 3G devices, but the reply reader is different. */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT^SCFG=\"Radio/Band\"",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)(mm_iface_modem_is_3g (self) ?
                                                    get_3g_band_ready :
                                                    get_2g_band_ready),
                              result);
}

/*****************************************************************************/
/* SET BANDS */

static gboolean
set_bands_finish (MMIfaceModem *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
scfg_set_ready (MMBaseModem *self,
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
set_bands_3g (MMIfaceModem *self,
              GArray *bands_array,
              GSimpleAsyncResult *result)
{
    GArray *bands_array_final;
    guint cinterion_band = 0;
    guint i;
    gchar *bands_string;
    gchar *cmd;

    /* The special case of ANY should be treated separately. */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        /* We build an array with all bands to set; so that we use the same
         * logic to build the cinterion_band, and so that we can log the list of
         * bands being set properly */
        bands_array_final = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), G_N_ELEMENTS (bands_3g));
        for (i = 0; i < G_N_ELEMENTS (bands_3g); i++)
            g_array_append_val (bands_array_final, bands_3g[i].mm_band);
    } else
        bands_array_final = g_array_ref (bands_array);

    for (i = 0; i < G_N_ELEMENTS (bands_3g); i++) {
        guint j;

        for (j = 0; j < bands_array_final->len; j++) {
            if (g_array_index (bands_array_final, MMModemBand, j) == bands_3g[i].mm_band) {
                cinterion_band |= bands_3g[i].cinterion_band_flag;
                break;
            }
        }
    }

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array_final->data,
                                                 bands_array_final->len);
    g_array_unref (bands_array_final);

    if (!cinterion_band) {
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "The given band combination is not supported: '%s'",
                                         bands_string);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        g_free (bands_string);
        return;
    }

    mm_dbg ("Setting new bands to use: '%s'", bands_string);

    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing a number here!
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",%u", cinterion_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              15,
                              FALSE,
                              (GAsyncReadyCallback)scfg_set_ready,
                              result);
    g_free (cmd);
    g_free (bands_string);
}

static void
set_bands_2g (MMIfaceModem *self,
              GArray *bands_array,
              GSimpleAsyncResult *result)
{
    GArray *bands_array_final;
    gchar *cinterion_band = NULL;
    guint i;
    gchar *bands_string;
    gchar *cmd;

    /* If the iface properly checked the given list against the supported bands,
     * it's not possible to get an array longer than 4 here. */
    g_assert (bands_array->len <= 4);

    /* The special case of ANY should be treated separately. */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        const CinterionBand2G *all;

        /* All bands is the last element in our 2G bands array */
        all = &bands_2g[G_N_ELEMENTS (bands_2g) - 1];

        /* We build an array with all bands to set; so that we use the same
         * logic to build the cinterion_band, and so that we can log the list of
         * bands being set properly */
        bands_array_final = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
        g_array_append_vals (bands_array_final, all->mm_bands, all->n_mm_bands);
    } else
        bands_array_final = g_array_ref (bands_array);

    for (i = 0; !cinterion_band && i < G_N_ELEMENTS (bands_2g); i++) {
        GArray *supported_combination;

        supported_combination = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), bands_2g[i].n_mm_bands);
        g_array_append_vals (supported_combination, bands_2g[i].mm_bands, bands_2g[i].n_mm_bands);

        /* Check if the given array is exactly one of the supported combinations */
        if (mm_common_bands_garray_cmp (bands_array_final, supported_combination))
            cinterion_band = g_strdup (bands_2g[i].cinterion_band);

        g_array_unref (supported_combination);
    }

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array_final->data,
                                                 bands_array_final->len);
    g_array_unref (bands_array_final);

    if (!cinterion_band) {
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "The given band combination is not supported: '%s'",
                                         bands_string);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        g_free (bands_string);
        return;
    }


    mm_dbg ("Setting new bands to use: '%s'", bands_string);
    cinterion_band = (mm_broadband_modem_take_and_convert_to_current_charset (
                          MM_BROADBAND_MODEM (self),
                          cinterion_band));
    if (!cinterion_band) {
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Couldn't convert band set to current charset");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        g_free (bands_string);
        return;
    }

    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rbp>,<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing double-quote enclosed strings here!
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",\"%s\",\"%s\"",
                           cinterion_band,
                           cinterion_band);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              15,
                              FALSE,
                              (GAsyncReadyCallback)scfg_set_ready,
                              result);

    g_free (cmd);
    g_free (cinterion_band);
    g_free (bands_string);
}

static void
set_bands (MMIfaceModem *self,
           GArray *bands_array,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* The bands that we get here are previously validated by the interface, and
     * that means that ALL the bands given here were also given in the list of
     * supported bands. BUT BUT, that doesn't mean that the exact list of bands
     * will end up being valid, as not all combinations are possible. E.g,
     * Cinterion modems supporting only 2G have specific combinations allowed.
     */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_bands);

    if (mm_iface_modem_is_3g (self))
        set_bands_3g (self, bands_array, result);
    else
        set_bands_2g (self, bands_array, result);
}

/*****************************************************************************/
/* FLOW CONTROL */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_flow_control_ready (MMBroadbandModemCinterion *self,
                          GAsyncResult *res,
                          GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical. We DO need RTS/CTS in order to have
         * proper modem disabling. */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_flow_control);

    /* We need to enable RTS/CTS so that CYCLIC SLEEP mode works */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "\\Q3",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              result);
}

/*****************************************************************************/

MMBroadbandModemCinterion *
mm_broadband_modem_cinterion_new (const gchar *device,
                                  const gchar **drivers,
                                  const gchar *plugin,
                                  guint16 vendor_id,
                                  guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_CINTERION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_cinterion_init (MMBroadbandModemCinterion *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_CINTERION,
                                              MMBroadbandModemCinterionPrivate);

    /* Set defaults */
    self->priv->sind_psinfo = TRUE; /* Initially, always try to get psinfo */
}

static void
finalize (GObject *object)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (object);

    g_free (self->priv->sleep_mode_cmd);

    G_OBJECT_CLASS (mm_broadband_modem_cinterion_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_bands = set_bands;
    iface->set_bands_finish = set_bands_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface->enable_unsolicited_events = enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = enable_unsolicited_events_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
}

static void
mm_broadband_modem_cinterion_class_init (MMBroadbandModemCinterionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemCinterionPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
