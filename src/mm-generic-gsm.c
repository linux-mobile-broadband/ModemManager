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
 * Copyright (C) 2009 Ericsson
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mm-generic-gsm.h"
#include "mm-modem-gsm-card.h"
#include "mm-modem-gsm-network.h"
#include "mm-modem-gsm-sms.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_card_init (MMModemGsmCard *gsm_card_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_gsm_sms_init (MMModemGsmSms *gsm_sms_class);
static void modem_simple_init (MMModemSimple *class);

G_DEFINE_TYPE_EXTENDED (MMGenericGsm, mm_generic_gsm, MM_TYPE_MODEM_BASE, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_SMS, modem_gsm_sms_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init))

#define MM_GENERIC_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_GENERIC_GSM, MMGenericGsmPrivate))

typedef struct {
    char *driver;
    char *plugin;
    char *device;

    gboolean valid;

    char *oper_code;
    char *oper_name;
    guint32 ip_method;
    gboolean unsolicited_registration;

    MMModemGsmNetworkRegStatus reg_status;
    guint pending_reg_id;
    MMCallbackInfo *pending_reg_info;

    guint32 signal_quality;
    guint32 cid;

    MMSerialPort *primary;
    MMSerialPort *secondary;
    MMPort *data;
} MMGenericGsmPrivate;

static void get_registration_status (MMSerialPort *port, MMCallbackInfo *info);
static void read_operator_code_done (MMSerialPort *port,
                                     GString *response,
                                     GError *error,
                                     gpointer user_data);

static void read_operator_name_done (MMSerialPort *port,
                                     GString *response,
                                     GError *error,
                                     gpointer user_data);

static void reg_state_changed (MMSerialPort *port,
                               GMatchInfo *match_info,
                               gpointer user_data);

MMModem *
mm_generic_gsm_new (const char *device,
                    const char *driver,
                    const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

void
mm_generic_gsm_set_unsolicited_registration (MMGenericGsm *modem,
                                             gboolean enabled)
{
    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    MM_GENERIC_GSM_GET_PRIVATE (modem)->unsolicited_registration = enabled;
}

void
mm_generic_gsm_set_cid (MMGenericGsm *modem, guint32 cid)
{
    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    MM_GENERIC_GSM_GET_PRIVATE (modem)->cid = cid;
}

guint32
mm_generic_gsm_get_cid (MMGenericGsm *modem)
{
    g_return_val_if_fail (MM_IS_GENERIC_GSM (modem), 0);

    return MM_GENERIC_GSM_GET_PRIVATE (modem)->cid;
}

static void
got_signal_quality (MMModem *modem,
                    guint32 result,
                    GError *error,
                    gpointer user_data)
{
}

void
mm_generic_gsm_set_reg_status (MMGenericGsm *modem,
                               MMModemGsmNetworkRegStatus status)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->reg_status != status) {
        priv->reg_status = status;

        g_debug ("Registration state changed: %d", status);

        if (status == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME ||
            status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {
            mm_serial_port_queue_command (priv->primary, "+COPS=3,2;+COPS?", 3, read_operator_code_done, modem);
            mm_serial_port_queue_command (priv->primary, "+COPS=3,0;+COPS?", 3, read_operator_name_done, modem);
            mm_modem_gsm_network_get_signal_quality (MM_MODEM_GSM_NETWORK (modem), got_signal_quality, NULL);
        } else {
            g_free (priv->oper_code);
            g_free (priv->oper_name);
            priv->oper_code = priv->oper_name = NULL;

            mm_modem_gsm_network_registration_info (MM_MODEM_GSM_NETWORK (modem), priv->reg_status,
                                                    priv->oper_code, priv->oper_name);
        }

        mm_generic_gsm_update_enabled_state (modem, TRUE, MM_MODEM_STATE_REASON_NONE);
    }
}

typedef struct {
    const char *result;
    guint code;
} CPinResult;

static void
pin_check_done (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;
    static CPinResult results[] = {
        { "SIM PIN", MM_MOBILE_ERROR_SIM_PIN },
        { "SIM PUK", MM_MOBILE_ERROR_SIM_PUK },
        { "PH-SIM PIN", MM_MOBILE_ERROR_PH_SIM_PIN },
        { "PH-FSIM PIN", MM_MOBILE_ERROR_PH_FSIM_PIN },
        { "PH-FSIM PUK", MM_MOBILE_ERROR_PH_FSIM_PUK },
        { "SIM PIN2", MM_MOBILE_ERROR_SIM_PIN2 },
        { "SIM PUK2", MM_MOBILE_ERROR_SIM_PUK2 },
        { "PH-NET PIN", MM_MOBILE_ERROR_NETWORK_PIN },
        { "PH-NET PUK", MM_MOBILE_ERROR_NETWORK_PUK },
        { "PH-NETSUB PIN", MM_MOBILE_ERROR_NETWORK_SUBSET_PIN },
        { "PH-NETSUB PUK", MM_MOBILE_ERROR_NETWORK_SUBSET_PUK },
        { "PH-SP PIN", MM_MOBILE_ERROR_SERVICE_PIN },
        { "PH-SP PUK", MM_MOBILE_ERROR_SERVICE_PUK },
        { "PH-CORP PIN", MM_MOBILE_ERROR_CORP_PIN },
        { "PH-CORP PUK", MM_MOBILE_ERROR_CORP_PUK },
        { NULL, MM_MOBILE_ERROR_PHONE_FAILURE },
    };

    if (error)
        info->error = g_error_copy (error);
    else if (g_str_has_prefix (response->str, "+CPIN: ")) {
        const char *str = response->str + 7;

        if (g_str_has_prefix (str, "READY"))
            parsed = TRUE;
        else {
            CPinResult *iter = &results[0];

            /* Translate the error */
            while (iter->result) {
                if (g_str_has_prefix (str, iter->result)) {
                    info->error = mm_mobile_error_for_code (iter->code);
                    parsed = TRUE;
                    break;
                }
                iter++;
            }
        }
    }

    if (!info->error && !parsed) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Could not parse PIN request response '%s'",
                                   response->str);
    }

    mm_callback_info_schedule (info);
}

