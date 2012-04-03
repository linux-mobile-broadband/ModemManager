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

#include <libmm-common.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-wavecom.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemWavecom, mm_broadband_modem_wavecom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

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
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
full_functionality_status_ready (MMBaseModem *self,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
get_current_functionality_status_ready (MMBaseModem *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_warn ("Failed checking if power-up command is needed: '%s'. "
                 "Will assume it isn't.",
                 error->message);
        g_error_free (error);
        /* On error, just assume we don't need the power-up command */
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CFUN:");
    if (response && *response == '1') {
        /* If reported functionality status is '1', then we do not need to
         * issue the power-up command. Otherwise, do it. */
        mm_dbg ("Already in full functionality status, skipping power-up command");
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    mm_warn ("Not in full functionality status, power-up command is needed. "
             "Note that it may reboot the modem.");

    /* Try to go to full functionality mode without rebooting the system.
     * Works well if we previously switched off the power with CFUN=4
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1,0",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)full_functionality_status_ready,
                              simple);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_up);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)get_current_functionality_status_ready,
                              result);
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

MMBroadbandModemWavecom *
mm_broadband_modem_wavecom_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_WAVECOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
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
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
mm_broadband_modem_wavecom_class_init (MMBroadbandModemWavecomClass *klass)
{
}
