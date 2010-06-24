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
 * Copyright (C) 2009 - 2010 Ericsson
 */

#include <config.h>
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
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"
#include "mm-options.h"
#include "mm-properties-changed-signal.h"
#include "mm-utils.h"

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
    gboolean pin_checked;
    guint32 pin_check_tries;
    guint pin_check_timeout;

    MMModemGsmAllowedMode allowed_mode;

    gboolean roam_allowed;

    char *oper_code;
    char *oper_name;
    guint32 ip_method;

    GPtrArray *reg_regex;

    guint poll_id;

    /* CREG and CGREG info */
    gboolean creg_poll;
    gboolean cgreg_poll;
    /* Index 0 for CREG, index 1 for CGREG */
    gulong lac[2];
    gulong cell_id[2];
    MMModemGsmAccessTech act;

    /* Index 0 for CREG, index 1 for CGREG */
    MMModemGsmNetworkRegStatus reg_status[2];
    guint pending_reg_id;
    MMCallbackInfo *pending_reg_info;
    gboolean manual_reg;

    guint signal_quality_id;
    time_t signal_quality_timestamp;
    guint32 signal_quality;
    gint cid;

    guint32 charsets;
    guint32 cur_charset;

    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMQcdmSerialPort *qcdm;
    MMPort *data;
} MMGenericGsmPrivate;

static void get_registration_status (MMAtSerialPort *port, MMCallbackInfo *info);
static void read_operator_code_done (MMAtSerialPort *port,
                                     GString *response,
                                     GError *error,
                                     gpointer user_data);

static void read_operator_name_done (MMAtSerialPort *port,
                                     GString *response,
                                     GError *error,
                                     gpointer user_data);

static void reg_state_changed (MMAtSerialPort *port,
                               GMatchInfo *match_info,
                               gpointer user_data);

static void get_reg_status_done (MMAtSerialPort *port,
                                 GString *response,
                                 GError *error,
                                 gpointer user_data);

static gboolean handle_reg_status_response (MMGenericGsm *self,
                                            GString *response,
                                            GError **error);

static MMModemGsmAccessTech etsi_act_to_mm_act (gint act);

static void _internal_update_access_technology (MMGenericGsm *modem,
                                                MMModemGsmAccessTech act);

static void reg_info_updated (MMGenericGsm *self,
                              gboolean update_rs,
                              MMGenericGsmRegType rs_type,
                              MMModemGsmNetworkRegStatus status,
                              gboolean update_code,
                              const char *oper_code,
                              gboolean update_name,
                              const char *oper_name);

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

gint
mm_generic_gsm_get_cid (MMGenericGsm *modem)
{
    g_return_val_if_fail (MM_IS_GENERIC_GSM (modem), 0);

    return MM_GENERIC_GSM_GET_PRIVATE (modem)->cid;
}

typedef struct {
    const char *result;
    const char *normalized;
    guint code;
} CPinResult;

static CPinResult unlock_results[] = {
    /* Longer entries first so we catch the correct one with strcmp() */
    { "PH-NETSUB PIN", "ph-netsub-pin", MM_MOBILE_ERROR_NETWORK_SUBSET_PIN },
    { "PH-NETSUB PUK", "ph-netsub-puk", MM_MOBILE_ERROR_NETWORK_SUBSET_PUK },
    { "PH-FSIM PIN",   "ph-fsim-pin",   MM_MOBILE_ERROR_PH_FSIM_PIN },
    { "PH-FSIM PUK",   "ph-fsim-puk",   MM_MOBILE_ERROR_PH_FSIM_PUK },
    { "PH-CORP PIN",   "ph-corp-pin",   MM_MOBILE_ERROR_CORP_PIN },
    { "PH-CORP PUK",   "ph-corp-puk",   MM_MOBILE_ERROR_CORP_PUK },
    { "PH-SIM PIN",    "ph-sim-pin",    MM_MOBILE_ERROR_PH_SIM_PIN },
    { "PH-NET PIN",    "ph-net-pin",    MM_MOBILE_ERROR_NETWORK_PIN },
    { "PH-NET PUK",    "ph-net-puk",    MM_MOBILE_ERROR_NETWORK_PUK },
    { "PH-SP PIN",     "ph-sp-pin",     MM_MOBILE_ERROR_SERVICE_PIN },
    { "PH-SP PUK",     "ph-sp-puk",     MM_MOBILE_ERROR_SERVICE_PUK },
    { "SIM PIN2",      "sim-pin2",      MM_MOBILE_ERROR_SIM_PIN2 },
    { "SIM PUK2",      "sim-puk2",      MM_MOBILE_ERROR_SIM_PUK2 },
    { "SIM PIN",       "sim-pin",       MM_MOBILE_ERROR_SIM_PIN },
    { "SIM PUK",       "sim-puk",       MM_MOBILE_ERROR_SIM_PUK },
    { NULL,            NULL,            MM_MOBILE_ERROR_PHONE_FAILURE },
};

static GError *
error_for_unlock_required (const char *unlock)
{
    CPinResult *iter = &unlock_results[0];

    if (!unlock || !strlen (unlock))
        return NULL;

    /* Translate the error */
    while (iter->result) {
        if (!strcmp (iter->normalized, unlock))
            return mm_mobile_error_for_code (iter->code);
        iter++;
    }

    return g_error_new (MM_MOBILE_ERROR,
                        MM_MOBILE_ERROR_UNKNOWN,
                        "Unknown unlock request '%s'", unlock);
}

static void
get_unlock_retries_cb (MMModem *modem,
                       guint32 result,
                       GError *error,
                       gpointer user_data)
{
    if (!error)
        mm_modem_base_set_unlock_retries (MM_MODEM_BASE (modem), result);
    else
        mm_modem_base_set_unlock_retries (MM_MODEM_BASE (modem), MM_MODEM_GSM_CARD_UNLOCK_RETRIES_NOT_SUPPORTED);
}

static void
pin_check_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (response && strstr (response->str, "+CPIN: ")) {
        const char *str = strstr (response->str, "+CPIN: ") + 7;

        if (g_str_has_prefix (str, "READY")) {
            mm_modem_base_set_unlock_required (MM_MODEM_BASE (info->modem), NULL);
            if (MM_MODEM_GSM_CARD_GET_INTERFACE (info->modem)->get_unlock_retries)
                mm_modem_base_set_unlock_retries (MM_MODEM_BASE (info->modem), 0);
            else
                mm_modem_base_set_unlock_retries (MM_MODEM_BASE (info->modem),
                                                  MM_MODEM_GSM_CARD_UNLOCK_RETRIES_NOT_SUPPORTED);
            parsed = TRUE;
        } else {
            CPinResult *iter = &unlock_results[0];

            /* Translate the error */
            while (iter->result) {
                if (g_str_has_prefix (str, iter->result)) {
                    info->error = mm_mobile_error_for_code (iter->code);
                    mm_modem_base_set_unlock_required (MM_MODEM_BASE (info->modem), iter->normalized);
                    mm_modem_gsm_card_get_unlock_retries (MM_MODEM_GSM_CARD (info->modem),
                                                          iter->normalized,
                                                          get_unlock_retries_cb,
                                                          NULL);
                    parsed = TRUE;
                    break;
                }
                iter++;
            }
        }
    }

    if (!parsed) {
        /* Assume unlocked if we don't recognize the pin request result */
        mm_modem_base_set_unlock_required (MM_MODEM_BASE (info->modem), NULL);
        mm_modem_base_set_unlock_retries (MM_MODEM_BASE (info->modem), 0);

        if (!info->error) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Could not parse PIN request response '%s'",
                                       response->str);
        }
    }

    mm_callback_info_schedule (info);
}

static void
check_pin (MMGenericGsm *modem,
           MMModemFn callback,
           gpointer user_data)
{
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_at_serial_port_queue_command (priv->primary, "+CPIN?", 3, pin_check_done, info);
}

static void
get_imei_cb (MMModem *modem,
             const char *result,
             GError *error,
             gpointer user_data)
{
    if (modem) {
        mm_modem_base_set_equipment_identifier (MM_MODEM_BASE (modem), error ? "" : result);
        mm_serial_port_close (MM_SERIAL_PORT (MM_GENERIC_GSM_GET_PRIVATE (modem)->primary));
    }
}

/*****************************************************************************/

static MMModemGsmNetworkRegStatus
gsm_reg_status (MMGenericGsm *self)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */

    if (   priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING)
        return priv->reg_status[0];

    if (   priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING)
        return priv->reg_status[1];

    if (priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING)
        return priv->reg_status[0];

    if (priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING)
        return priv->reg_status[1];

    if (priv->reg_status[0] != MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN)
        return priv->reg_status[0];

    return priv->reg_status[1];
}

void
mm_generic_gsm_update_enabled_state (MMGenericGsm *self,
                                     gboolean stay_connected,
                                     MMModemStateReason reason)
{
    /* While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED.
     */
    if (stay_connected && (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_DISCONNECTING))
        return;

    switch (gsm_reg_status (self)) {
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

    if (priv->primary && priv->data && priv->pin_checked)
        new_valid = TRUE;

    mm_modem_base_set_valid (MM_MODEM_BASE (self), new_valid);
}


static void initial_pin_check_done (MMModem *modem, GError *error, gpointer user_data);

static gboolean
pin_check_again (gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    priv->pin_check_timeout = 0;
    check_pin (self, initial_pin_check_done, NULL);
    return FALSE;
}

static void
initial_pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMGenericGsmPrivate *priv;

    /* modem could have been removed before we get here, in which case
     * 'modem' will be NULL.
     */
    if (!modem)
        return;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));
    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (   error
        && priv->pin_check_tries++ < 3
        && !mm_modem_base_get_unlock_required (MM_MODEM_BASE (modem))) {
        /* Try it again a few times */
        if (priv->pin_check_timeout)
            g_source_remove (priv->pin_check_timeout);
        priv->pin_check_timeout = g_timeout_add_seconds (2, pin_check_again, modem);
    } else {
        priv->pin_checked = TRUE;
        mm_serial_port_close (MM_SERIAL_PORT (priv->primary));
        check_valid (MM_GENERIC_GSM (modem));
    }
}

