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

    guint32 auth_idx;
} MMModemHsoPrivate;

#define OWANDATA_TAG "_OWANDATA: "

MMModem *
mm_modem_hso_new (const char *device,
                  const char *driver,
                  const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HSO,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_STATIC,
                                   NULL));
}

#define IGNORE_ERRORS_TAG "ignore-errors"

static void
hso_call_control_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error && !mm_callback_info_get_data (info, IGNORE_ERRORS_TAG))
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static guint32
hso_get_cid (MMModemHso *self)
{
    guint32 cid;

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (self));
    if (cid == 0)
        cid = 1;

    return cid;
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
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_callback_info_set_data (info, IGNORE_ERRORS_TAG, GUINT_TO_POINTER (ignore_errors), NULL);

    command = g_strdup_printf ("AT_OWANCALL=%d,%d,1", hso_get_cid (self), activate ? 1 : 0);
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, command, 3, hso_call_control_done, info);
    g_free (command);
}

static void
connect_pending_done (MMModemHso *self)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);

    if (priv->connect_pending_data) {
        mm_callback_info_schedule (priv->connect_pending_data);
        priv->connect_pending_data = NULL;
    }

    if (priv->connect_pending_id) {
        g_source_remove (priv->connect_pending_id);
        priv->connect_pending_id = 0;
    }
}

static gboolean
hso_connect_timed_out (gpointer data)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (data);

    priv->connect_pending_data->error = g_error_new_literal (MM_SERIAL_ERROR,
                                                             MM_SERIAL_RESPONSE_TIMEOUT,
                                                             "Connection timed out");
    connect_pending_done (MM_MODEM_HSO (data));

    return FALSE;
}

static void
hso_enabled (MMModem *modem,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (modem);
        GSource *source;

        source = g_timeout_source_new_seconds (30);
        g_source_set_closure (source, g_cclosure_new_object (G_CALLBACK (hso_connect_timed_out), G_OBJECT (modem)));
        g_source_attach (source, NULL);
        priv->connect_pending_data = info;
        priv->connect_pending_id = g_source_get_id (source);
        g_source_unref (source);
    }
}

static void
clear_old_context (MMModem *modem,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Success, activate the PDP context and start the data session */
        hso_call_control (MM_MODEM_HSO (modem), TRUE, FALSE, hso_enabled, info);
    }
}

static void
auth_done (MMSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemHso *self = MM_MODEM_HSO (info->modem);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);

    if (error) {
        priv->auth_idx++;
        if (auth_commands[priv->auth_idx]) {
            /* Try the next auth command */
            _internal_hso_modem_authenticate (self, info);
        } else {
            /* Reset to 0 so that something gets tried for the next connection */
            priv->auth_idx = 0;

            info->error = g_error_copy (error);
            mm_callback_info_schedule (info);
        }
    } else {
        priv->auth_idx = 0;

        /* success, kill any existing connections first */
        hso_call_control (self, FALSE, TRUE, clear_old_context, info);
    }
}

static void
_internal_hso_modem_authenticate (MMModemHso *self, MMCallbackInfo *info)
{
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    MMSerialPort *primary;
    guint32 cid;
    char *command;
    const char *username, *password;

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    cid = hso_get_cid (self);

    username = mm_callback_info_get_data (info, "username");
    password = mm_callback_info_get_data (info, "password");

    if (!username && !password)
		command = g_strdup_printf ("%s=%d,0", auth_commands[priv->auth_idx], cid);
    else {
        command = g_strdup_printf ("%s=%d,1,\"%s\",\"%s\"",
                                   auth_commands[priv->auth_idx],
                                   cid,
                                   password ? password : "",
                                   username ? username : "");

    }

    mm_serial_port_queue_command (primary, command, 3, auth_done, info);
    g_free (command);
}

void
mm_hso_modem_authenticate (MMModemHso *self,
                           const char *username,
                           const char *password,
                           MMModemFn callback,
                           gpointer user_data)
{
    MMCallbackInfo *info;

    g_return_if_fail (MM_IS_MODEM_HSO (self));
    g_return_if_fail (callback != NULL);

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    if (username)
        mm_callback_info_set_data (info, "username", g_strdup (username), g_free);
    if (password)
        mm_callback_info_set_data (info, "password", g_strdup (password), g_free);

    _internal_hso_modem_authenticate (self, info);
}

/*****************************************************************************/

static void
enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (modem), error, info);
}

static void
parent_enable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *self = MM_GENERIC_GSM (modem);

    if (error) {
        mm_generic_gsm_enable_complete (self, error, info);
        return;
    }

    /* HSO needs manual PIN checking */
    mm_generic_gsm_check_pin (self, enable_done, info);
}

