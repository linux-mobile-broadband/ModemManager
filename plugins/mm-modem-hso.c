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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <dbus/dbus-glib.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-hso.h"
#include "mm-modem-simple.h"
#include "mm-serial-parsers.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void impl_hso_authenticate (MMModemHso *self,
                                   const char *username,
                                   const char *password,
                                   DBusGMethodInvocation *context);

#include "mm-modem-gsm-hso-glue.h"

static void modem_init (MMModem *modem_class);
static void modem_simple_init (MMModemSimple *simple_class);

G_DEFINE_TYPE_EXTENDED (MMModemHso, mm_modem_hso, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init))

#define MM_MODEM_HSO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_HSO, MMModemHsoPrivate))

static void _internal_hso_modem_authenticate (MMModemHso *self, MMCallbackInfo *info);

const char *auth_commands[] = {
	"$QCPDPP",
	/* Icera-based devices (GI0322/Quicksilver, iCON 505) don't implement
	 * $QCPDPP, but instead use _OPDPP with the same arguments.
	 */
	"_OPDPP",
	NULL
};

typedef struct {
    /* Pending connection attempt */
    MMCallbackInfo *connect_pending_data;
    guint connect_pending_id;

    char *username;
    char *password;

    guint32 auth_idx;
} MMModemHsoPrivate;

#define OWANDATA_TAG "_OWANDATA: "

MMModem *
mm_modem_hso_new (const char *device,
                  const char *driver,
                  const char *plugin,
                  guint32 vendor,
                  guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HSO,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_STATIC,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

#include "mm-modem-option-utils.c"

/*****************************************************************************/

static gint
hso_get_cid (MMModemHso *self)
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
auth_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHso *self;
    MMModemHsoPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_HSO (info->modem);
    priv = MM_MODEM_HSO_GET_PRIVATE (self);

    if (error) {
        priv->auth_idx++;
        if (auth_commands[priv->auth_idx]) {
            /* Try the next auth command */
            _internal_hso_modem_authenticate (self, info);
            return;
        } else
            info->error = g_error_copy (error);
    }

    /* Reset to 0 so something gets tried the next connection */
    priv->auth_idx = 0;
    mm_callback_info_schedule (info);
}

static void
_internal_hso_modem_authenticate (MMModemHso *self, MMCallbackInfo *info)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    MMAtSerialPort *primary;
    gint cid;
    char *command;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    cid = hso_get_cid (self);
    g_warn_if_fail (cid >= 0);

    /* Both user and password are required; otherwise firmware returns an error */
    if (!priv->username || !priv->password)
		command = g_strdup_printf ("%s=%d,0", auth_commands[priv->auth_idx], cid);
    else {
        command = g_strdup_printf ("%s=%d,1,\"%s\",\"%s\"",
                                   auth_commands[priv->auth_idx],
                                   cid,
                                   priv->password ? priv->password : "",
                                   priv->username ? priv->username : "");

    }

    mm_at_serial_port_queue_command (primary, command, 3, auth_done, info);
    g_free (command);
}

void
mm_hso_modem_authenticate (MMModemHso *self,
                           const char *username,
                           const char *password,
                           MMModemFn callback,
                           gpointer user_data)
{
    MMModemHsoPrivate *priv;
    MMCallbackInfo *info;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM_HSO (self));
    g_return_if_fail (callback != NULL);

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    priv = MM_MODEM_HSO_GET_PRIVATE (self);

    g_free (priv->username);
    priv->username = (username && strlen (username)) ? g_strdup (username) : NULL;

    g_free (priv->password);
    priv->password = (password && strlen (password)) ? g_strdup (password) : NULL;

    _internal_hso_modem_authenticate (self, info);
}

/*****************************************************************************/

static void
connect_pending_done (MMModemHso *self)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
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
connection_enabled (MMAtSerialPort *port,
                    GMatchInfo *match_info,
                    gpointer user_data)
{
    MMModemHso *self = MM_MODEM_HSO (user_data);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    char *str;

    str = g_match_info_fetch (match_info, 2);
    if (str[0] == '1')
        connect_pending_done (self);
    else if (str[0] == '3') {
        MMCallbackInfo *info = priv->connect_pending_data;

        if (info) {
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                               "Call setup failed");
        }

        connect_pending_done (self);
    } else if (str[0] == '0') {
        /* FIXME: disconnected. do something when we have modem status signals */
    }

    g_free (str);
}

/*****************************************************************************/

#define IGNORE_ERRORS_TAG "ignore-errors"

static void
hso_call_control_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error && !mm_callback_info_get_data (info, IGNORE_ERRORS_TAG))
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
hso_call_control (MMModemHso *self,
                  gboolean activate,
                  gboolean ignore_errors,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_callback_info_set_data (info, IGNORE_ERRORS_TAG, GUINT_TO_POINTER (ignore_errors), NULL);

    command = g_strdup_printf ("AT_OWANCALL=%d,%d,1", hso_get_cid (self), activate ? 1 : 0);
    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, command, 3, hso_call_control_done, info);
    g_free (command);
}

