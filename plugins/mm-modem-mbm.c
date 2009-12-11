/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 Ericsson AB
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mm-modem-mbm.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_simple_init (MMModemSimple *class);

G_DEFINE_TYPE_EXTENDED (MMModemMbm, mm_modem_mbm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init))

#define MM_MODEM_MBM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_MBM, MMModemMbmPrivate))

#define MBM_E2NAP_DISCONNECTED 0
#define MBM_E2NAP_CONNECTED    1
#define MBM_E2NAP_CONNECTING   2

#define MBM_SIGNAL_INDICATOR 2

#define MBM_NETWORK_MODE_ANY  1
#define MBM_NETWORK_MODE_2G   5
#define MBM_NETWORK_MODE_3G   6

#define MBM_ERINFO_2G_GPRS  1
#define MBM_ERINFO_2G_EGPRS 2
#define MBM_ERINFO_3G_UMTS  1
#define MBM_ERINFO_3G_HSDPA 2

typedef struct {
    guint reg_id;
    gboolean have_emrdy;
    char  *network_device;
    MMCallbackInfo *pending_connect_info;
    int account_index;
    int network_mode;
    const char *username;
    const char *password;
} MMModemMbmPrivate;

enum {
    PROP_0,
    PROP_NETWORK_DEVICE,

    LAST_PROP
};

static void
mbm_modem_authenticate (MMModemMbm *self,
                        const char *username,
                        const char *password,
                        gpointer user_data);

static const char *
mbm_simple_get_string_property (GHashTable *properties, const char *name, GError **error);

static uint
mbm_simple_get_uint_property (GHashTable *properties, const char *name, GError **error);

MMModem *
mm_modem_mbm_new (const char *device,
                  const char *driver,
                  const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_MBM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP,
                                   NULL));
}

/*****************************************************************************/
/*    Gsm network class override functions                                   */
/*****************************************************************************/

typedef struct {
    MMModemGsmNetwork *modem;
    const char *network_id;
    MMModemFn callback;
    gpointer user_data;
} RegisterData;

static gboolean
register_done (gpointer user_data)
{
    RegisterData *reg_data = user_data;
    MMModemMbm *self = MM_MODEM_MBM (reg_data->modem);
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);
    MMModemGsmNetwork *parent_modem_iface;
    MMSerialPort *primary;

    priv->reg_id = 0;

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_serial_port_queue_command (primary, "+CREG=1", 3, NULL, NULL);
    mm_serial_port_queue_command (primary, "+CMER=3,0,0,1", 3, NULL, NULL);

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self));
    parent_modem_iface->do_register (MM_MODEM_GSM_NETWORK (self),
                                     reg_data->network_id,
                                     reg_data->callback,
                                     reg_data->user_data);
    return FALSE;
}

static void
do_register (MMModemGsmNetwork *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMModemMbm *self = MM_MODEM_MBM (modem);
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);
    RegisterData *reg_data;

    reg_data = g_malloc0 (sizeof(RegisterData));
    reg_data->modem = modem;
    reg_data->network_id = network_id;
    reg_data->callback = callback;
    reg_data->user_data = user_data;

    /* wait so sim pin is done */
    if (priv->reg_id)
        g_source_remove (priv->reg_id);
    priv->reg_id = g_timeout_add_full (G_PRIORITY_DEFAULT, 500,
                                       register_done, reg_data,
                                       g_free);
}

static int
mbm_parse_network_mode (MMModemGsmMode network_mode)
{
    switch (network_mode) {
    case MM_MODEM_GSM_MODE_ANY:
    case MM_MODEM_GSM_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_MODE_2G_PREFERRED:
        return MBM_NETWORK_MODE_ANY;
    case MM_MODEM_GSM_MODE_GPRS:
    case MM_MODEM_GSM_MODE_EDGE:
    case MM_MODEM_GSM_MODE_2G_ONLY:
        return MBM_NETWORK_MODE_2G;
    case MM_MODEM_GSM_MODE_3G_ONLY:
    case MM_MODEM_GSM_MODE_HSDPA:
    case MM_MODEM_GSM_MODE_HSUPA:
    case MM_MODEM_GSM_MODE_HSPA:
        return MBM_NETWORK_MODE_3G;
    default:
        return MBM_NETWORK_MODE_ANY;
    }
}

