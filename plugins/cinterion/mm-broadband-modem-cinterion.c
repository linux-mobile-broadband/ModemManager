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
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-cinterion.h"
#include "mm-modem-helpers-cinterion.h"
#include "mm-common-cinterion.h"

static void iface_modem_init      (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemCinterion, mm_broadband_modem_cinterion, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init))

struct _MMBroadbandModemCinterionPrivate {
    /* Flag to know if we should try AT^SIND or not to get psinfo */
    gboolean sind_psinfo;

    /* Command to go into sleep mode */
    gchar *sleep_mode_cmd;

    /* Cached manual selection attempt */
    gchar *manual_operator_id;

    /* Cached supported bands in Cinterion format */
    guint supported_bands;

    /* Cached supported modes for SMS setup */
    GArray *cnmi_supported_mode;
    GArray *cnmi_supported_mt;
    GArray *cnmi_supported_bm;
    GArray *cnmi_supported_ds;
    GArray *cnmi_supported_bfr;
};

/*****************************************************************************/
/* Unsolicited events enabling */

static gboolean
enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
enable_unsolicited_events (MMIfaceModem3gpp *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    /* AT=CMER=[<mode>[,<keyp>[,<disp>[,<ind>[,<bfr>]]]]]
     *  but <ind> should be either not set, or equal to 0 or 2.
     * Enabled with 2.
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CMER=3,0,0,2",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Enable unsolicited events (SMS indications) (Messaging interface) */

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cnmi_test_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gboolean
value_supported (const GArray *array,
                 const guint value)
{
    guint i;

    if (!array)
        return FALSE;

    for (i = 0; i < array->len; i++) {
        if (g_array_index (array, guint, i) == value)
            return TRUE;
    }
    return FALSE;
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (_self);
    GString *cmd;
    GError *error = NULL;
    GSimpleAsyncResult *simple;

    simple = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        messaging_enable_unsolicited_events);

    /* AT+CNMI=<mode>,[<mt>[,<bm>[,<ds>[,<bfr>]]]] */
    cmd = g_string_new ("+CNMI=");

    /* Mode 2 or 1 */
    if (!error) {
        if (value_supported (self->priv->cnmi_supported_mode, 2))
            g_string_append_printf (cmd, "%u,", 2);
        else if (value_supported (self->priv->cnmi_supported_mode, 1))
            g_string_append_printf (cmd, "%u,", 1);
        else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "SMS settings don't accept [2,1] <mode>");
    }

    /* mt 2 or 1 */
    if (!error) {
        if (value_supported (self->priv->cnmi_supported_mt, 2))
            g_string_append_printf (cmd, "%u,", 2);
        else if (value_supported (self->priv->cnmi_supported_mt, 1))
            g_string_append_printf (cmd, "%u,", 1);
        else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "SMS settings don't accept [2,1] <mt>");
    }

    /* bm 2 or 0 */
    if (!error) {
        if (value_supported (self->priv->cnmi_supported_bm, 2))
            g_string_append_printf (cmd, "%u,", 2);
        else if (value_supported (self->priv->cnmi_supported_bm, 0))
            g_string_append_printf (cmd, "%u,", 0);
        else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "SMS settings don't accept [2,0] <bm>");
    }

    /* ds 2, 1 or 0 */
    if (!error) {
        if (value_supported (self->priv->cnmi_supported_ds, 2))
            g_string_append_printf (cmd, "%u,", 2);
        else if (value_supported (self->priv->cnmi_supported_ds, 1))
            g_string_append_printf (cmd, "%u,", 1);
        else if (value_supported (self->priv->cnmi_supported_ds, 0))
            g_string_append_printf (cmd, "%u,", 0);
        else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "SMS settings don't accept [2,1,0] <ds>");
    }

    /* bfr 1 */
    if (!error) {
        if (value_supported (self->priv->cnmi_supported_bfr, 1))
            g_string_append_printf (cmd, "%u", 1);
        /* otherwise, skip setting it */
    }

    /* Early error report */
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        g_string_free (cmd, TRUE);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd->str,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cnmi_test_ready,
                              simple);
    g_string_free (cmd, TRUE);
}

/*****************************************************************************/
/* Check if Messaging supported (Messaging interface) */

