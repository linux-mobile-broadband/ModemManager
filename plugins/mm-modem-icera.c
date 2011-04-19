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
 * Copyright (C) 2010 Red Hat, Inc.
 */

/******************************************
 * Generic utilities for Icera-based modems
 ******************************************/

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "mm-modem-icera.h"

#include "mm-modem.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-at-serial-port.h"
#include "mm-generic-gsm.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

struct _MMModemIceraPrivate {
    /* Pending connection attempt */
    MMCallbackInfo *connect_pending_data;
    guint connect_pending_id;

    char *username;
    char *password;

    MMModemGsmAccessTech last_act;
};

#define MM_MODEM_ICERA_GET_PRIVATE(m) (MM_MODEM_ICERA_GET_INTERFACE (m)->get_private(m))

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (!g_str_has_prefix (response->str, "%IPSYS: ")) {
        int a, b;

        if (sscanf (response->str + 8, "%d,%d", &a, &b)) {
            MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;

            switch (a) {
            case 0:
                mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
                break;
            case 1:
                mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
                break;
            case 2:
                mode = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
                break;
            case 3:
                mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
                break;
            default:
                break;
            }

            mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
            parsed = TRUE;
        }
    }

    if (!error && !parsed)
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse allowed mode results");

    mm_callback_info_schedule (info);
}

void
mm_modem_icera_get_allowed_mode (MMModemIcera *self,
                                 MMModemUIntFn callback,
                                 gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }
    mm_at_serial_port_queue_command (port, "%IPSYS?", 3, get_allowed_mode_done, info);
}

static void
set_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

   mm_callback_info_schedule (info);
}

void
mm_modem_icera_set_allowed_mode (MMModemIcera *self,
                                 MMModemGsmAllowedMode mode,
                                 MMModemFn callback,
                                 gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    int i;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        i = 0;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        i = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        i = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        i = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        i = 5;
        break;
    }

    command = g_strdup_printf ("%%IPSYS=%d", i);
    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static MMModemGsmAccessTech
nwstate_to_act (const char *str)
{
    /* small 'g' means CS, big 'G' means PS */
    if (!strcmp (str, "2g"))
        return MM_MODEM_GSM_ACCESS_TECH_GSM;
    else if (!strcmp (str, "2G-GPRS"))
        return MM_MODEM_GSM_ACCESS_TECH_GPRS;
    else if (!strcmp (str, "2G-EDGE"))
        return MM_MODEM_GSM_ACCESS_TECH_EDGE;
    else if (!strcmp (str, "3G"))
        return MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (!strcmp (str, "3g"))
        return MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (!strcmp (str, "3G-HSDPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSDPA;
    else if (!strcmp (str, "3G-HSUPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSUPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSPA;

    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static void
nwstate_changed (MMAtSerialPort *port,
                 GMatchInfo *info,
                 gpointer user_data)
{
    MMModemIcera *self = MM_MODEM_ICERA (user_data);
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;
    int rssi = -1;

    str = g_match_info_fetch (info, 1);
    if (str) {
        rssi = atoi (str);
        rssi = CLAMP (rssi, -1, 5);
        g_free (str);
    }

    str = g_match_info_fetch (info, 3);
    if (str) {
        act = nwstate_to_act (str);
        g_free (str);
    }

    MM_MODEM_ICERA_GET_PRIVATE (self)->last_act = act;
    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (self), act);
}

static void
pacsp_received (MMAtSerialPort *port,
                GMatchInfo *info,
                gpointer user_data)
{
    return;
}

static void
get_nwstate_done (MMAtSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMModemIcera *self = MM_MODEM_ICERA (info->modem);
        MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);

        /* The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we have to work around that.
         */
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->last_act), NULL);
        priv->last_act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    }

    mm_callback_info_schedule (info);
}

void
mm_modem_icera_get_access_technology (MMModemIcera *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "%NWSTATE=1", 3, get_nwstate_done, info);
}

/****************************************************************/

static void
disconnect_ipdpact_done (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

void
mm_modem_icera_do_disconnect (MMGenericGsm *gsm,
                              gint cid,
                              MMModemFn callback,
                              gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;
    char *command;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("%%IPDPACT=%d,0", cid);
    mm_at_serial_port_queue_command (primary, command, 3, disconnect_ipdpact_done, info);
    g_free (command);
}

/*****************************************************************************/

static void
connect_pending_done (MMModemIcera *self)
{
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);
    GError *error = NULL;

    if (priv->connect_pending_data) {
        if (priv->connect_pending_data->error) {
            error = priv->connect_pending_data->error;
            priv->connect_pending_data->error = NULL;
        }

        /* Complete the connect */
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (self), error, priv->connect_pending_data);
        priv->connect_pending_data = NULL;
    }

    if (priv->connect_pending_id) {
        g_source_remove (priv->connect_pending_id);
        priv->connect_pending_id = 0;
    }
}

