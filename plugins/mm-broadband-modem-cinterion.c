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
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-cinterion.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemCinterion, mm_broadband_modem_cinterion, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

struct _MMBroadbandModemCinterionPrivate {
    /* Command to go into sleep mode */
    gchar *sleep_mode_cmd;

    /* Supported networks */
    gboolean only_geran;
    gboolean only_utran;
    gboolean both_geran_utran;
};

/*****************************************************************************/
/* MODEM POWER UP */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    /* Ignore errors */
    return TRUE;
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MMBroadbandModemCinterion *cinterion = MM_BROADBAND_MODEM_CINTERION (self);
    GSimpleAsyncResult *result;

    if (cinterion->priv->sleep_mode_cmd)
        mm_base_modem_at_command_ignore_reply (MM_BASE_MODEM (self),
                                               cinterion->priv->sleep_mode_cmd,
                                               5);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_down);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* AFTER POWER UP */

static gboolean
modem_after_power_up_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return !!mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
}

static gboolean
parse_supported_functionality_status (MMBroadbandModemCinterion *self,
                                      gpointer none,
                                      const gchar *command,
                                      const gchar *response,
                                      gboolean last_command,
                                      const GError *error,
                                      GVariant **result,
                                      GError **result_error)
{
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
    g_free (self->priv->sleep_mode_cmd);
    if (strstr (response, "4") != NULL) {
        mm_dbg ("Device supports CFUN=4 sleep mode");
        self->priv->sleep_mode_cmd = g_strdup ("+CFUN=4");
    } else if (strstr (response, "7") != NULL) {
        mm_dbg ("Device supports CFUN=7 sleep mode");
        self->priv->sleep_mode_cmd = g_strdup ("+CFUN=7");
    } else {
        mm_warn ("Unknown functionality mode to go into sleep mode");
        self->priv->sleep_mode_cmd = NULL;
    }

    /* Keep on with next command */
    return FALSE;
}

static gboolean
parse_supported_networks (MMBroadbandModemCinterion *self,
                          gpointer none,
                          const gchar *command,
                          const gchar *response,
                          gboolean last_command,
                          const GError *error,
                          GVariant **result,
                          GError **result_error)
{
    /* Note: Documentation says that AT+WS46=? is replied with '+WS46:' followed
     * by a list of supported network modes between parenthesis, but the EGS5
     * used to test this didn't use the 'WS46:' prefix. Also, more than one
     * numeric ID may appear in the list, that's why they are checked
     * separately. * */

    if (strstr (response, "12") != NULL) {
        mm_dbg ("Device allows 2G-only network mode");
        self->priv->only_geran = TRUE;
    }

    if (strstr (response, "22") != NULL) {
        mm_dbg ("Device allows 3G-only network mode");
        self->priv->only_utran = TRUE;
    }

    if (strstr (response, "25") != NULL) {
        mm_dbg ("Device allows 2G/3G network mode");
        self->priv->both_geran_utran = TRUE;
    }

    /* If no expected ID found, error */
    if (!self->priv->only_geran &&
        !self->priv->only_utran &&
        !self->priv->both_geran_utran) {
        mm_warn ("Invalid list of supported networks: '%s'", response);
        *result_error = g_error_new (MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Invalid list of supported networks: '%s'",
                                     response);
        return FALSE;
    }

    /* Keep on with next command */
    return FALSE;
}

static const MMBaseModemAtCommand after_power_up_commands[] = {
    { "+CFUN=?",  3, FALSE, (MMBaseModemAtResponseProcessor)parse_supported_functionality_status },
    { "+WS46=?", 3, FALSE, (MMBaseModemAtResponseProcessor)parse_supported_networks },
    { NULL }
};

static void
modem_after_power_up (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        after_power_up_commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        callback,
        user_data);
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
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              result);
}

/*****************************************************************************/

MMBroadbandModemCinterion *
mm_broadband_modem_cinterion_new (const gchar *device,
                                  const gchar *driver,
                                  const gchar *plugin,
                                  guint16 vendor_id,
                                  guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_CINTERION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
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
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->modem_after_power_up = modem_after_power_up;
    iface->modem_after_power_up_finish = modem_after_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
mm_broadband_modem_cinterion_class_init (MMBroadbandModemCinterionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemCinterionPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