static void
initial_pin_check (MMGenericGsm *self)
{
    GError *error = NULL;
    MMGenericGsmPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_GSM (self));
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    g_return_if_fail (priv->primary != NULL);

    if (mm_serial_port_open (MM_SERIAL_PORT (priv->primary), &error))
        check_pin (self, initial_pin_check_done, NULL);
    else {
        g_warning ("%s: failed to open serial port: (%d) %s",
                   __func__,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        g_clear_error (&error);

        /* Ensure the modem is still somewhat usable if opening the serial
         * port fails for some reason.
         */
        initial_pin_check_done (MM_MODEM (self), NULL, NULL);
    }
}

static void
initial_imei_check (MMGenericGsm *self)
{
    GError *error = NULL;
    MMGenericGsmPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_GSM (self));
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    g_return_if_fail (priv->primary != NULL);

    if (mm_serial_port_open (MM_SERIAL_PORT (priv->primary), &error)) {
        /* Make sure echoing is off */
        mm_at_serial_port_queue_command (priv->primary, "E0", 3, NULL, NULL);

        /* Get modem's imei number */
        mm_modem_gsm_card_get_imei (MM_MODEM_GSM_CARD (self),
                                    get_imei_cb,
                                    NULL);
    } else {
        g_warning ("%s: failed to open serial port: (%d) %s",
                   __func__,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        g_clear_error (&error);
    }
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
    if (!port) {
        g_warn_if_fail (port != NULL);
        return NULL;
    }

    if (MM_IS_AT_SERIAL_PORT (port)) {
        GPtrArray *array;
        int i;

        mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_new (),
                                               mm_serial_parser_v1_destroy);

        /* Set up CREG unsolicited message handlers */
        array = mm_gsm_creg_regex_get (FALSE);
        for (i = 0; i < array->len; i++) {
            regex = g_ptr_array_index (array, i);

            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, reg_state_changed, self, NULL);
        }
        mm_gsm_creg_regex_destroy (array);

        if (ptype == MM_PORT_TYPE_PRIMARY) {
            priv->primary = MM_AT_SERIAL_PORT (port);
            if (!priv->data) {
                priv->data = port;
                g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            }

            /* Get modem's initial lock/unlock state */
            initial_pin_check (self);

            /* Get modem's IMEI number */
            initial_imei_check (self);

        } else if (ptype == MM_PORT_TYPE_SECONDARY)
            priv->secondary = MM_AT_SERIAL_PORT (port);
    } else if (MM_IS_QCDM_SERIAL_PORT (port)) {
        if (!priv->qcdm)
            priv->qcdm = MM_QCDM_SERIAL_PORT (port);
    } else if (!strcmp (subsys, "net")) {
        /* Net device (if any) is the preferred data port */
        if (!priv->data || MM_IS_AT_SERIAL_PORT (priv->data)) {
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

    if (port == (MMPort *) priv->primary) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->primary = NULL;
    }

    if (port == priv->data) {
        priv->data = NULL;
        g_object_notify (G_OBJECT (modem), MM_MODEM_DATA_DEVICE);
    }

    if (port == (MMPort *) priv->secondary) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->secondary = NULL;
    }

    if (port == (MMPort *) priv->qcdm) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->qcdm = NULL;
    }

    check_valid (MM_GENERIC_GSM (modem));
}

static void
reg_poll_response (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);

    if (!error)
        handle_reg_status_response (self, response, NULL);
}

static void
periodic_signal_quality_cb (MMModem *modem,
                            guint32 result,
                            GError *error,
                            gpointer user_data)
{
    /* Cached signal quality already updated */
}

static void
periodic_access_tech_cb (MMModem *modem,
                         guint32 act,
                         GError *error,
                         gpointer user_data)
{
    if (modem && !error && act)
        mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (modem), act);
}

static gboolean
periodic_poll_cb (gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMAtSerialPort *port;

    port = mm_generic_gsm_get_best_at_port (self, NULL);
    if (!port)
        return TRUE;  /* oh well, try later */

    if (priv->creg_poll)
        mm_at_serial_port_queue_command (port, "+CREG?", 10, reg_poll_response, self);
    if (priv->cgreg_poll)
        mm_at_serial_port_queue_command (port, "+CGREG?", 10, reg_poll_response, self);

    mm_modem_gsm_network_get_signal_quality (MM_MODEM_GSM_NETWORK (self),
                                                periodic_signal_quality_cb,
                                                NULL);

    if (MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology)
        MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology (self, periodic_access_tech_cb, NULL);

    return TRUE;  /* continue running */
}

static void
cgreg1_done (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->modem) {
        if (info->error) {
            MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

            g_clear_error (&info->error);

            /* The modem doesn't like unsolicited CGREG, so we'll need to poll */
            priv->cgreg_poll = TRUE;
        }
        /* Success; get initial state */
        mm_at_serial_port_queue_command (port, "+CGREG?", 10, reg_poll_response, info->modem);
    }
    mm_callback_info_schedule (info);
}

static void
cgreg2_done (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* Ignore errors except modem removal errors */
    info->error = mm_modem_check_removed (info->modem, error);
    if (info->modem) {
        if (info->error) {
            g_clear_error (&info->error);
            /* Try CGREG=1 instead */
            mm_at_serial_port_queue_command (port, "+CGREG=1", 3, cgreg1_done, info);
        } else {
            /* Success; get initial state */
            mm_at_serial_port_queue_command (port, "+CGREG?", 10, reg_poll_response, info->modem);

            /* All done */
            mm_callback_info_schedule (info);
        }
    } else {
        /* Modem got removed */
        mm_callback_info_schedule (info);
    }
}

static void
creg1_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->modem) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        if (info->error) {
            g_clear_error (&info->error);

            /* The modem doesn't like unsolicited CREG, so we'll need to poll */
            priv->creg_poll = TRUE;
        }
        /* Success; get initial state */
        mm_at_serial_port_queue_command (port, "+CREG?", 10, reg_poll_response, info->modem);

        /* Now try to set up CGREG messages */
        mm_at_serial_port_queue_command (port, "+CGREG=2", 3, cgreg2_done, info);
    } else {
        /* Modem got removed */
        mm_callback_info_schedule (info);
    }
}

static void
creg2_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* Ignore errors except modem removal errors */
    info->error = mm_modem_check_removed (info->modem, error);
    if (info->modem) {
        if (info->error) {
            g_clear_error (&info->error);
            mm_at_serial_port_queue_command (port, "+CREG=1", 3, creg1_done, info);
        } else {
            /* Success; get initial state */
            mm_at_serial_port_queue_command (port, "+CREG?", 10, reg_poll_response, info->modem);

            /* Now try to set up CGREG messages */
            mm_at_serial_port_queue_command (port, "+CGREG=2", 3, cgreg2_done, info);
        }
    } else {
        /* Modem got removed */
        mm_callback_info_schedule (info);
    }
}

static void
enable_failed (MMModem *modem, GError *error, MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;

    info->error = mm_modem_check_removed (modem, error);

    if (modem) {
        mm_modem_set_state (modem,
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

        if (priv->primary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->primary)))
            mm_serial_port_close_force (MM_SERIAL_PORT (priv->primary));
        if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary)))
            mm_serial_port_close_force (MM_SERIAL_PORT (priv->secondary));
    }

    mm_callback_info_schedule (info);
}

static guint32 best_charsets[] = {
    MM_MODEM_CHARSET_UTF8,
    MM_MODEM_CHARSET_UCS2,
    MM_MODEM_CHARSET_8859_1,
    MM_MODEM_CHARSET_IRA,
    MM_MODEM_CHARSET_UNKNOWN
};

static void
enabled_set_charset_done (MMModem *modem,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint idx;

    /* only modem removals are really a hard error */
    if (error) {
        if (!modem) {
            enable_failed (modem, error, info);
            return;
        }

        /* Try the next best charset */
        idx = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "best-charset")) + 1;
        if (best_charsets[idx] == MM_MODEM_CHARSET_UNKNOWN) {
            GError *tmp_error;

            /* No more character sets we can use */
            tmp_error = g_error_new_literal (MM_MODEM_ERROR,
                                             MM_MODEM_ERROR_UNSUPPORTED_CHARSET,
                                             "Failed to find a usable modem character set");
            enable_failed (modem, tmp_error, info);
            g_error_free (tmp_error);
        } else {
            /* Send the new charset */
            mm_callback_info_set_data (info, "best-charset", GUINT_TO_POINTER (idx), NULL);
            mm_modem_set_charset (modem, best_charsets[idx], enabled_set_charset_done, info);
        }
    } else {
        /* Modem is now enabled; update the state */
        mm_generic_gsm_update_enabled_state (MM_GENERIC_GSM (modem), FALSE, MM_MODEM_STATE_REASON_NONE);

        /* Set up unsolicited registration notifications */
        mm_at_serial_port_queue_command (MM_GENERIC_GSM_GET_PRIVATE (modem)->primary,
                                         "+CREG=2", 3, creg2_done, info);
    }
}

static void
supported_charsets_done (MMModem *modem,
                         guint32 charsets,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (!modem) {
        enable_failed (modem, error, info);
        return;
    }

    /* Switch the device's charset; we prefer UTF-8, but UCS2 will do too */
    mm_modem_set_charset (modem, MM_MODEM_CHARSET_UTF8, enabled_set_charset_done, info);
}

static void
get_allowed_mode_done (MMModem *modem,
                       MMModemGsmAllowedMode mode,
                       GError *error,
                       gpointer user_data)
{
    if (modem) {
        mm_generic_gsm_update_allowed_mode (MM_GENERIC_GSM (modem),
                                            error ? MM_MODEM_GSM_ALLOWED_MODE_ANY : mode);
    }
}

