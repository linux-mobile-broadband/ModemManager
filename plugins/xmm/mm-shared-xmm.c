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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-xmm.h"
#include "mm-modem-helpers-xmm.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-xmm-private-tag"
static GQuark private_quark;

typedef enum {
    GPS_ENGINE_STATE_OFF,
    GPS_ENGINE_STATE_STANDALONE,
    GPS_ENGINE_STATE_AGPS_MSA,
    GPS_ENGINE_STATE_AGPS_MSB,
} GpsEngineState;

typedef struct {
    /* Broadband modem class support */
    MMBroadbandModemClass *broadband_modem_class_parent;

    /* Modem interface support */
    GArray      *supported_modes;
    GArray      *supported_bands;
    MMModemMode  allowed_modes;

    /* Location interface support */
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  supported_sources;
    MMModemLocationSource  enabled_sources;
    GpsEngineState         gps_engine_state;
    MMPortSerialAt        *gps_port;
    GRegex                *xlsrstop_regex;
    GRegex                *nmea_regex;
} Private;

static void
private_free (Private *priv)
{
    g_clear_object (&priv->gps_port);
    if (priv->supported_modes)
        g_array_unref (priv->supported_modes);
    if (priv->supported_bands)
        g_array_unref (priv->supported_bands);
    g_regex_unref (priv->xlsrstop_regex);
    g_regex_unref (priv->nmea_regex);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMSharedXmm *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);
        priv->gps_engine_state = GPS_ENGINE_STATE_OFF;

        /* Setup regex for URCs */
        priv->xlsrstop_regex = g_regex_new ("\\r\\n\\+XLSRSTOP:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        priv->nmea_regex     = g_regex_new ("(?:\\r\\n)?(?:\\r\\n)?(\\$G.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

        /* Setup parent class' MMBroadbandModemClass */
        g_assert (MM_SHARED_XMM_GET_INTERFACE (self)->peek_parent_broadband_modem_class);
        priv->broadband_modem_class_parent = MM_SHARED_XMM_GET_INTERFACE (self)->peek_parent_broadband_modem_class (self);

        /* Setup parent class' MMIfaceModemLocation */
        g_assert (MM_SHARED_XMM_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_XMM_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/
/* Supported modes/bands (Modem interface) */

GArray *
mm_shared_xmm_load_supported_modes_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    Private *priv;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return NULL;

    priv = get_private (MM_SHARED_XMM (self));
    g_assert (priv->supported_modes);
    return g_array_ref (priv->supported_modes);
}

GArray *
mm_shared_xmm_load_supported_bands_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    Private *priv;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return NULL;

    priv = get_private (MM_SHARED_XMM (self));
    g_assert (priv->supported_bands);
    return g_array_ref (priv->supported_bands);
}

static void
xact_test_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    Private     *priv;

    priv = get_private (MM_SHARED_XMM (self));

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response ||
        !mm_xmm_parse_xact_test_response (response,
                                          self,
                                          &priv->supported_modes,
                                          &priv->supported_bands,
                                          &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_load_supported_modes_bands (GTask *task)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (g_task_get_source_object (task)),
        "+XACT=?",
        3,
        TRUE, /* allow caching */
        (GAsyncReadyCallback)xact_test_ready,
        task);
}

