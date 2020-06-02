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
#include "mm-log-object.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-wavecom.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemWavecom, mm_broadband_modem_wavecom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init))

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
    { (1 << 0), MM_MODEM_BAND_UTRAN_1 },
    { (1 << 1), MM_MODEM_BAND_UTRAN_2 },
    { (1 << 2), MM_MODEM_BAND_UTRAN_3 },
    { (1 << 3), MM_MODEM_BAND_UTRAN_4 },
    { (1 << 4), MM_MODEM_BAND_UTRAN_5 },
    { (1 << 5), MM_MODEM_BAND_UTRAN_6 },
    { (1 << 6), MM_MODEM_BAND_UTRAN_7 },
    { (1 << 7), MM_MODEM_BAND_UTRAN_8 },
    { (1 << 8), MM_MODEM_BAND_UTRAN_9 }
};

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem  *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
supported_ms_classes_query_ready (MMBaseModem  *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    GArray                 *all;
    GArray                 *combinations;
    GArray                 *filtered;
    const gchar            *response;
    GError                 *error = NULL;
    MMModemModeCombination  mode;
    MMModemMode             mode_all;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");
    mode_all = MM_MODEM_MODE_NONE;
    if (strstr (response, WAVECOM_MS_CLASS_A_IDSTR))
        mode_all |= MM_MODEM_MODE_3G;
    if (strstr (response, WAVECOM_MS_CLASS_B_IDSTR))
        mode_all |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
    if (strstr (response, WAVECOM_MS_CLASS_CG_IDSTR))
        mode_all |= MM_MODEM_MODE_2G;
    if (strstr (response, WAVECOM_MS_CLASS_CC_IDSTR))
        mode_all |= MM_MODEM_MODE_CS;

    /* If none received, error */
    if (mode_all == MM_MODEM_MODE_NONE) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get supported mobile station classes: '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    /* Build ALL mask */
    all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    mode.allowed = mode_all;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (all, mode);

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 7);
    /* CS only */
    mode.allowed = MM_MODEM_MODE_CS;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* CS and 2G */
    mode.allowed = (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G);
    mode.preferred = MM_MODEM_MODE_2G;
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
load_supported_modes (MMIfaceModem        *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CGCLASS=?",
        3,
        FALSE,
        (GAsyncReadyCallback)supported_ms_classes_query_ready,
        task);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadCurrentModesResult;