static void
get_enable_info_done (MMModem *modem,
                      const char *manufacturer,
                      const char *model,
                      const char *version,
                      GError *error,
                      gpointer user_data)
{
    /* Modem base class handles the response for us */
}

void
mm_generic_gsm_enable_complete (MMGenericGsm *self,
                                GError *error,
                                MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (self));
    g_return_if_fail (info != NULL);

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (error) {
        enable_failed ((MMModem *) self, error, info);
        return;
    }

    /* Open the second port here if the modem has one.  We'll use it for
     * signal strength and registration updates when the device is connected,
     * but also many devices will send unsolicited registration or other
     * messages to the secondary port but not the primary.
     */
    if (priv->secondary) {
        if (!mm_serial_port_open (MM_SERIAL_PORT (priv->secondary), &error)) {
            if (mm_options_debug ()) {
                g_warning ("%s: error opening secondary port: (%d) %s",
                           __func__,
                           error ? error->code : -1,
                           error && error->message ? error->message : "(unknown)");
            }
        }
    }

    /* Try to enable XON/XOFF flow control */
    mm_at_serial_port_queue_command (priv->primary, "+IFC=1,1", 3, NULL, NULL);

    /* Grab device info right away */
    mm_modem_get_info (MM_MODEM (self), get_enable_info_done, NULL);

    /* Get allowed mode */
    if (MM_GENERIC_GSM_GET_CLASS (self)->get_allowed_mode)
        MM_GENERIC_GSM_GET_CLASS (self)->get_allowed_mode (self, get_allowed_mode_done, NULL);

    /* And supported character sets */
    mm_modem_get_supported_charsets (MM_MODEM (self), supported_charsets_done, info);
}

static void
real_do_enable_power_up_done (MMGenericGsm *self,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    /* Ignore power-up errors as not all devices actually support CFUN=1 */
    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), NULL, info);
}

static void
enable_done (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Let subclasses handle the power up command response/error; many devices
     * don't support +CFUN, but for those that do let them handle the error
     * correctly.
     */
    g_assert (MM_GENERIC_GSM_GET_CLASS (info->modem)->do_enable_power_up_done);
    MM_GENERIC_GSM_GET_CLASS (info->modem)->do_enable_power_up_done (MM_GENERIC_GSM (info->modem),
                                                                     response,
                                                                     error,
                                                                     info);
}

static void
init_done (MMAtSerialPort *port,
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
     * E0 when it's in the same line as ATZ (Option GIO322).
     */
    mm_at_serial_port_queue_command (port, "E0", 2, NULL, NULL);

    /* Some phones (like Blackberries) don't support +CMEE=1, so make it
     * optional.  It completely violates 3GPP TS 27.007 (9.1) but what can we do...
     */
    mm_at_serial_port_queue_command (port, "+CMEE=1", 2, NULL, NULL);

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_INIT_CMD_OPTIONAL, &cmd, NULL);
    mm_at_serial_port_queue_command (port, cmd, 2, NULL, NULL);
    g_free (cmd);

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_POWER_UP_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_at_serial_port_queue_command (port, cmd, 5, enable_done, user_data);
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
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), cmd, 3, init_done, user_data);
    g_free (cmd);
}

static void
real_do_enable (MMGenericGsm *self, MMModemFn callback, gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    mm_serial_port_flash (MM_SERIAL_PORT (priv->primary), 100, FALSE, enable_flash_done, info);
}

static void
enable (MMModem *modem,
        MMModemFn callback,
        gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    GError *error = NULL;
    const char *unlock;

    /* If the device needs a PIN, deal with that now, but we don't care
     * about SIM-PIN2/SIM-PUK2 since the device is operational without it.
     */
    unlock = mm_modem_base_get_unlock_required (MM_MODEM_BASE (modem));
    if (unlock && strcmp (unlock, "sim-puk2") && strcmp (unlock, "sim-pin2")) {
        MMCallbackInfo *info;

        info = mm_callback_info_new (modem, callback, user_data);
        info->error = error_for_unlock_required (unlock);
        mm_callback_info_schedule (info);
        return;
    }

    /* First, reset the previously used CID */
    priv->cid = -1;

    if (!mm_serial_port_open (MM_SERIAL_PORT (priv->primary), &error)) {
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
disable_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMGenericGsm *self = MM_GENERIC_GSM (info->modem);

        mm_serial_port_close_force (MM_SERIAL_PORT (port));
        mm_modem_set_state (MM_MODEM (info->modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        /* Clear out circuit-switched registration info... */
        reg_info_updated (self,
                          MM_GENERIC_GSM_REG_TYPE_CS,
                          TRUE, MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN,
                          TRUE, NULL,
                          TRUE, NULL);
        /* ...and packet-switched registration info */
        reg_info_updated (self,
                          MM_GENERIC_GSM_REG_TYPE_PS,
                          TRUE, MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN,
                          TRUE, NULL,
                          TRUE, NULL);
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

    /* Disable unsolicited messages */
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "AT+CREG=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "AT+CGREG=0", 3, NULL, NULL);

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_POWER_DOWN_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), cmd, 5, disable_done, user_data);
    else
        disable_done (MM_AT_SERIAL_PORT (port), NULL, NULL, user_data);
    g_free (cmd);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMModemState state;

    /* First, reset the previously used CID and clean up registration */
    g_warn_if_fail (priv->cid == -1);
    priv->cid = -1;

    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    if (priv->poll_id) {
        g_source_remove (priv->poll_id);
        priv->poll_id = 0;
    }

    if (priv->signal_quality_id) {
        g_source_remove (priv->signal_quality_id);
        priv->signal_quality_id = 0;
    }

    if (priv->pin_check_timeout) {
        g_source_remove (priv->pin_check_timeout);
        priv->pin_check_timeout = 0;
    }

    priv->lac[0] = 0;
    priv->lac[1] = 0;
    priv->cell_id[0] = 0;
    priv->cell_id[1] = 0;
    _internal_update_access_technology (self, MM_MODEM_GSM_ACCESS_TECH_UNKNOWN);

    /* Close the secondary port if its open */
    if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary)))
        mm_serial_port_close_force (MM_SERIAL_PORT (priv->secondary));

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
        mm_serial_port_flash (MM_SERIAL_PORT (priv->primary), 1000, TRUE, disable_flash_done, info);
    else
        disable_flash_done (MM_SERIAL_PORT (priv->primary), NULL, info);
}

static void
get_string_done (MMAtSerialPort *port,
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
get_mnc_length_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int sw1, sw2;
    const char *imsi;
    gboolean success = FALSE;
    char hex[51];
    char *bin;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    memset (hex, 0, sizeof (hex));
    if (sscanf (response->str, "+CRSM:%d,%d,\"%50c\"", &sw1, &sw2, (char *) &hex) == 3)
        success = TRUE;
    else {
        /* May not include quotes... */
        if (sscanf (response->str, "+CRSM:%d,%d,%50c", &sw1, &sw2, (char *) &hex) == 3)
            success = TRUE;
    }

    if (!success) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the CRSM response");
        goto done;
    }

    if ((sw1 == 0x90 && sw2 == 0x00) || (sw1 == 0x91) || (sw1 == 0x92) || (sw1 == 0x9f)) {
        gsize buflen = 0;
        guint32 mnc_len;

        /* Make sure the buffer is only hex characters */
        while (buflen < sizeof (hex) && hex[buflen]) {
            if (!isxdigit (hex[buflen])) {
                hex[buflen] = 0x0;
                break;
            }
            buflen++;
        }

        /* Convert hex string to binary */
        bin = utils_hexstr2bin (hex, &buflen);
        if (!bin || buflen < 4) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "SIM returned malformed response '%s'",
                                       hex);
            goto done;
        }

        /* MNC length is byte 4 of this SIM file */
        mnc_len = bin[3] & 0xFF;
        if (mnc_len == 2 || mnc_len == 3) {
            imsi = mm_callback_info_get_data (info, "imsi");
            mm_callback_info_set_result (info, g_strndup (imsi, 3 + mnc_len), g_free);
        } else {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "SIM returned invalid MNC length %d (should be either 2 or 3)",
                                       mnc_len);
        }
    } else {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                                   sw1, sw2);
    }

done:
    mm_callback_info_schedule (info);
}

static void
get_operator_id_imsi_done (MMModem *modem,
                           const char *result,
                           GError *error,
                           gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    mm_callback_info_set_data (info, "imsi", g_strdup (result), g_free);

    /* READ BINARY of EFad (Administrative Data) ETSI 51.011 section 10.3.18 */
    mm_at_serial_port_queue_command_cached (priv->primary,
                                            "+CRSM=176,28589,0,0,4",
                                            3,
                                            get_mnc_length_done,
                                            info);
}

static void
get_imei (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_at_serial_port_queue_command_cached (priv->primary, "+CGSN", 3, get_string_done, info);
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_at_serial_port_queue_command_cached (priv->primary, "+CIMI", 3, get_string_done, info);
}

static void
get_operator_id (MMModemGsmCard *modem,
                 MMModemStringFn callback,
                 gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_modem_gsm_card_get_imsi (MM_MODEM_GSM_CARD (modem),
                                get_operator_id_imsi_done,
                                info);
}

static void
get_card_info (MMModem *modem,
               MMModemInfoFn callback,
               gpointer user_data)
{
    MMAtSerialPort *port;
    GError *error = NULL;

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &error);
    mm_modem_base_get_card_info (MM_MODEM_BASE (modem), port, error, callback, user_data);
    g_clear_error (&error);
}

#define PIN_PORT_TAG "pin-port"

static void
pin_puk_recheck_done (MMModem *modem, GError *error, gpointer user_data);

static gboolean
pin_puk_recheck_again (gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    MM_GENERIC_GSM_GET_PRIVATE (info->modem)->pin_check_timeout = 0;
    check_pin (MM_GENERIC_GSM (info->modem), pin_puk_recheck_done, info);
    return FALSE;
}