void
mm_shared_xmm_load_supported_modes (MMIfaceModem        *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_XMM (self));

    if (!priv->supported_modes) {
        common_load_supported_modes_bands (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_xmm_load_supported_bands (MMIfaceModem        *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_XMM (self));

    if (!priv->supported_bands) {
        common_load_supported_modes_bands (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Current modes (Modem interface) */

gboolean
mm_shared_xmm_load_current_modes_finish (MMIfaceModem  *self,
                                         GAsyncResult  *res,
                                         MMModemMode   *allowed,
                                         MMModemMode   *preferred,
                                         GError       **error)
{
    MMModemModeCombination *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *allowed   = result->allowed;
    *preferred = result->preferred;
    g_free (result);
    return TRUE;
}

static void
xact_query_modes_ready (MMBaseModem  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    const gchar            *response;
    GError                 *error = NULL;
    Private                *priv;
    MMModemModeCombination *result;

    priv = get_private (MM_SHARED_XMM (self));
    result = g_new0 (MMModemModeCombination, 1);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response || !mm_xmm_parse_xact_query_response (response, result, NULL, &error)) {
        priv->allowed_modes = MM_MODEM_MODE_NONE;
        g_free (result);
        g_task_return_error (task, error);
    } else {
        priv->allowed_modes = result->allowed;
        g_task_return_pointer (task, result, g_free);
    }
    g_object_unref (task);
}

void
mm_shared_xmm_load_current_modes (MMIfaceModem        *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+XACT?",
        3,
        FALSE,
        (GAsyncReadyCallback)xact_query_modes_ready,
        task);
}

/*****************************************************************************/
/* Current bands (Modem interface) */

GArray *
mm_shared_xmm_load_current_bands_finish (MMIfaceModem  *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}


static void
xact_query_bands_ready (MMBaseModem  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    GArray      *result = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response ||
        !mm_xmm_parse_xact_query_response (response, NULL, &result, &error))
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, result, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

void
mm_shared_xmm_load_current_bands (MMIfaceModem        *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+XACT?",
        3,
        FALSE,
        (GAsyncReadyCallback)xact_query_bands_ready,
        task);
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

gboolean
mm_shared_xmm_set_current_modes_finish (MMIfaceModem  *self,
                                        GAsyncResult  *res,
                                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
xact_set_modes_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_xmm_set_current_modes (MMIfaceModem        *self,
                                 MMModemMode          allowed,
                                 MMModemMode          preferred,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    GTask                  *task;
    MMModemModeCombination  mode;
    gchar                  *command;
    GError                 *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (allowed != MM_MODEM_MODE_ANY) {
        mode.allowed   = allowed;
        mode.preferred = preferred;
    } else {
        Private *priv;

        priv = get_private (MM_SHARED_XMM (self));
        mode.allowed = mm_xmm_get_modem_mode_any (priv->supported_modes);
        mode.preferred = MM_MODEM_MODE_NONE;
    }

    command = mm_xmm_build_xact_set_command (&mode, NULL, &error);
    if (!command) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)xact_set_modes_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

gboolean
mm_shared_xmm_set_current_bands_finish (MMIfaceModem  *self,
                                        GAsyncResult  *res,
                                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
xact_set_bands_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
     g_object_unref (task);
}

static gchar *
validate_and_build_command_set_current_bands (MMSharedXmm  *self,
                                              const GArray *bands_array,
                                              const GArray *supported_modes,
                                              MMModemMode   allowed_modes,
                                              GError      **error)
{
    gboolean  band_2g_found = FALSE;
    gboolean  band_3g_found = FALSE;
    gboolean  band_4g_found = FALSE;
    GArray   *unapplied_bands;
    GError   *inner_error = NULL;
    guint     i;

    /* ANY applies only to the currently selected modes */
    if (bands_array->len == 1 && g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        MMModemModeCombination mode;
        MMModemMode            unapplied;

        /* If we are enabling automatic band selection to a mode combination that does not include
         * all supported modes, warn about it because automatic band selection wouldn't be executed
         * for the non-selected modes.
         *
         * This is a known limitation of the modem firmware.
         */
        unapplied = mm_xmm_get_modem_mode_any (supported_modes) & ~(allowed_modes);
        if (unapplied != MM_MODEM_MODE_NONE) {
            g_autofree gchar *str = NULL;

            str = mm_modem_mode_build_string_from_mask (unapplied);
            mm_obj_warn (self, "automatic band selection not applied to non-current modes %s", str);
        }

        /* Nothing else to validate, go build the command right away */

        /* We must create the set command with an explicit set of allowed modes.
         * We pass NONE as preferred, but that WON'T change the currently selected preferred mode,
         * it will be ignored when the command is processed as an empty field will be given */
        mode.allowed = allowed_modes;
        mode.preferred = MM_MODEM_MODE_NONE;
        return mm_xmm_build_xact_set_command (&mode, bands_array, error);
    }

    unapplied_bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
    for (i = 0; i < bands_array->len; i++) {
        MMModemBand band;

        band = g_array_index (bands_array, MMModemBand, i);
        if (mm_common_band_is_eutran (band)) {
            band_4g_found = TRUE;
            if (!(allowed_modes & MM_MODEM_MODE_4G))
                g_array_append_val (unapplied_bands, band);
        }
        if (mm_common_band_is_utran (band)) {
            band_3g_found = TRUE;
            if (!(allowed_modes & MM_MODEM_MODE_3G))
                g_array_append_val (unapplied_bands, band);
        }
        if (mm_common_band_is_gsm (band)) {
            band_2g_found = TRUE;
            if (!(allowed_modes & MM_MODEM_MODE_2G))
                g_array_append_val (unapplied_bands, band);
        }
    }

    /* If 2G selected, there must be at least one 2G band */
    if ((allowed_modes & MM_MODEM_MODE_2G) && !band_2g_found) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                   "At least one GSM band is required when 2G mode is allowed");
        goto out;
    }

    /* If 3G selected, there must be at least one 3G band */
    if ((allowed_modes & MM_MODEM_MODE_3G) && !band_3g_found) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                   "At least one UTRAN band is required when 3G mode is allowed");
        goto out;
    }

    /* If 4G selected, there must be at least one 4G band */
    if ((allowed_modes & MM_MODEM_MODE_4G) && !band_4g_found) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                   "At least one E-UTRAN band is required when 4G mode is allowed");
        goto out;
    }

    /* Don't try to modify bands for modes that are not enabled */
    if (unapplied_bands->len > 0) {
        gchar *str;

        str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)unapplied_bands->data, unapplied_bands->len);
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                   "Cannot update bands for modes not currently allowed: %s", str);
        g_free (str);
        goto out;
    }

