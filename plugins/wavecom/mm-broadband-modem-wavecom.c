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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-wavecom.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemWavecom, mm_broadband_modem_wavecom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

#define WAVECOM_MS_CLASS_CC_IDSTR "\"CC\""
#define WAVECOM_MS_CLASS_CG_IDSTR "\"CG\""
#define WAVECOM_MS_CLASS_B_IDSTR  "\"B\""
#define WAVECOM_MS_CLASS_A_IDSTR  "\"A\""

/* Setup relationship between 2G bands in the modem (identified by a
 * single digit in ASCII) and the bitmask in ModemManager. */
typedef struct {
    gchar wavecom_band;
    guint n_mm_bands;
    MMModemBand mm_bands[4];
} WavecomBand2G;
static const WavecomBand2G bands_2g[] = {
    { '0', 1, { MM_MODEM_BAND_G850, 0, 0, 0 }},
    { '1', 1, { MM_MODEM_BAND_EGSM, 0, 0, 0 }},
    { '2', 1, { MM_MODEM_BAND_DCS,  0, 0, 0 }},
    { '3', 1, { MM_MODEM_BAND_PCS,  0, 0, 0 }},
    { '4', 2, { MM_MODEM_BAND_G850, MM_MODEM_BAND_PCS, 0, 0 }},
    { '5', 2, { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, 0, 0 }},
    { '6', 2, { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_PCS, 0, 0 }},
    { '7', 4, { MM_MODEM_BAND_DCS,  MM_MODEM_BAND_PCS, MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM }}
};

/* Setup relationship between the 3G band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    guint32 wavecom_band_flag;
    MMModemBand mm_band;
} WavecomBand3G;
static const WavecomBand3G bands_3g[] = {
    { (1 << 0), MM_MODEM_BAND_U2100 },
    { (1 << 1), MM_MODEM_BAND_U1900 },
    { (1 << 2), MM_MODEM_BAND_U1800 },
    { (1 << 3), MM_MODEM_BAND_U17IV },
    { (1 << 4), MM_MODEM_BAND_U850  },
    { (1 << 5), MM_MODEM_BAND_U800  },
    { (1 << 6), MM_MODEM_BAND_U2600 },
    { (1 << 7), MM_MODEM_BAND_U900  },
    { (1 << 8), MM_MODEM_BAND_U17IX }
};

/*****************************************************************************/
/* Supported modes (Modem interface) */

static MMModemMode
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_MODE_NONE;

    return (MMModemMode) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                               G_SIMPLE_ASYNC_RESULT (res)));
}

static void
supported_ms_classes_query_ready (MMBaseModem *self,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;
    MMModemMode mode;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");
    mode = MM_MODEM_MODE_NONE;

    if (strstr (response, WAVECOM_MS_CLASS_A_IDSTR)) {
        mm_dbg ("Modem supports Class A mobile station");
        mode |= MM_MODEM_MODE_3G;
    }

    if (strstr (response, WAVECOM_MS_CLASS_B_IDSTR)) {
        mm_dbg ("Modem supports Class B mobile station");
        mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
    }

    if (strstr (response, WAVECOM_MS_CLASS_CG_IDSTR)) {
        mm_dbg ("Modem supports Class CG mobile station");
        mode |= MM_MODEM_MODE_2G;
    }

    if (strstr (response, WAVECOM_MS_CLASS_CC_IDSTR)) {
        mm_dbg ("Modem supports Class CC mobile station");
        mode |= MM_MODEM_MODE_CS;
    }

    /* If none received, error */
    if (mode == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get supported mobile station classes: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (mode),
                                                   NULL);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_modes);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CGCLASS=?",
        3,
        FALSE,
        (GAsyncReadyCallback)supported_ms_classes_query_ready,
        result);
}

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

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
wwsm_read_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    LoadAllowedModesResult result;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    result.allowed = MM_MODEM_MODE_NONE;
    result.preferred = MM_MODEM_MODE_NONE;

    /* Possible responses:
     *   +WWSM: 0    (2G only)
     *   +WWSM: 1    (3G only)
     *   +WWSM: 2,0  (Any)
     *   +WWSM: 2,1  (2G preferred)
     *   +WWSM: 2,2  (3G preferred)
     */
    r = g_regex_new ("\\r\\n\\+WWSM: ([0-2])(,([0-2]))?.*$", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        guint allowed = 0;

        if (mm_get_uint_from_match_info (match_info, 1, &allowed)) {
            switch (allowed) {
            case 0:
                result.allowed = MM_MODEM_MODE_2G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 1:
                result.allowed = MM_MODEM_MODE_3G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 2: {
                guint preferred = 0;

                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

                /* 3, to avoid the comma */
                if (mm_get_uint_from_match_info (match_info, 3, &preferred)) {
                    switch (preferred) {
                    case 0:
                        result.preferred = MM_MODEM_MODE_NONE;
                        break;
                    case 1:
                        result.preferred = MM_MODEM_MODE_2G;
                        break;
                    case 2:
                        result.preferred = MM_MODEM_MODE_3G;
                        break;
                    default:
                        g_warn_if_reached ();
                        break;
                    }
                }
                break;
            }
            default:
                g_warn_if_reached ();
                break;
            }
        }
    }

    if (result.allowed == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Unknown wireless data service reply: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (simple, &result, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);

    g_regex_unref (r);
    if (match_info)
        g_match_info_free (match_info);
}