static void
pin_puk_recheck_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMSerialPort *port;

    /* Clear the pin check timeout to ensure that it won't ever get a
     * stale MMCallbackInfo if the modem got removed.  We'll reschedule it here
     * anyway if needed.
     */
    if (info->modem) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        if (priv->pin_check_timeout)
            g_source_remove (priv->pin_check_timeout);
        priv->pin_check_timeout = 0;
    }

    /* modem could have been removed before we get here, in which case
     * 'modem' will be NULL.
     */
    info->error = mm_modem_check_removed (modem, error);

    /* If the modem wasn't removed, and the modem isn't ready yet, ask it for
     * the current PIN status a few times since some devices take a bit to fully
     * enable themselves after a SIM PIN/PUK unlock.
     */
    if (   info->modem
        && info->error
        && !g_error_matches (info->error, MM_MODEM_ERROR, MM_MODEM_ERROR_REMOVED)) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        if (priv->pin_check_tries < 4) {
            g_clear_error (&info->error);
            priv->pin_check_tries++;
            priv->pin_check_timeout = g_timeout_add_seconds (2, pin_puk_recheck_again, info);
            return;
        }
    }

    /* Otherwise, clean up and return the PIN check result */
    port = mm_callback_info_get_data (info, PIN_PORT_TAG);
    if (modem && port)
        mm_serial_port_close (port);

    mm_callback_info_schedule (info);
}

static void
send_puk_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        mm_serial_port_close (MM_SERIAL_PORT (port));
        return;
    }

    /* Get latest PIN status */
    MM_GENERIC_GSM_GET_PRIVATE (info->modem)->pin_check_tries = 0;
    check_pin (MM_GENERIC_GSM (info->modem), pin_puk_recheck_done, info);
}

static void
send_puk (MMModemGsmCard *modem,
          const char *puk,
          const char *pin,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    /* Ensure we have a usable port to use for the unlock */
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
    mm_callback_info_set_data (info, PIN_PORT_TAG, port, NULL);

    command = g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin);
    mm_at_serial_port_queue_command (port, command, 3, send_puk_done, info);
    g_free (command);
}

static void
send_pin_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        mm_serial_port_close (MM_SERIAL_PORT (port));
        return;
    }

    /* Get latest PIN status */
    MM_GENERIC_GSM_GET_PRIVATE (info->modem)->pin_check_tries = 0;
    check_pin (MM_GENERIC_GSM (info->modem), pin_puk_recheck_done, info);
}

static void
send_pin (MMModemGsmCard *modem,
          const char *pin,
          MMModemFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    /* Ensure we have a usable port to use for the unlock */
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
    mm_callback_info_set_data (info, PIN_PORT_TAG, port, NULL);

    command = g_strdup_printf ("+CPIN=\"%s\"", pin);
    mm_at_serial_port_queue_command (port, command, 3, send_pin_done, info);
    g_free (command);
}

static void
enable_pin_done (MMAtSerialPort *port,
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
    mm_at_serial_port_queue_command (priv->primary, command, 3, enable_pin_done, info);
    g_free (command);
}

static void
change_pin_done (MMAtSerialPort *port,
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
    mm_at_serial_port_queue_command (priv->primary, command, 3, change_pin_done, info);
    g_free (command);
}

static void
get_unlock_retries (MMModemGsmCard *modem,
                    const char *pin_type,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMCallbackInfo *info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    mm_callback_info_set_result (info,
                                 GUINT_TO_POINTER (MM_MODEM_GSM_CARD_UNLOCK_RETRIES_NOT_SUPPORTED),
                                 NULL);

    mm_callback_info_schedule (info);
}

static void
reg_info_updated (MMGenericGsm *self,
                  gboolean update_rs,
                  MMGenericGsmRegType rs_type,
                  MMModemGsmNetworkRegStatus status,
                  gboolean update_code,
                  const char *oper_code,
                  gboolean update_name,
                  const char *oper_name)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMModemGsmNetworkRegStatus old_status;
    gboolean changed = FALSE;

    if (update_rs) {
        g_return_if_fail (   rs_type == MM_GENERIC_GSM_REG_TYPE_CS
                          || rs_type == MM_GENERIC_GSM_REG_TYPE_PS);

        old_status = gsm_reg_status (self);
        priv->reg_status[rs_type - 1] = status;
        if (gsm_reg_status (self) != old_status)
            changed = TRUE;
    }

    if (update_code) {
        if (g_strcmp0 (oper_code, priv->oper_code) != 0) {
            g_free (priv->oper_code);
            priv->oper_code = g_strdup (oper_code);
            changed = TRUE;
        }
    }

    if (update_name) {
        if (g_strcmp0 (oper_name, priv->oper_name) != 0) {
            g_free (priv->oper_name);
            priv->oper_name = g_strdup (oper_name);
            changed = TRUE;
        }
    }

    if (changed) {
        mm_modem_gsm_network_registration_info (MM_MODEM_GSM_NETWORK (self),
                                                gsm_reg_status (self),
                                                priv->oper_code,
                                                priv->oper_name);
    }
}

static void
convert_operator_from_ucs2 (char **operator)
{
    const char *p;
    char *converted;
    size_t len;

    g_return_if_fail (operator != NULL);
    g_return_if_fail (*operator != NULL);

    p = *operator;
    len = strlen (p);

    /* Len needs to be a multiple of 4 for UCS2 */
    if ((len < 4) || ((len % 4) != 0))
        return;

    while (*p) {
        if (!isxdigit (*p++))
            return;
    }

    converted = mm_modem_charset_hex_to_utf8 (*operator, MM_MODEM_CHARSET_UCS2);
    if (converted) {
        g_free (*operator);
        *operator = converted;
    }
}

static char *
parse_operator (const char *reply, MMModemCharset cur_charset)
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

    /* Some modems (Option & HSO) return the operator name as a hexadecimal
     * string of the bytes of the operator name as encoded by the current
     * character set.
     */
    if (operator && (cur_charset == MM_MODEM_CHARSET_UCS2))
        convert_operator_from_ucs2 (&operator);

    return operator;
}

static void
read_operator_code_done (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    char *oper;

    if (!error) {
        oper = parse_operator (response->str, MM_MODEM_CHARSET_UNKNOWN);
        if (oper) {
            reg_info_updated (self, FALSE, MM_GENERIC_GSM_REG_TYPE_UNKNOWN, 0,
                              TRUE, oper,
                              FALSE, NULL);
        }
    }
}

static void
read_operator_name_done (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    char *oper;

    if (!error) {
        oper = parse_operator (response->str, priv->cur_charset);
        if (oper) {
            reg_info_updated (self, FALSE, MM_GENERIC_GSM_REG_TYPE_UNKNOWN, 0,
                              FALSE, NULL,
                              TRUE, oper);
        }
    }
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

static void
got_signal_quality (MMModem *modem,
                    guint32 quality,
                    GError *error,
                    gpointer user_data)
{
    mm_generic_gsm_update_signal_quality (MM_GENERIC_GSM (modem), quality);
}

static void
roam_disconnect_done (MMModem *modem,
                      GError *error,
                      gpointer user_data)
{
    g_message ("Disconnected because roaming is not allowed");
}

static void
get_reg_act_done (MMModem *modem,
                  guint32 act,
                  GError *error,
                  gpointer user_data)
{
    if (modem && !error && act)
        mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (modem), act);
}

