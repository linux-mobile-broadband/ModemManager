/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 - 2010 Ericsson AB
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *         Dan Williams <dcbw@redhat.com>
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mm-modem-mbm.h"
#include "mm-modem-simple.h"
#include "mm-modem-gsm-card.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-log.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_simple_init (MMModemSimple *class);
static void modem_gsm_card_init (MMModemGsmCard *class);

G_DEFINE_TYPE_EXTENDED (MMModemMbm, mm_modem_mbm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init))

#define MM_MODEM_MBM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_MBM, MMModemMbmPrivate))

#define MBM_E2NAP_DISCONNECTED 0
#define MBM_E2NAP_CONNECTED    1
#define MBM_E2NAP_CONNECTING   2

#define MBM_NETWORK_MODE_ANY  1
#define MBM_NETWORK_MODE_2G   5
#define MBM_NETWORK_MODE_3G   6

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

MMModem *
mm_modem_mbm_new (const char *device,
                  const char *driver,
                  const char *plugin,
                  guint32 vendor,
                  guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_MBM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_DHCP,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
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

    priv->reg_id = 0;

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
mbm_parse_allowed_mode (MMModemGsmAllowedMode network_mode)
{
    switch (network_mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        return MBM_NETWORK_MODE_ANY;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        return MBM_NETWORK_MODE_2G;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        return MBM_NETWORK_MODE_3G;
    default:
        return MBM_NETWORK_MODE_ANY;
    }
}

static void
mbm_set_allowed_mode_done (MMAtSerialPort *port,
                           GString *response,
                           GError *error,
                           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    command = g_strdup_printf ("+CFUN=%d", mbm_parse_allowed_mode (mode));
    mm_at_serial_port_queue_command (port, command, 3, mbm_set_allowed_mode_done, info);
    g_free (command);
}

static void
mbm_erinfo_received (MMAtSerialPort *port,
                     GMatchInfo *info,
                     gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (info, 2);
    if (str) {
        switch (atoi (str)) {
        case 1:
            act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
            break;
        case 2:
            act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
            break;
        default:
            break;
        }
    }
    g_free (str);

    /* 3G modes take precedence */
    str = g_match_info_fetch (info, 3);
    if (str) {
        switch (atoi (str)) {
        case 1:
            act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
            break;
        case 2:
            act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
            break;
        default:
            break;
        }
    }
    g_free (str);

    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
}

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else if (!g_str_has_prefix (response->str, "CFUN: ")) {
        MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        int a;

        a = atoi (response->str + 6);
        if (a == MBM_NETWORK_MODE_2G)
            mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (a == MBM_NETWORK_MODE_3G)
            mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
        parsed = TRUE;
    }

    if (!error && !parsed)
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse allowed mode results");

    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+CFUN?", 3, get_allowed_mode_done, info);
}

/*****************************************************************************/
/*    Simple Modem class override functions                                  */
/*****************************************************************************/

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

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (simple);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;

    priv->username = mbm_simple_get_string_property (properties, "username", &info->error);
    priv->password = mbm_simple_get_string_property (properties, "password", &info->error);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
mbm_enable_done (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Start unsolicited signal strength and access technology responses */
    mm_at_serial_port_queue_command (port, "*ERINFO=1", 3, NULL, NULL);

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
mbm_enap0_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv;
    char *command;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);

    if (!priv->network_mode)
        priv->network_mode = MBM_NETWORK_MODE_ANY;

    command = g_strdup_printf ("+CFUN=%d", priv->network_mode);
    mm_at_serial_port_queue_command (port, command, 3, mbm_enable_done, info);
    g_free (command);
}

static void
mbm_init_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);

    if (error) {
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    if (!priv->network_mode)
        priv->network_mode = MBM_NETWORK_MODE_ANY;

    mm_at_serial_port_queue_command (port, "*ENAP=0", 3, mbm_enap0_done, info);
}

static void
do_init (MMAtSerialPort *port, MMCallbackInfo *info)
{
    mm_at_serial_port_queue_command (port, "&F E0 V1 X4 &C1 +CMEE=1", 3, mbm_init_done, info);
}