static void
mbm_set_network_mode_done (MMSerialPort *port,
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
set_network_mode (MMModemGsmNetwork *modem,
                  MMModemGsmMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    command = g_strdup_printf ("+CFUN=%d", mbm_parse_network_mode (mode));
    mm_serial_port_queue_command (primary, command, 3, mbm_set_network_mode_done, info);
    g_free (command);
}

static void
get_network_mode_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *erinfo;
    int mode = 0, gsm = 0, umts = 0;
    gboolean parsed = FALSE;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    erinfo = strstr (response->str, "*ERINFO:");
    if (!erinfo)
        goto done;

    if (sscanf (erinfo + 8, "%d,%d,%d", &mode, &gsm, &umts) != 3)
        goto done;

    if (gsm || umts) {
        MMModemGsmMode mm_mode = MM_MODEM_GSM_MODE_ANY;

        if (gsm == MBM_ERINFO_2G_GPRS)
            mm_mode = MM_MODEM_GSM_MODE_GPRS;
        else if (gsm == MBM_ERINFO_2G_EGPRS)
            mm_mode = MM_MODEM_GSM_MODE_EDGE;
        else if (umts == MBM_ERINFO_3G_UMTS)
            mm_mode = MM_MODEM_GSM_MODE_UMTS;
        else if (umts == MBM_ERINFO_3G_HSDPA)
            mm_mode = MM_MODEM_GSM_MODE_HSDPA;
        else
            g_debug ("%s unknown network mode %d,%d", __FUNCTION__, gsm, umts);

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mm_mode), NULL);
        parsed = TRUE;
    }

done:
    if (!error && !parsed) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse network mode results");
    }

    mm_callback_info_schedule (info);
}

static void
get_network_mode (MMModemGsmNetwork *modem,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, "*ERINFO?", 3, get_network_mode_done, info);
}

/*****************************************************************************/
/*    Simple Modem class override functions                                  */
/*****************************************************************************/

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (simple);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;
    uint network_mode = 0;

    priv->username = mbm_simple_get_string_property (properties, "username", &info->error);
    priv->password = mbm_simple_get_string_property (properties, "password", &info->error);

    network_mode = mbm_simple_get_uint_property (properties, "network_mode", &info->error);
    priv->network_mode = mbm_parse_network_mode (network_mode);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
mbm_enable_done (MMSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
mbm_enap0_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);
    char *command;

    if (!priv->network_mode)
        priv->network_mode = MBM_NETWORK_MODE_ANY;

    command = g_strdup_printf ("+CFUN=%d", priv->network_mode);
    mm_serial_port_queue_command (port, command, 3, mbm_enable_done, info);
    g_free (command);
}

static void
mbm_init_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);

    if (error) {
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    if (!priv->network_mode)
        priv->network_mode = MBM_NETWORK_MODE_ANY;

    mm_serial_port_queue_command (port, "*ENAP=0", 3, mbm_enap0_done, info);
}

static void
do_init (MMSerialPort *port, MMCallbackInfo *info)
{
    mm_serial_port_queue_command (port, "&F E0 V1 X4 &C1 +CMEE=1", 3, mbm_init_done, info);
}

static void
mbm_emrdy_done (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);

    if (   error
        && error->domain == MM_SERIAL_ERROR
        && error->code == MM_SERIAL_RESPONSE_TIMEOUT) {
        g_warning ("%s: timed out waiting for EMRDY response.", __func__);
    } else
        priv->have_emrdy = TRUE;

    do_init (port, info);
}

static void
do_enable (MMGenericGsm *self, MMModemFn callback, gpointer user_data)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    primary = mm_generic_gsm_get_port (self, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    if (priv->have_emrdy) {
        /* Modem is ready, no need to check EMRDY */
        do_init (primary, info);
    } else
        mm_serial_port_queue_command (primary, "*EMRDY?", 5, mbm_emrdy_done, info);
}