static gboolean
messaging_check_support_finish (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cnmi_format_check_ready (MMBroadbandModemCinterion *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Parse */
    if (!mm_cinterion_parse_cnmi_test (response,
                                       &self->priv->cnmi_supported_mode,
                                       &self->priv->cnmi_supported_mt,
                                       &self->priv->cnmi_supported_bm,
                                       &self->priv->cnmi_supported_ds,
                                       &self->priv->cnmi_supported_bfr,
                                       &error)) {
        mm_warn ("error reading SMS setup: %s", error->message);
        g_error_free (error);
    }

    /* CNMI command is supported; assume we have full messaging capabilities */
    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
messaging_check_support (MMIfaceModemMessaging *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        messaging_check_support);

    /* We assume that CDMA-only modems don't have messaging capabilities */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "CDMA-only modems don't have messaging capabilities");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Check CNMI support */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cnmi_format_check_ready,
                              result);
}

/*****************************************************************************/
/* MODEM POWER DOWN */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
sleep_ready (MMBaseModem *self,
             GAsyncResult *res,
             GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    /* Ignore errors */
    if (error) {
        mm_dbg ("Couldn't send power down command: '%s'", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
send_sleep_mode_command (MMBroadbandModemCinterion *self,
                         GSimpleAsyncResult *operation_result)
{
    if (self->priv->sleep_mode_cmd &&
        self->priv->sleep_mode_cmd[0]) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  self->priv->sleep_mode_cmd,
                                  5,
                                  FALSE,
                                  (GAsyncReadyCallback)sleep_ready,
                                  operation_result);
        return;
    }

    /* No default command; just finish without sending anything */
    g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete_in_idle (operation_result);
    g_object_unref (operation_result);
}

static void
supported_functionality_status_query_ready (MMBroadbandModemCinterion *self,
                                            GAsyncResult *res,
                                            GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;

    g_assert (self->priv->sleep_mode_cmd == NULL);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_warn ("Couldn't query supported functionality status: '%s'",
                 error->message);
        g_error_free (error);
        self->priv->sleep_mode_cmd = g_strdup ("");
    } else {
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
        if (strstr (response, "4") != NULL) {
            mm_dbg ("Device supports CFUN=4 sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("+CFUN=4");
        } else if (strstr (response, "7") != NULL) {
            mm_dbg ("Device supports CFUN=7 sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("+CFUN=7");
        } else {
            mm_warn ("Unknown functionality mode to go into sleep mode");
            self->priv->sleep_mode_cmd = g_strdup ("");
        }
    }

    send_sleep_mode_command (self, operation_result);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MMBroadbandModemCinterion *cinterion = MM_BROADBAND_MODEM_CINTERION (self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_down);

    /* If sleep command already decided, use it. */
    if (cinterion->priv->sleep_mode_cmd)
        send_sleep_mode_command (MM_BROADBAND_MODEM_CINTERION (self),
                                 result);
    else
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "+CFUN=?",
            3,
            FALSE,
            (GAsyncReadyCallback)supported_functionality_status_query_ready,
            result);
}

/*****************************************************************************/
/* Modem Power Off */

#define MAX_POWER_OFF_WAIT_TIME_SECS 20

typedef struct {
    MMBroadbandModemCinterion *self;
    MMPortSerialAt *port;
    GSimpleAsyncResult *result;
    GRegex *shutdown_regex;
    gboolean shutdown_received;
    gboolean smso_replied;
    gboolean serial_open;
    guint timeout_id;
} PowerOffContext;

static void
power_off_context_complete_and_free (PowerOffContext *ctx)
{
    if (ctx->serial_open)
        mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    if (ctx->timeout_id)
        g_source_remove (ctx->timeout_id);
    mm_port_serial_at_add_unsolicited_msg_handler (ctx->port, ctx->shutdown_regex, NULL, NULL, NULL);
    g_object_unref (ctx->port);
    g_object_unref (ctx->self);
    g_regex_unref (ctx->shutdown_regex);
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_slice_free (PowerOffContext, ctx);
}

static gboolean
modem_power_off_finish (MMIfaceModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
complete_power_off (PowerOffContext *ctx)
{
    if (!ctx->shutdown_received || !ctx->smso_replied)
        return;

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    power_off_context_complete_and_free (ctx);
}

static void
smso_ready (MMBaseModem *self,
            GAsyncResult *res,
            PowerOffContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        power_off_context_complete_and_free (ctx);
        return;
    }

    /* Set as replied */
    ctx->smso_replied = TRUE;
    complete_power_off (ctx);
}

static void
shutdown_received (MMPortSerialAt *port,
                   GMatchInfo *match_info,
                   PowerOffContext *ctx)
{
    /* Cleanup handler */
    mm_port_serial_at_add_unsolicited_msg_handler (port, ctx->shutdown_regex, NULL, NULL, NULL);
    /* Set as received */
    ctx->shutdown_received = TRUE;
    complete_power_off (ctx);
}

static gboolean
power_off_timeout_cb (PowerOffContext *ctx)
{
    ctx->timeout_id = 0;

    /* The SMSO reply should have come earlier */
    g_warn_if_fail (ctx->smso_replied == TRUE);

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Power off operation timed out");
    power_off_context_complete_and_free (ctx);

    return G_SOURCE_REMOVE;
}

static void
modem_power_off (MMIfaceModem *self,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    PowerOffContext *ctx;
    GError *error = NULL;

    ctx = g_slice_new0 (PowerOffContext);
    ctx->self = g_object_ref (self);
    ctx->port = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_power_off);
    ctx->shutdown_regex = g_regex_new ("\\r\\n\\^SHUTDOWN\\r\\n",
                                       G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    ctx->timeout_id = g_timeout_add_seconds (MAX_POWER_OFF_WAIT_TIME_SECS,
                                             (GSourceFunc)power_off_timeout_cb,
                                             ctx);

    /* We'll need to wait for a ^SHUTDOWN before returning the action, which is
     * when the modem tells us that it is ready to be shutdown */
    mm_port_serial_at_add_unsolicited_msg_handler (
        ctx->port,
        ctx->shutdown_regex,
        (MMPortSerialAtUnsolicitedMsgFn)shutdown_received,
        ctx,
        NULL);

    /* In order to get the ^SHUTDOWN notification, we must keep the port open
     * during the wait time */
    ctx->serial_open = mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error);
    if (G_UNLIKELY (error)) {
        g_simple_async_result_take_error (ctx->result, error);
        power_off_context_complete_and_free (ctx);
        return;
    }

    /* Note: we'll use a timeout < MAX_POWER_OFF_WAIT_TIME_SECS for the AT command,
     * so we're sure that the AT command reply will always come before the timeout
     * fires */
    g_assert (MAX_POWER_OFF_WAIT_TIME_SECS > 5);
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   ctx->port,
                                   "^SMSO",
                                   5,
                                   FALSE, /* allow_cached */
                                   FALSE, /* is_raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)smso_ready,
                                   ctx);
}

/*****************************************************************************/
/* ACCESS TECHNOLOGIES */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    *access_technologies = (MMModemAccessTechnology) GPOINTER_TO_UINT (
        g_simple_async_result_get_op_res_gpointer (
            G_SIMPLE_ASYNC_RESULT (res)));
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
smong_query_ready (MMBroadbandModemCinterion *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *operation_result)
{
    const gchar             *response;
    GError                  *error = NULL;
    MMModemAccessTechnology  access_tech;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
    } else if (!mm_cinterion_parse_smong_response (response, &access_tech, &error)) {
        /* We'll reset here the flag to try to use SIND/psinfo the next time */
        self->priv->sind_psinfo = TRUE;
        g_simple_async_result_take_error (operation_result, error);
    } else {
        /* We'll default to use SMONG then */
        self->priv->sind_psinfo = FALSE;
        g_simple_async_result_set_op_res_gpointer (operation_result, GUINT_TO_POINTER (access_tech), NULL);
    }

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static MMModemAccessTechnology
get_access_technology_from_psinfo (const gchar *psinfo,
                                   GError **error)
{
    guint psinfoval;

    if (mm_get_uint_from_str (psinfo, &psinfoval)) {
        switch (psinfoval) {
        case 0:
            return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        case 1:
        case 2:
            return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        case 3:
        case 4:
            return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
        case 5:
        case 6:
            return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        case 7:
        case 8:
            return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
        case 9:
        case 10:
            return (MM_MODEM_ACCESS_TECHNOLOGY_HSDPA | MM_MODEM_ACCESS_TECHNOLOGY_HSUPA);
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't get network capabilities, "
                 "invalid psinfo value: '%s'",
                 psinfo);
    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
sind_query_ready (MMBroadbandModemCinterion *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *operation_result)
{
    const gchar *response;
    GError *error = NULL;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    /* The AT^SIND? command replies a list of several different indicators.
     * We will only look for 'psinfo' which is the one which may tell us
     * the available network access technology. Note that only 3G-enabled
     * devices seem to have this indicator.
     *
     * AT+SIND?
     * ^SIND: battchg,1,1
     * ^SIND: signal,1,99
     * ...
     */
    regex = g_regex_new ("\\r\\n\\^SIND:\\s*psinfo,\\s*(\\d*),\\s*(\\d*)", 0, 0, NULL);
    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, NULL)) {
        MMModemAccessTechnology act;
        gchar *ind_value;

        ind_value = g_match_info_fetch (match_info, 2);
        act = get_access_technology_from_psinfo (ind_value, &error);
        g_free (ind_value);
        g_simple_async_result_set_op_res_gpointer (operation_result, GUINT_TO_POINTER (act), NULL);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
    } else {
        /* If there was no 'psinfo' indicator, we'll try AT^SMONG and read the cell
         * info table. */
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SMONG",
            3,
            FALSE,
            (GAsyncReadyCallback)smong_query_ready,
            operation_result);
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MMBroadbandModemCinterion *broadband = MM_BROADBAND_MODEM_CINTERION (self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_access_technologies);

    if (broadband->priv->sind_psinfo) {
        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            "^SIND?",
            3,
            FALSE,
            (GAsyncReadyCallback)sind_query_ready,
            result);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "^SMONG",
        3,
        FALSE,
        (GAsyncReadyCallback)smong_query_ready,
        result);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_array_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 3);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    /* Filter out those unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations);
    g_array_unref (all);
    g_array_unref (combinations);

    g_simple_async_result_set_op_res_gpointer (simple, filtered, (GDestroyNotify) g_array_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_supported_modes));
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_access_technology_update_ready (MMBroadbandModemCinterion *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
    else {
        /* Request immediate access tech update */
        mm_iface_modem_refresh_access_technologies (MM_IFACE_MODEM (self));
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    }
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_current_modes (MMIfaceModem *_self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (_self);

    g_assert (preferred == MM_MODEM_MODE_NONE);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_current_modes);

    /* For dual 2G/3G devices... */
    if (mm_iface_modem_is_2g (_self) &&
        mm_iface_modem_is_3g (_self)) {
        gchar *command;

        /* We will try to simulate the possible allowed modes here. The
         * Cinterion devices do not seem to allow setting preferred access
         * technology in 3G devices, but they allow restricting to a given
         * one:
         * - 2G-only is forced by forcing GERAN RAT (AcT=0)
         * - 3G-only is forced by forcing UTRAN RAT (AcT=2)
         * - for the remaining ones, we default to automatic selection of RAT,
         *   which is based on the quality of the connection.
         */

        if (allowed == MM_MODEM_MODE_3G)
            command = g_strdup ("+COPS=,,,2");
        else if (allowed == MM_MODEM_MODE_2G)
            command = g_strdup ("+COPS=,,,0");
        else if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G)) {
            /* no AcT given, defaults to Auto. For this case, we cannot provide
             * AT+COPS=,,, (i.e. just without a last value). Instead, we need to
             * re-run the last manual/automatic selection command which succeeded,
             * (or auto by default if none was launched) */
            if (self->priv->manual_operator_id)
                command = g_strdup_printf ("+COPS=1,2,\"%s\"", self->priv->manual_operator_id);
            else
                command = g_strdup ("+COPS=0");
        } else
            g_assert_not_reached ();

        mm_base_modem_at_command (
            MM_BASE_MODEM (self),
            command,
            20,
            FALSE,
            (GAsyncReadyCallback)allowed_access_technology_update_ready,
            result);
        g_free (command);
        return;
    }

    /* For 2G-only and 3G-only devices, we already stated that we don't
     * support mode switching. */
    g_assert_not_reached ();
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

