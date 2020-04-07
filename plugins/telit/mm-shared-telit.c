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

#define PRIVATE_TAG "shared-telit-private-tag"
static GQuark private_quark;

typedef struct {
    gboolean  alternate_3g_bands;
    GArray   *supported_bands;
} Private;

static void
private_free (Private *priv)
{
    if (priv->supported_bands)
        g_array_unref (priv->supported_bands);
    g_slice_free (Private, priv);
}

static void
initialize_alternate_3g_band (MMSharedTelit *self,
                              Private       *priv)
{
    MMPort         *primary;
    MMKernelDevice *port;

    primary = MM_PORT (mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)));
    port = mm_port_peek_kernel_device (primary);

    /* Lookup for the tag specifying that we're using the alternate 3G band mapping */
    priv->alternate_3g_bands = mm_kernel_device_get_global_property_as_boolean (port, "ID_MM_TELIT_BND_ALTERNATE");
    if (priv->alternate_3g_bands)
        mm_obj_dbg (self, "telit modem using alternate 3G band mask setup");
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
        initialize_alternate_3g_band (self, priv);
        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
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
mm_shared_telit_load_supported_bands_ready (MMBaseModem  *self,
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

        bands = mm_telit_parse_bnd_test_response (response,
                                                  mm_iface_modem_is_2g (MM_IFACE_MODEM (self)),
                                                  mm_iface_modem_is_3g (MM_IFACE_MODEM (self)),
                                                  mm_iface_modem_is_4g (MM_IFACE_MODEM (self)),
                                                  priv->alternate_3g_bands,
                                                  self,
                                                  &error);
        if (!bands)
            g_task_return_error (task, error);
        else {
            /* Store supported bands to be able to build ANY when setting */
            priv->supported_bands = g_array_ref (bands);
            g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
        }
    }

    g_object_unref (task);
}

void
mm_shared_telit_modem_load_supported_bands (MMIfaceModem        *self,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback) mm_shared_telit_load_supported_bands_ready,
                              g_task_new (self, NULL, callback, user_data));
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
mm_shared_telit_load_current_bands_ready (MMBaseModem  *self,
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

        bands = mm_telit_parse_bnd_query_response (response,
                                                   mm_iface_modem_is_2g (MM_IFACE_MODEM (self)),
                                                   mm_iface_modem_is_3g (MM_IFACE_MODEM (self)),
                                                   mm_iface_modem_is_4g (MM_IFACE_MODEM (self)),
                                                   priv->alternate_3g_bands,
                                                   self,
                                                   &error);
        if (!bands)
            g_task_return_error (task, error);
        else
            g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    }

    g_object_unref (task);
}

void
mm_shared_telit_modem_load_current_bands (MMIfaceModem        *self,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) mm_shared_telit_load_current_bands_ready,
                              g_task_new (self, NULL, callback, user_data));
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

void
mm_shared_telit_modem_set_current_bands (MMIfaceModem        *self,
                                         GArray              *bands_array,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    GTask   *task;
    GError  *error = NULL;
    gchar   *cmd;
    Private *priv;

    priv = get_private (MM_SHARED_TELIT (self));

    task = g_task_new (self, NULL, callback, user_data);

    if (bands_array->len == 1 && g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        if (!priv->supported_bands) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Couldn't build ANY band settings: unknown supported bands");
            g_object_unref (task);
            return;
        }
        bands_array = priv->supported_bands;
    }

    cmd = mm_telit_build_bnd_request (bands_array,
                                      mm_iface_modem_is_2g (self),
                                      mm_iface_modem_is_3g (self),
                                      mm_iface_modem_is_4g (self),
                                      priv->alternate_3g_bands,
                                      &error);
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
mm_shared_telit_ws46_set_ready (MMBaseModem *self,
                                GAsyncResult *res,
                                GTask *task)
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
    GTask *task;
    gchar *command;
    gint ws46_mode = -1;

    task = g_task_new (self, NULL, callback, user_data);

    if (allowed == MM_MODEM_MODE_2G)
        ws46_mode = 12;
    else if (allowed == MM_MODEM_MODE_3G)
        ws46_mode = 22;
    else if (allowed == MM_MODEM_MODE_4G)
        ws46_mode = 28;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (mm_iface_modem_is_3gpp_lte (self))
            ws46_mode = 29;
        else
            ws46_mode = 25;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G))
        ws46_mode = 30;
    else if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G))
        ws46_mode = 31;
    else if (allowed == (MM_MODEM_MODE_2G  | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G) ||
             allowed == MM_MODEM_MODE_ANY)
        ws46_mode = 25;

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
        (GAsyncReadyCallback)mm_shared_telit_ws46_set_ready,
        task);
    g_free (command);
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