typedef struct {
    MMModem *modem;
    MMModemFn callback;
    gpointer user_data;
} DisableInfo;

static void
disable_creg_cmer_done (MMSerialPort *port,
                        GString *response,
                        GError *error,
                        gpointer user_data)

{
    MMModem *parent_modem_iface;
    DisableInfo *info = user_data;

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
    parent_modem_iface->disable (info->modem, info->callback, info->user_data);
    g_free (info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMSerialPort *primary;
    DisableInfo *info;

    info = g_malloc0 (sizeof (DisableInfo));
    info->callback = callback;
    info->user_data = user_data;
    info->modem = modem;

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Turn off unsolicited +CIEV signal strength indicator */
    mm_serial_port_queue_command (primary, "+CREG=0;+CMER=0", 5, disable_creg_cmer_done, info);
}

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (modem);

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);
    priv->pending_connect_info = info;

    mbm_modem_authenticate (MM_MODEM_MBM (modem), priv->username, priv->password, info);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    mm_modem_set_state (modem, MM_MODEM_STATE_DISCONNECTING, MM_MODEM_STATE_REASON_NONE);

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, "*ENAP=0", 3, NULL, NULL);

    mm_generic_gsm_update_enabled_state (MM_GENERIC_GSM (modem), FALSE, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);
    mm_callback_info_schedule (info);
}

/*****************************************************************************/

static void
mbm_emrdy_received (MMSerialPort *port,
                    GMatchInfo *info,
                    gpointer user_data)
{
    MM_MODEM_MBM_GET_PRIVATE (user_data)->have_emrdy = TRUE;
}

static void
mbm_pacsp_received (MMSerialPort *port,
                     GMatchInfo *info,
                     gpointer user_data)
{
    return;
}

static void
mbm_ciev_received (MMSerialPort *port,
                   GMatchInfo *info,
                   gpointer user_data)
{
    int quality = 0, ind = 0;
    const char *str;

    str = g_match_info_fetch (info, 1);
    if (str)
        ind = atoi (str);

    if (ind == MBM_SIGNAL_INDICATOR) {
        str = g_match_info_fetch (info, 2);
        if (str) {
            quality = atoi (str);
            mm_modem_gsm_network_signal_quality (MM_MODEM_GSM_NETWORK (user_data), quality * 20);
        }
    }
}

static void
mbm_do_connect_done (MMModemMbm *self)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);

    if (priv->pending_connect_info) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (self), NULL, priv->pending_connect_info);
        priv->pending_connect_info = NULL;
    }
}

static void
mbm_e2nap_received (MMSerialPort *port,
                    GMatchInfo *info,
                    gpointer user_data)
{
    int state = 0;
    const char *str;

    str = g_match_info_fetch (info, 1);
    if (str)
        state = atoi (str);

    if (MBM_E2NAP_DISCONNECTED == state)
        g_debug ("%s, disconnected", __func__);
    else if (MBM_E2NAP_CONNECTED == state) {
        g_debug ("%s, connected", __func__);
        mbm_do_connect_done (MM_MODEM_MBM (user_data));
    } else if (MBM_E2NAP_CONNECTING == state)
        g_debug("%s, connecting", __func__);
    else {
        /* Should not happen */
        g_debug("%s, undefined e2nap status",__FUNCTION__);
        g_assert_not_reached ();
    }
}

static void
enap_poll_response (MMSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint state;
    guint count;

    g_assert (info);

    count = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mbm-enap-poll-count"));

    if (sscanf (response->str, "*ENAP: %d", &state) == 1 && state == 1) {
        /* Success!  Connected... */
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), NULL, info);
        return;
    }

    mm_callback_info_set_data (info, "mbm-enap-poll-count", GUINT_TO_POINTER (++count), NULL);

    /* lets give it about 50 seconds */
    if (count > 50) {
        GError *poll_error;

        poll_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_BUSY);
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), poll_error, info);
        g_error_free (poll_error);
    }
}

static gboolean
enap_poll (gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMSerialPort *port = mm_generic_gsm_get_port (MM_GENERIC_GSM (info->modem), MM_PORT_TYPE_PRIMARY);

    g_assert (port);

    mm_serial_port_queue_command (port, "AT*ENAP?", 3, enap_poll_response, user_data);
    /* we cancel this in the _done function if all is fine */
    return TRUE;
}