static void
icera_disconnect_done (MMModem *modem,
                       GError *error,
                       gpointer user_data)
{
    mm_info ("Modem signaled disconnection from the network");
}

static void
connection_enabled (MMAtSerialPort *port,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemIcera *self = MM_MODEM_ICERA (user_data);
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);
    MMCallbackInfo *info = priv->connect_pending_data;
    char *str;
    int status, cid, tmp;

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (self));
    if (cid < 0)
        return;

    str = g_match_info_fetch (match_info, 1);
    g_return_if_fail (str != NULL);
    tmp = atoi (str);
    g_free (str);

    /* Make sure the unsolicited message's CID matches the current CID */
    if (tmp != cid)
        return;

    str = g_match_info_fetch (match_info, 2);
    g_return_if_fail (str != NULL);
    status = atoi (str);
    g_free (str);

    switch (status) {
    case 0:
        /* Disconnected */
        if (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_CONNECTED)
            mm_modem_disconnect (MM_MODEM (self), icera_disconnect_done, NULL);
        break;
    case 1:
        /* Connected */
        connect_pending_done (self);
        break;
    case 2:
        /* Connecting */
        break;
    case 3:
        /* Call setup failure? */
        if (info) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_GENERAL,
                                               "Call setup failed");
        }
        connect_pending_done (self);
        break;
    default:
        mm_warn ("Unknown Icera connect status %d", status);
        break;
    }
}

/****************************************************************/

static gint
_get_cid (MMModemIcera *self)
{
    gint cid;

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (self));
    if (cid < 0) {
        g_warn_if_fail (cid >= 0);
        cid = 0;
    }
    return cid;
}

static void
icera_call_control (MMModemIcera *self,
                    gboolean activate,
                    MMAtSerialResponseFn callback,
                    gpointer user_data)
{
    char *command;
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("%%IPDPACT=%d,%d", _get_cid (self), activate ? 1 : 0);
    mm_at_serial_port_queue_command (primary, command, 3, callback, user_data);
    g_free (command);
}

static void
timeout_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    connect_pending_done (MM_MODEM_ICERA (user_data));
}

static gboolean
icera_connect_timed_out (gpointer data)
{
    MMModemIcera *self = MM_MODEM_ICERA (data);
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);
    MMCallbackInfo *info = priv->connect_pending_data;

    priv->connect_pending_id = 0;

    if (info) {
        info->error = g_error_new_literal (MM_SERIAL_ERROR,
                                           MM_SERIAL_ERROR_RESPONSE_TIMEOUT,
                                           "Connection timed out");
    }

    icera_call_control (self, FALSE, timeout_done, self);
    return FALSE;
}

static void
icera_connected (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
    } else {
        MMModemIcera *self = MM_MODEM_ICERA (info->modem);
        MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);

        g_warn_if_fail (priv->connect_pending_id == 0);
        if (priv->connect_pending_id)
            g_source_remove (priv->connect_pending_id);

        priv->connect_pending_data = info;
        priv->connect_pending_id = g_timeout_add_seconds (30, icera_connect_timed_out, self);
    }
}

static void
old_context_clear_done (MMAtSerialPort *port,
                        GString *response,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Activate the PDP context and start the data session */
    icera_call_control (MM_MODEM_ICERA (info->modem), TRUE, icera_connected, info);
}

static void
auth_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
    else {
        /* Ensure the PDP context is deactivated */
        icera_call_control (MM_MODEM_ICERA (info->modem), FALSE, old_context_clear_done, info);
    }
}

void
mm_modem_icera_do_connect (MMModemIcera *self,
                           const char *number,
                           MMModemFn callback,
                           gpointer user_data)
{
    MMModem *modem = MM_MODEM (self);
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMAtSerialPort *primary;
    gint cid;
    char *command;

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    cid = _get_cid (self);


    /* Both user and password are required; otherwise firmware returns an error */
    if (!priv->username || !priv->password)
		command = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", cid);
    else {
        command = g_strdup_printf ("%%IPDPCFG=%d,0,1,\"%s\",\"%s\"",
                                   cid,
                                   priv->username ? priv->username : "",
                                   priv->password ? priv->password : "");

    }

    mm_at_serial_port_queue_command (primary, command, 3, auth_done, info);
    g_free (command);
}

/****************************************************************/


static void
free_dns_array (gpointer data)
{
    g_array_free ((GArray *) data, TRUE);
}

static void
ip4_config_invoke (MMCallbackInfo *info)
{
    MMModemIp4Fn callback = (MMModemIp4Fn) info->callback;

    callback (info->modem,
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "ip4-address")),
              (GArray *) mm_callback_info_get_data (info, "ip4-dns"),
              info->error, info->user_data);
}

#define IPDPADDR_TAG "%IPDPADDR: "