static gboolean
load_current_modes_finish (MMIfaceModem  *self,
                           GAsyncResult  *res,
                           MMModemMode   *allowed,
                           MMModemMode   *preferred,
                           GError       **error)
{
    g_autofree LoadCurrentModesResult *result = NULL;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *allowed   = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
wwsm_read_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    g_autoptr(GRegex)                  r = NULL;
    g_autoptr(GMatchInfo)              match_info = NULL;
    g_autofree LoadCurrentModesResult *result = NULL;
    const gchar                       *response;
    GError                            *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    result = g_new0 (LoadCurrentModesResult, 1);
    result->allowed = MM_MODEM_MODE_NONE;
    result->preferred = MM_MODEM_MODE_NONE;

    /* Possible responses:
     *   +WWSM: 0    (2G only)
     *   +WWSM: 1    (3G only)
     *   +WWSM: 2,0  (Any)
     *   +WWSM: 2,1  (2G preferred)
     *   +WWSM: 2,2  (3G preferred)
     */
    r = g_regex_new ("\\r\\n\\+WWSM: ([0-2])(,([0-2]))?.*$", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match (r, response, 0, &match_info)) {
        guint allowed = 0;

        if (mm_get_uint_from_match_info (match_info, 1, &allowed)) {
            switch (allowed) {
            case 0:
                result->allowed = MM_MODEM_MODE_2G;
                result->preferred = MM_MODEM_MODE_NONE;
                break;
            case 1:
                result->allowed = MM_MODEM_MODE_3G;
                result->preferred = MM_MODEM_MODE_NONE;
                break;
            case 2: {
                guint preferred = 0;

                result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

                /* 3, to avoid the comma */
                if (mm_get_uint_from_match_info (match_info, 3, &preferred)) {
                    switch (preferred) {
                    case 0:
                        result->preferred = MM_MODEM_MODE_NONE;
                        break;
                    case 1:
                        result->preferred = MM_MODEM_MODE_2G;
                        break;
                    case 2:
                        result->preferred = MM_MODEM_MODE_3G;
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

    if (result->allowed == MM_MODEM_MODE_NONE)
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Unknown wireless data service reply: '%s'",
                                 response);
    else
        g_task_return_pointer (task, g_steal_pointer (&result), g_free);
    g_object_unref (task);
}

static void
current_ms_class_ready (MMBaseModem  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    g_autofree LoadCurrentModesResult *result = NULL;
    const gchar                       *response;
    GError                            *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");

    if (strncmp (response,
                 WAVECOM_MS_CLASS_A_IDSTR,
                 strlen (WAVECOM_MS_CLASS_A_IDSTR)) == 0) {
        mm_obj_dbg (self, "configured as a Class A mobile station");
        /* For 3G devices, query WWSM status */
        mm_base_modem_at_command (self,
                                  "+WWSM?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)wwsm_read_ready,
                                  task);
        return;
    }

    result = g_new0 (LoadCurrentModesResult, 1);
    result->allowed = MM_MODEM_MODE_NONE;
    result->preferred = MM_MODEM_MODE_NONE;

    if (strncmp (response,
                 WAVECOM_MS_CLASS_B_IDSTR,
                 strlen (WAVECOM_MS_CLASS_B_IDSTR)) == 0) {
        mm_obj_dbg (self, "configured as a Class B mobile station");
        result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
        result->preferred = MM_MODEM_MODE_2G;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CG_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CG_IDSTR)) == 0) {
        mm_obj_dbg (self, "configured as a Class CG mobile station");
        result->allowed = MM_MODEM_MODE_2G;
        result->preferred = MM_MODEM_MODE_NONE;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CC_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CC_IDSTR)) == 0) {
        mm_obj_dbg (self, "configured as a Class CC mobile station");
        result->allowed = MM_MODEM_MODE_CS;
        result->preferred = MM_MODEM_MODE_NONE;
    }

    if (result->allowed == MM_MODEM_MODE_NONE)
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Unknown mobile station class: '%s'",
                                 response);
    else
        g_task_return_pointer (task, g_steal_pointer (&result), g_free);
    g_object_unref (task);
}

static void
load_current_modes (MMIfaceModem        *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGCLASS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)current_ms_class_ready,
                              task);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    gchar *cgclass_command;
    gchar *wwsm_command;
} SetCurrentModesContext;

static void
set_current_modes_context_free (SetCurrentModesContext *ctx)
{
    g_free (ctx->cgclass_command);
    g_free (ctx->wwsm_command);
    g_free (ctx);
}

static gboolean
set_current_modes_finish (MMIfaceModem  *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
wwsm_update_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
cgclass_update_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    SetCurrentModesContext *ctx;
    GError                 *error = NULL;

    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!ctx->wwsm_command) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->wwsm_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wwsm_update_ready,
                              task);
}

static void
set_current_modes (MMIfaceModem        *self,
                   MMModemMode          allowed,
                   MMModemMode          preferred,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask                  *task;
    SetCurrentModesContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new0 (SetCurrentModesContext, 1);
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_current_modes_context_free);

    /* Handle ANY/NONE */
    if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE) {
        if (mm_iface_modem_is_3g (self)) {
            allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            preferred = MM_MODEM_MODE_NONE;
        } else {
            allowed = (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G);
            preferred = MM_MODEM_MODE_2G;
        }
    }

    if (allowed == MM_MODEM_MODE_CS)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CC_IDSTR);
    else if (allowed == MM_MODEM_MODE_2G)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CG_IDSTR);
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS) &&
             preferred == MM_MODEM_MODE_2G)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_B_IDSTR);
    else if (allowed & MM_MODEM_MODE_3G) {
        if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
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
        g_autofree gchar *allowed_str = NULL;
        g_autofree gchar *preferred_str = NULL;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested mode (allowed: '%s', preferred: '%s') not "
                                 "supported by the modem.",
                                 allowed_str, preferred_str);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->cgclass_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cgclass_update_ready,
                              task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