void
mm_generic_gsm_set_reg_status (MMGenericGsm *self,
                               MMGenericGsmRegType rs_type,
                               MMModemGsmNetworkRegStatus status)
{
    MMGenericGsmPrivate *priv;
    MMAtSerialPort *port;

    g_return_if_fail (MM_IS_GENERIC_GSM (self));

    g_return_if_fail (   rs_type == MM_GENERIC_GSM_REG_TYPE_CS
                      || rs_type == MM_GENERIC_GSM_REG_TYPE_PS);

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (priv->reg_status[rs_type - 1] == status)
        return;

    g_debug ("%s registration state changed: %d",
             (rs_type == MM_GENERIC_GSM_REG_TYPE_CS) ? "CS" : "PS",
             status);
    priv->reg_status[rs_type - 1] = status;

    port = mm_generic_gsm_get_best_at_port (self, NULL);

    if (status == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME ||
        status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {

        /* If we're connected and we're not supposed to roam, but the device
         * just roamed, disconnect the connection to avoid charging the user
         * loads of money.
         */
        if (   (status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING)
            && (mm_modem_get_state (MM_MODEM (self)) == MM_MODEM_STATE_CONNECTED)
            && (priv->roam_allowed == FALSE)) {
            mm_modem_disconnect (MM_MODEM (self), roam_disconnect_done, NULL);
        } else {
            /* Grab the new operator name and MCC/MNC */
            if (port) {
                mm_at_serial_port_queue_command (port, "+COPS=3,2;+COPS?", 3, read_operator_code_done, self);
                mm_at_serial_port_queue_command (port, "+COPS=3,0;+COPS?", 3, read_operator_name_done, self);
            }

            /* And update signal quality and access technology */
            mm_modem_gsm_network_get_signal_quality (MM_MODEM_GSM_NETWORK (self), got_signal_quality, NULL);
            if (MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology)
                MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology (self, get_reg_act_done, NULL);
        }
    } else
        reg_info_updated (self, FALSE, rs_type, 0, TRUE, NULL, TRUE, NULL);

    mm_generic_gsm_update_enabled_state (self, TRUE, MM_MODEM_STATE_REASON_NONE);
}

/* Returns TRUE if the modem is "done", ie has registered or been denied */
static gboolean
reg_status_updated (MMGenericGsm *self,
                    MMGenericGsmRegType rs_type,
                    int new_value,
                    GError **error)
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

    mm_generic_gsm_set_reg_status (self, rs_type, status);

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

static MMGenericGsmRegType
cgreg_to_reg_type (gboolean cgreg)
{
    return (cgreg ? MM_GENERIC_GSM_REG_TYPE_PS : MM_GENERIC_GSM_REG_TYPE_CS);
}

static void
reg_state_changed (MMAtSerialPort *port,
                   GMatchInfo *match_info,
                   gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    guint32 state = 0, idx;
    gulong lac = 0, cell_id = 0;
    gint act = -1;
    gboolean cgreg = FALSE;
    GError *error = NULL;

    if (!mm_gsm_parse_creg_response (match_info, &state, &lac, &cell_id, &act, &cgreg, &error)) {
        if (mm_options_debug ()) {
            g_warning ("%s: error parsing unsolicited registration: %s",
                       __func__,
                       error && error->message ? error->message : "(unknown)");
        }
        return;
    }

    if (reg_status_updated (self, cgreg_to_reg_type (cgreg), state, NULL)) {
        /* If registration is finished (either registered or failed) but the
         * registration query hasn't completed yet, just remove the timeout and
         * let the registration query complete.
         */
        if (priv->pending_reg_id) {
            g_source_remove (priv->pending_reg_id);
            priv->pending_reg_id = 0;
        }
    }

    idx = cgreg ? 1 : 0;
    priv->lac[idx] = lac;
    priv->cell_id[idx] = cell_id;

    /* Only update access technology if it appeared in the CREG/CGREG response */
    if (act != -1)
        mm_generic_gsm_update_access_technology (self, etsi_act_to_mm_act (act));
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

static gboolean
handle_reg_status_response (MMGenericGsm *self,
                            GString *response,
                            GError **error)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GMatchInfo *match_info;
    guint32 status = 0, idx;
    gulong lac = 0, ci = 0;
    gint act = -1;
    gboolean cgreg = FALSE;
    guint i;

    /* Try to match the response */
    for (i = 0; i < priv->reg_regex->len; i++) {
        GRegex *r = g_ptr_array_index (priv->reg_regex, i);

        if (g_regex_match (r, response->str, 0, &match_info))
            break;
        g_match_info_free (match_info);
        match_info = NULL;
    }

    if (!match_info) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Unknown registration status response");
        return FALSE;
    }

    /* And parse it */
    if (!mm_gsm_parse_creg_response (match_info, &status, &lac, &ci, &act, &cgreg, error)) {
        g_match_info_free (match_info);
        return FALSE;
    }

    /* Success; update cached location information */
    idx = cgreg ? 1 : 0;
    priv->lac[idx] = lac;
    priv->cell_id[idx] = ci;

    /* Only update access technology if it appeared in the CREG/CGREG response */
    if (act != -1)
        mm_generic_gsm_update_access_technology (self, etsi_act_to_mm_act (act));

    if (status >= 0) {
        /* Update cached registration status */
        reg_status_updated (self, cgreg_to_reg_type (cgreg), status, NULL);
    }

    return TRUE;
}

#define CGREG_TRIED_TAG "cgreg-tried"

static void
get_reg_status_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    guint id;
    MMModemGsmNetworkRegStatus status;

    /* This function should only get called during the connect sequence when
     * polling for registration state, since explicit registration requests
     * from D-Bus clients are filled from the cached registration state.
     */
    g_return_if_fail (info == priv->pending_reg_info);

    if (error) {
        gboolean cgreg_tried = !!mm_callback_info_get_data (info, CGREG_TRIED_TAG);

        /* If this was a +CREG error, try +CGREG.  Some devices (blackberries)
         * respond to +CREG with an error but return a valid +CGREG response.
         * So try both.  If we get an error from both +CREG and +CGREG, that's
         * obviously a hard fail.
         */
        if (cgreg_tried == FALSE) {
            mm_callback_info_set_data (info, CGREG_TRIED_TAG, GUINT_TO_POINTER (TRUE), NULL);
            mm_at_serial_port_queue_command (port, "+CGREG?", 10, get_reg_status_done, info);
            return;
        } else {
            info->error = g_error_copy (error);
            goto reg_done;
        }
    }

    /* The unsolicited registration state handlers will intercept the CREG
     * response and update the cached registration state for us, so we usually
     * just need to check the cached state here.
     */

    if (strlen (response->str)) {
        /* But just in case the unsolicited handlers doesn't do it... */
        if (!handle_reg_status_response (self, response, &info->error))
            goto reg_done;
    }

    status = gsm_reg_status (self);
    if (   status != MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        && status != MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING
        && status != MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED) {
        /* If we're still waiting for automatic registration to complete or
         * fail, check again in a few seconds.
         */
        id = g_timeout_add_seconds (1, reg_status_again, info);
        mm_callback_info_set_data (info, REG_STATUS_AGAIN_TAG,
                                    GUINT_TO_POINTER (id),
                                    reg_status_again_remove);
        return;
    }

reg_done:
    /* This will schedule the pending registration's the callback for us */
    mm_generic_gsm_pending_registration_stop (self);
}

static void
get_registration_status (MMAtSerialPort *port, MMCallbackInfo *info)
{
    mm_at_serial_port_queue_command (port, "+CREG?", 10, get_reg_status_done, info);
}

static void
register_done (MMAtSerialPort *port,
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

        /* Don't use cached registration state here since it could be up to
         * 30 seconds old.  Get fresh registration state.
         */
        get_registration_status (port, info);
    }
}

static gboolean
registration_timed_out (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    g_warn_if_fail (info == priv->pending_reg_info);

    /* Clear out circuit-switched registration info... */
    reg_info_updated (MM_GENERIC_GSM (info->modem),
                      TRUE, MM_GENERIC_GSM_REG_TYPE_CS, MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE,
                      TRUE, NULL,
                      TRUE, NULL);
    /* ... and packet-switched registration info */
    reg_info_updated (MM_GENERIC_GSM (info->modem),
                      TRUE, MM_GENERIC_GSM_REG_TYPE_PS, MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE,
                      TRUE, NULL,
                      TRUE, NULL);

    info->error = mm_mobile_error_for_code (MM_MOBILE_ERROR_NETWORK_TIMEOUT);
    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (info->modem));

    return FALSE;
}

static gboolean
reg_is_idle (MMModemGsmNetworkRegStatus status)
{
    if (   status == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING
        || status == MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING)
        return FALSE;
    return TRUE;
}

static void
do_register (MMModemGsmNetwork *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    char *command = NULL;

    /* Clear any previous registration */
    mm_generic_gsm_pending_registration_stop (self);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    priv->pending_reg_id = g_timeout_add_seconds (60, registration_timed_out, info);
    priv->pending_reg_info = info;

    /* If the user sent a specific network to use, lock it in.  If no specific
     * network was given, and the modem is not registered and not searching,
     * kick it to search for a network.  Also do auto registration if the modem
     * had been set to manual registration last time but now is not.
     */
    if (network_id) {
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", network_id);
        priv->manual_reg = TRUE;
    } else if (reg_is_idle (gsm_reg_status (self)) || priv->manual_reg) {
        command = g_strdup ("+COPS=0,,");
        priv->manual_reg = FALSE;
    }

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

    if (command) {
        mm_at_serial_port_queue_command (priv->primary, command, 120, register_done, info);
        g_free (command);
    } else
        register_done (priv->primary, NULL, NULL, info);
}

static void
gsm_network_reg_info_invoke (MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    MMModemGsmNetworkRegInfoFn callback = (MMModemGsmNetworkRegInfoFn) info->callback;

    callback (MM_MODEM_GSM_NETWORK (info->modem),
              gsm_reg_status (MM_GENERIC_GSM (info->modem)),
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
    /* Registration info updates are handled internally either by unsolicited
     * updates or by polling.  Thus just return the cached registration state.
     */
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
connect_report_done (MMAtSerialPort *port,
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
connect_done (MMAtSerialPort *port,
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
        mm_at_serial_port_queue_command (priv->primary, "+CEER", 3, connect_report_done, info);
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

    mm_at_serial_port_queue_command (priv->primary, command, 60, connect_done, info);
    g_free (command);
}

static void
disconnect_done (MMModem *modem,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemState prev_state;

    info->error = mm_modem_check_removed (modem, error);
    if (info->error) {
        if (info->modem && modem) {
            /* Reset old state since the operation failed */
            prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_GSM_PREV_STATE_TAG));
            mm_modem_set_state (MM_MODEM (info->modem),
                                prev_state,
                                MM_MODEM_STATE_REASON_NONE);
        }
    } else {
        MMGenericGsm *self = MM_GENERIC_GSM (modem);
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

        mm_port_set_connected (priv->data, FALSE);
        priv->cid = -1;
        mm_generic_gsm_update_enabled_state (self, FALSE, MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
disconnect_all_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
disconnect_send_cgact (MMAtSerialPort *port,
                       gint cid,
                       MMAtSerialResponseFn callback,
                       gpointer user_data)
{
    char *command;

    if (cid >= 0)
        command = g_strdup_printf ("+CGACT=0,%d", cid);
    else {
        /* Disable all PDP contexts */
        command = g_strdup_printf ("+CGACT=0");
    }

    mm_at_serial_port_queue_command (port, command, 3, callback, user_data);
    g_free (command);
}

#define DISCONNECT_CGACT_DONE_TAG "disconnect-cgact-done"

static void
disconnect_flash_done (MMSerialPort *port,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter.  Other errors are hard errors.
         */
        if (   !g_error_matches (info->error, MM_MODEM_CONNECT_ERROR, MM_MODEM_CONNECT_ERROR_NO_CARRIER)
            && !g_error_matches (info->error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_FLASH_FAILED)) {
            mm_callback_info_schedule (info);
            return;
        }
        g_clear_error (&info->error);
    }

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    mm_port_set_connected (priv->data, FALSE);

    /* Don't bother doing the CGACT again if it was done on a secondary port */
    if (mm_callback_info_get_data (info, DISCONNECT_CGACT_DONE_TAG))
        disconnect_all_done (MM_AT_SERIAL_PORT (priv->primary), NULL, NULL, info);
    else {
        disconnect_send_cgact (MM_AT_SERIAL_PORT (priv->primary),
                               priv->cid,
                               disconnect_all_done,
                               info);
    }
}

static void
disconnect_secondary_cgact_done (MMAtSerialPort *port,
                                 GString *response,
                                 GError *error,
                                 gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsm *self;
    MMGenericGsmPrivate *priv;

    if (!info->modem) {
        info->error = mm_modem_check_removed (info->modem, error);
        mm_callback_info_schedule (info);
        return;
    }

    self = MM_GENERIC_GSM (info->modem);
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    /* Now that we've tried deactivating the PDP context on the secondary
     * port, continue with flashing the primary port.
     */
    if (!error)
        mm_callback_info_set_data (info, DISCONNECT_CGACT_DONE_TAG, GUINT_TO_POINTER (TRUE), NULL);

    mm_serial_port_flash (MM_SERIAL_PORT (priv->primary), 1000, TRUE, disconnect_flash_done, info);
}

static void
real_do_disconnect (MMGenericGsm *self,
                    gint cid,
                    MMModemFn callback,
                    gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    /* If the primary port is connected (with PPP) then try sending the PDP
     * context deactivation on the secondary port because not all modems will
     * respond to flashing (since either the modem or the kernel's serial
     * driver doesn't support it).
     */
    if (   mm_port_get_connected (MM_PORT (priv->primary))
        && priv->secondary
        && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary))) {
        disconnect_send_cgact (MM_AT_SERIAL_PORT (priv->secondary),
                               priv->cid,
                               disconnect_secondary_cgact_done,
                               info);
    } else {
        /* Just flash the primary port */
        mm_serial_port_flash (MM_SERIAL_PORT (priv->primary), 1000, TRUE, disconnect_flash_done, info);
    }
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMModemState state;

    priv->roam_allowed = TRUE;

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_GSM_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    mm_modem_set_state (modem, MM_MODEM_STATE_DISCONNECTING, MM_MODEM_STATE_REASON_NONE);

    g_assert (MM_GENERIC_GSM_GET_CLASS (self)->do_disconnect);
    MM_GENERIC_GSM_GET_CLASS (self)->do_disconnect (self, priv->cid, disconnect_done, info);
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
scan_done (MMAtSerialPort *port,
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

    mm_at_serial_port_queue_command (priv->primary, "+COPS=?", 120, scan_done, info);
}

/* SetApn */

#define APN_CID_TAG "generic-gsm-cid"

static void
set_apn_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        priv->cid = GPOINTER_TO_INT (mm_callback_info_get_data (info, APN_CID_TAG));
    }

    mm_callback_info_schedule (info);
}