static void
mbm_emrdy_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemMbmPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* EMRDY unsolicited response might have happened between the command
     * submission and the response.  This was seen once:
     *
     * (ttyACM0): --> 'AT*EMRDY?<CR>'
     * (ttyACM0): <-- 'T*EMRD<CR><LF>*EMRDY: 1<CR><LF>Y?'
     *
     * So suppress the warning if the unsolicited handler handled the response
     * before we get here.
     */
    priv = MM_MODEM_MBM_GET_PRIVATE (info->modem);
    if (!priv->have_emrdy) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            mm_warn ("timed out waiting for EMRDY response.");
        else
            priv->have_emrdy = TRUE;
    }

    do_init (port, info);
}

static void
do_enable (MMGenericGsm *self, MMModemFn callback, gpointer user_data)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    primary = mm_generic_gsm_get_at_port (self, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    if (priv->have_emrdy) {
        /* Modem is ready, no need to check EMRDY */
        do_init (primary, info);
    } else
        mm_at_serial_port_queue_command (primary, "*EMRDY?", 5, mbm_emrdy_done, info);
}

/*****************************************************************************/

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore all errors */
    mm_callback_info_schedule (info);
}

static void
invoke_call_parent_disable_fn (MMCallbackInfo *info)
{
    /* Note: we won't call the parent disable if info->modem is no longer
     * valid. The invoke is called always once the info gets scheduled, which
     * may happen during removed modem detection. */
    if (info->modem) {
        MMModem *parent_modem_iface;

        parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
        parent_modem_iface->disable (info->modem, (MMModemFn)info->callback, info->user_data);
    }
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMAtSerialPort *primary;
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (modem,
                                      invoke_call_parent_disable_fn,
                                      (GCallback)callback,
                                      user_data);

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Turn off unsolicited responses */
    mm_at_serial_port_queue_command (primary, "*ERINFO=0", 5, disable_unsolicited_done, info);
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
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, "*ENAP=0", 3, NULL, NULL);

    MM_GENERIC_GSM_CLASS (mm_modem_mbm_parent_class)->do_disconnect (gsm, cid, callback, user_data);
}

static void
reset (MMModem *modem,
       MMModemFn callback,
       gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    /* Ensure we have a usable port to use for the command */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (port)
        mm_at_serial_port_queue_command (port, "*E2RESET", 3, NULL, NULL);

    mm_callback_info_schedule (info);
}

static void
factory_reset_done (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    mm_serial_port_close (MM_SERIAL_PORT (port));
    mm_callback_info_schedule (info);
}

static void
factory_reset (MMModem *self,
               const char *code,
               MMModemFn callback,
               gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_new (self, callback, user_data);

    /* Ensure we have a usable port to use for the command */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Modem may not be enabled yet, which sometimes can't be done until
     * the device has been unlocked.  In this case we have to open the port
     * ourselves.
     */
    if (!mm_serial_port_open (MM_SERIAL_PORT (port), &info->error)) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "&F +CMEE=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+COPS=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CR=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CRC=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CREG=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CMER=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "*EPEE=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CNMI=2, 0, 0, 0, 0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CGREG=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "*EIAD=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CGSMS=3", 3, NULL, NULL);
    mm_at_serial_port_queue_command (port, "+CSCA=\"\",129", 3, factory_reset_done, info);
}

/*****************************************************************************/

static void
mbm_emrdy_received (MMAtSerialPort *port,
                    GMatchInfo *info,
                    gpointer user_data)
{
    MM_MODEM_MBM_GET_PRIVATE (user_data)->have_emrdy = TRUE;
}

static void
mbm_pacsp_received (MMAtSerialPort *port,
                     GMatchInfo *info,
                     gpointer user_data)
{
    return;
}

static void
mbm_do_connect_done (MMModemMbm *self, gboolean success)
{
    MMModemMbmPrivate *priv = MM_MODEM_MBM_GET_PRIVATE (self);

    if (!priv->pending_connect_info)
        return;

    if (success)
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (self), NULL, priv->pending_connect_info);
    else {
        GError *connect_error;

        connect_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_BUSY);
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (self), connect_error, priv->pending_connect_info);
        g_error_free (connect_error);
    }
    priv->pending_connect_info = NULL;
}