static void
current_ms_class_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");

    if (strncmp (response,
                 WAVECOM_MS_CLASS_A_IDSTR,
                 strlen (WAVECOM_MS_CLASS_A_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class A mobile station");
        /* For 3G devices, query WWSM status */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+WWSM?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)wwsm_read_ready,
                                  simple);
        return;
    }

    result.allowed = MM_MODEM_MODE_NONE;
    result.preferred = MM_MODEM_MODE_NONE;

    if (strncmp (response,
                 WAVECOM_MS_CLASS_B_IDSTR,
                 strlen (WAVECOM_MS_CLASS_B_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class B mobile station");
        result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
        result.preferred = MM_MODEM_MODE_2G;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CG_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CG_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CG mobile station");
        result.allowed = MM_MODEM_MODE_2G;
        result.preferred = MM_MODEM_MODE_NONE;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CC_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CC_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CC mobile station");
        result.allowed = MM_MODEM_MODE_CS;
        result.preferred = MM_MODEM_MODE_NONE;
    }

    if (result.allowed == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Unknown mobile station class: '%s'",
                                         response);
    else
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

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGCLASS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)current_ms_class_ready,
                              result);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    MMBroadbandModemWavecom *self;
    GSimpleAsyncResult *result;
    MMModemMode allowed;
    MMModemMode preferred;
    gchar *cgclass_command;
    gchar *wwsm_command;
} SetAllowedModesContext;

static void
set_allowed_modes_context_complete_and_free (SetAllowedModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx->cgclass_command);
    g_free (ctx->wwsm_command);
    g_free (ctx);
}

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
wwsm_update_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   SetAllowedModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    set_allowed_modes_context_complete_and_free (ctx);
}

static void
cgclass_update_ready (MMBaseModem *self,
                      GAsyncResult *res,
                      SetAllowedModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    if (!ctx->wwsm_command) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->wwsm_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wwsm_update_ready,
                              ctx);
}