static void
cid_range_read (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint32 cid = 0;

    if (error)
        info->error = g_error_copy (error);
    else if (g_str_has_prefix (response->str, "+CGDCONT:")) {
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

        mm_callback_info_set_data (info, APN_CID_TAG, GINT_TO_POINTER (cid), NULL);

        command = g_strdup_printf ("+CGDCONT=%d,\"IP\",\"%s\"", cid, apn);
        mm_at_serial_port_queue_command (port, command, 3, set_apn_done, info);
        g_free (command);
    }
}

static void
existing_apns_read (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean found = FALSE;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;
    else if (g_str_has_prefix (response->str, "+CGDCONT:")) {
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
                    MM_GENERIC_GSM_GET_PRIVATE (info->modem)->cid = num_cid;
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

done:
    if (found || info->error)
        mm_callback_info_schedule (info);
    else {
        /* APN not configured on the card. Get the allowed CID range */
        mm_at_serial_port_queue_command_cached (port, "+CGDCONT=?", 3, cid_range_read, info);
    }
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
    mm_at_serial_port_queue_command (priv->primary, "+CGDCONT?", 3, existing_apns_read, info);
}

/* GetSignalQuality */

static gboolean
emit_signal_quality_change (gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    priv->signal_quality_id = 0;
    priv->signal_quality_timestamp = time (NULL);
    mm_modem_gsm_network_signal_quality (MM_MODEM_GSM_NETWORK (self), priv->signal_quality);
    return FALSE;
}

void
mm_generic_gsm_update_signal_quality (MMGenericGsm *self, guint32 quality)
{
    MMGenericGsmPrivate *priv;
    guint delay = 0;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (self));
    g_return_if_fail (quality <= 100);

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (priv->signal_quality == quality)
        return;

    priv->signal_quality = quality;

    /* Some modems will send unsolcited signal quality changes quite often,
     * so rate-limit them to every few seconds.  Track the last time we
     * emitted signal quality so that we send the signal immediately if there
     * haven't been any updates in a while.
     */
    if (!priv->signal_quality_id) {
        if (priv->signal_quality_timestamp > 0) {
            time_t curtime;
            long int diff;

            curtime = time (NULL);
            diff = curtime - priv->signal_quality_timestamp;
            if (diff == 0) {
                /* If the device is sending more than one update per second,
                 * make sure we don't spam clients with signals.
                 */
                delay = 3;
            } else if ((diff > 0) && (diff <= 3)) {
                /* Emitted an update less than 3 seconds ago; schedule an update
                 * 3 seconds after the previous one.
                 */
                delay = (guint) diff;
            } else {
                /* Otherwise, we haven't emitted an update in the last 3 seconds,
                 * or the user turned their clock back, or something like that.
                 */
                delay = 0;
            }
        }

        priv->signal_quality_id = g_timeout_add_seconds (delay,
                                                         emit_signal_quality_change,
                                                         self);
    }
}

static void
get_signal_quality_done (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;
    gboolean parsed = FALSE;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    if (!strncmp (reply, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        if (sscanf (reply + 6, "%d, %d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality == 99) {
                info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                                   MM_MOBILE_ERROR_NO_NETWORK,
                                                   "No service");
            } else {
                /* Normalize the quality */
                quality = CLAMP (quality, 0, 31) * 100 / 31;

                mm_generic_gsm_update_signal_quality (MM_GENERIC_GSM (info->modem), quality);
                mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
            }
            parsed = TRUE;
        }
    }

    if (!parsed && !info->error) {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse signal quality results");
    }

done:
    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMModemGsmNetwork *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), NULL);
    if (port)
        mm_at_serial_port_queue_command (port, "+CSQ", 3, get_signal_quality_done, info);
    else {
        /* Use cached signal quality */
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->signal_quality), NULL);
        mm_callback_info_schedule (info);
    }
}

/*****************************************************************************/

typedef struct {
    MMModemGsmAccessTech mm_act;
    gint etsi_act;
} ModeEtsi;

static ModeEtsi modes_table[] = {
    { MM_MODEM_GSM_ACCESS_TECH_GSM,         0 },
    { MM_MODEM_GSM_ACCESS_TECH_GSM_COMPACT, 1 },
    { MM_MODEM_GSM_ACCESS_TECH_UMTS,        2 },
    { MM_MODEM_GSM_ACCESS_TECH_EDGE,        3 },
    { MM_MODEM_GSM_ACCESS_TECH_HSDPA,       4 },
    { MM_MODEM_GSM_ACCESS_TECH_HSUPA,       5 },
    { MM_MODEM_GSM_ACCESS_TECH_HSPA,        6 },
    { MM_MODEM_GSM_ACCESS_TECH_HSPA,        7 },  /* E-UTRAN/LTE => HSPA for now */
    { MM_MODEM_GSM_ACCESS_TECH_UNKNOWN,    -1 },
};

static MMModemGsmAccessTech
etsi_act_to_mm_act (gint act)
{
    ModeEtsi *iter = &modes_table[0];

    while (iter->mm_act != MM_MODEM_GSM_ACCESS_TECH_UNKNOWN) {
        if (iter->etsi_act == act)
            return iter->mm_act;
        iter++;
    }
    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static void
_internal_update_access_technology (MMGenericGsm *modem,
                                    MMModemGsmAccessTech act)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (modem != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (modem));
    g_return_if_fail (act >= MM_MODEM_GSM_ACCESS_TECH_UNKNOWN && act <= MM_MODEM_GSM_ACCESS_TECH_LAST);

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (act != priv->act) {
        MMModemDeprecatedMode old_mode;

        priv->act = act;
        g_object_notify (G_OBJECT (modem), MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY);

        /* Deprecated value */
        old_mode = mm_modem_gsm_network_act_to_old_mode (act);
        g_signal_emit_by_name (G_OBJECT (modem), "network-mode", old_mode);
    }
}

void
mm_generic_gsm_update_access_technology (MMGenericGsm *self,
                                         MMModemGsmAccessTech act)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (self));

    /* For plugins, don't update the access tech when the modem isn't enabled */
    if (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_ENABLED)
        _internal_update_access_technology (self, act);
}

void
mm_generic_gsm_update_allowed_mode (MMGenericGsm *self,
                                    MMModemGsmAllowedMode mode)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_GSM (self));

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (mode != priv->allowed_mode) {
        priv->allowed_mode = mode;
        g_object_notify (G_OBJECT (self), MM_MODEM_GSM_NETWORK_ALLOWED_MODE);
    }
}

static void
set_allowed_mode_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMModemGsmAllowedMode mode = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "mode"));

        mm_generic_gsm_update_allowed_mode (MM_GENERIC_GSM (info->modem), mode);
    }

    mm_callback_info_schedule (info);
}

static void
set_allowed_mode (MMModemGsmNetwork *net,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (net);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        if (!MM_GENERIC_GSM_GET_CLASS (self)->set_allowed_mode) {
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                               "Operation not supported");
        } else {
            mm_callback_info_set_data (info, "mode", GUINT_TO_POINTER (mode), NULL);
            MM_GENERIC_GSM_GET_CLASS (self)->set_allowed_mode (self, mode, set_allowed_mode_done, info);
        }
        break;
    default:
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Invalid mode.");
        break;
    }

    if (info->error)
        mm_callback_info_schedule (info);
}

/*****************************************************************************/
/* Charset stuff */