load_supported_bands_finish (MMIfaceModem  *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_bands (MMIfaceModem        *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask  *task;
    GArray *bands;

    task = g_task_new (self, NULL, callback, user_data);

    /* We do assume that we already know if the modem is 2G-only, 3G-only or
     * 2G+3G. This is checked quite before trying to load supported bands. */

#define _g_array_insert_enum(array,index,type,val) do { \
        type aux = (type)val;                           \
        g_array_insert_val (array, index, aux);         \
    } while (0)

    /* Add 3G-specific bands */
    if (mm_iface_modem_is_3g (self)) {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 10);
        _g_array_insert_enum (bands, 0, MMModemBand, MM_MODEM_BAND_UTRAN_1);
        _g_array_insert_enum (bands, 1, MMModemBand, MM_MODEM_BAND_UTRAN_2);
        _g_array_insert_enum (bands, 2, MMModemBand, MM_MODEM_BAND_UTRAN_3);
        _g_array_insert_enum (bands, 3, MMModemBand, MM_MODEM_BAND_UTRAN_4);
        _g_array_insert_enum (bands, 4, MMModemBand, MM_MODEM_BAND_UTRAN_5);
        _g_array_insert_enum (bands, 5, MMModemBand, MM_MODEM_BAND_UTRAN_6);
        _g_array_insert_enum (bands, 6, MMModemBand, MM_MODEM_BAND_UTRAN_7);
        _g_array_insert_enum (bands, 7, MMModemBand, MM_MODEM_BAND_UTRAN_8);
        _g_array_insert_enum (bands, 8, MMModemBand, MM_MODEM_BAND_UTRAN_9);
    }
    /* Add 2G-specific bands */
    else {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
        _g_array_insert_enum (bands, 0, MMModemBand, MM_MODEM_BAND_EGSM);
        _g_array_insert_enum (bands, 1, MMModemBand, MM_MODEM_BAND_DCS);
        _g_array_insert_enum (bands, 2, MMModemBand, MM_MODEM_BAND_PCS);
        _g_array_insert_enum (bands, 3, MMModemBand, MM_MODEM_BAND_G850);
    }

    g_task_return_pointer (task, bands, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem  *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
get_2g_band_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    const gchar *response;
    const gchar *p;
    GError      *error = NULL;
    GArray      *bands_array = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
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
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't parse current bands reply: '%s'",
                                 response);
    else
        g_task_return_pointer (task, bands_array, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
get_3g_band_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    const gchar *response;
    const gchar *p;
    GError      *error = NULL;
    GArray      *bands_array = NULL;
    guint32      wavecom_band;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
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
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't parse current bands reply: '%s'",
                                 response);
    else
        g_task_return_pointer (task, bands_array, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (mm_iface_modem_is_3g (self))
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "AT+WUBS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)get_3g_band_ready,
                                  task);
    else
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "AT+WMBS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)get_2g_band_ready,
                                  task);
}

/*****************************************************************************/
/* Set current_bands (Modem interface) */

static gboolean
set_current_bands_finish (MMIfaceModem  *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
wmbs_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_bands_3g (GTask  *task,
              GArray *bands_array)
{
    MMBroadbandModemWavecom *self;
    guint                    wavecom_band = 0;
    guint                    i;
    g_autoptr(GArray)        bands_array_final = NULL;
    g_autofree gchar        *cmd = NULL;

    self = g_task_get_source_object (task);

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

    if (wavecom_band == 0) {
        g_autofree gchar *bands_string = NULL;

        bands_string = mm_common_build_bands_string ((MMModemBand *)(gpointer)bands_array_final->data, bands_array_final->len);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "The given band combination is not supported: '%s'",
                                 bands_string);
        g_object_unref (task);
        return;
    }

    cmd = g_strdup_printf ("+WMBS=\"%u\",1", wavecom_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wmbs_set_ready,
                              task);
}

static void
set_bands_2g (GTask  *task,
              GArray *bands_array)
{
    MMBroadbandModemWavecom *self;
    gchar                    wavecom_band = '\0';
    guint                    i;
    g_autofree gchar        *cmd = NULL;
    g_autoptr(GArray)        bands_array_final = NULL;

    self = g_task_get_source_object (task);

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
        g_autoptr(GArray) supported_combination = NULL;

        supported_combination = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), bands_2g[i].n_mm_bands);
        g_array_append_vals (supported_combination, bands_2g[i].mm_bands, bands_2g[i].n_mm_bands);

        /* Check if the given array is exactly one of the supported combinations */
        if (mm_common_bands_garray_cmp (bands_array_final, supported_combination)) {
            wavecom_band = bands_2g[i].wavecom_band;
            break;
        }
    }

    if (wavecom_band == '\0') {
        g_autofree gchar *bands_string = NULL;

        bands_string = mm_common_build_bands_string ((MMModemBand *)(gpointer)bands_array_final->data, bands_array_final->len);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "The given band combination is not supported: '%s'",
                                 bands_string);
        g_object_unref (task);
        return;
    }

    cmd = g_strdup_printf ("+WMBS=%c,1", wavecom_band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wmbs_set_ready,
                              task);
}