typedef struct {
    MMBroadbandModemCinterion *self;
    GSimpleAsyncResult *result;
    gchar *operator_id;
} RegisterInNetworkContext;

static void
register_in_network_context_complete_and_free (RegisterInNetworkContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx->operator_id);
    g_slice_free (RegisterInNetworkContext, ctx);
}

static gboolean
register_in_network_finish (MMIfaceModem3gpp *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cops_write_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  RegisterInNetworkContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else {
        /* Update cached */
        g_free (ctx->self->priv->manual_operator_id);
        ctx->self->priv->manual_operator_id = g_strdup (ctx->operator_id);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    register_in_network_context_complete_and_free (ctx);
}

static void
register_in_network (MMIfaceModem3gpp *self,
                     const gchar *operator_id,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    RegisterInNetworkContext *ctx;
    gchar *command;

    ctx = g_slice_new (RegisterInNetworkContext);
    ctx->self = g_object_ref (self);
    ctx->operator_id = g_strdup (operator_id);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             register_in_network);

    /* If the user sent a specific network to use, lock it in. */
    if (operator_id)
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", operator_id);
    else
        command = g_strdup ("+COPS=0");

    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), NULL),
                                   command,
                                   120,
                                   FALSE,
                                   FALSE, /* raw */
                                   cancellable,
                                   (GAsyncReadyCallback)cops_write_ready,
                                   ctx);
    g_free (command);
}