static void
get_charsets_done (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        mm_callback_info_schedule (info);
        return;
    }

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    priv->charsets = MM_MODEM_CHARSET_UNKNOWN;
    if (!mm_gsm_parse_cscs_support_response (response->str, &priv->charsets)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the supported character sets response");
    } else
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->charsets), NULL);

    mm_callback_info_schedule (info);
}

static void
get_supported_charsets (MMModem *modem,
                        MMModemUIntFn callback,
                        gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    /* Use cached value if we have one */
    if (priv->charsets) {
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->charsets), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    /* Otherwise hit up the modem */
    port = mm_generic_gsm_get_best_at_port (self, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+CSCS=?", 3, get_charsets_done, info);
}

static void
set_get_charset_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;
    MMModemCharset tried_charset;
    const char *p;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        mm_callback_info_schedule (info);
        return;
    }

    p = response->str;
    if (g_str_has_prefix (p, "+CSCS:"))
        p += 6;
    while (*p == ' ')
        p++;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    priv->cur_charset = mm_modem_charset_from_string (p);

    tried_charset = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "charset"));

    if (tried_charset != priv->cur_charset) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_UNSUPPORTED_CHARSET,
                                   "Modem failed to change character set to %s",
                                   mm_modem_charset_to_string (tried_charset));
    }

    mm_callback_info_schedule (info);
}

#define TRIED_NO_QUOTES_TAG "tried-no-quotes"

static void
set_charset_done (MMAtSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        gboolean tried_no_quotes = !!mm_callback_info_get_data (info, TRIED_NO_QUOTES_TAG);
        MMModemCharset charset = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "charset"));
        char *command;

        if (!info->modem || tried_no_quotes) {
            mm_callback_info_schedule (info);
            return;
        }

        /* Some modems puke if you include the quotes around the character
         * set name, so lets try it again without them.
         */
        mm_callback_info_set_data (info, TRIED_NO_QUOTES_TAG, GUINT_TO_POINTER (TRUE), NULL);
        command = g_strdup_printf ("+CSCS=%s", mm_modem_charset_to_string (charset));
        mm_at_serial_port_queue_command (port, command, 3, set_charset_done, info);
        g_free (command);
    } else
        mm_at_serial_port_queue_command (port, "+CSCS?", 3, set_get_charset_done, info);
}

static gboolean
check_for_single_value (guint32 value)
{
    gboolean found = FALSE;
    guint32 i;

    for (i = 1; i <= 32; i++) {
        if (value & 0x1) {
            if (found)
                return FALSE;  /* More than one bit set */
            found = TRUE;
        }
        value >>= 1;
    }

    return TRUE;
}

static void
set_charset (MMModem *modem,
             MMModemCharset charset,
             MMModemFn callback,
             gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    const char *str;
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (modem, callback, user_data);

    if (!(priv->charsets & charset) || !check_for_single_value (charset)) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_UNSUPPORTED_CHARSET,
                                   "Character set 0x%X not supported",
                                   charset);
        mm_callback_info_schedule (info);
        return;
    }

    str = mm_modem_charset_to_string (charset);
    if (!str) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_UNSUPPORTED_CHARSET,
                                   "Unhandled character set 0x%X",
                                   charset);
        mm_callback_info_schedule (info);
        return;
    }

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_callback_info_set_data (info, "charset", GUINT_TO_POINTER (charset), NULL);

    command = g_strdup_printf ("+CSCS=\"%s\"", str);
    mm_at_serial_port_queue_command (port, command, 3, set_charset_done, info);
    g_free (command);
}

MMModemCharset
mm_generic_gsm_get_charset (MMGenericGsm *self)
{
    g_return_val_if_fail (self != NULL, MM_MODEM_CHARSET_UNKNOWN);
    g_return_val_if_fail (MM_IS_GENERIC_GSM (self), MM_MODEM_CHARSET_UNKNOWN);

    return MM_GENERIC_GSM_GET_PRIVATE (self)->cur_charset;
}

/*****************************************************************************/
/* MMModemGsmSms interface */

static void
sms_send_done (MMAtSerialPort *port,
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
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* FIXME: use the PDU mode instead */
    mm_at_serial_port_queue_command (port, "AT+CMGF=1", 3, NULL, NULL);

    command = g_strdup_printf ("+CMGS=\"%s\"\r%s\x1a", number, text);
    mm_at_serial_port_queue_command (port, command, 10, sms_send_done, info);
    g_free (command);
}

MMAtSerialPort *
mm_generic_gsm_get_at_port (MMGenericGsm *modem,
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

MMAtSerialPort *
mm_generic_gsm_get_best_at_port (MMGenericGsm *self, GError **error)
{
    MMGenericGsmPrivate *priv;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_GENERIC_GSM (self), NULL);

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (!mm_port_get_connected (MM_PORT (priv->primary)))
        return priv->primary;

    if (!priv->secondary) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                             "Cannot perform this operation while connected");
    }

    return priv->secondary;
}

/*****************************************************************************/
/* MMModemSimple interface */

typedef enum {
    SIMPLE_STATE_CHECK_PIN = 0,
    SIMPLE_STATE_ENABLE,
    SIMPLE_STATE_ALLOWED_MODE,
    SIMPLE_STATE_REGISTER,
    SIMPLE_STATE_SET_APN,
    SIMPLE_STATE_CONNECT,
    SIMPLE_STATE_DONE
} SimpleState;

/* Looks a value up in the simple connect properties dictionary.  If the
 * requested key is not present in the dict, NULL is returned.  If the
 * requested key is present but is not a string, an error is returned.
 */
static gboolean
simple_get_property (MMCallbackInfo *info,
                     const char *name,
                     GType expected_type,
                     const char **out_str,
                     guint32 *out_num,
                     gboolean *out_bool,
                     GError **error)
{
    GHashTable *properties = (GHashTable *) mm_callback_info_get_data (info, "simple-connect-properties");
    GValue *value;
    gint foo;

    g_return_val_if_fail (properties != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    if (out_str)
        g_return_val_if_fail (*out_str == NULL, FALSE);

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return FALSE;

    if ((expected_type == G_TYPE_STRING) && G_VALUE_HOLDS_STRING (value)) {
        *out_str = g_value_get_string (value);
        return TRUE;
    } else if (expected_type == G_TYPE_UINT) {
        if (G_VALUE_HOLDS_UINT (value)) {
            *out_num = g_value_get_uint (value);
            return TRUE;
        } else if (G_VALUE_HOLDS_INT (value)) {
            /* handle ints for convenience, but only if they are >= 0 */
            foo = g_value_get_int (value);
            if (foo >= 0) {
                *out_num = (guint) foo;
                return TRUE;
            }
        }
    } else if (expected_type == G_TYPE_BOOLEAN && G_VALUE_HOLDS_BOOLEAN (value)) {
        *out_bool = g_value_get_boolean (value);
        return TRUE;
    }

    g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                 "Invalid property type for '%s': %s (%s expected)",
                 name, G_VALUE_TYPE_NAME (value), g_type_name (expected_type));

    return FALSE;
}

static const char *
simple_get_string_property (MMCallbackInfo *info, const char *name, GError **error)
{
    const char *str = NULL;

    simple_get_property (info, name, G_TYPE_STRING, &str, NULL, NULL, error);
    return str;
}

static gboolean
simple_get_uint_property (MMCallbackInfo *info, const char *name, guint32 *out_val, GError **error)
{
    return simple_get_property (info, name, G_TYPE_UINT, NULL, out_val, NULL, error);
}

static gboolean
simple_get_bool_property (MMCallbackInfo *info, const char *name, gboolean *out_val, GError **error)
{
    return simple_get_property (info, name, G_TYPE_BOOLEAN, NULL, NULL, out_val, error);
}

