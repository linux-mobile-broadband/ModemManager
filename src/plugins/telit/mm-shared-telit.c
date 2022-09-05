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
 * Copyright (C) 2019 Daniele Palmas <dnlplm@gmail.com>
 */

#include <config.h>

#include <stdio.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-modem-helpers-telit.h"
#include "mm-shared-telit.h"

/*****************************************************************************/
/* Private data context */

#define TELIT_LM940_EXT_LTE_BND_SW_REVISION "24.01.516"

#define PRIVATE_TAG "shared-telit-private-tag"
static GQuark private_quark;

typedef struct {
    MMIfaceModem *iface_modem_parent;
    gboolean      alternate_3g_bands;
    gboolean      ext_4g_bands;
    GArray       *supported_bands;
    GArray       *supported_modes;
    gchar        *software_package_version;
} Private;

static void
private_free (Private *priv)
{
    if (priv->supported_bands)
        g_array_unref (priv->supported_bands);
    if (priv->supported_modes)
        g_array_unref (priv->supported_modes);
    g_free (priv->software_package_version);
    g_slice_free (Private, priv);
}

static gboolean
has_alternate_3g_bands (const gchar *revision)
{
    MMTelitModel model;

    model = mm_telit_model_from_revision (revision);
    return (model == MM_TELIT_MODEL_FN980 ||
            model == MM_TELIT_MODEL_FN990 ||
            model == MM_TELIT_MODEL_LM940 ||
            model == MM_TELIT_MODEL_LM960 ||
            model == MM_TELIT_MODEL_LN920);
}

static gboolean
is_bnd_4g_format_hex (const gchar *revision)
{
    MMTelitModel model;

    model = mm_telit_model_from_revision (revision);

    return (model == MM_TELIT_MODEL_FN980   ||
            model == MM_TELIT_MODEL_LE910C1 ||
            model == MM_TELIT_MODEL_LM940   ||
            model == MM_TELIT_MODEL_LM960   ||
            model == MM_TELIT_MODEL_LN920);
}

static gboolean
has_extended_4g_bands (const gchar *revision)
{
    MMTelitModel model;

    model = mm_telit_model_from_revision (revision);
    if (model == MM_TELIT_MODEL_LM940)
        return mm_telit_software_revision_cmp (revision, TELIT_LM940_EXT_LTE_BND_SW_REVISION) >= MM_TELIT_SW_REV_CMP_EQUAL;

    return (model == MM_TELIT_MODEL_FN980 ||
            model == MM_TELIT_MODEL_FN990 ||
            model == MM_TELIT_MODEL_LM960 ||
            model == MM_TELIT_MODEL_LN920);
}

static Private *
get_private (MMSharedTelit *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        if (MM_SHARED_TELIT_GET_INTERFACE (self)->peek_parent_modem_interface)
            priv->iface_modem_parent = MM_SHARED_TELIT_GET_INTERFACE (self)->peek_parent_modem_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

void
mm_shared_telit_store_supported_modes (MMSharedTelit *self,
                                       GArray        *modes)
{
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));
    priv->supported_modes = g_array_ref (modes);
}

void
mm_shared_telit_store_revision (MMSharedTelit *self,
                                const gchar   *revision)
{
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));
    g_clear_pointer (&priv->software_package_version, g_free);
    priv->software_package_version = g_strdup (revision);
    priv->alternate_3g_bands = has_alternate_3g_bands (revision);
    priv->ext_4g_bands = has_extended_4g_bands (revision);
}

void
mm_shared_telit_get_bnd_parse_config (MMIfaceModem *self, MMTelitBNDParseConfig *config)
{
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    config->modem_is_2g = mm_iface_modem_is_2g (self);
    config->modem_is_3g = mm_iface_modem_is_3g (self);
    config->modem_is_4g = mm_iface_modem_is_4g (self);
    config->modem_alternate_3g_bands = priv->alternate_3g_bands;
    config->modem_has_hex_format_4g_bands = is_bnd_4g_format_hex (priv->software_package_version);
    config->modem_ext_4g_bands = priv->ext_4g_bands;
}

/*****************************************************************************/
/* Load current mode (Modem interface) */

