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

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-xmm.h"
#include "mm-modem-helpers-xmm.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-xmm-private-tag"
static GQuark private_quark;

typedef struct {
    GArray      *supported_modes;
    GArray      *supported_bands;
    MMModemMode  allowed_modes;
} Private;

static void
private_free (Private *priv)
{
    if (priv->supported_modes)
        g_array_unref (priv->supported_modes);
    if (priv->supported_bands)
        g_array_unref (priv->supported_bands);
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
    Private     *priv;
    GArray      *result = NULL;

    priv = get_private (MM_SHARED_XMM (self));

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
        3,
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
validate_and_build_command_set_current_bands (const GArray *bands_array,
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
            gchar *str;

            str = mm_modem_mode_build_string_from_mask (unapplied);
            mm_warn ("Automatic band selection not applied to non-current modes %s", str);
            g_free (str);
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

        str = mm_common_build_bands_string ((const MMModemBand *)unapplied_bands->data, unapplied_bands->len);
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

    command = validate_and_build_command_set_current_bands (bands_array,
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
        3,
        FALSE,
        (GAsyncReadyCallback)xact_set_bands_ready,
        task);
    g_free (command);
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
    }

    return shared_xmm_type;
}