out:
    if (unapplied_bands)
        g_array_unref (unapplied_bands);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    return mm_xmm_build_xact_set_command (NULL, bands_array, error);
}

void
mm_shared_xmm_set_current_bands (MMIfaceModem        *self,
                                 GArray              *bands_array,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    GTask   *task;
    gchar   *command = NULL;
    GError  *error = NULL;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);

    /* Setting bands requires additional validation rules based on the
     * currently selected list of allowed modes */
    priv = get_private (MM_SHARED_XMM (self));
    if (priv->allowed_modes == MM_MODEM_MODE_NONE) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Cannot set bands if allowed modes are unknown");
        goto out;
    }

    command = validate_and_build_command_set_current_bands (MM_SHARED_XMM (self),
                                                            bands_array,
                                                            priv->supported_modes,
                                                            priv->allowed_modes,
                                                            &error);

out:
    if (!command) {
        g_assert (error);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)xact_set_bands_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

MMModemPowerState
mm_shared_xmm_load_power_state_finish (MMIfaceModem  *self,
                                       GAsyncResult  *res,
                                       GError       **error)
{
    guint        state;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return MM_MODEM_POWER_STATE_UNKNOWN;

    if (!mm_3gpp_parse_cfun_query_response (response, &state, error))
        return MM_MODEM_POWER_STATE_UNKNOWN;

    switch (state) {
    case 1:
        return MM_MODEM_POWER_STATE_ON;
    case 4:
        return MM_MODEM_POWER_STATE_LOW;
    default:
        break;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Unknown +CFUN state: %u", state);
    return MM_MODEM_POWER_STATE_UNKNOWN;
}

void
mm_shared_xmm_load_power_state (MMIfaceModem        *self,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power up/down/off (Modem interface) */

static gboolean
common_modem_power_operation_finish (MMSharedXmm   *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
power_operation_ready (MMBaseModem  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_modem_power_operation (MMSharedXmm         *self,
                              const gchar         *command,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              30,
                              FALSE,
                              (GAsyncReadyCallback) power_operation_ready,
                              task);
}

gboolean
mm_shared_xmm_reset_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return common_modem_power_operation_finish (MM_SHARED_XMM (self), res, error);
}

void
mm_shared_xmm_reset (MMIfaceModem        *self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    common_modem_power_operation (MM_SHARED_XMM (self), "+CFUN=16", callback, user_data);
}

gboolean
mm_shared_xmm_power_off_finish (MMIfaceModem  *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return common_modem_power_operation_finish (MM_SHARED_XMM (self), res, error);
}

void
mm_shared_xmm_power_off (MMIfaceModem        *self,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    common_modem_power_operation (MM_SHARED_XMM (self), "+CPWROFF", callback, user_data);
}

gboolean
mm_shared_xmm_power_down_finish (MMIfaceModem  *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    return common_modem_power_operation_finish (MM_SHARED_XMM (self), res, error);
}

void
mm_shared_xmm_power_down (MMIfaceModem        *self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    common_modem_power_operation (MM_SHARED_XMM (self), "+CFUN=4", callback, user_data);
}

gboolean
mm_shared_xmm_power_up_finish (MMIfaceModem  *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return common_modem_power_operation_finish (MM_SHARED_XMM (self), res, error);
}

void
mm_shared_xmm_power_up (MMIfaceModem        *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    common_modem_power_operation (MM_SHARED_XMM (self), "+CFUN=1", callback, user_data);
}

/*****************************************************************************/
/* Check support (Signal interface) */

gboolean
mm_shared_xmm_signal_check_support_finish  (MMIfaceModemSignal  *self,
                                            GAsyncResult        *res,
                                            GError             **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

void
mm_shared_xmm_signal_check_support (MMIfaceModemSignal  *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+XCESQ=?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load extended signal information (Signal interface) */

gboolean
mm_shared_xmm_signal_load_values_finish (MMIfaceModemSignal  *self,
                                         GAsyncResult        *res,
                                         MMSignal           **cdma,
                                         MMSignal           **evdo,
                                         MMSignal           **gsm,
                                         MMSignal           **umts,
                                         MMSignal           **lte,
                                         MMSignal           **nr5g,
                                         GError             **error)
{
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response || !mm_xmm_xcesq_response_to_signal_info (response, self, gsm, umts, lte, error))
        return FALSE;

    if (cdma)
        *cdma = NULL;
    if (evdo)
        *evdo = NULL;
    if (nr5g)
        *nr5g = NULL;

    return TRUE;
}

void
mm_shared_xmm_signal_load_values (MMIfaceModemSignal  *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+XCESQ?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load capabilities (Location interface) */

MMModemLocationSource
mm_shared_xmm_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    GError *inner_error = NULL;
    gssize  value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
xlcslsr_test_ready (MMBaseModem  *self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMModemLocationSource  sources;
    const gchar           *response;
    GError                *error = NULL;
    Private               *priv;
    gboolean               transport_protocol_invalid_supported;
    gboolean               transport_protocol_supl_supported;
    gboolean               standalone_position_mode_supported;
    gboolean               ms_assisted_based_position_mode_supported;
    gboolean               loc_response_type_nmea_supported;
    gboolean               gnss_type_gps_glonass_supported;

    priv = get_private (MM_SHARED_XMM (self));

    /* Recover parent sources */
    sources = GPOINTER_TO_UINT (g_task_get_task_data (task));

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response ||
        !mm_xmm_parse_xlcslsr_test_response (response,
                                             &transport_protocol_invalid_supported,
                                             &transport_protocol_supl_supported,
                                             &standalone_position_mode_supported,
                                             &ms_assisted_based_position_mode_supported,
                                             &loc_response_type_nmea_supported,
                                             &gnss_type_gps_glonass_supported,
                                             &error)) {
        mm_obj_dbg (self, "XLCSLSR based GPS control unsupported: %s", error->message);
        g_clear_error (&error);
    } else if (!transport_protocol_invalid_supported ||
               !standalone_position_mode_supported ||
               !loc_response_type_nmea_supported ||
               !gnss_type_gps_glonass_supported) {
        mm_obj_dbg (self, "XLCSLSR based GPS control unsupported: protocol invalid %s, standalone %s, nmea %s, gps/glonass %s",
                    transport_protocol_invalid_supported ? "supported" : "unsupported",
                    standalone_position_mode_supported   ? "supported" : "unsupported",
                    loc_response_type_nmea_supported     ? "supported" : "unsupported",
                    gnss_type_gps_glonass_supported      ? "supported" : "unsupported");
    } else {
        mm_obj_dbg (self, "XLCSLSR based GPS control supported");
        priv->supported_sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW);

        if (transport_protocol_supl_supported && ms_assisted_based_position_mode_supported) {
            mm_obj_dbg (self, "XLCSLSR based A-GPS control supported");
            priv->supported_sources |= (MM_MODEM_LOCATION_SOURCE_AGPS_MSA | MM_MODEM_LOCATION_SOURCE_AGPS_MSB);
        } else {
            mm_obj_dbg (self, "XLCSLSR based A-GPS control unsupported: protocol supl %s, ms assisted/based %s",
                        transport_protocol_supl_supported         ? "supported" : "unsupported",
                        ms_assisted_based_position_mode_supported ? "supported" : "unsupported");
        }

        sources |= priv->supported_sources;
    }

    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
run_xlcslsr_test (GTask *task)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (g_task_get_source_object (task)),
        "+XLCSLSR=?",
        3,
        TRUE, /* allow caching */
        (GAsyncReadyCallback)xlcslsr_test_ready,
        task);
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    MMModemLocationSource  sources;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_XMM (self));

    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If parent already supports GPS sources, we won't do anything else */
    if (sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        mm_obj_dbg (self, "no need to run XLCSLSR based location gathering");
        g_task_return_int (task, sources);
        g_object_unref (task);
        return;
    }

    /* Cache sources supported by the parent */
    g_task_set_task_data (task, GUINT_TO_POINTER (sources), NULL);
    run_xlcslsr_test (task);
}