static void
timeout_done (MMModem *modem,
              GError *error,
              gpointer user_data)
{
    if (modem)
        connect_pending_done (MM_MODEM_HSO (modem));
}

static gboolean
hso_connect_timed_out (gpointer data)
{
    MMModemHso *self = MM_MODEM_HSO (data);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    MMCallbackInfo *info = priv->connect_pending_data;

    priv->connect_pending_id = 0;

    if (info) {
        info->error = g_error_new_literal (MM_SERIAL_ERROR,
                                           MM_SERIAL_ERROR_RESPONSE_TIMEOUT,
                                           "Connection timed out");
    }

    hso_call_control (self, FALSE, TRUE, timeout_done, self);
    return FALSE;
}

static void
hso_enabled (MMModem *modem,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (modem), error, info);
    } else {
        MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (modem);

        priv->connect_pending_data = info;
        priv->connect_pending_id = g_timeout_add_seconds (30, hso_connect_timed_out, modem);
    }
}

static void
old_context_clear_done (MMModem *modem,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (modem), error, info);
    else {
        /* Success, activate the PDP context and start the data session */
        hso_call_control (MM_MODEM_HSO (modem), TRUE, FALSE, hso_enabled, info);
    }
}

static void
connect_auth_done (MMModem *modem,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (modem), error, info);
    } else {
        /* Now connect; kill any existing connections first */
        hso_call_control (MM_MODEM_HSO (modem), FALSE, TRUE, old_context_clear_done, info);
    }
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    MMModemHso *self = MM_MODEM_HSO (modem);
    MMCallbackInfo *auth_info, *connect_info;

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    connect_info = mm_callback_info_new (modem, callback, user_data);
    auth_info = mm_callback_info_new (modem, connect_auth_done, connect_info);
    _internal_hso_modem_authenticate (self, auth_info);
}

/*****************************************************************************/

static void
parent_disable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    mm_callback_info_schedule (info);
}

static void
disable_done (MMModem *modem,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModem *parent_modem_iface;

    /* Do the normal disable stuff */
    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
    parent_modem_iface->disable (info->modem, parent_disable_done, info);
}

static void
unsolicited_disable_done (MMModem *modem,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* Handle modem removal, but ignore other errors */
    if (g_error_matches (error, MM_MODEM_ERROR, MM_MODEM_ERROR_REMOVED))
        info->error = g_error_copy (error);
    else if (!modem) {
        info->error =  g_error_new_literal (MM_MODEM_ERROR,
                                            MM_MODEM_ERROR_REMOVED,
                                            "The modem was removed.");
    }

    if (info->error) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Otherwise, kill any existing connection */
    if (hso_get_cid (MM_MODEM_HSO (modem)) >= 0)
        hso_call_control (MM_MODEM_HSO (modem), FALSE, TRUE, disable_done, info);
    else
        disable_done (modem, NULL, info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMModemHso *self = MM_MODEM_HSO (modem);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    MMCallbackInfo *info;

    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    g_free (priv->username);
    priv->username = NULL;
    g_free (priv->password);
    priv->password = NULL;

    info = mm_callback_info_new (modem, callback, user_data);

    /* Turn off unsolicited messages so they don't pile up in the modem */
    option_change_unsolicited_messages (MM_GENERIC_GSM (modem), FALSE, unsolicited_disable_done, info);
}

/*****************************************************************************/

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    } else if (!g_str_has_prefix (response->str, OWANDATA_TAG)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Retrieving failed: invalid response.");
        goto out;
    }

    cid = hso_get_cid (MM_MODEM_HSO (info->modem));
    dns_array = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 2);
    items = g_strsplit (response->str + strlen (OWANDATA_TAG), ", ", 0);

    for (iter = items, i = 0; *iter; iter++, i++) {
        if (i == 0) { /* CID */
            long int num;

            errno = 0;
            num = strtol (*iter, NULL, 10);
            if (errno != 0 || num < 0 || (gint) num != cid) {
                info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Unknown CID in OWANDATA response ("
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

static void
get_ip4_config (MMModem *modem,
                MMModemIp4Fn callback,
                gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *primary;

    info = mm_callback_info_new_full (modem, ip4_config_invoke, G_CALLBACK (callback), user_data);
    command = g_strdup_printf ("AT_OWANDATA=%d", hso_get_cid (MM_MODEM_HSO (modem)));
    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, command, 3, get_ip4_config_done, info);
    g_free (command);
}

/*****************************************************************************/

static void
disconnect_owancall_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    mm_callback_info_schedule (info);
}

static void
do_disconnect (MMGenericGsm *gsm,
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

    command = g_strdup_printf ("AT_OWANCALL=%d,0,0", cid);
    mm_at_serial_port_queue_command (primary, command, 3, disconnect_owancall_done, info);
    g_free (command);
}

/*****************************************************************************/

static void
real_do_enable_power_up_done (MMGenericGsm *gsm,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    /* Enable Option unsolicited messages */
    if (gsm && !error)
        option_change_unsolicited_messages (gsm, TRUE, NULL, NULL);

    /* Chain up to parent */
    MM_GENERIC_GSM_CLASS (mm_modem_hso_parent_class)->do_enable_power_up_done (gsm, response, error, info);
}

/*****************************************************************************/

static void
impl_hso_auth_done (MMModem *modem,
                    GError *error,
                    gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context);
}

static void
impl_hso_authenticate (MMModemHso *self,
                       const char *username,
                       const char *password,
                       DBusGMethodInvocation *context)
{
    /* DBus doesn't support NULLs */
    if (username && strlen (username) == 0)
        username = NULL;
    if (password && strlen (password) == 0)
        password = NULL;

    mm_hso_modem_authenticate (self, username, password, impl_hso_auth_done, context);
}

/*****************************************************************************/

static const char *
hso_simple_get_string_property (GHashTable *properties, const char *name, GError **error)
{
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return NULL;

    if (G_VALUE_HOLDS_STRING (value))
        return g_value_get_string (value);

    g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                 "Invalid property type for '%s': %s (string expected)",
                 name, G_VALUE_TYPE_NAME (value));

    return NULL;
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (simple);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;

    priv->username = g_strdup (hso_simple_get_string_property (properties, "username", NULL));
    priv->password = g_strdup (hso_simple_get_string_property (properties, "password", NULL));

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

/*****************************************************************************/

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    option_get_allowed_mode (gsm, callback, user_data);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    option_set_allowed_mode (gsm, mode, callback, user_data);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    option_get_access_technology (gsm, callback, user_data);
}