static void
get_ip4_config_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
	char **items, **iter;
    GArray *dns_array;
    int i;
    guint32 tmp;
    gint cid;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    } else if (!g_str_has_prefix (response->str, IPDPADDR_TAG)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Retrieving failed: invalid response.");
        goto out;
    }

    cid = _get_cid (MM_MODEM_ICERA (info->modem));
    dns_array = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 2);

    /* %IPDPADDR: <cid>,<ip>,<gw>,<dns1>,<dns2>[,<nbns1>,<nbns2>] */
    items = g_strsplit (response->str + strlen (IPDPADDR_TAG), ", ", 0);

    for (iter = items, i = 0; *iter; iter++, i++) {
        if (i == 0) { /* CID */
            long int num;

            errno = 0;
            num = strtol (*iter, NULL, 10);
            if (errno != 0 || num < 0 || (gint) num != cid) {
                info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Unknown CID in IPDPADDR response ("
                                           "got %d, expected %d)", (guint) num, cid);
                break;
            }
        } else if (i == 1) { /* IP address */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                mm_callback_info_set_data (info, "ip4-address", GUINT_TO_POINTER (tmp), NULL);
        } else if (i == 3) { /* DNS 1 */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                g_array_append_val (dns_array, tmp);
        } else if (i == 4) { /* DNS 2 */
            if (inet_pton (AF_INET, *iter, &tmp) > 0)
                g_array_append_val (dns_array, tmp);
        }
    }

    g_strfreev (items);
    mm_callback_info_set_data (info, "ip4-dns", dns_array, free_dns_array);

 out:
    mm_callback_info_schedule (info);
}

void
mm_modem_icera_get_ip4_config (MMModemIcera *self,
                               MMModemIp4Fn callback,
                               gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *primary;

    info = mm_callback_info_new_full (MM_MODEM (self),
                                      ip4_config_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("%%IPDPADDR=%d", _get_cid (self));
    mm_at_serial_port_queue_command (primary, command, 3, get_ip4_config_done, info);
    g_free (command);
}

/****************************************************************/

static const char *
get_string_property (GHashTable *properties, const char *name)
{
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (value && G_VALUE_HOLDS_STRING (value))
        return g_value_get_string (value);
    return NULL;
}

void
mm_modem_icera_simple_connect (MMModemIcera *self, GHashTable *properties)
{
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);

    g_free (priv->username);
    priv->username = g_strdup (get_string_property (properties, "username"));
    g_free (priv->password);
    priv->password = g_strdup (get_string_property (properties, "password"));
}

/****************************************************************/

void
mm_modem_icera_register_unsolicted_handlers (MMModemIcera *self,
                                             MMAtSerialPort *port)
{
    GRegex *regex;

    /* %NWSTATE: <rssi>,<mccmnc>,<tech>,<connected>,<regulation> */
    regex = g_regex_new ("\\r\\n%NWSTATE:\\s*(-?\\d+),(\\d+),([^,]*),([^,]*),(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, nwstate_changed, self, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, pacsp_received, self, NULL);
    g_regex_unref (regex);

    /* %IPDPACT: <cid>,<status>,0 */
    regex = g_regex_new ("\\r\\n%IPDPACT:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, connection_enabled, self, NULL);
    g_regex_unref (regex);
}

void
mm_modem_icera_change_unsolicited_messages (MMModemIcera *self, gboolean enabled)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, enabled ? "%NWSTATE=1" : "%NWSTATE=0", 3, NULL, NULL);
}

/****************************************************************/

static void
is_icera_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error)
        mm_callback_info_set_result (info, GUINT_TO_POINTER (TRUE), NULL);
    mm_callback_info_schedule (info);
}

void
mm_modem_icera_is_icera (MMModemIcera *self,
                         MMModemUIntFn callback,
                         gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "%IPSYS?", 5, is_icera_done, info);
}

void
mm_modem_icera_cleanup (MMModemIcera *self)
{
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);

    /* Clear the pending connection if necessary */
    connect_pending_done (self);

    g_free (priv->username);
    priv->username = NULL;
    g_free (priv->password);
    priv->password = NULL;
}

/****************************************************************/

MMModemIceraPrivate *
mm_modem_icera_init_private (void)
{
    return g_malloc0 (sizeof (MMModemIceraPrivate));
}

void
mm_modem_icera_dispose_private (MMModemIcera *self)
{
    MMModemIceraPrivate *priv = MM_MODEM_ICERA_GET_PRIVATE (self);

    mm_modem_icera_cleanup (self);
    memset (priv, 0, sizeof (*priv));
    g_free (priv);
}

static void
mm_modem_icera_init (gpointer g_iface)
{
}

GType
mm_modem_icera_get_type (void)
{
    static GType icera_type = 0;

    if (!G_UNLIKELY (icera_type)) {
        const GTypeInfo icera_info = {
            sizeof (MMModemIcera), /* class_size */
            mm_modem_icera_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        icera_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModemIcera",
                                             &icera_info, 0);

        g_type_interface_add_prerequisite (icera_type, MM_TYPE_MODEM);
    }

    return icera_type;
}

