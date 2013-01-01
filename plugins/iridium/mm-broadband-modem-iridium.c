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
#include "mm-log.h"
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
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init));

/*****************************************************************************/
/* Initializing the modem (Modem interface) */

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static const MMBaseModemAtCommand modem_init_sequence[] = {
    /* Init command */
    { "E0 V1", 3, FALSE, NULL },
    { "+CMEE=1", 3, FALSE, NULL },
    { NULL }
};

static void
init_sequence_ready (MMBroadbandModem *self,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gboolean
after_atz_sleep_cb (GSimpleAsyncResult *simple)
{
    MMBaseModem *self;

    self = MM_BASE_MODEM (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
    /* Now, run the remaining sequence */
    mm_base_modem_at_sequence (self,
                               modem_init_sequence,
                               NULL,  /* response_processor_context */
                               NULL,  /* response_processor_context_free */
                               (GAsyncReadyCallback)init_sequence_ready,
                               simple);
    g_object_unref (self);
    return FALSE;
}

static void
atz_ready (MMBroadbandModem *self,
           GAsyncResult *res,
           GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Once ATZ reply is received, we need to wait a bit before going on,
     * otherwise, the next commands given will receive garbage as reply
     * (500ms should be enough) */
    g_timeout_add (500, (GSourceFunc)after_atz_sleep_cb, simple);
}

static void
modem_init (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_init);
    /* First, send ATZ alone */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "Z",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)atz_ready,
                              result);
}

/*****************************************************************************/
/* Operator Code and Name loading (3GPP interface) */

static gchar *
load_operator_code_finish (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    /* Only "90103" operator code is assumed */
    return g_strdup ("90103");
}

static gchar *
load_operator_name_finish (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    /* Only "IRIDIUM" operator name is assumed */
    return g_strdup ("IRIDIUM");
}

static void
load_operator_name_or_code (MMIfaceModem3gpp *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_operator_name_or_code);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_flow_control_ready (MMBroadbandModemIridium *self,
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

    /* Enable RTS/CTS flow control.
     * Other available values:
     *   AT&K0: Disable flow control
     *   AT&K3: RTS/CTS
     *   AT&K4: XOFF/XON
     *   AT&K6: Both RTS/CTS and XOFF/XON
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "&K3",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              result);
}

/*****************************************************************************/
/* Load supported modes (Modem inteface) */

static MMModemMode
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    /* Report CS only, Iridium connections are circuit-switched */
    return MM_MODEM_MODE_CS;
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_modes);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Create SIM (Modem inteface) */

static MMSim *
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

static MMBearer *
create_bearer_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New Iridium bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
create_bearer (MMIfaceModem *self,
               MMBearerProperties *properties,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    MMBearer *bearer;
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        create_bearer);
    mm_dbg ("Creating Iridium bearer...");
    bearer = mm_bearer_iridium_new (MM_BROADBAND_MODEM_IRIDIUM (self),
                                    properties);
    g_simple_async_result_set_op_res_gpointer (result,
                                               bearer,
                                               (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *primary;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_iridium_parent_class)->setup_ports (self);

    /* Set 9600 baudrate by default in the AT port */
    mm_dbg ("Baudrate will be set to 9600 bps...");
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary)
        return;

    g_object_set (G_OBJECT (primary),
                  MM_SERIAL_PORT_BAUD, 9600,
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
    /* Initialization */
    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;

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
    iface->modem_init_power_down = NULL;
    iface->modem_init_power_down_finish = NULL;

    /* Supported modes cannot be queried */
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Fixed operator code and name to be reported */
    iface->load_operator_name = load_operator_name_or_code;
    iface->load_operator_name_finish = load_operator_name_finish;
    iface->load_operator_code = load_operator_name_or_code;
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