static void
mbm_e2nap_received (MMAtSerialPort *port,
                    GMatchInfo *info,
                    gpointer user_data)
{
    int state = 0;
    char *str;

    str = g_match_info_fetch (info, 1);
    if (str)
        state = atoi (str);
    g_free (str);

    if (MBM_E2NAP_DISCONNECTED == state) {
        mm_dbg ("disconnected");
        mbm_do_connect_done (MM_MODEM_MBM (user_data), FALSE);
    } else if (MBM_E2NAP_CONNECTED == state) {
        mm_dbg ("connected");
        mbm_do_connect_done (MM_MODEM_MBM (user_data), TRUE);
    } else if (MBM_E2NAP_CONNECTING == state)
        mm_dbg ("connecting");
    else {
        /* Should not happen */
        mm_dbg ("unhandled E2NAP state %d", state);
        mbm_do_connect_done (MM_MODEM_MBM (user_data), FALSE);
    }
}

static void
enap_poll_response (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint state;
    guint count;

    g_assert (info);

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
    MMAtSerialPort *port;

    port = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (info->modem), MM_PORT_TYPE_PRIMARY);
    g_assert (port);

    mm_at_serial_port_queue_command (port, "AT*ENAP?", 3, enap_poll_response, user_data);
    /* we cancel this in the _done function if all is fine */
    return TRUE;
}

static void
enap_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint tid;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    tid = g_timeout_add_seconds (1, enap_poll, user_data);
    /* remember poll id as callback info object, with source_remove as free func */
    mm_callback_info_set_data (info, "mbm-enap-poll-id", GUINT_TO_POINTER (tid), (GFreeFunc) g_source_remove);
}

static void
mbm_auth_done (MMSerialPort *port,
               GByteArray *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *modem = MM_GENERIC_GSM (info->modem);
    char *command;

    if (error) {
        mm_generic_gsm_connect_complete (modem, error, info);
        return;
    }

    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "AT*E2NAP=1", 3, NULL, NULL);

    command = g_strdup_printf ("AT*ENAP=1,%d", mm_generic_gsm_get_cid (modem));
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), command, 3, enap_done, user_data);
    g_free (command);
}

static void
mbm_modem_authenticate (MMModemMbm *self,
                        const char *username,
                        const char *password,
                        gpointer user_data)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    if (username || password) {
        GByteArray *command;
        MMModemCharset cur_set;
        char *tmp;

        /* F3507g at least wants the username and password to be sent in the
         * modem's current character set.
         */
        cur_set = mm_generic_gsm_get_charset (MM_GENERIC_GSM (self));

        command = g_byte_array_sized_new (75);
        tmp = g_strdup_printf ("AT*EIAAUW=%d,1,", mm_generic_gsm_get_cid (MM_GENERIC_GSM (self)));
        g_byte_array_append (command, (const guint8 *) tmp, strlen (tmp));
        g_free (tmp);

        if (username)
            mm_modem_charset_byte_array_append (command, username, TRUE, cur_set);
        else
            g_byte_array_append (command, (const guint8 *) "\"\"", 2);

        g_byte_array_append (command, (const guint8 *) ",", 1);

        if (password)
            mm_modem_charset_byte_array_append (command, password, TRUE, cur_set);
        else
            g_byte_array_append (command, (const guint8 *) "\"\"", 2);

        g_byte_array_append (command, (const guint8 *) "\r", 1);

        mm_serial_port_queue_command (MM_SERIAL_PORT (primary),
                                      command,
                                      TRUE,
                                      3,
                                      mbm_auth_done,
                                      user_data);
    } else
        mbm_auth_done (MM_SERIAL_PORT (primary), NULL, NULL, user_data);
}

/*****************************************************************************/