gboolean
mm_shared_telit_load_current_modes_finish (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           MMModemMode *allowed,
                                           MMModemMode *preferred,
                                           GError **error)
{
    const gchar *response;
    const gchar *str;
    gint a;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    str = mm_strip_tag (response, "+WS46: ");

    if (!sscanf (str, "%d", &a)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse +WS46 response: '%s'",
                     response);
        return FALSE;
    }

    *preferred = MM_MODEM_MODE_NONE;
    switch (a) {
    case 12:
        *allowed = MM_MODEM_MODE_2G;
        return TRUE;
    case 22:
        *allowed = MM_MODEM_MODE_3G;
        return TRUE;
    case 25:
        if (mm_iface_modem_is_3gpp_lte (self))
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        else
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        return TRUE;
    case 28:
        *allowed = MM_MODEM_MODE_4G;
        return TRUE;
    case 29:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        return TRUE;
    case 30:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
        return TRUE;
    case 31:
        *allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        return TRUE;
    case 36:
        *allowed = MM_MODEM_MODE_5G;
        return TRUE;
    case 37:
        *allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        return TRUE;
    case 38:
        *allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        return TRUE;
    case 40:
        *allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
        return TRUE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse unexpected +WS46 response: '%s'",
                 response);
    return FALSE;
}

void
mm_shared_telit_load_current_modes (MMIfaceModem *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+WS46?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

GArray *
mm_shared_telit_modem_load_supported_bands_finish (MMIfaceModem  *self,
                                                   GAsyncResult  *res,
                                                   GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_bands_ready (MMBaseModem  *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    Private     *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response)
        g_task_return_error (task, error);
    else {
        GArray *bands;
        MMTelitBNDParseConfig config;

        mm_shared_telit_get_bnd_parse_config (MM_IFACE_MODEM (self), &config);

        bands = mm_telit_parse_bnd_test_response (response, &config, self, &error);
        if (!bands)
            g_task_return_error (task, error);
        else {
            /* Store supported bands to be able to build ANY when setting */
            priv->supported_bands = g_array_ref (bands);
            if (priv->ext_4g_bands)
                mm_obj_dbg (self, "telit modem using extended 4G band setup");
            g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
        }
    }

    g_object_unref (task);
}

static void
load_supported_bands_at (MMIfaceModem *self,
                         GTask        *task)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback) load_supported_bands_ready,
                              task);
}

static void
parent_load_supported_bands_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    GArray  *bands;
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    bands = priv->iface_modem_parent->load_supported_bands_finish (MM_IFACE_MODEM (self), res, &error);
    if (bands) {
        g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
        g_object_unref (task);
    } else {
        mm_obj_dbg (self, "parent load supported bands failure, falling back to AT commands");
        load_supported_bands_at (self, task);
        g_clear_error (&error);
    }
}

void
mm_shared_telit_modem_load_supported_bands (MMIfaceModem        *self,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_TELIT (self));

    if (priv->iface_modem_parent &&
        priv->iface_modem_parent->load_supported_bands &&
        priv->iface_modem_parent->load_supported_bands_finish) {
        priv->iface_modem_parent->load_supported_bands (self,
                                                        (GAsyncReadyCallback) parent_load_supported_bands_ready,
                                                        task);
    } else
        load_supported_bands_at (self, task);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

GArray *
mm_shared_telit_modem_load_current_bands_finish (MMIfaceModem  *self,
                                                 GAsyncResult  *res,
                                                 GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_current_bands_ready (MMBaseModem  *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response)
        g_task_return_error (task, error);
    else {
        GArray *bands;
        MMTelitBNDParseConfig config;

        mm_shared_telit_get_bnd_parse_config (MM_IFACE_MODEM (self), &config);

        bands = mm_telit_parse_bnd_query_response (response, &config, self, &error);
        if (!bands)
            g_task_return_error (task, error);
        else
            g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    }

    g_object_unref (task);
}

static void
load_current_bands_at (MMIfaceModem *self,
                       GTask        *task)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_current_bands_ready,
                              task);
}