void
mm_generic_gsm_check_pin (MMGenericGsm *modem,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command (priv->primary, "+CPIN?", 3, pin_check_done, info);
}

/*****************************************************************************/

void
mm_generic_gsm_update_enabled_state (MMGenericGsm *self,
                                     gboolean stay_connected,
                                     MMModemStateReason reason)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    /* While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED.
     */
    if (stay_connected && (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_DISCONNECTING))
        return;

    switch (priv->reg_status) {
    case MM_MODEM_GSM_NETWORK_REG_STATUS_HOME:
    case MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING:
        mm_modem_set_state (MM_MODEM (self), MM_MODEM_STATE_REGISTERED, reason);
        break;
    case MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING:
        mm_modem_set_state (MM_MODEM (self), MM_MODEM_STATE_SEARCHING, reason);
        break;
    case MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE:
    case MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED:
    case MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN:
    default:
        mm_modem_set_state (MM_MODEM (self), MM_MODEM_STATE_ENABLED, reason);
        break;
    }
}

static void
check_valid (MMGenericGsm *self)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    gboolean new_valid = FALSE;

    if (priv->primary && priv->data)
        new_valid = TRUE;

    mm_modem_base_set_valid (MM_MODEM_BASE (self), new_valid);
}

static gboolean
owns_port (MMModem *modem, const char *subsys, const char *name)
{
    return !!mm_modem_base_get_port (MM_MODEM_BASE (modem), subsys, name);
}

MMPort *
mm_generic_gsm_grab_port (MMGenericGsm *self,
                          const char *subsys,
                          const char *name,
                          MMPortType ptype,
                          GError **error)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMPort *port = NULL;
    GRegex *regex;

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), FALSE);

    port = mm_modem_base_add_port (MM_MODEM_BASE (self), subsys, name, ptype);
    if (port && MM_IS_SERIAL_PORT (port)) {
        mm_serial_port_set_response_parser (MM_SERIAL_PORT (port),
                                            mm_serial_parser_v1_parse,
                                            mm_serial_parser_v1_new (),
                                            mm_serial_parser_v1_destroy);

        regex = g_regex_new ("\\r\\n\\+CREG: (\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, reg_state_changed, self, NULL);
        g_regex_unref (regex);

        if (ptype == MM_PORT_TYPE_PRIMARY) {
            priv->primary = MM_SERIAL_PORT (port);
            if (!priv->data) {
                priv->data = port;
                g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            }
            check_valid (self);
        } else if (ptype == MM_PORT_TYPE_SECONDARY)
            priv->secondary = MM_SERIAL_PORT (port);
    } else {
        /* Net device (if any) is the preferred data port */
        if (!priv->data || MM_IS_SERIAL_PORT (priv->data)) {
            priv->data = port;
            g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            check_valid (self);
        }
    }

    return port;
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;

    if (priv->primary)
        g_return_val_if_fail (suggested_type != MM_PORT_TYPE_PRIMARY, FALSE);

    if (!strcmp (subsys, "tty")) {
        if (suggested_type != MM_PORT_TYPE_UNKNOWN)
            ptype = suggested_type;
        else {
            if (!priv->primary)
                ptype = MM_PORT_TYPE_PRIMARY;
            else if (!priv->secondary)
                ptype = MM_PORT_TYPE_SECONDARY;
        }
    }

    return !!mm_generic_gsm_grab_port (self, subsys, name, ptype, error);
}

static void
release_port (MMModem *modem, const char *subsys, const char *name)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMPort *port;

    if (strcmp (subsys, "tty") && strcmp (subsys, "net"))
        return;

    port = mm_modem_base_get_port (MM_MODEM_BASE (modem), subsys, name);
    if (!port)
        return;

    if (port == MM_PORT (priv->primary)) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->primary = NULL;
    }

    if (port == priv->data) {
        priv->data = NULL;
        g_object_notify (G_OBJECT (modem), MM_MODEM_DATA_DEVICE);
    }

    if (port == MM_PORT (priv->secondary)) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->secondary = NULL;
    }

    check_valid (MM_GENERIC_GSM (modem));
}