/*****************************************************************************/

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    const char *sys[] = { "tty", "net", NULL };
    GUdevClient *client;
    GUdevDevice *device = NULL;
    MMPort *port = NULL;
    const char *sysfs_path;

    client = g_udev_client_new (sys);
    if (!client) {
        g_set_error (error, 0, 0, "Could not get udev client.");
        return FALSE;
    }

    device = g_udev_client_query_by_subsystem_and_name (client, subsys, name);
    if (!device) {
        g_set_error (error, 0, 0, "Could not get udev device.");
        goto out;
    }

    sysfs_path = g_udev_device_get_sysfs_path (device);
    if (!sysfs_path) {
        g_set_error (error, 0, 0, "Could not get udev device sysfs path.");
        goto out;
    }

    if (!strcmp (subsys, "tty")) {
        char *hsotype_path;
        char *contents = NULL;

        hsotype_path = g_build_filename (sysfs_path, "hsotype", NULL);
        if (g_file_get_contents (hsotype_path, &contents, NULL, NULL)) {
            if (g_str_has_prefix (contents, "Control"))
                ptype = MM_PORT_TYPE_PRIMARY;
            else if (g_str_has_prefix (contents, "Application") || g_str_has_prefix (contents, "Application2"))
                ptype = MM_PORT_TYPE_SECONDARY;
            g_free (contents);
        }
        g_free (hsotype_path);
    }

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (!port)
        goto out;

    if (MM_IS_AT_SERIAL_PORT (port)) {
        g_object_set (G_OBJECT (port), MM_SERIAL_PORT_SEND_DELAY, (guint64) 0, NULL);
        if (ptype == MM_PORT_TYPE_PRIMARY) {
            GRegex *regex;

            regex = g_regex_new ("_OWANCALL: (\\d),\\s*(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, connection_enabled, modem, NULL);
            g_regex_unref (regex);

            regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
            g_regex_unref (regex);
        }
        option_register_unsolicted_handlers (gsm, MM_AT_SERIAL_PORT (port));
    }

out:
    if (device)
        g_object_unref (device);
    g_object_unref (client);
    return !!port;
}

/*****************************************************************************/

static void
mm_modem_hso_init (MMModemHso *self)
{
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;
    modem_class->grab_port = grab_port;
}

static void
finalize (GObject *object)
{
    MMModemHso *self = MM_MODEM_HSO (object);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);

    /* Clear the pending connection if necessary */
    connect_pending_done (self);

    g_free (priv->username);
    g_free (priv->password);

    G_OBJECT_CLASS (mm_modem_hso_parent_class)->finalize (object);
}

static void
mm_modem_hso_class_init (MMModemHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_hso_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHsoPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
    gsm_class->do_disconnect = do_disconnect;
    gsm_class->do_enable_power_up_done = real_do_enable_power_up_done;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