/*****************************************************************************/
/* Supported bands (Modem interface) */

static GArray *
load_supported_bands_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
scfg_test_ready (MMBaseModem *_self,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (_self);
    const gchar *response;
    GError *error = NULL;
    GArray *bands;

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response)
        g_simple_async_result_take_error (simple, error);
    else if (!mm_cinterion_parse_scfg_test (response,
                                            mm_broadband_modem_get_current_charset (MM_BROADBAND_MODEM (self)),
                                            &bands,
                                            &error))
        g_simple_async_result_take_error (simple, error);
    else {
        mm_cinterion_build_band (bands, 0, FALSE, &self->priv->supported_bands, NULL);
        g_assert (self->priv->supported_bands != 0);
        g_simple_async_result_set_op_res_gpointer (simple, bands, (GDestroyNotify)g_array_unref);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_supported_bands (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *simple;

    simple = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_bands);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT^SCFG=?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)scfg_test_ready,
                              simple);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
load_current_bands_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
get_band_ready (MMBroadbandModemCinterion *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;
    GArray *bands = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response)
        g_simple_async_result_take_error (simple, error);
    else if (!mm_cinterion_parse_scfg_response (response,
                                                mm_broadband_modem_get_current_charset (MM_BROADBAND_MODEM (self)),
                                                &bands,
                                                &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple, bands, (GDestroyNotify)g_array_unref);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_current_bands (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_current_bands);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT^SCFG=\"Radio/Band\"",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)get_band_ready,
                              result);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