void
mm_shared_xmm_location_load_capabilities (MMIfaceModemLocation *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (MM_SHARED_XMM (self));
    task = g_task_new (self, NULL, callback, user_data);

    g_assert (priv->iface_modem_location_parent);

    if (!priv->iface_modem_location_parent->load_capabilities ||
        !priv->iface_modem_location_parent->load_capabilities_finish) {
        /* no parent capabilities */
        g_task_set_task_data (task, GUINT_TO_POINTER (MM_MODEM_LOCATION_SOURCE_NONE), NULL);
        run_xlcslsr_test (task);
        return;
    }

    priv->iface_modem_location_parent->load_capabilities (self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/
/* GPS engine state selection */

static void
nmea_received (MMPortSerialAt *port,
               GMatchInfo     *info,
               MMSharedXmm    *self)
{
    gchar *trace;

    trace = g_match_info_fetch (info, 1);
    mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), trace);
    g_free (trace);
}

static gboolean
gps_engine_state_select_finish (MMSharedXmm   *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
xlcslsr_ready (MMBaseModem  *self,
               GAsyncResult *res,
               GTask        *task)
{
    GpsEngineState  state;
    const gchar    *response;
    GError         *error = NULL;
    Private        *priv;

    priv = get_private (MM_SHARED_XMM (self));

    response = mm_base_modem_at_command_full_finish (self, res, &error);
    if (!response) {
        g_clear_object (&priv->gps_port);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    state = GPOINTER_TO_UINT (g_task_get_task_data (task));

    g_assert (priv->gps_port);
    mm_port_serial_at_add_unsolicited_msg_handler (priv->gps_port,
                                                   priv->nmea_regex,
                                                   (MMPortSerialAtUnsolicitedMsgFn)nmea_received,
                                                   self,
                                                   NULL);
    priv->gps_engine_state = state;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
gps_engine_start (GTask *task)
{
    GpsEngineState  state;
    MMSharedXmm    *self;
    Private        *priv;
    guint           transport_protocol = 0;
    guint           pos_mode = 0;
    gchar          *cmd;

    self  = g_task_get_source_object (task);
    priv  = get_private (self);
    state = GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Look for an AT port to use for GPS. Prefer secondary port if there is one,
     * otherwise use primary */
    g_assert (!priv->gps_port);
    priv->gps_port = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));
    if (!priv->gps_port) {
        priv->gps_port = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
        if (!priv->gps_port) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "No valid port found to control GPS");
            g_object_unref (task);
            return;
        }
    }

    switch (state) {
        case GPS_ENGINE_STATE_STANDALONE:
            transport_protocol = 2;
            pos_mode = 3;
            break;
        case GPS_ENGINE_STATE_AGPS_MSB:
            transport_protocol = 1;
            pos_mode = 1;
            break;
        case GPS_ENGINE_STATE_AGPS_MSA:
            transport_protocol = 1;
            pos_mode = 2;
            break;
        case GPS_ENGINE_STATE_OFF:
        default:
            g_assert_not_reached ();
            break;
    }

    /*
     * AT+XLCSLSR
     *    transport_protocol:  2 (invalid) or 1 (supl)
     *    pos_mode:            3 (standalone), 1 (msb) or 2 (msa)
     *    client_id:           <empty>
     *    client_id_type:      <empty>
     *    mlc_number:          <empty>
     *    mlc_number_type:     <empty>
     *    interval:            1 (seconds)
     *    service_type_id:     <empty>
     *    pseudonym_indicator: <empty>
     *    loc_response_type:   1 (NMEA strings)
     *    nmea_mask:           118 (01110110: GGA,GSA,GSV,RMC,VTG)
     *    gnss_type:           0 (GPS or GLONASS)
     */
    g_assert (priv->gps_port);
    cmd = g_strdup_printf ("AT+XLCSLSR=%u,%u,,,,,1,,,1,118,0", transport_protocol, pos_mode);
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   priv->gps_port,
                                   cmd,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)xlcslsr_ready,
                                   task);
    g_free (cmd);
}

