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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-messaging.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-nokia.h"
#include "mm-sim-nokia.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNokia, mm_broadband_modem_nokia, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init));

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_nokia_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New Nokia SIM */
    mm_sim_nokia_new (MM_BASE_MODEM (self),
                      NULL, /* cancellable */
                      callback,
                      user_data);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static MMModemMode
modem_load_supported_modes_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return (MMModemMode)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                              G_SIMPLE_ASYNC_RESULT (res)));
}

static void
modem_load_supported_modes (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMModemMode mode;

    /* Nokia phones don't seem to like AT+WS46?, they just report 2G even if
     * 3G is supported, so we'll just assume they actually do 3G. */
    mode = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

    /* Then, if the modem has LTE caps, it does 4G */
    if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
            mode |= MM_MODEM_MODE_4G;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_modes);
    g_simple_async_result_set_op_res_gpointer (result,
                                               GUINT_TO_POINTER (mode),
                                               NULL);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Modem initialization (Modem interface) */

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
}

static const MMBaseModemAtCommand modem_init_sequence[] = {
    /* Send the init command twice; some devices (Nokia N900) appear to take a
     * few commands before responding correctly.  Instead of penalizing them for
     * being stupid the first time by failing to enable the device, just
     * try again.
     *
     * TODO: only send init command 2nd time if 1st time failed?
     *
     * Also, when initializing a Nokia phone, first enable the echo,
     * and then disable it, so that we get it properly disabled.
     */
    { "Z E1 E0 V1", 3, FALSE, NULL },
    { "Z E1 E0 V1", 3, FALSE, mm_base_modem_response_processor_no_result_continue },

    /* Setup errors */
    { "+CMEE=1", 3, FALSE, NULL },

    /* Additional OPTIONAL initialization */
    { "X4 &C1",  3, FALSE, NULL },

    { NULL }
};

static void
modem_init (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               modem_init_sequence,
                               NULL,  /* response_processor_context */
                               NULL,  /* response_processor_context_free */
                               callback,
                               user_data);
}

/*****************************************************************************/

MMBroadbandModemNokia *
mm_broadband_modem_nokia_new (const gchar *device,
                              const gchar **drivers,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOKIA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_nokia_init (MMBroadbandModemNokia *self)
{
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    /* Don't even try to check messaging support */
    iface->check_support = NULL;
    iface->check_support_finish = NULL;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Setup custom modem init */
    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;

    /* Create Nokia-specific SIM*/
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Nokia handsets (at least N85) do not support "power on"; they do
     * support "power off" but you proabably do not want to turn off the
     * power on your telephone if something went wrong with connecting
     * process. So, disabling both these operations.  The Nokia GSM/UMTS command
     * reference v1.2 also states that only CFUN=0 (turn off but still charge)
     * and CFUN=1 (full functionality) are supported, and since the phone has
     * to be in CFUN=1 before we'll be able to talk to it in the first place,
     * we shouldn't bother with CFUN at all.
     */
    iface->load_power_state = NULL;
    iface->load_power_state_finish = NULL;
    iface->modem_power_up = NULL;
    iface->modem_power_up_finish = NULL;
    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;

    iface->load_supported_modes = modem_load_supported_modes;
    iface->load_supported_modes_finish = modem_load_supported_modes_finish;
}

static void
mm_broadband_modem_nokia_class_init (MMBroadbandModemNokiaClass *klass)
{
}