static void
set_current_bands (MMIfaceModem        *self,
                   GArray              *bands_array,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask *task;

    /* The bands that we get here are previously validated by the interface, and
     * that means that ALL the bands given here were also given in the list of
     * supported bands. BUT BUT, that doesn't mean that the exact list of bands
     * will end up being valid, as not all combinations are possible. E.g,
     * Wavecom modems supporting only 2G have specific combinations allowed.
     */
    task = g_task_new (self, NULL, callback, user_data);

    if (mm_iface_modem_is_3g (self))
        set_bands_3g (task, bands_array);
    else
        set_bands_2g (task, bands_array);
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
/* Register in network (3GPP interface) */

static gboolean
register_in_network_finish (MMIfaceModem3gpp  *self,
                            GAsyncResult      *res,
                            GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_registration_ready (MMIfaceModem3gpp *self,
                           GAsyncResult     *res,
                           GTask            *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->register_in_network_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
run_parent_registration (GTask *task)
{
    MMBroadbandModemWavecom *self;
    const gchar             *operator_id;

    self = g_task_get_source_object (task);
    operator_id = g_task_get_task_data (task);

    iface_modem_3gpp_parent->register_in_network (
        MM_IFACE_MODEM_3GPP (self),
        operator_id,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)parent_registration_ready,
        task);
}

static gboolean
parse_network_registration_mode (const gchar *reply,
                                 guint       *mode)
{
    g_autoptr(GRegex)     r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;

    g_assert (mode != NULL);

    if (!reply)
        return FALSE;

    r = g_regex_new ("\\+COPS:\\s*(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, reply, 0, &match_info);

    return (g_match_info_matches (match_info) && mm_get_uint_from_match_info (match_info, 1, mode));
}

static void
cops_ready (MMBaseModem  *self,
            GAsyncResult *res,
            GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    guint        mode;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!parse_network_registration_mode (response, &mode)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't parse current network registration mode: '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    /* If the modem is not configured for automatic registration, run parent */
    if (mode != 0) {
        run_parent_registration (task);
        return;
    }

    mm_obj_dbg (self, "device is already in automatic registration mode, not requesting it again");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
register_in_network (MMIfaceModem3gpp    *self,
                     const gchar         *operator_id,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    /* Store operator id as task data */
    g_task_set_task_data (task, g_strdup (operator_id), g_free);

    /* If requesting automatic registration, we first need to query what the
     * current mode is. We must NOT send +COPS=0 if it already is in 0 mode,
     * or the device will get stuck. */
    if (operator_id == NULL || operator_id[0] == '\0') {
        /* Check which is the current operator selection status */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+COPS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)cops_ready,
                                  task);
        return;
    }

    /* Otherwise, run parent's implementation right away */
    run_parent_registration (task);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem  *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_wait_cb (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem        *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GTask *task;

    /* A short wait is necessary for SIM to become ready, otherwise reloading
     * facility lock states may fail with a +CME ERROR: 515 error.
     */
    task = g_task_new (self, NULL, callback, user_data);
    g_timeout_add_seconds (5, (GSourceFunc)after_sim_unlock_wait_cb, task);
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
    mm_obj_warn (self, "not in full functionality status, power-up command is needed");
    mm_obj_warn (self, "the device maybe rebooted");

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

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_off_finish (MMIfaceModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_off (MMIfaceModem *self,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPOF=1",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *self)
{
    gpointer parser;
    MMPortSerialAt *primary;
    GRegex *regex;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_wavecom_parent_class)->setup_ports (self);

    /* Set 9600 baudrate by default in the AT port */
    mm_obj_dbg (self, "baudrate will be set to 9600 bps...");
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

    mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (primary),
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
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_current_bands = set_current_bands;
    iface->set_current_bands_finish = set_current_bands_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->modem_power_off = modem_power_off;
    iface->modem_power_off_finish = modem_power_off_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->register_in_network = register_in_network;
    iface->register_in_network_finish = register_in_network_finish;
}

static void
mm_broadband_modem_wavecom_class_init (MMBroadbandModemWavecomClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