static void
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    SetAllowedModesContext *ctx;

    ctx = g_new0 (SetAllowedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_allowed_modes);
    ctx->allowed = allowed;
    ctx->preferred = preferred;

    if (allowed == MM_MODEM_MODE_CS)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CC_IDSTR);
    else if (allowed == MM_MODEM_MODE_2G)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CG_IDSTR);
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS) &&
             preferred == MM_MODEM_MODE_NONE)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_B_IDSTR);
    else if (allowed & MM_MODEM_MODE_3G) {
        if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G) ||
            allowed == (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
            if (preferred == MM_MODEM_MODE_2G)
                ctx->wwsm_command = g_strdup ("+WWSM=2,1");
            else if (preferred == MM_MODEM_MODE_3G)
                ctx->wwsm_command = g_strdup ("+WWSM=2,2");
            else if (preferred == MM_MODEM_MODE_NONE)
                ctx->wwsm_command = g_strdup ("+WWSM=2,0");
        } else if (allowed == MM_MODEM_MODE_3G)
            ctx->wwsm_command = g_strdup ("+WWSM=1");

        if (ctx->wwsm_command)
            ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_A_IDSTR);
    }

    if (!ctx->cgclass_command) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Requested mode (allowed: '%s', preferred: '%s') not "
                                         "supported by the modem.",
                                         allowed_str,
                                         preferred_str);
        g_free (allowed_str);
        g_free (preferred_str);

        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->cgclass_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cgclass_update_ready,
                              ctx);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

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

    /* Add 3G-specific bands */
    if (mm_iface_modem_is_3g (self)) {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 10);
        _g_array_insert_enum (bands, 0, MMModemBand, MM_MODEM_BAND_U2100);
        _g_array_insert_enum (bands, 1, MMModemBand, MM_MODEM_BAND_U1800);
        _g_array_insert_enum (bands, 2, MMModemBand, MM_MODEM_BAND_U17IV);
        _g_array_insert_enum (bands, 3, MMModemBand, MM_MODEM_BAND_U800);
        _g_array_insert_enum (bands, 4, MMModemBand, MM_MODEM_BAND_U850);
        _g_array_insert_enum (bands, 5, MMModemBand, MM_MODEM_BAND_U900);
        _g_array_insert_enum (bands, 6, MMModemBand, MM_MODEM_BAND_U900);
        _g_array_insert_enum (bands, 7, MMModemBand, MM_MODEM_BAND_U17IX);
        _g_array_insert_enum (bands, 8, MMModemBand, MM_MODEM_BAND_U1900);
        _g_array_insert_enum (bands, 9, MMModemBand, MM_MODEM_BAND_U2600);
    }
    /* Add 2G-specific bands */
    else {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
        _g_array_insert_enum (bands, 0, MMModemBand, MM_MODEM_BAND_EGSM);
        _g_array_insert_enum (bands, 1, MMModemBand, MM_MODEM_BAND_DCS);
        _g_array_insert_enum (bands, 2, MMModemBand, MM_MODEM_BAND_PCS);
        _g_array_insert_enum (bands, 3, MMModemBand, MM_MODEM_BAND_G850);
    }

    g_simple_async_result_set_op_res_gpointer (result,
                                               bands,
                                               (GDestroyNotify)g_array_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

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
get_2g_band_ready (MMBroadbandModemWavecom *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    const gchar *p;
    GError *error = NULL;
    GArray *bands_array = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    p = mm_strip_tag (response, "+WMBS:");
    if (p) {
        guint i;

        for (i = 0; i < G_N_ELEMENTS (bands_2g); i++) {
            if (bands_2g[i].wavecom_band == *p) {
                guint j;

                if (G_UNLIKELY (!bands_array))
                    bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

                for (j = 0; j < bands_2g[i].n_mm_bands; j++)
                    g_array_append_val (bands_array, bands_2g[i].mm_bands[j]);
            }
        }
    }

    if (!bands_array)
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse current bands reply: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (operation_result,
                                                   bands_array,
                                                   (GDestroyNotify)g_array_unref);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
get_3g_band_ready (MMBroadbandModemWavecom *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    const gchar *p;
    GError *error = NULL;
    GArray *bands_array = NULL;
    guint32 wavecom_band;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* Example reply:
     *   AT+WUBS? -->
     *            <-- +WUBS: "3",1
     *            <-- OK
     * The "3" meaning here Band I and II are selected.
     */

    p = mm_strip_tag (response, "+WUBS:");
    if (*p == '"')
        p++;

    wavecom_band = atoi (p);
    if (wavecom_band > 0) {
        guint i;

        for (i = 0; i < G_N_ELEMENTS (bands_3g); i++) {
            if (bands_3g[i].wavecom_band_flag & wavecom_band) {
                if (G_UNLIKELY (!bands_array))
                    bands_array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
                g_array_append_val (bands_array, bands_3g[i].mm_band);
            }
        }
    }

    if (!bands_array)
        g_simple_async_result_set_error (operation_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse current bands reply: '%s'",
                                         response);
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

    if (mm_iface_modem_is_3g (self))
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "AT+WUBS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)get_3g_band_ready,
                                  result);
    else
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "AT+WMBS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)get_2g_band_ready,
                                  result);
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
wmbs_set_ready (MMBaseModem *self,
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
    guint wavecom_band = 0;
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
                wavecom_band |= bands_3g[i].wavecom_band_flag;
                break;
            }
        }
    }

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array_final->data,
                                                 bands_array_final->len);
    g_array_unref (bands_array_final);

    if (wavecom_band == 0) {
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
    cmd = g_strdup_printf ("+WMBS=\"%u\",1", wavecom_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wmbs_set_ready,
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
    gchar wavecom_band = '\0';
    guint i;
    gchar *bands_string;
    gchar *cmd;

    /* If the iface properly checked the given list against the supported bands,
     * it's not possible to get an array longer than 4 here. */
    g_assert (bands_array->len <= 4);

    /* The special case of ANY should be treated separately. */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        const WavecomBand2G *all;

        /* All bands is the last element in our 2G bands array */
        all = &bands_2g[G_N_ELEMENTS (bands_2g) - 1];

        /* We build an array with all bands to set; so that we use the same
         * logic to build the cinterion_band, and so that we can log the list of
         * bands being set properly */
        bands_array_final = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
        g_array_append_vals (bands_array_final, all->mm_bands, all->n_mm_bands);
    } else
        bands_array_final = g_array_ref (bands_array);

    for (i = 0; wavecom_band == '\0' && i < G_N_ELEMENTS (bands_2g); i++) {
        GArray *supported_combination;

        supported_combination = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), bands_2g[i].n_mm_bands);
        g_array_append_vals (supported_combination, bands_2g[i].mm_bands, bands_2g[i].n_mm_bands);

        /* Check if the given array is exactly one of the supported combinations */
        if (mm_common_bands_garray_cmp (bands_array_final, supported_combination))
            wavecom_band = bands_2g[i].wavecom_band;

        g_array_unref (supported_combination);
    }

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array_final->data,
                                                 bands_array_final->len);
    g_array_unref (bands_array_final);

    if (wavecom_band == '\0') {
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
    cmd = g_strdup_printf ("+WMBS=%c,1", wavecom_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wmbs_set_ready,
                              result);

    g_free (cmd);
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
     * Wavecom modems supporting only 2G have specific combinations allowed.
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
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    MMModemAccessTechnology act;
    const gchar *p;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    p = mm_strip_tag (response, "+WGPRSIND:");
    if (p) {
        switch (*p) {
        case '1':
            /* GPRS only */
            act = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
            break;
        case '2':
            /* EGPRS/EDGE supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
            break;
        case '3':
            /* 3G R99 supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
            break;
        case '4':
            /* HSDPA supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
            break;
        case '5':
            /* HSUPA supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
            break;
        default:
            break;
        }
    }

    if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse access technologies result: '%s'",
                     response);
        return FALSE;
    }

    /* We are reporting ALL 3GPP access technologies here */
    *access_technologies = act;
    *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    return TRUE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+WGPRS=9,2",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Flow control (Modem interface) */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    /* Wavecom doesn't have XOFF/XON flow control, so we enable RTS/CTS */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+IFC=2,2",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power up (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    mm_warn ("Not in full functionality status, power-up command is needed. "
             "Note that it may reboot the modem.");

    /* Try to go to full functionality mode without rebooting the system.
     * Works well if we previously switched off the power with CFUN=4
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1,0",
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
                              3,
                              FALSE,
                              callback,
                              user_data);
}

static void
setup_ports (MMBroadbandModem *self)
{
    gpointer parser;
    MMAtSerialPort *primary;
    GRegex *regex;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_wavecom_parent_class)->setup_ports (self);

    /* Set 9600 baudrate by default in the AT port */
    mm_dbg ("Baudrate will be set to 9600 bps...");
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary)
        return;

    /* AT+CPIN? replies will never have an OK appended */
    parser = mm_serial_parser_v1_new ();
    regex = g_regex_new ("\\r\\n\\+CPIN: .*\\r\\n",
                         G_REGEX_RAW | G_REGEX_OPTIMIZE,
                         0, NULL);
    mm_serial_parser_v1_set_custom_regex (parser, regex, NULL);
    g_regex_unref (regex);

    mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (primary),
                                           mm_serial_parser_v1_parse,
                                           parser,
                                           mm_serial_parser_v1_destroy);
}

/*****************************************************************************/

MMBroadbandModemWavecom *
mm_broadband_modem_wavecom_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_WAVECOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_wavecom_init (MMBroadbandModemWavecom *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
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
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_init_power_down = modem_power_down;
    iface->modem_init_power_down_finish = modem_power_down_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
mm_broadband_modem_wavecom_class_init (MMBroadbandModemWavecomClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