static void
send_epin_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *pin_type;
    int attempts_left = 0;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    pin_type = mm_callback_info_get_data (info, "pin_type");

    if (strstr (pin_type, MM_MODEM_GSM_CARD_SIM_PIN))
        sscanf (response->str, "*EPIN: %d", &attempts_left);
    else if (strstr (pin_type, MM_MODEM_GSM_CARD_SIM_PUK))
        sscanf (response->str, "*EPIN: %*d, %d", &attempts_left);
    else if (strstr (pin_type, MM_MODEM_GSM_CARD_SIM_PIN2))
        sscanf (response->str, "*EPIN: %*d, %*d, %d", &attempts_left);
    else if (strstr (pin_type, MM_MODEM_GSM_CARD_SIM_PUK2))
        sscanf (response->str, "*EPIN: %*d, %*d, %*d, %d", &attempts_left);
    else {
        mm_dbg ("unhandled pin type '%s'", pin_type);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Unhandled PIN type");
    }

    if (attempts_left < 0 || attempts_left > 998) {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid PIN attempts left %d", attempts_left);
        attempts_left = 0;
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (attempts_left), NULL);

done:
    mm_serial_port_close (MM_SERIAL_PORT (port));
    mm_callback_info_schedule (info);
}

static void
mbm_get_unlock_retries (MMModemGsmCard *modem,
                        const char *pin_type,
                        MMModemUIntFn callback,
                        gpointer user_data)
{
    MMAtSerialPort *port;
    char *command;
    MMCallbackInfo *info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    mm_dbg ("pin type '%s'", pin_type);

    /* Ensure we have a usable port to use for the command */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Modem may not be enabled yet, which sometimes can't be done until
     * the device has been unlocked.  In this case we have to open the port
     * ourselves.
     */
    if (!mm_serial_port_open (MM_SERIAL_PORT (port), &info->error)) {
        mm_callback_info_schedule (info);
        return;
    }

    /* if the modem have not yet been enabled we need to make sure echoing is turned off */
    command = g_strdup_printf ("E0");
    mm_at_serial_port_queue_command (port, command, 3, NULL, NULL);
    g_free (command);

    mm_callback_info_set_data (info, "pin_type", g_strdup (pin_type), g_free);

    command = g_strdup_printf ("*EPIN?");
    mm_at_serial_port_queue_command (port, command, 3, send_epin_done, info);
    g_free (command);
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
            if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                    ptype = MM_PORT_TYPE_PRIMARY;
            else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
                ptype = MM_PORT_TYPE_SECONDARY;
        } else
            ptype = suggested_type;
    }

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        GRegex *regex;

        /* The Ericsson modems always have a free AT command port, so we
         * don't need to flash the ports when disconnecting to get back to
         * command mode.  F5521gw R2A07 resets port properties like echo when
         * flashed, leading to confusion.  bgo #650740
         */
        g_object_set (G_OBJECT (port), MM_SERIAL_PORT_FLASH_OK, FALSE, NULL);

        if (ptype == MM_PORT_TYPE_PRIMARY) {
            regex = g_regex_new ("\\r\\n\\*E2NAP: (\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, mbm_e2nap_received, modem, NULL);
            g_regex_unref (regex);

            /* Catch the extended error status bit of the command too */
            regex = g_regex_new ("\\r\\n\\*E2NAP: (\\d),.*\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, mbm_e2nap_received, modem, NULL);
            g_regex_unref (regex);
        }

        regex = g_regex_new ("\\r\\n\\*EMRDY: \\d\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, mbm_emrdy_received, modem, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, mbm_pacsp_received, modem, NULL);
        g_regex_unref (regex);

        /* also consume unsolicited mbm messages we are not interested in them - see LP: #416418 */
        regex = g_regex_new ("\\R\\*ESTKSMENU:.*\\R", G_REGEX_RAW | G_REGEX_OPTIMIZE | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF, G_REGEX_MATCH_NEWLINE_CRLF, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\*EMWI: (\\d),(\\d).*\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\*ERINFO:\\s*(\\d),(\\d),(\\d).*\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, mbm_erinfo_received, modem, NULL);
        g_regex_unref (regex);
    }

    return TRUE;
}

/*****************************************************************************/

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_unlock_retries = mbm_get_unlock_retries;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->do_register = do_register;
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
    modem_class->reset = reset;
    modem_class->factory_reset = factory_reset;
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
    gsm_class->do_disconnect = do_disconnect;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->set_allowed_mode = set_allowed_mode;
}

