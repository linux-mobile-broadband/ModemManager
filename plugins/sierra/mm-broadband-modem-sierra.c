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
#include "mm-common-sierra.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierra, mm_broadband_modem_sierra, MM_TYPE_BROADBAND_MODEM, 0,
                            G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    /* We are reporting ALL 3GPP access technologies here */
    *access_technologies = ((MMModemAccessTechnology) GPOINTER_TO_UINT (
                                g_simple_async_result_get_op_res_gpointer (
                                    G_SIMPLE_ASYNC_RESULT (res))));
    *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    return TRUE;
}

static void
cnti_set_ready (MMBaseModem *self,
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
            g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (act), NULL);
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
                                  (GAsyncReadyCallback)cnti_set_ready,
                                  result);
        return;
    }

    /* Cannot do this in CDMA modems */
    g_simple_async_result_set_error (result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "Cannot load access technologies in CDMA modems");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
selrat_query_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
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

        if (mm_get_uint_from_match_info (match_info, 1, &mode) &&
            mode >= 0 &&
            mode <= 4) {
            switch (mode) {
            case 0:
                result.allowed = MM_MODEM_MODE_ANY;
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
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_3G;
                break;
            case 4:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_2G;
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
    }

    if (error)
        g_simple_async_result_take_error (simple, error);
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
    MMAtSerialPort *primary;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

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
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_query_ready,
                                   result);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
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
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *primary;
    gint idx = -1;
    gchar *command;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

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
        if (preferred == MM_MODEM_MODE_3G)
            idx = 3;
        else if (preferred == MM_MODEM_MODE_2G)
            idx = 4;
        else if (preferred == MM_MODEM_MODE_NONE)
            idx = 0;
    } else if (allowed == MM_MODEM_MODE_ANY)
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
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_set_ready,
                                   result);
    g_free (command);
}

/*****************************************************************************/

MMBroadbandModemSierra *
mm_broadband_modem_sierra_new (const gchar *device,
                               const gchar *driver,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_sierra_init (MMBroadbandModemSierra *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->modem_power_up = mm_common_sierra_modem_power_up;
    iface->modem_power_up_finish = mm_common_sierra_modem_power_up_finish;
    iface->create_sim = mm_common_sierra_create_sim;
    iface->create_sim_finish = mm_common_sierra_create_sim_finish;
}

static void
mm_broadband_modem_sierra_class_init (MMBroadbandModemSierraClass *klass)
{
}