static gboolean
set_current_bands_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
scfg_set_ready (MMBaseModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical */
        g_simple_async_result_take_error (operation_result, error);
    else {
        /* Request immediate access tech update */
        mm_iface_modem_refresh_access_technologies (MM_IFACE_MODEM (self));
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    }

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_bands_3g (MMIfaceModem *_self,
              GArray *bands_array,
              GSimpleAsyncResult *simple)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (_self);
    GError *error = NULL;
    guint band = 0;
    gchar *cmd;

    if (!mm_cinterion_build_band (bands_array,
                                  self->priv->supported_bands,
                                  FALSE, /* 2G and 3G */
                                  &band,
                                  &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing a number here!
     *
     * The optional <rbe> field is set to 1, so that changes take effect
     * immediately.
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",%u,1", band);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              15,
                              FALSE,
                              (GAsyncReadyCallback)scfg_set_ready,
                              simple);
    g_free (cmd);
}

static void
set_bands_2g (MMIfaceModem *_self,
              GArray *bands_array,
              GSimpleAsyncResult *simple)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (_self);
    GError *error = NULL;
    guint band = 0;
    gchar *cmd;
    gchar *bandstr;

    if (!mm_cinterion_build_band (bands_array,
                                  self->priv->supported_bands,
                                  TRUE, /* 2G only */
                                  &band,
                                  &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* Build string with the value, in the proper charset */
    bandstr = g_strdup_printf ("%u", band);
    bandstr = mm_broadband_modem_take_and_convert_to_current_charset (MM_BROADBAND_MODEM (self), bandstr);
    if (!bandstr) {
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Couldn't convert band set to current charset");
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rbp>,<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing double-quote enclosed strings here!
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",\"%s\",\"%s\"", bandstr, bandstr);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              15,
                              FALSE,
                              (GAsyncReadyCallback)scfg_set_ready,
                              simple);

    g_free (cmd);
    g_free (bandstr);
}