static void
enap_done (MMSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint tid;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    tid = g_timeout_add_seconds (1, enap_poll, user_data);
    /* remember poll id as callback info object, with source_remove as free func */
    mm_callback_info_set_data (info, "mbm-enap-poll-id", GUINT_TO_POINTER (tid), (GFreeFunc) g_source_remove);
    mm_serial_port_queue_command (port, "AT*E2NAP=1", 3, NULL, NULL);
}

static void
mbm_auth_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *modem = MM_GENERIC_GSM (info->modem);
    char *command;
    guint32 cid;

    if (error) {
        mm_generic_gsm_connect_complete (modem, error, info);
        return;
    }

    cid = mm_generic_gsm_get_cid (modem);
    command = g_strdup_printf ("AT*ENAP=1,%d", cid);
    mm_serial_port_queue_command (port, command, 3, enap_done, user_data);
    g_free (command);
}

static void
mbm_modem_authenticate (MMModemMbm *self,
                        const char *username,
                        const char *password,
                        gpointer user_data)
{
    MMSerialPort *primary;

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    if (username || password) {
        char *command;

        command = g_strdup_printf ("*EIAAUW=%d,1,\"%s\",\"%s\"",
                                   mm_generic_gsm_get_cid (MM_GENERIC_GSM (self)),
                                   username ? username : "",
                                   password ? password : "");

        mm_serial_port_queue_command (primary, command, 3, mbm_auth_done, user_data);
        g_free (command);
    } else
        mbm_auth_done (primary, NULL, NULL, user_data);
}

static const char *
mbm_simple_get_string_property (GHashTable *properties, const char *name, GError **error)
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

static uint
mbm_simple_get_uint_property (GHashTable *properties, const char *name, GError **error)
{
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return 0;

    if (G_VALUE_HOLDS_UINT (value))
        return g_value_get_uint (value);

    g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                 "Invalid property type for '%s': %s (uint expected)",
                 name, G_VALUE_TYPE_NAME (value));

    return 0;
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
    MMPort *port = NULL;

    if (!strcmp (subsys, "tty")) {
        if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
            if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_PRIMARY))
                    ptype = MM_PORT_TYPE_PRIMARY;
            else if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_SECONDARY))
                ptype = MM_PORT_TYPE_SECONDARY;
        } else
            ptype = suggested_type;
    }

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_SERIAL_PORT (port) && (ptype == MM_PORT_TYPE_PRIMARY)) {
        GRegex *regex;

        mm_generic_gsm_set_unsolicited_registration (MM_GENERIC_GSM (modem), TRUE);

        regex = g_regex_new ("\\r\\n\\*EMRDY: \\d\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, mbm_emrdy_received, modem, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\*E2NAP: (\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, mbm_e2nap_received, modem, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, mbm_pacsp_received, modem, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+CIEV: (\\d),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, mbm_ciev_received, modem, NULL);
        g_regex_unref (regex);

        /* also consume unsolicited mbm messages we are not interested in them - see LP: #416418 */
        regex = g_regex_new ("\\R\\*ESTKSMENU:.*\\R", G_REGEX_RAW | G_REGEX_OPTIMIZE | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF, G_REGEX_MATCH_NEWLINE_CRLF, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, modem, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\*EMWI: (\\d),(\\d).*\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);
    }

    return TRUE;
}

/*****************************************************************************/

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->do_register = do_register;
    class->get_network_mode = get_network_mode;
    class->set_network_mode = set_network_mode;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->disconnect = disconnect;
}

static void
mm_modem_mbm_init (MMModemMbm *self)
{
}

static void
finalize (GObject *object)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (object);

    if (priv->reg_id)
        g_source_remove (priv->reg_id);

    g_free (priv->network_device);

    G_OBJECT_CLASS (mm_modem_mbm_parent_class)->finalize (object);
}

static void
mm_modem_mbm_class_init (MMModemMbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_mbm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemMbmPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    gsm_class->do_enable = do_enable;
}