static gboolean
simple_get_allowed_mode (MMCallbackInfo *info,
                         MMModemGsmAllowedMode *out_mode,
                         GError **error)
{
    MMModemDeprecatedMode old_mode = MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_ANY;
    MMModemGsmAllowedMode allowed_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
    GError *tmp_error = NULL;

    /* check for new allowed mode first */
    if (simple_get_uint_property (info, "allowed_mode", &allowed_mode, &tmp_error)) {
        if (allowed_mode > MM_MODEM_GSM_ALLOWED_MODE_LAST) {
            g_set_error (&tmp_error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                         "Invalid allowed mode %d", old_mode);
        } else {
            *out_mode = allowed_mode;
            return TRUE;
        }
    } else if (!tmp_error) {
        /* and if not, the old allowed mode */
        if (simple_get_uint_property (info, "network_mode", &old_mode, &tmp_error)) {
            if (old_mode > MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_LAST) {
                g_set_error (&tmp_error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Invalid allowed mode %d", old_mode);
            } else {
                *out_mode = mm_modem_gsm_network_old_mode_to_allowed (old_mode);
                return TRUE;
            }
        }
    }

    if (error)
        *error = tmp_error;
    return FALSE;
}

static void
simple_state_machine (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;
    const char *str, *unlock = NULL;
    SimpleState state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "simple-connect-state"));
    SimpleState next_state = state;
    gboolean done = FALSE;
    MMModemGsmAllowedMode allowed_mode;
    gboolean home_only = FALSE;

    info->error = mm_modem_check_removed (modem, error);
    if (info->error)
        goto out;

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (mm_options_debug ()) {
        GTimeVal tv;
        char *data_device;

        g_object_get (G_OBJECT (modem), MM_MODEM_DATA_DEVICE, &data_device, NULL);
        g_get_current_time (&tv);
        g_debug ("<%ld.%ld> (%s): simple connect state %d",
                 tv.tv_sec, tv.tv_usec, data_device, state);
        g_free (data_device);
    }

    switch (state) {
    case SIMPLE_STATE_CHECK_PIN:
        next_state = SIMPLE_STATE_ENABLE;

        /* If we need a PIN, send it now, but we don't care about SIM-PIN2/SIM-PUK2
         * since the device is operational without it.
         */
        unlock = mm_modem_base_get_unlock_required (MM_MODEM_BASE (modem));
        if (unlock && strcmp (unlock, "sim-puk2") && strcmp (unlock, "sim-pin2")) {
            gboolean success = FALSE;

            if (!strcmp (unlock, "sim-pin")) {
                str = simple_get_string_property (info, "pin", &info->error);
                if (str) {
                    mm_modem_gsm_card_send_pin (MM_MODEM_GSM_CARD (modem), str, simple_state_machine, info);
                    success = TRUE;
                }
            }
            if (!success && !info->error)
                info->error = error_for_unlock_required (unlock);
            break;
        }
        /* Fall through if no PIN required */
    case SIMPLE_STATE_ENABLE:
        next_state = SIMPLE_STATE_ALLOWED_MODE;
        mm_modem_enable (modem, simple_state_machine, info);
        break;
    case SIMPLE_STATE_ALLOWED_MODE:
        next_state = SIMPLE_STATE_REGISTER;
        if (   simple_get_allowed_mode (info, &allowed_mode, &info->error)
            && (allowed_mode != priv->allowed_mode)) {
            mm_modem_gsm_network_set_allowed_mode (MM_MODEM_GSM_NETWORK (modem),
                                                   allowed_mode,
                                                   simple_state_machine,
                                                   info);
            break;
        } else if (info->error)
            break;
        /* otherwise fall through as no allowed mode was sent */
    case SIMPLE_STATE_REGISTER:
        next_state = SIMPLE_STATE_SET_APN;
        str = simple_get_string_property (info, "network_id", &info->error);
        if (info->error)
            str = NULL;
        mm_modem_gsm_network_register (MM_MODEM_GSM_NETWORK (modem), str, simple_state_machine, info);
        break;
    case SIMPLE_STATE_SET_APN:
        next_state = SIMPLE_STATE_CONNECT;
        str = simple_get_string_property (info, "apn", &info->error);
        if (str || info->error) {
            if (str)
                mm_modem_gsm_network_set_apn (MM_MODEM_GSM_NETWORK (modem), str, simple_state_machine, info);
            break;
        }
        /* Fall through if no APN or no 'apn' property error */
    case SIMPLE_STATE_CONNECT:
        next_state = SIMPLE_STATE_DONE;
        str = simple_get_string_property (info, "number", &info->error);
        if (!info->error) {
            if (simple_get_bool_property (info, "home_only", &home_only, &info->error)) {
                MMModemGsmNetworkRegStatus status;

                priv->roam_allowed = !home_only;

                /* Don't connect if we're not supposed to be roaming */
                status = gsm_reg_status (MM_GENERIC_GSM (modem));
                if (home_only && (status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING)) {
                    info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                                       MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED,
                                                       "Roaming is not allowed.");
                    break;
                }
            } else if (info->error)
                break;

            mm_modem_connect (modem, str, simple_state_machine, info);
        }
        break;
    case SIMPLE_STATE_DONE:
        done = TRUE;
        break;
    }

 out:
    if (info->error || done)
        mm_callback_info_schedule (info);
    else
        mm_callback_info_set_data (info, "simple-connect-state", GUINT_TO_POINTER (next_state), NULL);
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMCallbackInfo *info;

    /* If debugging, list all the simple connect properties */
    if (mm_options_debug ()) {
        GHashTableIter iter;
        gpointer key, value;
        GTimeVal tv;
        char *data_device;

        g_object_get (G_OBJECT (simple), MM_MODEM_DATA_DEVICE, &data_device, NULL);
        g_get_current_time (&tv);

        g_hash_table_iter_init (&iter, properties);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            char *val_str;

            val_str = g_strdup_value_contents ((GValue *) value);
            g_debug ("<%ld.%ld> (%s): %s => %s",
                     tv.tv_sec, tv.tv_usec,
                     data_device, (const char *) key, val_str);
            g_free (val_str);
        }
        g_free (data_device);
    }

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

#define SS_HASH_TAG "simple-get-status"

static void
simple_status_got_signal_quality (MMModem *modem,
                                  guint32 result,
                                  GError *error,
                                  gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GHashTable *properties;

    if (!error) {
        properties = (GHashTable *) mm_callback_info_get_data (info, SS_HASH_TAG);
        g_hash_table_insert (properties, "signal_quality", simple_uint_value (result));
    }
    mm_callback_info_chain_complete_one (info);
}

static void
simple_status_got_band (MMModem *modem,
                        guint32 result,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GHashTable *properties;

    if (!error) {
        properties = (GHashTable *) mm_callback_info_get_data (info, SS_HASH_TAG);
        g_hash_table_insert (properties, "band", simple_uint_value (result));
    }
    mm_callback_info_chain_complete_one (info);
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

    info->error = mm_modem_check_removed ((MMModem *) modem, error);
    if (!info->error) {
        properties = (GHashTable *) mm_callback_info_get_data (info, SS_HASH_TAG);

        g_hash_table_insert (properties, "registration_status", simple_uint_value (status));
        g_hash_table_insert (properties, "operator_code", simple_string_value (oper_code));
        g_hash_table_insert (properties, "operator_name", simple_string_value (oper_name));
    }
    mm_callback_info_chain_complete_one (info);
}

static void
simple_get_status_invoke (MMCallbackInfo *info)
{
    MMModemSimpleGetStatusFn callback = (MMModemSimpleGetStatusFn) info->callback;

    callback (MM_MODEM_SIMPLE (info->modem),
              (GHashTable *) mm_callback_info_get_data (info, SS_HASH_TAG),
              info->error, info->user_data);
}

static void
simple_get_status (MMModemSimple *simple,
                   MMModemSimpleGetStatusFn callback,
                   gpointer user_data)
{
    MMModemGsmNetwork *gsm = MM_MODEM_GSM_NETWORK (simple);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (simple);
    GHashTable *properties;
    MMCallbackInfo *info;
    MMModemDeprecatedMode old_mode;

    info = mm_callback_info_new_full (MM_MODEM (simple),
                                      simple_get_status_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, simple_free_gvalue);
    mm_callback_info_set_data (info, SS_HASH_TAG, properties, (GDestroyNotify) g_hash_table_unref);

    mm_callback_info_chain_start (info, 3);
    mm_modem_gsm_network_get_signal_quality (gsm, simple_status_got_signal_quality, info);
    mm_modem_gsm_network_get_band (gsm, simple_status_got_band, info);
    mm_modem_gsm_network_get_registration_info (gsm, simple_status_got_reg_info, info);

    if (priv->act > -1) {
        /* Deprecated key */
        old_mode = mm_modem_gsm_network_act_to_old_mode (priv->act);
        g_hash_table_insert (properties, "network_mode", simple_uint_value (old_mode));

        /* New key */
        g_hash_table_insert (properties, "access_technology", simple_uint_value (priv->act));
    }
}

/*****************************************************************************/

static void
modem_state_changed (MMGenericGsm *self, GParamSpec *pspec, gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMModemState state;

    /* Start polling registration status and signal quality when enabled */

    state = mm_modem_get_state (MM_MODEM (self));
    if (state >= MM_MODEM_STATE_ENABLED) {
        if (!priv->poll_id)
            priv->poll_id = g_timeout_add_seconds (30, periodic_poll_cb, self);
    } else {
        if (priv->poll_id)
            g_source_remove (priv->poll_id);
        priv->poll_id = 0;
    }
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
    modem_class->get_supported_charsets = get_supported_charsets;
    modem_class->set_charset = set_charset;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imei = get_imei;
    class->get_imsi = get_imsi;
    class->get_operator_id = get_operator_id;
    class->send_pin = send_pin;
    class->send_puk = send_puk;
    class->enable_pin = enable_pin;
    class->change_pin = change_pin;
    class->get_unlock_retries = get_unlock_retries;
}

static void
modem_gsm_network_init (MMModemGsmNetwork *class)
{
    class->do_register = do_register;
    class->get_registration_info = get_registration_info;
    class->set_allowed_mode = set_allowed_mode;
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
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    priv->act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    priv->reg_regex = mm_gsm_creg_regex_get (TRUE);
    priv->roam_allowed = TRUE;

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_NETWORK_ALLOWED_MODE,
                                                    MM_MODEM_GSM_NETWORK_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY,
                                                    MM_MODEM_GSM_NETWORK_DBUS_INTERFACE);

    g_signal_connect (self, "notify::" MM_MODEM_STATE,
                      G_CALLBACK (modem_state_changed), NULL);
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
    case MM_GENERIC_GSM_PROP_ALLOWED_MODE:
    case MM_GENERIC_GSM_PROP_ACCESS_TECHNOLOGY:
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
        g_value_set_string (value, "Z E0 V1");
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
    case MM_GENERIC_GSM_PROP_ALLOWED_MODE:
        g_value_set_uint (value, priv->allowed_mode);
        break;
    case MM_GENERIC_GSM_PROP_ACCESS_TECHNOLOGY:
        if (mm_modem_get_state (MM_MODEM (object)) >= MM_MODEM_STATE_ENABLED)
            g_value_set_uint (value, priv->act);
        else
            g_value_set_uint (value, MM_MODEM_GSM_ACCESS_TECH_UNKNOWN);
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

    if (priv->pin_check_timeout) {
        g_source_remove (priv->pin_check_timeout);
        priv->pin_check_timeout = 0;
    }

    if (priv->poll_id) {
        g_source_remove (priv->poll_id);
        priv->poll_id = 0;
    }

    if (priv->signal_quality_id) {
        g_source_remove (priv->signal_quality_id);
        priv->signal_quality_id = 0;
    }

    mm_gsm_creg_regex_destroy (priv->reg_regex);

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
    klass->do_enable_power_up_done = real_do_enable_power_up_done;
    klass->do_disconnect = real_do_disconnect;

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

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_ALLOWED_MODE,
                                      MM_MODEM_GSM_NETWORK_ALLOWED_MODE);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_ACCESS_TECHNOLOGY,
                                      MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY);

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