static void
xlsrstop_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GpsEngineState  state;
    GError         *error = NULL;
    Private        *priv;

    mm_base_modem_at_command_full_finish (self, res, &error);

    priv = get_private (MM_SHARED_XMM (self));
    state = GPOINTER_TO_UINT (g_task_get_task_data (task));

    g_assert (priv->gps_port);
    mm_port_serial_at_add_unsolicited_msg_handler (priv->gps_port, priv->nmea_regex, NULL, NULL, NULL);
    g_clear_object (&priv->gps_port);
    priv->gps_engine_state = GPS_ENGINE_STATE_OFF;

    /* If already reached requested state, we're done */
    if (state == priv->gps_engine_state) {
        /* If we had an error when requesting this specific state, report it */
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Ignore errors if the stop operation was an intermediate one */
    g_clear_error (&error);

    /* Otherwise, start with new state */
    gps_engine_start (task);
}

static void
gps_engine_stop (GTask *task)
{
    MMSharedXmm *self;
    Private     *priv;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    g_assert (priv->gps_port);
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   priv->gps_port,
                                   "+XLSRSTOP",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)xlsrstop_ready,
                                   task);
}

static void
gps_engine_state_select (MMSharedXmm         *self,
                         GpsEngineState       state,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (state), NULL);

    priv = get_private (self);

    /* If already in the requested state, we're done */
    if (state == priv->gps_engine_state) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* If states are different we always STOP first */
    if (priv->gps_engine_state != GPS_ENGINE_STATE_OFF) {
        gps_engine_stop (task);
        return;
    }

    /* If GPS already stopped, go on to START right away */
    g_assert (state != GPS_ENGINE_STATE_OFF);
    gps_engine_start (task);
}