void
mm_generic_gsm_enable_complete (MMGenericGsm *modem,
                                GError *error,
                                MMCallbackInfo *info)
{
    g_return_if_fail (modem != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (modem));
    g_return_if_fail (info != NULL);

    if (error) {
        mm_modem_set_state (MM_MODEM (modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        info->error = g_error_copy (error);
    } else {
        /* Modem is enabled; update the state */
        mm_generic_gsm_update_enabled_state (modem, FALSE, MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
enable_done (MMSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Ignore power-up command errors, not all devices actually support
     * CFUN=1.
     */
    /* FIXME: instead of just ignoring errors, since we allow subclasses
     * to override the power-on command, make a class function for powering
     * on the phone and let the subclass decided whether it wants to handle
     * errors or ignore them.
     */

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), NULL, info);
}

static void
init_done (MMSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *cmd = NULL;

    if (error) {
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    /* Ensure echo is off after the init command; some modems ignore the
        * E0 when it's in the same like as ATZ (Option GIO322).
        */
    mm_serial_port_queue_command (port, "E0 +CMEE=1", 2, NULL, NULL);

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_INIT_CMD_OPTIONAL, &cmd, NULL);
    mm_serial_port_queue_command (port, cmd, 2, NULL, NULL);
    g_free (cmd);

    if (MM_GENERIC_GSM_GET_PRIVATE (info->modem)->unsolicited_registration)
        mm_serial_port_queue_command (port, "+CREG=1", 5, NULL, NULL);
    else
        mm_serial_port_queue_command (port, "+CREG=0", 5, NULL, NULL);

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_POWER_UP_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_serial_port_queue_command (port, cmd, 5, enable_done, user_data);
    else
        enable_done (port, NULL, NULL, user_data);
    g_free (cmd);
}

static void
enable_flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    char *cmd = NULL;

    if (error) {
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_INIT_CMD, &cmd, NULL);
    mm_serial_port_queue_command (port, cmd, 3, init_done, user_data);
    g_free (cmd);
}

static void
real_do_enable (MMGenericGsm *self, MMModemFn callback, gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_serial_port_flash (priv->primary, 100, enable_flash_done, info);
}

static void
enable (MMModem *modem,
        MMModemFn callback,
        gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    GError *error = NULL;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    if (!mm_serial_port_open (priv->primary, &error)) {
        MMCallbackInfo *info;

        g_assert (error);
        info = mm_callback_info_new (modem, callback, user_data);
        info->error = error;
        mm_callback_info_schedule (info);
        return;
    }

    mm_modem_set_state (modem, MM_MODEM_STATE_ENABLING, MM_MODEM_STATE_REASON_NONE);

    g_assert (MM_GENERIC_GSM_GET_CLASS (modem)->do_enable);
    MM_GENERIC_GSM_GET_CLASS (modem)->do_enable (MM_GENERIC_GSM (modem), callback, user_data);
}

static void
disable_done (MMSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        mm_serial_port_close (port);
        mm_modem_set_state (MM_MODEM (info->modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);
        priv->reg_status = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
    }
    mm_callback_info_schedule (info);
}

static void
disable_flash_done (MMSerialPort *port,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemState prev_state;
    char *cmd = NULL;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* Reset old state since the operation failed */
            prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_GSM_PREV_STATE_TAG));
            mm_modem_set_state (MM_MODEM (info->modem),
                                prev_state,
                                MM_MODEM_STATE_REASON_NONE);
        }

        mm_callback_info_schedule (info);
        return;
    }

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_POWER_DOWN_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_serial_port_queue_command (port, cmd, 5, disable_done, user_data);
    else
        disable_done (port, NULL, NULL, user_data);
    g_free (cmd);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMModemState state;

    /* First, reset the previously used CID and clean up registration */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);
    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_GSM_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    mm_modem_set_state (MM_MODEM (info->modem),
                        MM_MODEM_STATE_DISABLING,
                        MM_MODEM_STATE_REASON_NONE);

    if (mm_port_get_connected (MM_PORT (priv->primary)))
        mm_serial_port_flash (priv->primary, 1000, disable_flash_done, info);
    else
        disable_flash_done (priv->primary, NULL, info);
}

static void
get_string_done (MMSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else
        mm_callback_info_set_result (info, g_strdup (response->str), g_free);

    mm_callback_info_schedule (info);
}

static void
get_imei (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command_cached (priv->primary, "+CGSN", 3, get_string_done, info);
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command_cached (priv->primary, "+CIMI", 3, get_string_done, info);
}

static void
card_info_invoke (MMCallbackInfo *info)
{
    MMModemInfoFn callback = (MMModemInfoFn) info->callback;

    callback (info->modem,
              (char *) mm_callback_info_get_data (info, "card-info-manufacturer"),
              (char *) mm_callback_info_get_data (info, "card-info-model"),
              (char *) mm_callback_info_get_data (info, "card-info-version"),
              info->error, info->user_data);
}

#define GMI_RESP_TAG "+CGMI:"
#define GMM_RESP_TAG "+CGMM:"
#define GMR_RESP_TAG "+CGMR:"

static const char *
strip_tag (const char *str, const char *tag)
{
    /* Strip the response header, if any */
    if (strncmp (str, tag, strlen (tag)) == 0)
        str += strlen (tag);
    while (*str && isspace (*str))
        str++;
    return str;
}