static void
set_current_bands (MMIfaceModem *self,
                   GArray *bands_array,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* The bands that we get here are previously validated by the interface, and
     * that means that ALL the bands given here were also given in the list of
     * supported bands. BUT BUT, that doesn't mean that the exact list of bands
     * will end up being valid, as not all combinations are possible. E.g,
     * Cinterion modems supporting only 2G have specific combinations allowed.
     */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_current_bands);

    if (mm_iface_modem_is_3g (self))
        set_bands_3g (self, bands_array, result);
    else
        set_bands_2g (self, bands_array, result);
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
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              result);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

typedef struct {
    MMBroadbandModemCinterion *self;
    GSimpleAsyncResult *result;
    MMUnlockRetries *retries;
    guint i;
} LoadUnlockRetriesContext;

typedef struct {
    MMModemLock lock;
    const gchar *command;
} UnlockRetriesMap;

static const UnlockRetriesMap unlock_retries_map [] = {
    { MM_MODEM_LOCK_SIM_PIN,     "^SPIC=\"SC\""   },
    { MM_MODEM_LOCK_SIM_PUK,     "^SPIC=\"SC\",1" },
    { MM_MODEM_LOCK_SIM_PIN2,    "^SPIC=\"P2\""   },
    { MM_MODEM_LOCK_SIM_PUK2,    "^SPIC=\"P2\",1" },
    { MM_MODEM_LOCK_PH_FSIM_PIN, "^SPIC=\"PS\""   },
    { MM_MODEM_LOCK_PH_FSIM_PUK, "^SPIC=\"PS\",1" },
    { MM_MODEM_LOCK_PH_NET_PIN,  "^SPIC=\"PN\""   },
    { MM_MODEM_LOCK_PH_NET_PUK,  "^SPIC=\"PN\",1" },
};

static void
load_unlock_retries_context_complete_and_free (LoadUnlockRetriesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->retries);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LoadUnlockRetriesContext, ctx);
}

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return (MMUnlockRetries *) g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void load_unlock_retries_context_step (LoadUnlockRetriesContext *ctx);

static void
spic_ready (MMBaseModem *self,
            GAsyncResult *res,
            LoadUnlockRetriesContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_dbg ("Couldn't load retry count for lock '%s': %s",
                mm_modem_lock_get_string (unlock_retries_map[ctx->i].lock),
                error->message);
        g_error_free (error);
    } else {
        guint val;

        response = mm_strip_tag (response, "^SPIC:");
        if (!mm_get_uint_from_str (response, &val))
            mm_dbg ("Couldn't parse retry count value for lock '%s'",
                    mm_modem_lock_get_string (unlock_retries_map[ctx->i].lock));
        else
            mm_unlock_retries_set (ctx->retries, unlock_retries_map[ctx->i].lock, val);
    }

    /* Go to next lock value */
    ctx->i++;
    load_unlock_retries_context_step (ctx);
}

static void
load_unlock_retries_context_step (LoadUnlockRetriesContext *ctx)
{
    if (ctx->i == G_N_ELEMENTS (unlock_retries_map)) {
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->retries),
                                                   (GDestroyNotify)g_object_unref);
        load_unlock_retries_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (ctx->self),
        unlock_retries_map[ctx->i].command,
        3,
        FALSE,
        (GAsyncReadyCallback)spic_ready,
        ctx);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    LoadUnlockRetriesContext *ctx;

    ctx = g_slice_new0 (LoadUnlockRetriesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_unlock_retries);
    ctx->retries = mm_unlock_retries_new ();
    ctx->i = 0;

    load_unlock_retries_context_step (ctx);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

#define MAX_AFTER_SIM_UNLOCK_RETRIES 15

typedef enum {
    CINTERION_SIM_STATUS_REMOVED        = 0,
    CINTERION_SIM_STATUS_INSERTED       = 1,
    CINTERION_SIM_STATUS_INIT_COMPLETED = 5,
} CinterionSimStatus;

typedef struct {
    MMBroadbandModemCinterion *self;
    GSimpleAsyncResult *result;
    guint retries;
    guint timeout_id;
} AfterSimUnlockContext;

static void
after_sim_unlock_context_complete_and_free (AfterSimUnlockContext *ctx)
{
    g_assert (ctx->timeout_id == 0);
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (AfterSimUnlockContext, ctx);
}

static gboolean
after_sim_unlock_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void after_sim_unlock_context_step (AfterSimUnlockContext *ctx);

