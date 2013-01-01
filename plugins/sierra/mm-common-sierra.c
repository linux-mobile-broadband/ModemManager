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

#include "mm-common-sierra.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-sim-sierra.h"

static MMIfaceModem *iface_modem_parent;

/*****************************************************************************/
/* Modem power up (Modem interface) */

gboolean
mm_common_sierra_modem_power_up_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
sierra_power_up_wait_cb (GSimpleAsyncResult *result)
{
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete (result);
    g_object_unref (result);
    return FALSE;
}

static void
cfun_enable_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    guint i;
    const gchar **drivers;
    gboolean is_new_sierra = FALSE;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Many Sierra devices return OK immediately in response to CFUN=1 but
     * need some time to finish powering up, otherwise subsequent commands
     * may return failure or even crash the modem.  Give more time for older
     * devices like the AC860 and C885, which aren't driven by the 'sierra_net'
     * driver.  Assume any DirectIP (ie, sierra_net) device is new enough
     * to allow a lower timeout.
     */
    drivers = mm_base_modem_get_drivers (MM_BASE_MODEM (self));
    for (i = 0; drivers[i]; i++) {
        if (g_str_equal (drivers[i], "sierra_net")) {
            is_new_sierra = TRUE;
            break;
        }
    }

    /* The modem object will be valid in the callback as 'result' keeps a
     * reference to it. */
    g_timeout_add_seconds (is_new_sierra ? 5 : 10, (GSourceFunc)sierra_power_up_wait_cb, simple);
}

static void
pcstate_enable_ready (MMBaseModem *self,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    /* Ignore errors for now; we're not sure if all Sierra CDMA devices support
     * at!pcstate.
     */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_common_sierra_modem_power_up (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_common_sierra_modem_power_up);

    /* For CDMA modems, run !pcstate */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "!pcstate=1",
                                  5,
                                  FALSE,
                                  (GAsyncReadyCallback)pcstate_enable_ready,
                                  result);
        return;
    }

    mm_warn ("Not in full functionality status, power-up command is needed. "
             "Note that it may reboot the modem.");

    /* Try to go to full functionality mode without rebooting the system.
     * Works well if we previously switched off the power with CFUN=4
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1,0", /* ",0" requests no reset */
                              10,
                              FALSE,
                              (GAsyncReadyCallback)cfun_enable_ready,
                              result);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

MMModemPowerState
mm_common_sierra_load_power_state_finish (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_POWER_STATE_UNKNOWN;

    return (MMModemPowerState)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_power_state_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MMModemPowerState state;

    state = iface_modem_parent->load_power_state_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (state), NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_common_sierra_load_power_state (MMIfaceModem *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_common_sierra_load_power_state);

    /* Assume we're initially offline in CDMA-only modems so that we power-up
     * with !pcstate */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Assuming offline in CDMA-only modem...");
        g_simple_async_result_set_op_res_gpointer (result, GUINT_TO_POINTER (MM_MODEM_POWER_STATE_OFF), NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Otherwise run parent's */
    iface_modem_parent->load_power_state (self,
                                          (GAsyncReadyCallback)parent_load_power_state_ready,
                                          result);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

MMSim *
mm_common_sierra_create_sim_finish (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return mm_sim_sierra_new_finish (res, error);
}

void
mm_common_sierra_create_sim (MMIfaceModem *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    /* New Sierra SIM */
    mm_sim_sierra_new (MM_BASE_MODEM (self),
                       NULL, /* cancellable */
                       callback,
                       user_data);
}

/*****************************************************************************/
/* Setup ports */

void
mm_common_sierra_setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *ports[2];
    guint i;
    GRegex *pacsp_regex;

    pacsp_regex = g_regex_new ("\\r\\n\\+PACSP.*\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_CARRIER_DETECT, FALSE,
                      NULL);

        if (i == 1) {
            /* Built-in echo removal conflicts with the APP1 port's limited AT
             * parser, which doesn't always prefix responses with <CR><LF>.
             */
            g_object_set (ports[i],
                          MM_AT_SERIAL_PORT_REMOVE_ECHO, FALSE,
                          NULL);
        }

        mm_at_serial_port_add_unsolicited_msg_handler (
            ports[i],
            pacsp_regex,
            NULL, NULL, NULL);
    }

    g_regex_unref (pacsp_regex);
}

/*****************************************************************************/

void
mm_common_sierra_peek_parent_interfaces (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);
}