static void
get_version_done (MMSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *resp = strip_tag (response->str, GMR_RESP_TAG);

    if (!error)
        mm_callback_info_set_data (info, "card-info-version", g_strdup (resp), g_free);
    else if (!info->error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
get_model_done (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *resp = strip_tag (response->str, GMM_RESP_TAG);

    if (!error)
        mm_callback_info_set_data (info, "card-info-model", g_strdup (resp), g_free);
    else if (!info->error)
        info->error = g_error_copy (error);
}

static void
get_manufacturer_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *resp = strip_tag (response->str, GMI_RESP_TAG);

    if (!error)
        mm_callback_info_set_data (info, "card-info-manufacturer", g_strdup (resp), g_free);
    else
        info->error = g_error_copy (error);
}

static void
get_card_info (MMModem *modem,
               MMModemInfoFn callback,
               gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      card_info_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_serial_port_queue_command_cached (priv->primary, "+CGMI", 3, get_manufacturer_done, info);
    mm_serial_port_queue_command_cached (priv->primary, "+CGMM", 3, get_model_done, info);
    mm_serial_port_queue_command_cached (priv->primary, "+CGMR", 3, get_version_done, info);
}

static void
send_puk_done (MMSerialPort *port,
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
send_puk (MMModemGsmCard *modem,
          const char *puk,
          const char *pin,
          MMModemFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin);
    mm_serial_port_queue_command (priv->primary, command, 3, send_puk_done, info);
    g_free (command);
}

static void
send_pin_done (MMSerialPort *port,
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
send_pin (MMModemGsmCard *modem,
          const char *pin,
          MMModemFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPIN=\"%s\"", pin);
    mm_serial_port_queue_command (priv->primary, command, 3, send_pin_done, info);
    g_free (command);
}

static void
enable_pin_done (MMSerialPort *port,
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
enable_pin (MMModemGsmCard *modem,
            const char *pin,
            gboolean enabled,
            MMModemFn callback,
            gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CLCK=\"SC\",%d,\"%s\"", enabled ? 1 : 0, pin);
    mm_serial_port_queue_command (priv->primary, command, 3, enable_pin_done, info);
    g_free (command);
}

static void
change_pin_done (MMSerialPort *port,
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
change_pin (MMModemGsmCard *modem,
            const char *old_pin,
            const char *new_pin,
            MMModemFn callback,
            gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPWD=\"SC\",\"%s\",\"%s\"", old_pin, new_pin);
    mm_serial_port_queue_command (priv->primary, command, 3, change_pin_done, info);
    g_free (command);
}

static char *
parse_operator (const char *reply)
{
    char *operator = NULL;

    if (reply && !strncmp (reply, "+COPS: ", 7)) {
        /* Got valid reply */
		GRegex *r;
		GMatchInfo *match_info;

		reply += 7;
		r = g_regex_new ("(\\d),(\\d),\"(.+)\"", G_REGEX_UNGREEDY, 0, NULL);
		if (!r)
            return NULL;

		g_regex_match (r, reply, 0, &match_info);
		if (g_match_info_matches (match_info))
            operator = g_match_info_fetch (match_info, 3);

		g_match_info_free (match_info);
		g_regex_unref (r);
    }

    return operator;
}

static void
read_operator_code_done (MMSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (user_data);
    char *oper;

    if (error)
        return;

    oper = parse_operator (response->str);
    if (!oper)
        return;

    g_free (priv->oper_code);
    priv->oper_code = oper;
}

static void
read_operator_name_done (MMSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (user_data);
    char *oper;

    if (error)
        return;

    oper = parse_operator (response->str);
    if (!oper)
        return;

    g_free (priv->oper_name);
    priv->oper_name = oper;

    mm_modem_gsm_network_registration_info (MM_MODEM_GSM_NETWORK (user_data),
                                            priv->reg_status,
                                            priv->oper_code,
                                            priv->oper_name);
}

/* Registration */
#define REG_STATUS_AGAIN_TAG "reg-status-again"

void
mm_generic_gsm_pending_registration_stop (MMGenericGsm *modem)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->pending_reg_id) {
        /* Clear the registration timeout handler */
        g_source_remove (priv->pending_reg_id);
        priv->pending_reg_id = 0;
    }

    if (priv->pending_reg_info) {
        /* Clear any ongoing registration status callback */
        mm_callback_info_set_data (priv->pending_reg_info, REG_STATUS_AGAIN_TAG, NULL, NULL);

        /* And schedule the callback */
        mm_callback_info_schedule (priv->pending_reg_info);
        priv->pending_reg_info = NULL;
    }
}

static gboolean
reg_status_updated (MMGenericGsm *self, int new_value, GError **error)
{
    MMModemGsmNetworkRegStatus status;
    gboolean status_done = FALSE;

    switch (new_value) {
    case 0:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE;
        break;
    case 1:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_HOME;
        break;
    case 2:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING;
        break;
    case 3:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED;
        break;
    case 4:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
        break;
    case 5:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING;
        break;
    default:
        status = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
        break;
    }

    mm_generic_gsm_set_reg_status (self, status);

    /* Registration has either completed successfully or completely failed */
    switch (status) {
    case MM_MODEM_GSM_NETWORK_REG_STATUS_HOME:
    case MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING:
        /* Successfully registered - stop registration */
        status_done = TRUE;
        break;
    case MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED:
        /* registration failed - stop registration */
        if (error)
            *error = mm_mobile_error_for_code (MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED);
        status_done = TRUE;
        break;
    case MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING:
        if (error)
            *error = mm_mobile_error_for_code (MM_MOBILE_ERROR_NETWORK_TIMEOUT);
        break;
    case MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE:
        if (error)
            *error = mm_mobile_error_for_code (MM_MOBILE_ERROR_NO_NETWORK);
        break;
    default:
        if (error)
            *error = mm_mobile_error_for_code (MM_MOBILE_ERROR_UNKNOWN);
        break;
    }
    return status_done;
}

static void
reg_state_changed (MMSerialPort *port,
                   GMatchInfo *match_info,
                   gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    char *str;
    gboolean done;

    str = g_match_info_fetch (match_info, 1);
    done = reg_status_updated (self, atoi (str), NULL);
    g_free (str);

    if (done) {
        /* If registration is finished (either registered or failed) but the
         * registration query hasn't completed yet, just remove the timeout and
         * let the registration query complete.
         */
        if (priv->pending_reg_id) {
            g_source_remove (priv->pending_reg_id);
            priv->pending_reg_id = 0;
        }
    }
}

static gboolean
reg_status_again (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    g_warn_if_fail (info == priv->pending_reg_info);

    if (priv->pending_reg_info)
        get_registration_status (priv->primary, info);

    return FALSE;
}

static void
reg_status_again_remove (gpointer data)
{
    guint id = GPOINTER_TO_UINT (data);

    /* Technically the GSource ID can be 0, but in practice it won't be */
    if (id > 0)
        g_source_remove (id);
}

static void
get_reg_status_done (MMSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    int reg_status = -1;
    GRegex *r;
    GMatchInfo *match_info;
    char *tmp;
    guint id;

    g_warn_if_fail (info == priv->pending_reg_info);

    if (error) {
        info->error = g_error_copy (error);
        goto reg_done;
    }

    r = g_regex_new ("\\+CREG:\\s*(\\d+),\\s*(\\d+)",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE,
                     0, &info->error);
    if (r) {
        g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, &info->error);
        if (g_match_info_matches (match_info)) {
            /* Get reg status */
            tmp = g_match_info_fetch (match_info, 2);
            if (isdigit (tmp[0])) {
                tmp[1] = '\0';
                reg_status = atoi (tmp);
            } else {
                info->error = g_error_new (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Unknown registration status '%s'",
                                           tmp);
            }
            g_free (tmp);
        } else {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_GENERAL,
                                               "Could not parse the registration status response");
        }
        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (   reg_status >= 0
        && !reg_status_updated (self, reg_status, &info->error)
        && priv->pending_reg_info) {
        g_clear_error (&info->error);

        /* Not registered yet; poll registration status again */
        id = g_timeout_add_seconds (1, reg_status_again, info);
        mm_callback_info_set_data (info, REG_STATUS_AGAIN_TAG,
                                   GUINT_TO_POINTER (id),
                                   reg_status_again_remove);
        return;
    }