static GpsEngineState
gps_engine_state_get_expected (MMModemLocationSource sources)
{
    /* If at lease one of GPS nmea/raw sources enabled, engine started */
    if (sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        /* If MSA A-GPS is enabled, MSA mode */
        if (sources & MM_MODEM_LOCATION_SOURCE_AGPS_MSA)
            return GPS_ENGINE_STATE_AGPS_MSA;
        /* If MSB A-GPS is enabled, MSB mode */
        if (sources & MM_MODEM_LOCATION_SOURCE_AGPS_MSB)
            return GPS_ENGINE_STATE_AGPS_MSB;
        /* Otherwise, STANDALONE */
        return GPS_ENGINE_STATE_STANDALONE;
    }
    /* If no GPS nmea/raw sources enabled, engine stopped */
    return GPS_ENGINE_STATE_OFF;
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

gboolean
mm_shared_xmm_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_gps_engine_state_select_ready (MMSharedXmm  *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    MMModemLocationSource  source;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_XMM (self));

    if (!gps_engine_state_select_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv->enabled_sources &= ~source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_disable_location_gathering_ready (MMIfaceModemLocation *self,
                                         GAsyncResult         *res,
                                         GTask                *task)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_XMM (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->disable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_xmm_disable_location_gathering (MMIfaceModemLocation  *self,
                                          MMModemLocationSource  source,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data)
{
    Private *priv;
    GTask   *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_XMM (self));
    g_assert (priv->iface_modem_location_parent);

    /* Only consider request if it applies to one of the sources we are
     * supporting, otherwise run parent disable */
    if (!(priv->supported_sources & source)) {
        /* If disabling implemented by the parent, run it. */
        if (priv->iface_modem_location_parent->disable_location_gathering &&
            priv->iface_modem_location_parent->disable_location_gathering_finish) {
            priv->iface_modem_location_parent->disable_location_gathering (self,
                                                                           source,
                                                                           (GAsyncReadyCallback)parent_disable_location_gathering_ready,
                                                                           task);
            return;
        }
        /* Otherwise, we're done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* We only expect GPS sources here */
    g_assert (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                        MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                        MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                        MM_MODEM_LOCATION_SOURCE_AGPS_MSB));

    /* Update engine based on the expected sources */
    gps_engine_state_select (MM_SHARED_XMM (self),
                             gps_engine_state_get_expected (priv->enabled_sources & ~source),
                             (GAsyncReadyCallback) disable_gps_engine_state_select_ready,
                             task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

gboolean
mm_shared_xmm_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_gps_engine_state_select_ready (MMSharedXmm  *self,
                                      GAsyncResult *res,
                                      GTask        *task)
{
    MMModemLocationSource  source;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_XMM (self));

    if (!gps_engine_state_select_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv->enabled_sources |= source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_XMM (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_xmm_enable_location_gathering (MMIfaceModemLocation  *self,
                                         MMModemLocationSource  source,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
    Private  *priv;
    GTask    *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_XMM (self));
    g_assert (priv->iface_modem_location_parent);

    /* Only consider request if it applies to one of the sources we are
     * supporting, otherwise run parent enable */
    if (priv->iface_modem_location_parent->enable_location_gathering &&
        priv->iface_modem_location_parent->enable_location_gathering_finish &&
        !(priv->supported_sources & source)) {
        priv->iface_modem_location_parent->enable_location_gathering (self,
                                                                      source,
                                                                      (GAsyncReadyCallback)parent_enable_location_gathering_ready,
                                                                      task);
        return;
    }

    /* We only expect GPS sources here */
    g_assert (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                        MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                        MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                        MM_MODEM_LOCATION_SOURCE_AGPS_MSB));

    /* Update engine based on the expected sources */
    gps_engine_state_select (MM_SHARED_XMM (self),
                             gps_engine_state_get_expected (priv->enabled_sources | source),
                             (GAsyncReadyCallback) enable_gps_engine_state_select_ready,
                             task);
}

/*****************************************************************************/
/* Location: Load SUPL server */

gchar *
mm_shared_xmm_location_load_supl_server_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
xlcsslp_query_ready (MMBaseModem  *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    gchar       *supl_address;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response || !mm_xmm_parse_xlcsslp_query_response (response, &supl_address, &error))
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, supl_address, g_free);
    g_object_unref (task);
}

void
mm_shared_xmm_location_load_supl_server (MMIfaceModemLocation *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+XLCSSLP?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)xlcsslp_query_ready,
                              task);
}