static gboolean
simstatus_timeout_cb (AfterSimUnlockContext *ctx)
{
    ctx->timeout_id = 0;
    after_sim_unlock_context_step (ctx);
    return G_SOURCE_REMOVE;
}

static void
simstatus_check_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       AfterSimUnlockContext *ctx)
{
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    if (response) {
        gchar *descr = NULL;
        guint val = 0;

        if (mm_cinterion_parse_sind_response (response, &descr, NULL, &val, NULL) &&
            g_str_equal (descr, "simstatus") &&
            val == CINTERION_SIM_STATUS_INIT_COMPLETED) {
            /* SIM ready! */
            g_free (descr);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
            after_sim_unlock_context_complete_and_free (ctx);
            return;
        }

        g_free (descr);
    }

    /* Need to retry after 1 sec */
    g_assert (ctx->timeout_id == 0);
    ctx->timeout_id = g_timeout_add_seconds (1, (GSourceFunc)simstatus_timeout_cb, ctx);
}

static void
after_sim_unlock_context_step (AfterSimUnlockContext *ctx)
{
    if (ctx->retries == 0) {
        /* Too much wait, go on anyway */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        after_sim_unlock_context_complete_and_free (ctx);
        return;
    }

    /* Recheck */
    ctx->retries--;
    mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                              "^SIND=\"simstatus\",1",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)simstatus_check_ready,
                              ctx);
}

static void
after_sim_unlock (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    AfterSimUnlockContext *ctx;

    ctx = g_slice_new0 (AfterSimUnlockContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             after_sim_unlock);
    ctx->retries = MAX_AFTER_SIM_UNLOCK_RETRIES;

    after_sim_unlock_context_step (ctx);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_cinterion_parent_class)->setup_ports (self);

    mm_common_cinterion_setup_gps_port (self);
}

/*****************************************************************************/

MMBroadbandModemCinterion *
mm_broadband_modem_cinterion_new (const gchar *device,
                                  const gchar **drivers,
                                  const gchar *plugin,
                                  guint16 vendor_id,
                                  guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_CINTERION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
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

    /* Set defaults */
    self->priv->sind_psinfo = TRUE; /* Initially, always try to get psinfo */
}

static void
finalize (GObject *object)
{
    MMBroadbandModemCinterion *self = MM_BROADBAND_MODEM_CINTERION (object);

    g_free (self->priv->sleep_mode_cmd);
    g_free (self->priv->manual_operator_id);

    if (self->priv->cnmi_supported_mode)
        g_array_unref (self->priv->cnmi_supported_mode);
    if (self->priv->cnmi_supported_mt)
        g_array_unref (self->priv->cnmi_supported_mt);
    if (self->priv->cnmi_supported_bm)
        g_array_unref (self->priv->cnmi_supported_bm);
    if (self->priv->cnmi_supported_ds)
        g_array_unref (self->priv->cnmi_supported_ds);
    if (self->priv->cnmi_supported_bfr)
        g_array_unref (self->priv->cnmi_supported_bfr);

    G_OBJECT_CLASS (mm_broadband_modem_cinterion_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_current_bands = set_current_bands;
    iface->set_current_bands_finish = set_current_bands_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->modem_after_sim_unlock = after_sim_unlock;
    iface->modem_after_sim_unlock_finish = after_sim_unlock_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->modem_power_off = modem_power_off;
    iface->modem_power_off_finish = modem_power_off_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface->enable_unsolicited_events = enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = enable_unsolicited_events_finish;
    iface->register_in_network = register_in_network;
    iface->register_in_network_finish = register_in_network_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->check_support = messaging_check_support;
    iface->check_support_finish = messaging_check_support_finish;
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    mm_common_cinterion_peek_parent_location_interface (iface);

    iface->load_capabilities = mm_common_cinterion_location_load_capabilities;
    iface->load_capabilities_finish = mm_common_cinterion_location_load_capabilities_finish;
    iface->enable_location_gathering = mm_common_cinterion_enable_location_gathering;
    iface->enable_location_gathering_finish = mm_common_cinterion_enable_location_gathering_finish;
    iface->disable_location_gathering = mm_common_cinterion_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_common_cinterion_disable_location_gathering_finish;
}

static void
mm_broadband_modem_cinterion_class_init (MMBroadbandModemCinterionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemCinterionPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