static void
enable (MMModem *modem, MMModemFn callback, gpointer user_data)
{
    MMModem *parent_modem_iface;
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
    parent_modem_iface->enable (info->modem, parent_enable_done, info);
}

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
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;

    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

    /* Kill any existing connection */
    hso_call_control (MM_MODEM_HSO (modem), FALSE, TRUE, disable_done, info);
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);
    mm_callback_info_schedule (info);
}


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
get_ip4_config_done (MMSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
	char **items, **iter;
    GArray *dns_array;
    int i;
    guint32 tmp;
    guint cid;

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
            if (errno != 0 || num < 0 || (guint) num != cid) {
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
    MMSerialPort *primary;

    info = mm_callback_info_new_full (modem, ip4_config_invoke, G_CALLBACK (callback), user_data);
    command = g_strdup_printf ("AT_OWANDATA=%d", hso_get_cid (MM_MODEM_HSO (modem)));
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, command, 3, get_ip4_config_done, info);
    g_free (command);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (modem, callback, user_data);
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, "AT_OWANCALL=1,0,0", 3, NULL, info);
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

static void
connection_enabled (MMSerialPort *port,
                    GMatchInfo *info,
                    gpointer user_data)
{
    MMModemHso *self = MM_MODEM_HSO (user_data);
    MMModemHsoPrivate *priv = MM_MODEM_HSO_GET_PRIVATE (self);
    char *str;

    str = g_match_info_fetch (info, 2);
    if (str[0] == '1')
        connect_pending_done (self);
    else if (str[0] == '3') {
        MMCallbackInfo *cb_info = priv->connect_pending_data;

        if (cb_info)
            cb_info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                                  "Call setup failed");

        connect_pending_done (self);
    } else if (str[0] == '0')
        /* FIXME: disconnected. do something when we have modem status signals */
        ;

    g_free (str);
}

/*****************************************************************************/
/* MMModemSimple interface */

typedef enum {
    SIMPLE_STATE_BEGIN = 0,
    SIMPLE_STATE_PARENT_CONNECT,
    SIMPLE_STATE_AUTHENTICATE,
    SIMPLE_STATE_DONE
} SimpleState;

static const char *
simple_get_string_property (MMCallbackInfo *info, const char *name, GError **error)
{
    GHashTable *properties = (GHashTable *) mm_callback_info_get_data (info, "simple-connect-properties");
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
simple_state_machine (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;
    const char *username;
    const char *password;
    GHashTable *properties = (GHashTable *) mm_callback_info_get_data (info, "simple-connect-properties");
    SimpleState state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "simple-connect-state"));

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    switch (state) {
    case SIMPLE_STATE_BEGIN:
        state = SIMPLE_STATE_PARENT_CONNECT;
        parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (modem));
        parent_iface->connect (MM_MODEM_SIMPLE (modem), properties, simple_state_machine, info);
        break;
    case SIMPLE_STATE_PARENT_CONNECT:
        state = SIMPLE_STATE_AUTHENTICATE;
        username = simple_get_string_property (info, "username", &info->error);
        password = simple_get_string_property (info, "password", &info->error);
        mm_hso_modem_authenticate (MM_MODEM_HSO (modem), username, password, simple_state_machine, info);
        break;
    case SIMPLE_STATE_AUTHENTICATE:
        state = SIMPLE_STATE_DONE;
        break;
    default:
        break;
    }

 out:
    if (info->error || state == SIMPLE_STATE_DONE)
        mm_callback_info_schedule (info);
    else
        mm_callback_info_set_data (info, "simple-connect-state", GUINT_TO_POINTER (state), NULL);
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (simple), callback, user_data);
    mm_callback_info_set_data (info, "simple-connect-properties", 
                               g_hash_table_ref (properties),
                               (GDestroyNotify) g_hash_table_unref);

    simple_state_machine (MM_MODEM (simple), NULL, info);
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

    if (MM_IS_SERIAL_PORT (port)) {
        g_object_set (G_OBJECT (port), MM_SERIAL_PORT_SEND_DELAY, (guint64) 10000, NULL);
        if (ptype == MM_PORT_TYPE_PRIMARY) {
            GRegex *regex;

            mm_generic_gsm_set_unsolicited_registration (gsm, TRUE);

            regex = g_regex_new ("_OWANCALL: (\\d),\\s*(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, connection_enabled, modem, NULL);
            g_regex_unref (regex);
        }
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
    modem_class->enable = enable;
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;
    modem_class->disconnect = disconnect;
    modem_class->grab_port = grab_port;
}

static void
finalize (GObject *object)
{
    /* Clear the pending connection if necessary */
    connect_pending_done (MM_MODEM_HSO (object));

    G_OBJECT_CLASS (mm_modem_hso_parent_class)->finalize (object);
}

static void
mm_modem_hso_class_init (MMModemHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_modem_hso_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemHsoPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}

