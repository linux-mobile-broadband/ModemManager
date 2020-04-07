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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-broadband-modem-iridium.h"
#include "mm-sim-iridium.h"
#include "mm-bearer-iridium.h"
#include "mm-modem-helpers.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemIridium, mm_broadband_modem_iridium, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init))

/*****************************************************************************/
/* Operator Code loading (3GPP interface) */

static gchar *
load_operator_code_finish (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_code (MMIfaceModem3gpp *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    /* Only "90103" operator code is assumed */
    g_task_return_pointer (task, g_strdup ("90103"), g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Operator Name loading (3GPP interface) */

static gchar *
load_operator_name_finish (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_name (MMIfaceModem3gpp *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    /* Only "IRIDIUM" operator name is assumed */
    g_task_return_pointer (task, g_strdup ("IRIDIUM"), g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Enable unsolicited events (SMS indications) (Messaging interface) */

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* AT+CNMI=<mode>,[<mt>[,<bm>[,<ds>[,<bfr>]]]]
     *  but <bm> can only be 0,
     *  and <ds> can only be either 0 or 1
     *
     * Note: Modem may return +CMS ERROR:322, which indicates Memory Full,
     * not a big deal
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=2,1,0,0,1",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Signal quality (Modem interface) */

static guint
load_signal_quality_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    gint quality = 0;
    const gchar *result;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!result)
        return 0;

    /* Skip possible whitespaces after '+CSQF:' and before the response */
    result = mm_strip_tag (result, "+CSQF:");
    while (*result == ' ')
        result++;

    if (sscanf (result, "%d", &quality))
        /* Normalize the quality. <rssi> is NOT given in dBs,
         * given as a relative value between 0 and 5 */
        quality = CLAMP (quality, 0, 5) * 100 / 5;
    else
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse signal quality results");

    return quality;
}

static void
load_signal_quality (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    /* The iridium modem may have a huge delay to get signal quality if we pass
     * AT+CSQ, so we'll default to use AT+CSQF, which is a fast version that
     * returns right away the last signal quality value retrieved */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CSQF",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Flow control (Modem interface) */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_flow_control_ready (MMBroadbandModemIridium *self,
                          GAsyncResult *res,
                          GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical. We DO need RTS/CTS in order to have
         * proper modem disabling. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    /* Enable RTS/CTS flow control.
     * Other available values:
     *   AT&K0: Disable flow control
     *   AT&K3: RTS/CTS
     *   AT&K4: XOFF/XON
     *   AT&K6: Both RTS/CTS and XOFF/XON
     */
    g_object_set (self, MM_BROADBAND_MODEM_FLOW_CONTROL, MM_FLOW_CONTROL_RTS_CTS, NULL);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "&K3",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load supported modes (Modem inteface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GArray *combinations;
    MMModemModeCombination mode;
    GTask *task;

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);

    /* Report CS only, Iridium connections are circuit-switched */
    mode.allowed = MM_MODEM_MODE_CS;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, combinations, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Create SIM (Modem inteface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_iridium_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New Iridium SIM */
    mm_sim_iridium_new (MM_BASE_MODEM (self),
                        NULL, /* cancellable */
                        callback,
                        user_data);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
create_bearer_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
create_bearer (MMIfaceModem *self,
               MMBearerProperties *properties,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    MMBaseBearer *bearer;
    GTask *task;

    mm_obj_dbg (self, "creating Iridium bearer...");
    bearer = mm_bearer_iridium_new (MM_BROADBAND_MODEM_IRIDIUM (self),
                                    properties);
    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

/*****************************************************************************/

static const gchar *primary_init_sequence[] = {
    /* Disable echo */
    "E0",
    /* Get word responses */
    "V1",
    /* Extended numeric codes */
    "+CMEE=1",
    NULL
};

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt *primary;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_iridium_parent_class)->setup_ports (self);

    /* Set 9600 baudrate by default in the AT port */
    mm_obj_dbg (self, "baudrate will be set to 9600 bps...");
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary)
        return;

    g_object_set (G_OBJECT (primary),
                  MM_PORT_SERIAL_BAUD, 9600,
                  MM_PORT_SERIAL_AT_INIT_SEQUENCE, primary_init_sequence,
                  NULL);
}

/*****************************************************************************/

MMBroadbandModemIridium *
mm_broadband_modem_iridium_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_IRIDIUM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Allow only up to 3 consecutive timeouts in the serial port */
                         MM_BASE_MODEM_MAX_TIMEOUTS, 3,
                         /* Only CS network is supported by the Iridium modem */
                         MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, FALSE,
                         NULL);
}

static void
mm_broadband_modem_iridium_init (MMBroadbandModemIridium *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Create Iridium-specific SIM and bearer*/
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;
    iface->create_bearer = create_bearer;
    iface->create_bearer_finish = create_bearer_finish;

    /* CSQF-based signal quality */
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;

    /* RTS/CTS flow control */
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;

    /* No need to power-up/power-down the modem */
    iface->load_power_state = NULL;
    iface->load_power_state_finish = NULL;
    iface->modem_power_up = NULL;
    iface->modem_power_up_finish = NULL;
    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;

    /* Supported modes cannot be queried */
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Fixed operator code and name to be reported */
    iface->load_operator_name = load_operator_name;
    iface->load_operator_name_finish = load_operator_name_finish;
    iface->load_operator_code = load_operator_code;
    iface->load_operator_code_finish = load_operator_code_finish;

    /* Don't try to scan networks with AT+COPS=?.
     * It does work, but it will only reply about the Iridium network
     * being found (so not very helpful, as that is the only one expected), but
     * also, it will use a non-standard reply format. Instead of supporting that
     * specific format used, just fully skip it.
     * For reference, the result is:
     *  +COPS:(002),"IRIDIUM","IRIDIUM","90103",,(000-001),(000-002)
     */
    iface->scan_networks = NULL;
    iface->scan_networks_finish = NULL;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
}

static void
mm_broadband_modem_iridium_class_init (MMBroadbandModemIridiumClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