static void
parent_load_current_bands_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    GArray  *bands;
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    bands = priv->iface_modem_parent->load_current_bands_finish (MM_IFACE_MODEM (self), res, &error);
    if (bands) {
        g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
        g_object_unref (task);
    } else {
        mm_obj_dbg (self, "parent load current bands failure, falling back to AT commands");
        load_current_bands_at (self, task);
        g_clear_error (&error);
    }
}

void
mm_shared_telit_modem_load_current_bands (MMIfaceModem        *self,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_TELIT (self));

    if (priv->iface_modem_parent &&
        priv->iface_modem_parent->load_current_bands &&
        priv->iface_modem_parent->load_current_bands_finish) {
        priv->iface_modem_parent->load_current_bands (self,
                                                      (GAsyncReadyCallback) parent_load_current_bands_ready,
                                                      task);
    } else
        load_current_bands_at (self, task);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

gboolean
mm_shared_telit_modem_set_current_bands_finish (MMIfaceModem  *self,
                                                GAsyncResult  *res,
                                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_current_bands_ready (MMBaseModem  *self,
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
set_current_bands_at (MMIfaceModem *self,
                      GTask        *task)
{
    GError  *error = NULL;
    gchar   *cmd;
    GArray  *bands_array;
    MMTelitBNDParseConfig config;

    bands_array = g_task_get_task_data (task);
    g_assert (bands_array);

    if (bands_array->len == 1 && g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        Private *priv;

        priv = get_private (MM_SHARED_TELIT (self));
        if (!priv->supported_bands) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Couldn't build ANY band settings: unknown supported bands");
            g_object_unref (task);
            return;
        }
        bands_array = priv->supported_bands;
    }

    mm_shared_telit_get_bnd_parse_config (self, &config);
    cmd = mm_telit_build_bnd_request (bands_array, &config, &error);
    if (!cmd) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              20,
                              FALSE,
                              (GAsyncReadyCallback)set_current_bands_ready,
                              task);
    g_free (cmd);
}

static void
parent_set_current_bands_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                GTask        *task)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    if (priv->iface_modem_parent->set_current_bands_finish (MM_IFACE_MODEM (self), res, &error)) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
    } else {
        g_clear_error (&error);
        set_current_bands_at (self, task);
    }
}

void
mm_shared_telit_modem_set_current_bands (MMIfaceModem        *self,
                                         GArray              *bands_array,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_array_ref (bands_array), (GDestroyNotify)g_array_unref);

    if (priv->iface_modem_parent &&
        priv->iface_modem_parent->set_current_bands &&
        priv->iface_modem_parent->set_current_bands_finish) {
        priv->iface_modem_parent->set_current_bands (self,
                                                     bands_array,
                                                     (GAsyncReadyCallback) parent_set_current_bands_ready,
                                                     task);
    } else
        set_current_bands_at (self, task);
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