reg_done:
    /* This will schedule the callback for us */
    mm_generic_gsm_pending_registration_stop (self);
}

static void
get_registration_status (MMSerialPort *port, MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    g_warn_if_fail (info == priv->pending_reg_info);

    mm_serial_port_queue_command (port, "+CREG?", 10, get_reg_status_done, info);
}

static void
register_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    mm_callback_info_unref (info);

    /* If the registration timed out (and thus pending_reg_info will be NULL)
     * and the modem eventually got around to sending the response for the
     * registration request then just ignore the response since the callback is
     * already called.
     */

    if (priv->pending_reg_info) {
        g_warn_if_fail (info == priv->pending_reg_info);

        /* Ignore errors here, get the actual registration status */
        get_registration_status (port, info);
    }
}

static gboolean
registration_timed_out (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    g_warn_if_fail (info == priv->pending_reg_info);

    priv->reg_status = MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE;

    info->error = mm_mobile_error_for_code (MM_MOBILE_ERROR_NETWORK_TIMEOUT);
    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (info->modem));

    return FALSE;
}

static void
do_register (MMModemGsmNetwork *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    /* Clear any previous registration */
    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    priv->pending_reg_id = g_timeout_add_seconds (60, registration_timed_out, info);
    priv->pending_reg_info = info;

    if (network_id)
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", network_id);
    else
        command = g_strdup ("+COPS=0,,");

    /* Ref the callback info to ensure it stays alive for register_done() even
     * if the timeout triggers and ends registration (which calls the callback
     * and unrefs the callback info).  Some devices (hso) will delay the
     * registration response until the registration is done (and thus
     * unsolicited registration responses will arrive before the +COPS is
     * complete).  Most other devices will return the +COPS response immediately
     * and the unsolicited response (if any) at a later time.
     *
     * To handle both these cases, unsolicited registration responses will just
     * remove the pending registration timeout but we let the +COPS command
     * complete.  For those devices that delay the +COPS response (hso) the
     * callback will be called from register_done().  For those devices that
     * return the +COPS response immediately, we'll poll the registration state
     * and call the callback from get_reg_status_done() in response to the
     * polled response.  The registration timeout will only be triggered when
     * the +COPS response is never received.
     */
    mm_callback_info_ref (info);
    mm_serial_port_queue_command (priv->primary, command, 120, register_done, info);
    g_free (command);
}

static void
gsm_network_reg_info_invoke (MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    MMModemGsmNetworkRegInfoFn callback = (MMModemGsmNetworkRegInfoFn) info->callback;

    callback (MM_MODEM_GSM_NETWORK (info->modem),
              priv->reg_status,
              priv->oper_code,
              priv->oper_name,
              info->error,
              info->user_data);
}

static void
get_registration_info (MMModemGsmNetwork *self,
                       MMModemGsmNetworkRegInfoFn callback,
                       gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self),
                                      gsm_network_reg_info_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_callback_info_schedule (info);
}

