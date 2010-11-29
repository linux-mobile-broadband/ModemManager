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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <config.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-sierra-gsm.h"
#include "mm-errors.h"
#include "mm-modem-simple.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);
static void modem_simple_init (MMModemSimple *class);

G_DEFINE_TYPE_EXTENDED (MMModemSierraGsm, mm_modem_sierra_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init))

#define MM_MODEM_SIERRA_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_SIERRA_GSM, MMModemSierraGsmPrivate))

typedef struct {
    guint enable_wait_id;
    gboolean has_net;
    char *username;
    char *password;
} MMModemSierraGsmPrivate;

MMModem *
mm_modem_sierra_gsm_new (const char *device,
                         const char *driver,
                         const char *plugin,
                         guint32 vendor,
                         guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIERRA_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/*****************************************************************************/

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GRegex *r = NULL;
    GMatchInfo *match_info;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    /* Example response: !SELRAT: 03, UMTS 3G Preferred */
    r = g_regex_new ("!SELRAT:\\s*(\\d+).*$", 0, 0, NULL);
    if (!r) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the allowed mode response");
        goto done;
    }

    if (g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, &info->error)) {
        MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        char *str;

        str = g_match_info_fetch (match_info, 1);
        switch (atoi (str)) {
        case 0:
            mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
            break;
        case 1:
            mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
            break;
        case 2:
            mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
            break;
        case 3:
            mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
            break;
        case 4:
            mode = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
            break;
        default:
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Failed to parse the allowed mode response: '%s'",
                                       response->str);
            break;
        }
        g_free (str);

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
    }

done:
    if (r)
        g_regex_unref (r);
    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_set_error_literal (&info->error, MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                             "Cannot perform this operation while connected");
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (primary, "!SELRAT?", 3, get_allowed_mode_done, info);
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

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;
    char *command;
    int idx = 0;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_set_error_literal (&info->error, MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                             "Cannot perform this operation while connected");
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        idx = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        idx = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        idx = 4;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        idx = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        break;
    }

    command = g_strdup_printf ("!SELRAT=%d", idx);
    mm_at_serial_port_queue_command (primary, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static void
get_act_request_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;

    if (error)
        info->error = g_error_copy (error);
    else {
        p = mm_strip_tag (response->str, "*CNTI:");
        p = strchr (p, ',');
        if (p)
            act = mm_gsm_string_to_access_tech (p + 1);
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *modem,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (modem, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "*CNTI=0", 3, get_act_request_done, info);
}

static void
get_sim_iccid_done (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    const char *p;
    char buf[21];
    int i;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    p = mm_strip_tag (response->str, "!ICCID:");
    if (!p) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse !ICCID response");
        goto done;
    }

    memset (buf, 0, sizeof (buf));
    for (i = 0; i < 20; i++) {
        if (!isdigit (p[i]) && (p[i] != 'F') && (p[i] == 'f')) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "CRSM ICCID response contained invalid character '%c'",
                                       p[i]);
            goto done;
        }
        if (p[i] == 'F' || p[i] == 'f') {
            buf[i] = 0;
            break;
        }
        buf[i] = p[i];
    }

    if (i == 19 || i == 20)
        mm_callback_info_set_result (info, g_strdup (buf), g_free);
    else {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Invalid +CRSM ICCID response size (was %d, expected 19 or 20)",
                                   i);
    }

done:
    mm_serial_port_close (MM_SERIAL_PORT (port));
    mm_callback_info_schedule (info);
}

static void
get_sim_iccid (MMGenericGsm *modem,
               MMModemStringFn callback,
               gpointer callback_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;
    GError *error = NULL;

    port = mm_generic_gsm_get_best_at_port (modem, &error);
    if (!port)
        goto error;

    if (!mm_serial_port_open (MM_SERIAL_PORT (port), &error))
        goto error;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, callback_data);
    mm_at_serial_port_queue_command (port, "!ICCID?", 3, get_sim_iccid_done, info);
    return;

error:
    callback (MM_MODEM (modem), NULL, error, callback_data);
    g_clear_error (&error);
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static gboolean
sierra_enabled (gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsm *modem;
    MMModemSierraGsmPrivate *priv;

    /* Make sure we don't use an invalid modem that may have been removed */
    if (info->modem) {
        modem = MM_GENERIC_GSM (info->modem);
        priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (modem);
        priv->enable_wait_id = 0;
        MM_GENERIC_GSM_CLASS (mm_modem_sierra_gsm_parent_class)->do_enable_power_up_done (modem, NULL, NULL, info);
    }
    return FALSE;
}