/*****************************************************************************/

gboolean
mm_shared_xmm_location_set_supl_server_finish (MMIfaceModemLocation  *self,
                                               GAsyncResult          *res,
                                               GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
xlcsslp_set_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_xmm_location_set_supl_server (MMIfaceModemLocation   *self,
                                        const gchar            *supl,
                                        GAsyncReadyCallback     callback,
                                        gpointer                user_data)
{
    GTask   *task;
    gchar   *cmd = NULL;
    gchar   *fqdn = NULL;
    guint32  ip;
    guint16  port;

    task = g_task_new (self, NULL, callback, user_data);

    mm_parse_supl_address (supl, &fqdn, &ip, &port, NULL);
    g_assert (port);
    if (fqdn)
        cmd = g_strdup_printf ("+XLCSSLP=1,%s,%u", fqdn, port);
    else if (ip) {
        struct in_addr a = { .s_addr = ip };
        gchar buf[INET_ADDRSTRLEN + 1] = { 0 };

        /* we got 'ip' from inet_pton(), so this next step should always succeed */
        g_assert (inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1));
        cmd = g_strdup_printf ("+XLCSSLP=0,%s,%u", buf, port);
    } else
        g_assert_not_reached ();

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)xlcsslp_set_ready,
                              task);
    g_free (cmd);
    g_free (fqdn);
}