void
mm_generic_gsm_connect_complete (MMGenericGsm *modem,
                                 GError *error,
                                 MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (modem != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (modem));
    g_return_if_fail (info != NULL);

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (error) {
        mm_generic_gsm_update_enabled_state (modem, FALSE, MM_MODEM_STATE_REASON_NONE);
        info->error = g_error_copy (error);
    } else {
        /* Modem is connected; update the state */
        mm_port_set_connected (priv->data, TRUE);
        mm_modem_set_state (MM_MODEM (modem),
                            MM_MODEM_STATE_CONNECTED,
                            MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
connect_report_done (MMSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GError *real_error;

    /* If the CEER command was successful, copy that error reason into the
     * callback's error.  If not, use the original error.
     */

    /* Have to do this little dance since mm_generic_gsm_connect_complete()
     * copies the provided error into the callback info.
     */
    real_error = info->error;
    info->error = NULL;

    if (   !error
        && g_str_has_prefix (response->str, "+CEER: ")
        && (strlen (response->str) > 7)) {
        /* copy the connect failure reason into the error */
        g_free (real_error->message);
        real_error->message = g_strdup (response->str + 7); /* skip the "+CEER: " */
    }

    mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), real_error, info);
    g_error_free (real_error);
}

static void
connect_done (MMSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    if (error) {
        info->error = g_error_copy (error);
        /* Try to get more information why it failed */
        priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
        mm_serial_port_queue_command (priv->primary, "+CEER", 3, connect_report_done, info);
    } else
        mm_generic_gsm_connect_complete (MM_GENERIC_GSM (info->modem), NULL, info);
}

static void
connect (MMModem *modem,
         const char *number,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;
    guint32 cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    if (cid > 0) {
        GString *str;

        str = g_string_new ("D");
        if (g_str_has_suffix (number, "#"))
            str = g_string_append_len (str, number, strlen (number) - 1);
        else
            str = g_string_append (str, number);

        g_string_append_printf (str, "***%d#", cid);
        command = g_string_free (str, FALSE);
    } else
        command = g_strconcat ("DT", number, NULL);

    mm_serial_port_queue_command (priv->primary, command, 60, connect_done, info);
    g_free (command);
}

static void
disconnect_flash_done (MMSerialPort *port,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemState prev_state;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* Reset old state since the operation failed */
            prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_GSM_PREV_STATE_TAG));
            mm_modem_set_state (MM_MODEM (info->modem),
                                prev_state,
                                MM_MODEM_STATE_REASON_NONE);
        }
    } else {
        MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

        mm_port_set_connected (priv->data, FALSE);
        mm_generic_gsm_update_enabled_state (self, FALSE, MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMModemState state;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_GSM_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    mm_modem_set_state (modem, MM_MODEM_STATE_DISCONNECTING, MM_MODEM_STATE_REASON_NONE);
    mm_serial_port_flash (priv->primary, 1000, disconnect_flash_done, info);
}

static void
gsm_network_scan_invoke (MMCallbackInfo *info)
{
    MMModemGsmNetworkScanFn callback = (MMModemGsmNetworkScanFn) info->callback;

    callback (MM_MODEM_GSM_NETWORK (info->modem),
              (GPtrArray *) mm_callback_info_get_data (info, "scan-results"),
              info->error,
              info->user_data);
}

static void
scan_done (MMSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GPtrArray *results;

    if (error)
        info->error = g_error_copy (error);
    else {
        results = mm_gsm_parse_scan_response (response->str, &info->error);
        if (results)
            mm_callback_info_set_data (info, "scan-results", results, mm_gsm_destroy_scan_data);
    }

    mm_callback_info_schedule (info);
}

static void
scan (MMModemGsmNetwork *modem,
      MMModemGsmNetworkScanFn callback,
      gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      gsm_network_scan_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_serial_port_queue_command (priv->primary, "+COPS=?", 120, scan_done, info);
}

/* SetApn */

static void
set_apn_done (MMSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else
        mm_generic_gsm_set_cid (MM_GENERIC_GSM (info->modem),
                                GPOINTER_TO_UINT (mm_callback_info_get_data (info, "cid")));

    mm_callback_info_schedule (info);
}

static void
cid_range_read (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint32 cid = 0;

    if (error)
        info->error = g_error_copy (error);
    else if (g_str_has_prefix (response->str, "+CGDCONT: ")) {
        GRegex *r;
        GMatchInfo *match_info;

        r = g_regex_new ("\\+CGDCONT:\\s*\\((\\d+)-(\\d+)\\),\\(?\"(\\S+)\"",
                         G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                         0, &info->error);
        if (r) {
            g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, &info->error);
            while (cid == 0 && g_match_info_matches (match_info)) {
                char *tmp;

                tmp = g_match_info_fetch (match_info, 3);
                if (!strcmp (tmp, "IP")) {
                    int max_cid;
                    int highest_cid = GPOINTER_TO_INT (mm_callback_info_get_data (info, "highest-cid"));

                    g_free (tmp);

                    tmp = g_match_info_fetch (match_info, 2);
                    max_cid = atoi (tmp);

                    if (highest_cid < max_cid)
                        cid = highest_cid + 1;
                    else
                        cid = highest_cid;
                }

                g_free (tmp);
                g_match_info_next (match_info, NULL);
            }

            if (cid == 0)
                /* Choose something */
                cid = 1;
        }
    } else
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the response");

    if (info->error)
        mm_callback_info_schedule (info);
    else {
        const char *apn = (const char *) mm_callback_info_get_data (info, "apn");
        char *command;

        mm_callback_info_set_data (info, "cid", GUINT_TO_POINTER (cid), NULL);

        command = g_strdup_printf ("+CGDCONT=%d,\"IP\",\"%s\"", cid, apn);
        mm_serial_port_queue_command (port, command, 3, set_apn_done, info);
        g_free (command);
    }
}

static void
existing_apns_read (MMSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean found = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (g_str_has_prefix (response->str, "+CGDCONT: ")) {
        GRegex *r;
        GMatchInfo *match_info;

        r = g_regex_new ("\\+CGDCONT:\\s*(\\d+)\\s*,\"(\\S+)\",\"(\\S+)\",\"(\\S*)\"",
                         G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                         0, &info->error);
        if (r) {
            const char *new_apn = (const char *) mm_callback_info_get_data (info, "apn");

            g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, &info->error);
            while (!found && g_match_info_matches (match_info)) {
                char *cid;
                char *pdp_type;
                char *apn;
                int num_cid;

                cid = g_match_info_fetch (match_info, 1);
                num_cid = atoi (cid);
                pdp_type = g_match_info_fetch (match_info, 2);
                apn = g_match_info_fetch (match_info, 3);

                if (!strcmp (apn, new_apn)) {
                    mm_generic_gsm_set_cid (MM_GENERIC_GSM (info->modem), (guint32) num_cid);
                    found = TRUE;
                }

                if (!found && !strcmp (pdp_type, "IP")) {
                    int highest_cid;

                    highest_cid = GPOINTER_TO_INT (mm_callback_info_get_data (info, "highest-cid"));
                    if (num_cid > highest_cid)
                        mm_callback_info_set_data (info, "highest-cid", GINT_TO_POINTER (num_cid), NULL);
                }

                g_free (cid);
                g_free (pdp_type);
                g_free (apn);
                g_match_info_next (match_info, NULL);
            }

            g_match_info_free (match_info);
            g_regex_unref (r);
        }
    } else if (strlen (response->str) == 0) {
        /* No APNs configured, just don't set error */
    } else
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the response");

    if (found || info->error)
        mm_callback_info_schedule (info);
    else
        /* APN not configured on the card. Get the allowed CID range */
        mm_serial_port_queue_command_cached (port, "+CGDCONT=?", 3, cid_range_read, info);
}

static void
set_apn (MMModemGsmNetwork *modem,
         const char *apn,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_set_data (info, "apn", g_strdup (apn), g_free);

    /* Start by searching if the APN is already in card */
    mm_serial_port_queue_command (priv->primary, "+CGDCONT?", 3, existing_apns_read, info);
}

/* GetSignalQuality */