gboolean
mm_shared_telit_set_current_modes_finish (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ws46_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error)
        /* Let the error be critical. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_telit_set_current_modes (MMIfaceModem *self,
                                   MMModemMode allowed,
                                   MMModemMode preferred,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GTask   *task;
    gchar   *command;
    Private *priv;
    gint     ws46_mode = -1;

    priv = get_private (MM_SHARED_TELIT (self));
    task = g_task_new (self, NULL, callback, user_data);

    if (allowed == MM_MODEM_MODE_ANY && priv->supported_modes) {
        guint i;

        allowed = MM_MODEM_MODE_NONE;
        /* Process list of modes to gather supported ones */
        for (i = 0; i < priv->supported_modes->len; i++) {
            if (g_array_index (priv->supported_modes, MMModemMode, i) & MM_MODEM_MODE_2G)
                allowed |= MM_MODEM_MODE_2G;
            if (g_array_index (priv->supported_modes, MMModemMode, i) & MM_MODEM_MODE_3G)
                allowed |= MM_MODEM_MODE_3G;
            if (g_array_index (priv->supported_modes, MMModemMode, i) & MM_MODEM_MODE_4G)
                allowed |= MM_MODEM_MODE_4G;
            if (g_array_index (priv->supported_modes, MMModemMode, i) & MM_MODEM_MODE_5G)
                allowed |= MM_MODEM_MODE_5G;
        }
    }

    if (allowed == MM_MODEM_MODE_2G)
        ws46_mode = 12;
    else if (allowed == MM_MODEM_MODE_3G)
        ws46_mode = 22;
    else if (allowed == MM_MODEM_MODE_4G)
        ws46_mode = 28;
    else if (allowed == MM_MODEM_MODE_5G)
        ws46_mode = 36;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (mm_iface_modem_is_3gpp_lte (self))
            ws46_mode = 29;
        else
            ws46_mode = 25;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G))
        ws46_mode = 30;
    else if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G))
        ws46_mode = 31;
    else if (allowed == (MM_MODEM_MODE_2G  | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G))
        ws46_mode = 25;
    else if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G))
        ws46_mode = 40;
    else if (allowed == (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G))
        ws46_mode = 37;
    else if (allowed == (MM_MODEM_MODE_3G |MM_MODEM_MODE_4G | MM_MODEM_MODE_5G))
        ws46_mode = 38;

    /* Telit modems do not support preferred mode selection */
    if ((ws46_mode < 0) || (preferred != MM_MODEM_MODE_NONE)) {
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
        g_free (allowed_str);
        g_free (preferred_str);

        g_object_unref (task);
        return;
    }

    command = g_strdup_printf ("AT+WS46=%d", ws46_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback) ws46_set_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Revision loading */

gchar *
mm_shared_telit_modem_load_revision_finish (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_revision_ready (MMBaseModem *self,
                     GAsyncResult *res,
                     GTask *task)
{
    GError *error;
    GVariant *result;

    result = mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (!result) {
        g_task_return_error (task, error);
        g_object_unref (task);
    } else {
        gchar *revision = NULL;

        revision = g_variant_dup_string (result, NULL);
        mm_shared_telit_store_revision (MM_SHARED_TELIT (self), revision);
        g_task_return_pointer (task, revision, g_free);
        g_object_unref (task);
    }
}

/*
 * parse AT#SWPKGV command
 * Execution command returns the software package version without #SWPKGV: command echo.
 * The response is as follows:
 *
 * AT#SWPKGV
 * <Telit Software Package Version>-<Production Parameters Version>
 * <Modem FW Version> (Usually the same value returned by AT+GMR)
 * <Production Parameters Version>
 * <Application FW Version>
 */
static MMBaseModemAtResponseProcessorResult
software_package_version_ready (MMBaseModem   *self,
                                gpointer       none,
                                const gchar   *command,
                                const gchar   *response,
                                gboolean       last_command,
                                const GError  *error,
                                GVariant     **result,
                                GError       **result_error)
{
    gchar *version = NULL;

    if (error) {
        *result = NULL;

        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {
            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        *result_error = NULL;
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    version = mm_telit_parse_swpkgv_response (response);
    if (!version)
        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;

    *result = g_variant_new_take_string (version);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
}

static const MMBaseModemAtCommand revisions[] = {
    { "#SWPKGV",  3, TRUE, software_package_version_ready },
    { "+CGMR",   3, TRUE, mm_base_modem_response_processor_string_ignore_at_errors },
    { "+GMR",   3, TRUE, mm_base_modem_response_processor_string_ignore_at_errors },
    { NULL }
};

void
mm_shared_telit_modem_load_revision (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GTask *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_TELIT (self));

    mm_obj_dbg (self, "loading revision...");
    if (priv->software_package_version) {
        g_task_return_pointer (task,
                               g_strdup (priv->software_package_version),
                               g_free);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        revisions,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        (GAsyncReadyCallback) load_revision_ready,
        task);
}

/*****************************************************************************/

static void
shared_telit_init (gpointer g_iface)
{
}

GType
mm_shared_telit_get_type (void)
{
    static GType shared_telit_type = 0;

    if (!G_UNLIKELY (shared_telit_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedTelit),  /* class_size */
            shared_telit_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_telit_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedTelit", &info, 0);
        g_type_interface_add_prerequisite (shared_telit_type, MM_TYPE_IFACE_MODEM);
    }

    return shared_telit_type;
}