/*****************************************************************************/

void
mm_shared_xmm_setup_ports (MMBroadbandModem *self)
{
    Private        *priv;
    MMPortSerialAt *ports[2];
    guint           i;

    priv = get_private (MM_SHARED_XMM (self));
    g_assert (priv->broadband_modem_class_parent);
    g_assert (priv->broadband_modem_class_parent->setup_ports);

    /* Parent setup first always */
    priv->broadband_modem_class_parent->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Setup primary and secondary ports */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* After running AT+XLSRSTOP we may get an unsolicited response
         * reporting its status, we just ignore it. */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            priv->xlsrstop_regex,
            NULL, NULL, NULL);



        /* make sure GPS is stopped in case it was left enabled */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       ports[i],
                                       "+XLSRSTOP",
                                       3, FALSE, FALSE, NULL, NULL, NULL);
    }
}

/*****************************************************************************/

static void
shared_xmm_init (gpointer g_iface)
{
}

GType
mm_shared_xmm_get_type (void)
{
    static GType shared_xmm_type = 0;

    if (!G_UNLIKELY (shared_xmm_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedXmm),  /* class_size */
            shared_xmm_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_xmm_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedXmm", &info, 0);
        g_type_interface_add_prerequisite (shared_xmm_type, MM_TYPE_IFACE_MODEM);
        g_type_interface_add_prerequisite (shared_xmm_type, MM_TYPE_IFACE_MODEM_LOCATION);
    }

    return shared_xmm_type;
}