static void
get_signal_quality_done (MMSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;

    if (error)
        info->error = g_error_copy (error);
    else if (!strncmp (reply, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        reply += 6;

        if (sscanf (reply, "%d, %d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality == 99) {
                info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                                   MM_MOBILE_ERROR_NO_NETWORK,
                                                   "No service");
            } else {
                /* Normalize the quality */
                quality = CLAMP (quality, 0, 31) * 100 / 31;

                priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
                priv->signal_quality = quality;
                mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
            }
        } else
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                               "Could not parse signal quality results");
    }

    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMModemGsmNetwork *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    gboolean connected;

    connected = mm_port_get_connected (MM_PORT (priv->primary));
    if (connected && !priv->secondary) {
        g_message ("Returning saved signal quality %d", priv->signal_quality);
        callback (MM_MODEM (modem), priv->signal_quality, NULL, user_data);
        return;
    }

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command (connected ? priv->secondary : priv->primary, "+CSQ", 3, get_signal_quality_done, info);
}

/*****************************************************************************/
/* MMModemGsmSms interface */

static void
sms_send_done (MMSerialPort *port,
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
sms_send (MMModemGsmSms *modem, 
          const char *number,
          const char *text,
          const char *smsc,
          guint validity,
          guint class,
          MMModemFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info;
    char *command;
    gboolean connected;
    MMSerialPort *port = NULL;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    connected = mm_port_get_connected (MM_PORT (priv->primary));
    if (connected)
        port = priv->secondary;
    else
        port = priv->primary;

    if (!port) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                            "Cannot send SMS while connected");
        mm_callback_info_schedule (info);
        return;
    }

    /* FIXME: use the PDU mode instead */
    mm_serial_port_queue_command (port, "AT+CMGF=1", 3, NULL, NULL);

    command = g_strdup_printf ("+CMGS=\"%s\"\r%s\x1a", number, text);
    mm_serial_port_queue_command (port, command, 10, sms_send_done, info);
    g_free (command);
}

MMSerialPort *
mm_generic_gsm_get_port (MMGenericGsm *modem,
                         MMPortType ptype)
{
    g_return_val_if_fail (MM_IS_GENERIC_GSM (modem), NULL);
    g_return_val_if_fail (ptype != MM_PORT_TYPE_UNKNOWN, NULL);

    if (ptype == MM_PORT_TYPE_PRIMARY)
        return MM_GENERIC_GSM_GET_PRIVATE (modem)->primary;
    else if (ptype == MM_PORT_TYPE_SECONDARY)
        return MM_GENERIC_GSM_GET_PRIVATE (modem)->secondary;

    return NULL;
}

/*****************************************************************************/
/* MMModemSimple interface */

typedef enum {
    SIMPLE_STATE_BEGIN = 0,
    SIMPLE_STATE_ENABLE,
    SIMPLE_STATE_CHECK_PIN,
    SIMPLE_STATE_REGISTER,
    SIMPLE_STATE_SET_APN,
    SIMPLE_STATE_CONNECT,
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
    const char *str;
    SimpleState state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "simple-connect-state"));
    gboolean need_pin = FALSE;

    if (error) {
        if (g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_SIM_PIN)) {
            need_pin = TRUE;
            state = SIMPLE_STATE_CHECK_PIN;
        } else {
            info->error = g_error_copy (error);
            goto out;
        }
    }

    switch (state) {
    case SIMPLE_STATE_BEGIN:
        state = SIMPLE_STATE_ENABLE;
        mm_modem_enable (modem, simple_state_machine, info);
        break;
    case SIMPLE_STATE_ENABLE:
        state = SIMPLE_STATE_CHECK_PIN;
        mm_generic_gsm_check_pin (MM_GENERIC_GSM (modem), simple_state_machine, info);
        break;
    case SIMPLE_STATE_CHECK_PIN:
        if (need_pin) {
            str = simple_get_string_property (info, "pin", &info->error);
            if (str)
                mm_modem_gsm_card_send_pin (MM_MODEM_GSM_CARD (modem), str, simple_state_machine, info);
            else
                info->error = g_error_copy (error);
        } else {
            str = simple_get_string_property (info, "network_id", &info->error);
            state = SIMPLE_STATE_REGISTER;
            if (!info->error)
                mm_modem_gsm_network_register (MM_MODEM_GSM_NETWORK (modem), str, simple_state_machine, info);
        }
        break;
    case SIMPLE_STATE_REGISTER:
        str = simple_get_string_property (info, "apn", &info->error);
        if (str) {
            state = SIMPLE_STATE_SET_APN;
            mm_modem_gsm_network_set_apn (MM_MODEM_GSM_NETWORK (modem), str, simple_state_machine, info);
            break;
        }
        /* Fall through */
    case SIMPLE_STATE_SET_APN:
        str = simple_get_string_property (info, "number", &info->error);
        state = SIMPLE_STATE_CONNECT;
        mm_modem_connect (modem, str, simple_state_machine, info);
        break;
    case SIMPLE_STATE_CONNECT:
        state = SIMPLE_STATE_DONE;
        break;
    case SIMPLE_STATE_DONE:
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



static void
simple_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}

static GValue *
simple_uint_value (guint32 i)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, i);

    return val;
}

static GValue *
simple_string_value (const char *str)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, str);

    return val;
}

static void
simple_status_got_signal_quality (MMModem *modem,
                                  guint32 result,
                                  GError *error,
                                  gpointer user_data)
{
    if (error)
        g_warning ("Error getting signal quality: %s", error->message);
    else
        g_hash_table_insert ((GHashTable *) user_data, "signal_quality", simple_uint_value (result));
}

static void
simple_status_got_band (MMModem *modem,
                        guint32 result,
                        GError *error,
                        gpointer user_data)
{
    /* Ignore band errors since there's no generic implementation for it */
    if (!error)
        g_hash_table_insert ((GHashTable *) user_data, "band", simple_uint_value (result));
}

static void
simple_status_got_mode (MMModem *modem,
                        guint32 result,
                        GError *error,
                        gpointer user_data)
{
    /* Ignore network mode errors since there's no generic implementation for it */
    if (!error)
        g_hash_table_insert ((GHashTable *) user_data, "network_mode", simple_uint_value (result));
}

