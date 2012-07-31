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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    const gchar *response;
    GRegex *r;
    GMatchInfo *match_info;
    gint a = -1;
    gint b = -1;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    g_assert (response != NULL);
    g_assert (allowed != NULL);
    g_assert (preferred != NULL);

    r = g_regex_new ("\\$NWRAT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, error)) {
        g_prefix_error (error,
                        "Failed to parse mode/tech response '%s': ",
                        response);
        g_regex_unref (r);
        return FALSE;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &a) ||
        !mm_get_int_from_match_info (match_info, 2, &b) ||
        a < 0 || a > 2 ||
        b < 1 || b > 2) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse mode/tech response '%s': invalid modes reported",
                     response);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return FALSE;
    }

    switch (a) {
    case 0:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_NONE;
        break;
    case 1:
        if (b == 1) {
            *allowed = MM_MODEM_MODE_2G;
            *preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            *preferred = MM_MODEM_MODE_2G;
        }
        break;
    case 2:
        if (b == 1) {
            *allowed = MM_MODEM_MODE_3G;
            *preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            *preferred = MM_MODEM_MODE_3G;
        }
        break;
    default:
        /* We only allow mode 0|1|2 */
        g_assert_not_reached ();
        break;
    }

    g_match_info_free (match_info);
    g_regex_unref (r);
    return TRUE;
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWRAT?",
                              3,
                              FALSE,
                              callback,
                              user_data);
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
allowed_mode_update_ready (MMBroadbandModemNovatel *self,
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
    gchar *command;
    gint a = -1;
    gint b = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    if (allowed == MM_MODEM_MODE_2G) {
        a = 1;
        b = 1;
    } else if (allowed == MM_MODEM_MODE_3G) {
        a = 2;
        b = 1;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        b = 2;
        if (preferred == MM_MODEM_MODE_NONE)
            a = 0;
        else if (preferred == MM_MODEM_MODE_2G)
            a = 1;
        else if (preferred == MM_MODEM_MODE_3G)
            a = 2;
    }

    if (a < 0 || b < 0) {
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

    command = g_strdup_printf ("AT$NWRAT=%d,%d", a, b);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        result);
    g_free (command);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static const MMBaseModemAtCommand nwdmat_sequence[] = {
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, NULL },
    { NULL }
};

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_novatel_parent_class)->setup_ports (self);

    /* Flip secondary ports to AT mode */
    mm_base_modem_at_sequence (MM_BASE_MODEM (self), nwdmat_sequence, NULL, NULL, NULL, NULL);
}

/*****************************************************************************/

MMBroadbandModemNovatel *
mm_broadband_modem_novatel_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_novatel_init (MMBroadbandModemNovatel *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
}

static void
mm_broadband_modem_novatel_class_init (MMBroadbandModemNovatelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
