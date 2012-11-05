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
full_functionality_status_ready (MMBaseModem *self,
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

    /* Most Sierra devices return OK immediately in response to CFUN=1 but
     * need some time to finish powering up.  Give more time for older devices
     * like the AC860, which aren't driven by the 'sierra' driver.
     */
    drivers = mm_base_modem_get_drivers (MM_BASE_MODEM (self));
    for (i = 0; drivers[i]; i++) {
        if (g_str_equal (drivers[i], "sierra")) {
            is_new_sierra = TRUE;
            break;
        }
    }

    /* The modem object will be valid in the callback as 'result' keeps a
     * reference to it. */
    g_timeout_add_seconds (is_new_sierra ? 5 : 10, (GSourceFunc)sierra_power_up_wait_cb, simple);
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
                              "+CFUN=1,0", /* ",0" ensures no reset */
                              3,
                              FALSE,
                              (GAsyncReadyCallback)full_functionality_status_ready,
                              simple);
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

    /* For 3GPP modems, check if we'll need the power up */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)get_current_functionality_status_ready,
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
    GRegex *pacsp0_regex;

    pacsp0_regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

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
            pacsp0_regex,
            NULL, NULL, NULL);
    }

    g_regex_unref (pacsp0_regex);
}