static void
real_do_enable_power_up_done (MMGenericGsm *gsm,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (gsm);

    if (error) {
        /* Chain up to parent */
        MM_GENERIC_GSM_CLASS (mm_modem_sierra_gsm_parent_class)->do_enable_power_up_done (gsm, NULL, error, info);
        return;
    }

    /* Some Sierra devices return OK on +CFUN=1 right away but need some time
     * to finish initialization.
     */
    g_warn_if_fail (priv->enable_wait_id == 0);
    priv->enable_wait_id = g_timeout_add_seconds (10, sierra_enabled, info);
}

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
    MMPort *port;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);

    if (port) {
        if (MM_IS_AT_SERIAL_PORT (port)) {
            GRegex *regex;

            g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);

            regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
            g_regex_unref (regex);
        } else if (mm_port_get_subsys (port) == MM_PORT_SUBSYS_NET) {
            MM_MODEM_SIERRA_GSM_GET_PRIVATE (gsm)->has_net = TRUE;
            g_object_set (G_OBJECT (gsm), MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP, NULL);
        }
    }

    return !!port;
}

static void
ppp_connect_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (modem, error);
    mm_callback_info_schedule (info);
}

static void
net_activate_done (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
}


static void
auth_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gint cid;
    char *command;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (info->modem));
    g_warn_if_fail (cid >= 0);

    /* Activate data on the net port */
    command = g_strdup_printf ("!SCACT=1,%d", cid);
    mm_at_serial_port_queue_command (port, command, 3, net_activate_done, info);
    g_free (command);
}

static void
ps_attach_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemSierraGsmPrivate *priv;
    MMModem *parent_modem_iface;
    const char *number;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (info->modem);
    if (priv->has_net) {
        gint cid;
        char *command;

        /* If we have a net interface, we don't do PPP */

        cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (info->modem));
        g_warn_if_fail (cid >= 0);

        if (!priv->username || !priv->password)
            command = g_strdup_printf ("$QCPDPP=%d,0", cid);
        else {
            command = g_strdup_printf ("$QCPDPP=%d,1,\"%s\",\"%s\"",
                                       cid,
                                       priv->password ? priv->password : "",
                                       priv->username ? priv->username : "");

        }

        mm_at_serial_port_queue_command (port, command, 3, auth_done, info);
        g_free (command);
    } else {
        /* We've got a PS attach, chain up to parent for the connect */
        number = mm_callback_info_get_data (info, "number");
        parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
        parent_modem_iface->connect (info->modem, number, ppp_connect_done, info);
    }
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);
    mm_callback_info_set_data (info, "number", g_strdup (number), g_free);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Try to initiate a PS attach.  Some Sierra modems can get into a
     * state where if there is no PS attach when dialing, the next time
     * the modem tries to connect it won't ever register with the network.
     */
    mm_at_serial_port_queue_command (port, "+CGATT=1", 10, ps_attach_done, info);
}

static void
clear_user_pass (MMModemSierraGsm *self)
{
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (self);

    g_free (priv->username);
    priv->username = NULL;
    g_free (priv->password);
    priv->username = NULL;
}

static void
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    clear_user_pass (MM_MODEM_SIERRA_GSM (gsm));

    if (MM_MODEM_SIERRA_GSM_GET_PRIVATE (gsm)->has_net) {
        MMAtSerialPort *primary;
        char *command;

        primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
        g_assert (primary);

        /* If we have a net interface, deactivate it */
        command = g_strdup_printf ("!SCACT=0,%d", cid);
        mm_at_serial_port_queue_command (primary, command, 3, NULL, NULL);
        g_free (command);
    }

    MM_GENERIC_GSM_CLASS (mm_modem_sierra_gsm_parent_class)->do_disconnect (gsm, cid, callback, user_data);
}

/*****************************************************************************/
/*    Simple Modem class override functions                                  */
/*****************************************************************************/

static char *
simple_dup_string_property (GHashTable *properties, const char *name, GError **error)
{
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return NULL;

    if (G_VALUE_HOLDS_STRING (value))
        return g_value_dup_string (value);

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
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (simple);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;

    clear_user_pass (MM_MODEM_SIERRA_GSM (simple));
    priv->username = simple_dup_string_property (properties, "username", &info->error);
    priv->password = simple_dup_string_property (properties, "password", &info->error);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
    modem_class->connect = do_connect;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
mm_modem_sierra_gsm_init (MMModemSierraGsm *self)
{
}

static void
dispose (GObject *object)
{
    MMModemSierraGsmPrivate *priv = MM_MODEM_SIERRA_GSM_GET_PRIVATE (object);

    if (priv->enable_wait_id)
        g_source_remove (priv->enable_wait_id);

    clear_user_pass (MM_MODEM_SIERRA_GSM (object));
}

static void
mm_modem_sierra_gsm_class_init (MMModemSierraGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_sierra_gsm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemSierraGsmPrivate));

    object_class->dispose = dispose;
    gsm_class->do_enable_power_up_done = real_do_enable_power_up_done;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
    gsm_class->get_sim_iccid = get_sim_iccid;
    gsm_class->do_disconnect = do_disconnect;
}