static void
simple_status_got_reg_info (MMModemGsmNetwork *modem,
                            MMModemGsmNetworkRegStatus status,
                            const char *oper_code,
                            const char *oper_name,
                            GError *error,
                            gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GHashTable *properties;

    if (error)
        info->error = g_error_copy (error);
    else {
        properties = (GHashTable *) mm_callback_info_get_data (info, "simple-get-status");
 
        g_hash_table_insert (properties, "registration_status", simple_uint_value (status));
        g_hash_table_insert (properties, "operator_code", simple_string_value (oper_code));
        g_hash_table_insert (properties, "operator_name", simple_string_value (oper_name));
    }

    mm_callback_info_schedule (info);
}

static void
simple_get_status_invoke (MMCallbackInfo *info)
{
    MMModemSimpleGetStatusFn callback = (MMModemSimpleGetStatusFn) info->callback;

    callback (MM_MODEM_SIMPLE (info->modem),
              (GHashTable *) mm_callback_info_get_data (info, "simple-get-status"),
              info->error, info->user_data);
}

static void
simple_get_status (MMModemSimple *simple,
                   MMModemSimpleGetStatusFn callback,
                   gpointer user_data)
{
    MMModemGsmNetwork *gsm;
    GHashTable *properties;
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (simple),
                                      simple_get_status_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, simple_free_gvalue);
    mm_callback_info_set_data (info, "simple-get-status", properties, (GDestroyNotify) g_hash_table_unref);

    gsm = MM_MODEM_GSM_NETWORK (simple);
    mm_modem_gsm_network_get_signal_quality (gsm, simple_status_got_signal_quality, properties);
    mm_modem_gsm_network_get_band (gsm, simple_status_got_band, properties);
    mm_modem_gsm_network_get_mode (gsm, simple_status_got_mode, properties);
    mm_modem_gsm_network_get_registration_info (gsm, simple_status_got_reg_info, properties);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->owns_port = owns_port;
    modem_class->grab_port = grab_port;
    modem_class->release_port = release_port;
    modem_class->enable = enable;
    modem_class->disable = disable;
    modem_class->connect = connect;
    modem_class->disconnect = disconnect;
    modem_class->get_info = get_card_info;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imei = get_imei;
    class->get_imsi = get_imsi;
    class->send_pin = send_pin;
    class->send_puk = send_puk;
    class->enable_pin = enable_pin;
    class->change_pin = change_pin;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->do_register = do_register;
    class->get_registration_info = get_registration_info;
    class->set_apn = set_apn;
    class->scan = scan;
    class->get_signal_quality = get_signal_quality;
}

static void
modem_gsm_sms_init (MMModemGsmSms *class)
{
    class->send = sms_send;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
    class->get_status = simple_get_status;
}

static void
mm_generic_gsm_init (MMGenericGsm *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_MODEM_PROP_TYPE:
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
    case MM_GENERIC_GSM_PROP_POWER_DOWN_CMD:
    case MM_GENERIC_GSM_PROP_INIT_CMD:
    case MM_GENERIC_GSM_PROP_INIT_CMD_OPTIONAL:
    case MM_GENERIC_GSM_PROP_SUPPORTED_BANDS:
    case MM_GENERIC_GSM_PROP_SUPPORTED_MODES:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_DATA_DEVICE:
        if (priv->data)
            g_value_set_string (value, mm_port_get_device (priv->data));
        else
            g_value_set_string (value, NULL);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_GSM);
        break;
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
        g_value_set_string (value, "+CFUN=1");
        break;
    case MM_GENERIC_GSM_PROP_POWER_DOWN_CMD:
        /* CFUN=0 is dangerous and often will shoot devices in the head (that's
         * what it's supposed to do).  So don't use CFUN=0 by default, but let
         * specific plugins use it when they know it's safe to do so.  For
         * example, CFUN=0 will often make phones turn themselves off, but some
         * dedicated devices (ex Sierra WWAN cards) will just turn off their
         * radio but otherwise still work.
         */
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_INIT_CMD:
        g_value_set_string (value, "Z E0 V1 +CMEE=1");
        break;
    case MM_GENERIC_GSM_PROP_INIT_CMD_OPTIONAL:
        g_value_set_string (value, "X4 &C1");
        break;
    case MM_GENERIC_GSM_PROP_SUPPORTED_BANDS:
        g_value_set_uint (value, 0);
        break;
    case MM_GENERIC_GSM_PROP_SUPPORTED_MODES:
        g_value_set_uint (value, 0);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (object);

    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (object));

    g_free (priv->oper_code);
    g_free (priv->oper_name);

    G_OBJECT_CLASS (mm_generic_gsm_parent_class)->finalize (object);
}

static void
mm_generic_gsm_class_init (MMGenericGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_generic_gsm_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMGenericGsmPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    klass->do_enable = real_do_enable;

    /* Properties */
    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SUPPORTED_BANDS,
                                      MM_MODEM_GSM_CARD_SUPPORTED_BANDS);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SUPPORTED_MODES,
                                      MM_MODEM_GSM_CARD_SUPPORTED_MODES);

    g_object_class_install_property
        (object_class, MM_GENERIC_GSM_PROP_POWER_UP_CMD,
         g_param_spec_string (MM_GENERIC_GSM_POWER_UP_CMD,
                              "PowerUpCommand",
                              "Power up command",
                              "+CFUN=1",
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, MM_GENERIC_GSM_PROP_POWER_DOWN_CMD,
         g_param_spec_string (MM_GENERIC_GSM_POWER_DOWN_CMD,
                              "PowerDownCommand",
                              "Power down command",
                              "+CFUN=0",
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, MM_GENERIC_GSM_PROP_INIT_CMD,
         g_param_spec_string (MM_GENERIC_GSM_INIT_CMD,
                              "InitCommand",
                              "Initialization command",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, MM_GENERIC_GSM_PROP_INIT_CMD_OPTIONAL,
         g_param_spec_string (MM_GENERIC_GSM_INIT_CMD_OPTIONAL,
                              "InitCommandOptional",
                              "Optional initialization command (errors ignored)",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

