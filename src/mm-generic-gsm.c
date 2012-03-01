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
#include <errno.h>

#include "mm-generic-gsm.h"
#include "mm-modem-gsm-card.h"
#include "mm-modem-gsm-network.h"
#include "mm-modem-gsm-sms.h"
#include "mm-modem-gsm-ussd.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"
#include "mm-properties-changed-signal.h"
#include "mm-utils.h"
#include "mm-modem-location.h"
#include "mm-sms-utils.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_card_init (MMModemGsmCard *gsm_card_class);
static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);
static void modem_gsm_sms_init (MMModemGsmSms *gsm_sms_class);
static void modem_gsm_ussd_init (MMModemGsmUssd *gsm_ussd_class);
static void modem_simple_init (MMModemSimple *class);
static void modem_location_init (MMModemLocation *class);

G_DEFINE_TYPE_EXTENDED (MMGenericGsm, mm_generic_gsm, MM_TYPE_MODEM_BASE, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_SMS, modem_gsm_sms_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_LOCATION, modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_USSD, modem_gsm_ussd_init)
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
    char *simid;
    gboolean simid_checked;
    guint32 simid_tries;

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

    gboolean cmer_enabled;
    guint roam_ind;
    guint signal_ind;
    guint service_ind;

    guint signal_quality_id;
    time_t signal_emit_timestamp;
    time_t signal_update_timestamp;
    guint32 signal_quality;
    gint cid;

    guint32 charsets;
    guint32 cur_charset;

    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMQcdmSerialPort *qcdm;
    MMPort *data;

    /* Location API */
    guint32 loc_caps;
    gboolean loc_enabled;
    gboolean loc_signal;

    gboolean ussd_enabled;
    MMCallbackInfo *pending_ussd_info;
    MMModemGsmUssdState ussd_state;
    char *ussd_network_request;
    char *ussd_network_notification;

    /* SMS */
    GHashTable *sms_present;
    /* Map from SMS index numbers to parsed PDUs (themselves as hash tables) */
    GHashTable *sms_contents;
    /*
     * Map from multipart SMS reference numbers to SMSMultiPartMessage
     * structures.
     */
    GHashTable *sms_parts;
    gboolean sms_pdu_mode;
    gboolean sms_pdu_supported;

    guint sms_fetch_pending;
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

static void update_lac_ci (MMGenericGsm *self, gulong lac, gulong ci, guint idx);

static void ciev_received (MMAtSerialPort *port,
                           GMatchInfo *info,
                           gpointer user_data);

static void cmti_received (MMAtSerialPort *port,
                           GMatchInfo *info,
                           gpointer user_data);

static void cusd_received (MMAtSerialPort *port,
                           GMatchInfo *info,
                           gpointer user_data);

#define GS_HASH_TAG "get-sms"
static GValue *simple_string_value (const char *str);
static GValue *simple_uint_value (guint32 i);
static void simple_free_gvalue (gpointer data);

MMModem *
mm_generic_gsm_new (const char *device,
                    const char *driver,
                    const char *plugin,
                    guint vendor,
                    guint product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else if (response && strstr (response->str, "+CPIN: ")) {
        const char *str = strstr (response->str, "+CPIN: ") + 7;

        /* Some phones (Motorola EZX models) seem to quote the response */
        if (str[0] == '"')
            str++;

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
                                       response ? response->str : "(unknown)");
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

static void
get_info_cb (MMModem *modem,
             const char *manufacturer,
             const char *model,
             const char *version,
             GError *error,
             gpointer user_data)
{
    /* Base class handles saving the info for us */
    if (modem)
        mm_serial_port_close (MM_SERIAL_PORT (MM_GENERIC_GSM_GET_PRIVATE (modem)->primary));
}

/*****************************************************************************/

static MMModemGsmNetworkRegStatus
gsm_reg_status (MMGenericGsm *self, guint32 *out_idx)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    guint32 idx = 1;

    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */

    if (   priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {
        idx = 0;
        goto out;
    }

    if (   priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {
        idx = 1;
        goto out;
    }

    if (priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING) {
        idx = 0;
        goto out;
    }

    if (priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING) {
        idx = 1;
        goto out;
    }

    if (priv->reg_status[0] != MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN) {
        idx = 0;
        goto out;
    }

out:
    if (out_idx)
        *out_idx = idx;
    return priv->reg_status[idx];
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

    switch (gsm_reg_status (self, NULL)) {
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

    if (priv->primary && priv->data && priv->pin_checked && priv->simid_checked)
        new_valid = TRUE;

    mm_modem_base_set_valid (MM_MODEM_BASE (self), new_valid);
}


static void
get_iccid_done (MMModem *modem,
                const char *response,
                GError *error,
                gpointer user_data)
{
    MMGenericGsmPrivate *priv;
    const char *p = response;
    GChecksum *sum = NULL;

    if (error || !response || !strlen (response))
        goto done;

    sum = g_checksum_new (G_CHECKSUM_SHA1);

    /* Make sure it looks like an ICCID */
    while (*p) {
        if (!isdigit (*p)) {
            g_warning ("%s: invalid ICCID format (not a digit)", __func__);
            goto done;
        }
        g_checksum_update (sum, (const guchar *) p++, 1);
    }

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    g_free (priv->simid);
    priv->simid = g_strdup (g_checksum_get_string (sum));

    mm_dbg ("SIM ID source '%s'", response);
    mm_dbg ("SIM ID '%s'", priv->simid);

    g_object_notify (G_OBJECT (modem), MM_MODEM_GSM_CARD_SIM_IDENTIFIER);

done:
    if (sum)
        g_checksum_free (sum);

    if (modem) {
        MM_GENERIC_GSM_GET_PRIVATE (modem)->simid_checked = TRUE;
        check_valid (MM_GENERIC_GSM (modem));
    }
}

#define ICCID_CMD "+CRSM=176,12258,0,0,10"

static void
real_get_iccid_done (MMAtSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *str;
    int sw1, sw2;
    gboolean success = FALSE;
    char buf[21], swapped[21];

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    memset (buf, 0, sizeof (buf));
    str = mm_strip_tag (response->str, "+CRSM:");
    if (sscanf (str, "%d,%d,\"%20c\"", &sw1, &sw2, (char *) &buf) == 3)
        success = TRUE;
    else {
        /* May not include quotes... */
        if (sscanf (str, "%d,%d,%20c", &sw1, &sw2, (char *) &buf) == 3)
            success = TRUE;
    }

    if (!success) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the CRSM response");
        goto done;
    }

    if ((sw1 == 0x90 && sw2 == 0x00) || (sw1 == 0x91) || (sw1 == 0x92) || (sw1 == 0x9f)) {
        gsize len = 0;
        int f_pos = -1, i;

        /* Make sure the buffer is only digits or 'F' */
        for (len = 0; len < sizeof (buf) && buf[len]; len++) {
            if (isdigit (buf[len]))
                continue;
            if (buf[len] == 'F' || buf[len] == 'f') {
                buf[len] = 'F';  /* canonicalize the F */
                f_pos = len;
                continue;
            }
            if (buf[len] == '\"') {
                buf[len] = 0;
                break;
            }

            /* Invalid character */
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "CRSM ICCID response contained invalid character '%c'",
                                       buf[len]);
            goto done;
        }

        /* BCD encoded ICCIDs are 20 digits long */
        if (len != 20) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Invalid +CRSM ICCID response size (was %zd, expected 20)",
                                       len);
            goto done;
        }

        /* Ensure if there's an 'F' that it's second-to-last */
        if ((f_pos >= 0) && (f_pos != len - 2)) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_GENERAL,
                                               "Invalid +CRSM ICCID length (unexpected F)");
            goto done;
        }

        /* Swap digits in the EFiccid response to get the actual ICCID, each
         * group of 2 digits is reversed in the +CRSM response.  i.e.:
         *
         *    21436587 -> 12345678
         */
        memset (swapped, 0, sizeof (swapped));
        for (i = 0; i < 10; i++) {
            swapped[i * 2] = buf[(i * 2) + 1];
            swapped[(i * 2) + 1] = buf[i * 2];
        }

        /* Zero out the F for 19 digit ICCIDs */
        if (swapped[len - 1] == 'F')
            swapped[len - 1] = 0;

        mm_callback_info_set_result (info, g_strdup (swapped), g_free);
    } else {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        if (priv->simid_tries++ < 2) {
            /* Try one more time... Gobi 1K cards may reply to the first
             * request with '+CRSM: 106,134,""' which is bogus because
             * subsequent requests work fine.
             */
            mm_at_serial_port_queue_command (port, ICCID_CMD, 20, real_get_iccid_done, info);
            return;
        } else {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                                       sw1, sw2);
        }
    }

done:
    /* Balance open from real_get_sim_iccid() */
    mm_serial_port_close (MM_SERIAL_PORT (port));

    mm_callback_info_schedule (info);
}

static void
real_get_sim_iccid (MMGenericGsm *self,
                    MMModemStringFn callback,
                    gpointer callback_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    GError *error = NULL;

    port = mm_generic_gsm_get_best_at_port (self, &error);
    if (!port) {
        callback (MM_MODEM (self), NULL, error, callback_data);
        g_clear_error (&error);
        return;
    }

    if (!mm_serial_port_open (MM_SERIAL_PORT (port), &error)) {
        callback (MM_MODEM (self), NULL, error, callback_data);
        g_clear_error (&error);
        return;
    }

    info = mm_callback_info_string_new (MM_MODEM (self), callback, callback_data);

    /* READ BINARY of EFiccid (ICC Identification) ETSI TS 102.221 section 13.2 */
    mm_at_serial_port_queue_command (port, ICCID_CMD, 20, real_get_iccid_done, info);
}

static void
initial_iccid_check (MMGenericGsm *self)
{
    g_assert (MM_GENERIC_GSM_GET_CLASS (self)->get_sim_iccid);
    MM_GENERIC_GSM_GET_CLASS (self)->get_sim_iccid (self, get_iccid_done, NULL);
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
        /* Try to get the SIM ICCID after we've checked PIN status and the SIM
         * is ready.
         */
        initial_iccid_check (MM_GENERIC_GSM (modem));

        priv->pin_checked = TRUE;
        mm_serial_port_close (MM_SERIAL_PORT (priv->primary));
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

    if (mm_serial_port_open (MM_SERIAL_PORT (priv->primary), &error)) {
        mm_at_serial_port_queue_command (priv->primary, "+CMEE=1", 2, NULL, NULL);
        check_pin (self, initial_pin_check_done, NULL);
    } else {
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

static void
initial_info_check (MMGenericGsm *self)
{
    GError *error = NULL;
    MMGenericGsmPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_GSM (self));
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    g_return_if_fail (priv->primary != NULL);

    if (mm_serial_port_open (MM_SERIAL_PORT (priv->primary), &error)) {
        /* Make sure echoing is off */
        mm_at_serial_port_queue_command (priv->primary, "E0", 3, NULL, NULL);
        mm_modem_base_get_card_info (MM_MODEM_BASE (self),
                                     priv->primary,
                                     NULL,
                                     get_info_cb,
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

        regex = g_regex_new ("\\r\\n\\+CIEV: (\\d+),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, ciev_received, self, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+CMTI: \"(\\S+)\",(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, cmti_received, self, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+CUSD:\\s*(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, cusd_received, self, NULL);
        g_regex_unref (regex);

        if (ptype == MM_PORT_TYPE_PRIMARY) {
            priv->primary = MM_AT_SERIAL_PORT (port);
            if (!priv->data) {
                priv->data = port;
                g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            }

            /* Get the modem's general info */
            initial_info_check (self);

            /* Get modem's IMEI */
            initial_imei_check (self);

            /* Get modem's initial lock/unlock state; this also ensures the
             * SIM is ready by waiting if necessary for the SIM to initalize.
             */
            initial_pin_check (self);

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
add_loc_capability (MMGenericGsm *self, guint32 cap)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    guint32 old_caps = priv->loc_caps;

    priv->loc_caps |= cap;
    if (priv->loc_caps != old_caps) {
        g_object_notify (G_OBJECT (self), MM_MODEM_LOCATION_CAPABILITIES);
    }
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

    /* Don't poll signal quality if we got a notification in the past 10 seconds */
    if (time (NULL) - priv->signal_update_timestamp > 10) {
        mm_modem_gsm_network_get_signal_quality (MM_MODEM_GSM_NETWORK (self),
                                                 periodic_signal_quality_cb,
                                                 NULL);
    }

    if (MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology)
        MM_GENERIC_GSM_GET_CLASS (self)->get_access_technology (self, periodic_access_tech_cb, NULL);

    return TRUE;  /* continue running */
}

#define CREG_NUM_TAG "creg-num"
#define CGREG_NUM_TAG "cgreg-num"

static void
initial_unsolicited_reg_check_done (MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;
    guint creg_num, cgreg_num;

    if (!info->modem || info->error)
        goto done;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    if (!priv->secondary)
        goto done;

    /* Enable unsolicited registration responses on secondary ports too,
     * to ensure that we get the response even if the modem is connected
     * on the primary port.  We enable responses on both ports because we
     * cannot trust modems to reliably send the responses on the port we
     * enable them on.
     */

    creg_num = GPOINTER_TO_UINT (mm_callback_info_get_data (info, CREG_NUM_TAG));
    switch (creg_num) {
    case 1:
        mm_at_serial_port_queue_command (priv->secondary, "+CREG=1", 3, NULL, NULL);
        break;
    case 2:
        mm_at_serial_port_queue_command (priv->secondary, "+CREG=2", 3, NULL, NULL);
        break;
    default:
        break;
    }

    cgreg_num = GPOINTER_TO_UINT (mm_callback_info_get_data (info, CGREG_NUM_TAG));
    switch (cgreg_num) {
    case 1:
        mm_at_serial_port_queue_command (priv->secondary, "+CGREG=1", 3, NULL, NULL);
        break;
    case 2:
        mm_at_serial_port_queue_command (priv->secondary, "+CGREG=2", 3, NULL, NULL);
        break;
    default:
        break;
    }

done:
    mm_callback_info_schedule (info);
}

static void
cgreg1_done (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        /* The modem doesn't like unsolicited CGREG, so we'll need to poll */
        priv->cgreg_poll = TRUE;
    } else
        mm_callback_info_set_data (info, CGREG_NUM_TAG, GUINT_TO_POINTER (1), NULL);

    /* Success; get initial state */
    mm_at_serial_port_queue_command (port, "+CGREG?", 10, reg_poll_response, info->modem);

    initial_unsolicited_reg_check_done (info);
}

static void
cgreg2_done (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore errors */
    if (error) {
        /* Try CGREG=1 instead */
        mm_at_serial_port_queue_command (port, "+CGREG=1", 3, cgreg1_done, info);
    } else {
        add_loc_capability (MM_GENERIC_GSM (info->modem), MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI);

        mm_callback_info_set_data (info, CGREG_NUM_TAG, GUINT_TO_POINTER (2), NULL);

        /* Success; get initial state */
        mm_at_serial_port_queue_command (port, "+CGREG?", 10, reg_poll_response, info->modem);

        /* All done */
        initial_unsolicited_reg_check_done (info);
    }
}

static void
creg1_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsmPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    if (error) {
        /* The modem doesn't like unsolicited CREG, so we'll need to poll */
        priv->creg_poll = TRUE;
    } else
        mm_callback_info_set_data (info, CREG_NUM_TAG, GUINT_TO_POINTER (1), NULL);

    /* Success; get initial state */
    mm_at_serial_port_queue_command (port, "+CREG?", 10, reg_poll_response, info->modem);

    /* Now try to set up CGREG messages */
    mm_at_serial_port_queue_command (port, "+CGREG=2", 3, cgreg2_done, info);
}

static void
creg2_done (MMAtSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore errors */
    if (error)
        mm_at_serial_port_queue_command (port, "+CREG=1", 3, creg1_done, info);
    else {
        add_loc_capability (MM_GENERIC_GSM (info->modem), MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI);

        mm_callback_info_set_data (info, CREG_NUM_TAG, GUINT_TO_POINTER (2), NULL);

        /* Success; get initial state */
        mm_at_serial_port_queue_command (port, "+CREG?", 10, reg_poll_response, info->modem);

        /* Now try to set up CGREG messages */
        mm_at_serial_port_queue_command (port, "+CGREG=2", 3, cgreg2_done, info);
    }
}

static void
enable_failed (MMModem *modem, GError *error, MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;

    /* If modem already removed, do nothing */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);

    mm_modem_set_state (modem,
                        MM_MODEM_STATE_DISABLED,
                        MM_MODEM_STATE_REASON_NONE);

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->primary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->primary)))
        mm_serial_port_close_force (MM_SERIAL_PORT (priv->primary));
    if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary)))
        mm_serial_port_close_force (MM_SERIAL_PORT (priv->secondary));

    mm_callback_info_schedule (info);
}

static guint32 best_charsets[] = {
    MM_MODEM_CHARSET_UTF8,
    MM_MODEM_CHARSET_UCS2,
    MM_MODEM_CHARSET_8859_1,
    MM_MODEM_CHARSET_IRA,
    MM_MODEM_CHARSET_GSM,
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
    mm_modem_set_charset (modem, best_charsets[0], enabled_set_charset_done, info);
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
ciev_received (MMAtSerialPort *port,
               GMatchInfo *info,
               gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    int quality = 0, ind = 0;
    char *str;

    if (!priv->cmer_enabled)
        return;

    str = g_match_info_fetch (info, 1);
    if (str)
        ind = atoi (str);
    g_free (str);

    if (ind == priv->signal_ind) {
        str = g_match_info_fetch (info, 2);
        if (str) {
            quality = atoi (str);
            mm_generic_gsm_update_signal_quality (self, quality * 20);
        }
        g_free (str);
    }

    /* FIXME: handle roaming and service indicators */
}

typedef struct {
    /*
     * The key index number that refers to this multipart message -
     * usually the index number of the first part received.
     */
    guint index;

    /* Number of parts in the complete message */
    guint numparts;

    /* Number of parts missing from the message */
    guint missing;

    /* Array of (index numbers of) message parts, in order */
    guint *parts;
} SMSMultiPartMessage;

static void
sms_cache_insert (MMModem *modem, GHashTable *properties, guint idx)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    GHashTable *old_properties;
    GValue *ref;

    ref = g_hash_table_lookup (properties, "concat-reference");
    if (ref != NULL) {
        GValue *max, *seq;
        guint refnum, maxnum, seqnum;
        SMSMultiPartMessage *mpm;

        max = g_hash_table_lookup (properties, "concat-max");
        seq = g_hash_table_lookup (properties, "concat-sequence");
        if (max == NULL || seq == NULL) {
            /* Internal error - not all required data present */
            return;
        }

        refnum = g_value_get_uint (ref);
        maxnum = g_value_get_uint (max);
        seqnum = g_value_get_uint (seq);

        if (seqnum > maxnum) {
            /* Error - SMS says "part N of M", but N > M */
            return;
        }

        mpm = g_hash_table_lookup (priv->sms_parts, GUINT_TO_POINTER (refnum));
        if (mpm == NULL) {
            /* Create a new one */
            if (maxnum > 255)
                maxnum = 255;
            mpm = g_malloc0 (sizeof (*mpm));
            mpm->index = idx;
            mpm->numparts = maxnum;
            mpm->missing = maxnum;
            mpm->parts = g_malloc0 (maxnum * sizeof(*mpm->parts));
            g_hash_table_insert (priv->sms_parts, GUINT_TO_POINTER (refnum),
                                 mpm);
        }

        if (maxnum != mpm->numparts) {
            /* Error - other messages with this refnum claim a different number of parts */
            return;
        }

        if (mpm->parts[seqnum - 1] != 0) {
            /* Error - two SMS segments have claimed to be the same part of the same message. */
            return;
        }

        mpm->parts[seqnum - 1] = idx;
        mpm->missing--;
    }

    old_properties = g_hash_table_lookup (priv->sms_contents, GUINT_TO_POINTER (idx));
    if (old_properties != NULL)
        g_hash_table_unref (old_properties);

    g_hash_table_insert (priv->sms_contents, GUINT_TO_POINTER (idx),
                         g_hash_table_ref (properties));
}

/*
 * Takes a hash table representing a (possibly partial) SMS and
 * determines if it is the key part of a complete SMS. The complete
 * SMS, if any, is returned. If there is no such SMS (for example, not
 * all parts are present yet), NULL is returned. The passed-in hash
 * table is dereferenced, and the returned hash table is referenced.
 */
static GHashTable *
sms_cache_lookup_full (MMModem *modem,
                       GHashTable *properties,
                       GError **error)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    int i, refnum, indexnum;
    SMSMultiPartMessage *mpm;
    GHashTable *full, *part, *first;
    GHashTableIter iter;
    gpointer key, value;
    char *fulltext;
    char **textparts;
    GValue *ref, *idx, *text;

    ref = g_hash_table_lookup (properties, "concat-reference");
    if (ref == NULL)
        return properties;
    refnum = g_value_get_uint (ref);

    idx = g_hash_table_lookup (properties, "index");
    if (idx == NULL) {
        g_hash_table_unref (properties);
        return NULL;
    }

    indexnum = g_value_get_uint (idx);
    g_hash_table_unref (properties);

    mpm = g_hash_table_lookup (priv->sms_parts,
                                 GUINT_TO_POINTER (refnum));
    if (mpm == NULL) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Internal error - no multipart structure for multipart SMS");
        return NULL;
    }

    /* Check that this is the key */
    if (indexnum != mpm->index)
        return NULL;

    if (mpm->missing != 0)
        return NULL;

    /* Complete multipart message is present. Assemble it */
    textparts = g_malloc0((1 + mpm->numparts) * sizeof (*textparts));
    for (i = 0 ; i < mpm->numparts ; i++) {
        part = g_hash_table_lookup (priv->sms_contents,
                                    GUINT_TO_POINTER (mpm->parts[i]));
        if (part == NULL) {
            g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                         "Internal error - part %d (index %d) is missing",
                         i, mpm->parts[i]);
            g_free (textparts);
            return NULL;
        }
        text = g_hash_table_lookup (part, "text");
        if (text == NULL) {
            g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                         "Internal error - part %d (index %d) has no text element",
                         i, mpm->parts[i]);
            g_free (textparts);
            return NULL;
        }
        textparts[i] = g_value_dup_string (text);
    }
    textparts[i] = NULL;
    fulltext = g_strjoinv (NULL, textparts);
    g_strfreev (textparts);

    first = g_hash_table_lookup (priv->sms_contents,
                                 GUINT_TO_POINTER (mpm->parts[0]));
    full = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                  simple_free_gvalue);
    g_hash_table_iter_init (&iter, first);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        const char *keystr = key;
        if (strncmp (keystr, "concat-", 7) == 0)
            continue;
        if (strcmp (keystr, "text") == 0 ||
            strcmp (keystr, "index") == 0)
            continue;
        if (strcmp (keystr, "class") == 0) {
            GValue *val;
            val = g_slice_new0 (GValue);
            g_value_init (val, G_TYPE_UINT);
            g_value_copy (value, val);
            g_hash_table_insert (full, key, val);
        } else {
            GValue *val;
            val = g_slice_new0 (GValue);
            g_value_init (val, G_TYPE_STRING);
            g_value_copy (value, val);
            g_hash_table_insert (full, key, val);
        }
    }

    g_hash_table_insert (full, "index", simple_uint_value (mpm->index));
    g_hash_table_insert (full, "text", simple_string_value (fulltext));
    g_free (fulltext);

    return full;
}

static void
cmti_received_has_sms (MMModemGsmSms *modem,
                       GHashTable *properties,
                       GError *error,
                       gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    guint idx;
    gboolean complete;
    GValue *ref;

    if (properties == NULL)
        return;

    ref = g_hash_table_lookup (properties, "concat-reference");
    if (ref == NULL) {
        /* single-part message */
        GValue *idxval = g_hash_table_lookup (properties, "index");
        if (idxval == NULL)
            return;
        idx = g_value_get_uint (idxval);
        complete = TRUE;
    } else {
        SMSMultiPartMessage *mpm;
        mpm = g_hash_table_lookup (priv->sms_parts,
                                     GUINT_TO_POINTER (g_value_get_uint (ref)));
        if (mpm == NULL)
            return;
        idx = mpm->index;
        complete = (mpm->missing == 0);
    }

    if (complete)
        mm_modem_gsm_sms_completed (MM_MODEM_GSM_SMS (self), idx, TRUE);

    mm_modem_gsm_sms_received (MM_MODEM_GSM_SMS (self), idx, complete);
}

static void sms_get_invoke (MMCallbackInfo *info);
static void sms_get_done (MMAtSerialPort *port, GString *response,
                          GError *error, gpointer user_data);

static void
cmti_received (MMAtSerialPort *port,
               GMatchInfo *info,
               gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *cbinfo;
    guint idx = 0;
    char *str, *command;

    str = g_match_info_fetch (info, 2);
    if (str)
        idx = atoi (str);
    g_free (str);

    /* Don't signal multiple times if there are multiple CMTI notifications for a message */
    if (g_hash_table_lookup_extended (priv->sms_present, GINT_TO_POINTER (idx), NULL, NULL))
        return;

    /* Nothing is currently stored in the hash table - presence is all that matters. */
    g_hash_table_insert (priv->sms_present, GINT_TO_POINTER (idx), NULL);

    /* Retrieve the message */
    cbinfo = mm_callback_info_new_full (MM_MODEM (user_data),
                                        sms_get_invoke,
                                        G_CALLBACK (cmti_received_has_sms),
                                        user_data);
    mm_callback_info_set_data (cbinfo,
                               "complete-sms-only",
                               GUINT_TO_POINTER (FALSE),
                               NULL);

    if (priv->sms_fetch_pending != 0) {
        mm_err("sms_fetch_pending is %d, not 0", priv->sms_fetch_pending);
    }
    priv->sms_fetch_pending = idx;

    command = g_strdup_printf ("+CMGR=%d", idx);
    mm_at_serial_port_queue_command (port, command, 10, sms_get_done, cbinfo);
    /* Don't want to signal received here before we have the contents */
}

static void
cmer_cb (MMAtSerialPort *port,
         GString *response,
         GError *error,
         gpointer user_data)
{
    if (!error) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (user_data);

        priv->cmer_enabled = TRUE;

        /* Enable CMER on the secondary port if we can too */
        if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary)))
            mm_at_serial_port_queue_command (priv->secondary, "+CMER=3,0,0,1", 3, NULL, NULL);
    }
}

static void
cind_cb (MMAtSerialPort *port,
         GString *response,
         GError *error,
         gpointer user_data)
{
    MMGenericGsm *self;
    MMGenericGsmPrivate *priv;
    GHashTable *indicators;

    if (error)
        return;

    self = MM_GENERIC_GSM (user_data);
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    indicators = mm_parse_cind_test_response (response->str, NULL);
    if (indicators) {
        CindResponse *r;

        r = g_hash_table_lookup (indicators, "signal");
        if (r)
            priv->signal_ind = cind_response_get_index (r);

        r = g_hash_table_lookup (indicators, "roam");
        if (r)
            priv->roam_ind = cind_response_get_index (r);

        r = g_hash_table_lookup (indicators, "service");
        if (r)
            priv->service_ind = cind_response_get_index (r);

        mm_at_serial_port_queue_command (port, "+CMER=3,0,0,1", 3, cmer_cb, self);
        g_hash_table_destroy (indicators);
    }
}

static void
cusd_enable_cb (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    if (error) {
        mm_warn ("(%s): failed to enable USSD notifications.",
                 mm_port_get_device (MM_PORT (port)));
        return;
    }

    MM_GENERIC_GSM_GET_PRIVATE (user_data)->ussd_enabled = TRUE;
}

static void
sms_set_format_cb (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (error) {
        mm_warn ("(%s): failed to set SMS mode, assuming text mode",
                 mm_port_get_device (MM_PORT (port)));
        priv->sms_pdu_mode = FALSE;
        priv->sms_pdu_supported = FALSE;
    } else {
        mm_info ("(%s): using %s mode for SMS",
                 mm_port_get_device (MM_PORT (port)),
                 priv->sms_pdu_mode ? "PDU" : "text");
    }
}


#define CMGF_TAG "+CMGF:"

static void
sms_get_format_cb (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMGenericGsm *self;
    MMGenericGsmPrivate *priv;
    const char *reply;
    GRegex *r;
    GMatchInfo *match_info;
    char *s;
    int min = -1, max = -1;

    if (error) {
        mm_warn ("(%s): failed to query SMS mode, assuming text mode",
                 mm_port_get_device (MM_PORT (port)));
        return;
    }

    /* Strip whitespace and response tag */
    reply = response->str;
    if (g_str_has_prefix (reply, CMGF_TAG))
        reply += strlen (CMGF_TAG);
    while (isspace (*reply))
        reply++;

    r = g_regex_new ("\\(?\\s*(\\d+)\\s*[-,]?\\s*(\\d+)?\\s*\\)?", 0, 0, NULL);
    if (!r) {
        mm_warn ("(%s): failed to parse CMGF query result", mm_port_get_device (MM_PORT (port)));
        return;
    }

    self = MM_GENERIC_GSM (user_data);
    priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    if (g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, NULL)) {
        s = g_match_info_fetch (match_info, 1);
        if (s)
            min = atoi (s);
        g_free (s);

        s = g_match_info_fetch (match_info, 2);
        if (s)
            max = atoi (s);
        g_free (s);

        /* If the modem only supports PDU mode, use PDUs.
         * FIXME: when the PDU code is more robust, default to PDU if the
         * modem supports it.
         */
        if (min == 0 && max < 1) {
            /* Will get reset to FALSE on receipt of error */
            priv->sms_pdu_mode = TRUE;
            mm_at_serial_port_queue_command (priv->primary, "AT+CMGF=0", 3, sms_set_format_cb, self);
        } else
            mm_at_serial_port_queue_command (priv->primary, "AT+CMGF=1", 3, sms_set_format_cb, self);

        /* Save whether PDU mode is supported so we can fall back to it if text fails */
        if (min == 0)
            priv->sms_pdu_supported = TRUE;
    }
    g_match_info_free (match_info);

    g_regex_unref (r);
}

void
mm_generic_gsm_enable_complete (MMGenericGsm *self,
                                GError *error,
                                MMCallbackInfo *info)
{
    MMGenericGsmPrivate *priv;
    gchar *cmd = NULL;

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
            mm_dbg ("error opening secondary port: (%d) %s",
                    error ? error->code : -1,
                    error && error->message ? error->message : "(unknown)");
        }
    }

    /* Try to enable flow control */
    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_FLOW_CONTROL_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_at_serial_port_queue_command (priv->primary, cmd, 3, NULL, NULL);
    g_free (cmd);

    /* Set SMS storage location to ME */
    mm_at_serial_port_queue_command (priv->primary, "+CPMS=\"ME\",\"ME\",\"ME\"", 3, NULL, NULL);

    /* Enable SMS notifications */
    mm_at_serial_port_queue_command (priv->primary, "+CNMI=2,1,2,1,0", 3, NULL, NULL);

    /* Check and enable the right SMS mode */
    mm_at_serial_port_queue_command (priv->primary, "AT+CMGF=?", 3, sms_get_format_cb, self);

    /* Enable USSD notifications */
    mm_at_serial_port_queue_command (priv->primary, "+CUSD=1", 3, cusd_enable_cb, self);

    mm_at_serial_port_queue_command (priv->primary, "+CIND=?", 3, cind_cb, self);

    /* Try one more time to get the SIM ID */
    if (!priv->simid)
        MM_GENERIC_GSM_GET_CLASS (self)->get_sim_iccid (self, get_iccid_done, NULL);

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
enable_power_up_check_needed_done (MMModem *self,
                                   guint32 needed,
                                   GError *error,
                                   gpointer user_data)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    char *cmd = NULL;

    if (needed)
        g_object_get (G_OBJECT (self), MM_GENERIC_GSM_POWER_UP_CMD, &cmd, NULL);
    else
        mm_dbg ("Power-up not needed, skipping...");

    if (cmd && strlen (cmd))
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (priv->primary), cmd, 5, enable_done, user_data);
    else
        enable_done (MM_AT_SERIAL_PORT (priv->primary), NULL, NULL, user_data);
    g_free (cmd);
}

static void
init_done (MMAtSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *cmd = NULL;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

    /* Plugins can now check if they need the power up command or not */
    if (MM_GENERIC_GSM_GET_CLASS (info->modem)->do_enable_power_up_check_needed)
        MM_GENERIC_GSM_GET_CLASS (info->modem)->do_enable_power_up_check_needed (MM_GENERIC_GSM (info->modem),
                                                                                 enable_power_up_check_needed_done,
                                                                                 info);
    else
        enable_power_up_check_needed_done (info->modem, TRUE, NULL, info);
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

    /* Send the init command twice; some devices (Nokia N900) appear to take a
     * few commands before responding correctly.  Instead of penalizing them for
     * being stupid the first time by failing to enable the device, just
     * try again.
     */
    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_INIT_CMD, &cmd, NULL);
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), cmd, 3, NULL, NULL);
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
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
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info = user_data;
    MMModemState prev_state;
    char *cmd = NULL;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);

        /* Reset old state since the operation failed */
        prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_GSM_PREV_STATE_TAG));
        mm_modem_set_state (MM_MODEM (info->modem),
                            prev_state,
                            MM_MODEM_STATE_REASON_NONE);

        mm_callback_info_schedule (info);
        return;
    }

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    /* Disable unsolicited messages */
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "+CREG=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "+CGREG=0", 3, NULL, NULL);

    if (priv->ussd_enabled) {
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "+CUSD=0", 3, NULL, NULL);
        priv->ussd_enabled = FALSE;
    }

    if (priv->cmer_enabled) {
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "+CMER=0", 3, NULL, NULL);

        /* And on the secondary port */
        if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary)))
            mm_at_serial_port_queue_command (priv->secondary, "+CMER=0", 3, NULL, NULL);

        priv->cmer_enabled = FALSE;
    }

    g_object_get (G_OBJECT (info->modem), MM_GENERIC_GSM_POWER_DOWN_CMD, &cmd, NULL);
    if (cmd && strlen (cmd))
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), cmd, 5, disable_done, user_data);
    else
        disable_done (MM_AT_SERIAL_PORT (port), NULL, NULL, user_data);
    g_free (cmd);
}

static void
mark_disabled (gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    mm_modem_set_state (MM_MODEM (info->modem),
                        MM_MODEM_STATE_DISABLING,
                        MM_MODEM_STATE_REASON_NONE);

    if (mm_port_get_connected (MM_PORT (priv->primary)))
        mm_serial_port_flash (MM_SERIAL_PORT (priv->primary), 1000, TRUE, disable_flash_done, info);
    else
        disable_flash_done (MM_SERIAL_PORT (priv->primary), NULL, info);
}

static void
secondary_unsolicited_done (MMAtSerialPort *port,
                            GString *response,
                            GError *error,
                            gpointer user_data)
{
    mm_serial_port_close_force (MM_SERIAL_PORT (port));
    mark_disabled (user_data);
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

    mm_generic_gsm_ussd_cleanup (MM_GENERIC_GSM (modem));

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

    update_lac_ci (self, 0, 0, 0);
    update_lac_ci (self, 0, 0, 1);
    _internal_update_access_technology (self, MM_MODEM_GSM_ACCESS_TECH_UNKNOWN);

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_GSM_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    /* Clean up the secondary port if it's open */
    if (priv->secondary && mm_serial_port_is_open (MM_SERIAL_PORT (priv->secondary))) {
        mm_dbg("Shutting down secondary port");
        mm_at_serial_port_queue_command (priv->secondary, "+CREG=0", 3, NULL, NULL);
        mm_at_serial_port_queue_command (priv->secondary, "+CGREG=0", 3, NULL, NULL);
        mm_at_serial_port_queue_command (priv->secondary, "+CMER=0", 3, secondary_unsolicited_done, info);
    } else
        mark_disabled (info);
}

static void
get_string_done (MMAtSerialPort *port,
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
            g_free (bin);
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
        g_free (bin);
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
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMAtSerialPort *port;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    g_clear_error (&info->error);
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_callback_info_set_data (info, "imsi", g_strdup (result), g_free);

    /* READ BINARY of EFad (Administrative Data) ETSI 51.011 section 10.3.18 */
    mm_at_serial_port_queue_command_cached (port,
                                            "+CRSM=176,28589,0,0,4",
                                            3,
                                            get_mnc_length_done,
                                            info);
}

static void
get_spn_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    int sw1, sw2;
    gboolean success = FALSE;
    char hex[51];
    char *bin, *utf8;

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
        if (!bin) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "SIM returned malformed response '%s'",
                                       hex);
            goto done;
        }

        /* Remove the FF filler at the end */
        while (buflen > 1 && bin[buflen - 1] == (char)0xff)
            buflen--;

        /* First byte is metadata; remainder is GSM-7 unpacked into octets; convert to UTF8 */
        utf8 = (char *)mm_charset_gsm_unpacked_to_utf8 ((guint8 *)bin + 1, buflen - 1);
        g_free(bin);
        mm_callback_info_set_result(info, utf8, g_free);
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
get_imei (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port)
        mm_callback_info_schedule (info);
    else {
        g_clear_error (&info->error);
        mm_at_serial_port_queue_command_cached (port, "+CGSN", 3, get_string_done, info);
    }
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port)
        mm_callback_info_schedule (info);
    else {
        g_clear_error (&info->error);
        mm_at_serial_port_queue_command_cached (port, "+CIMI", 3, get_string_done, info);
    }
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
get_spn (MMModemGsmCard *modem,
         MMModemStringFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port)
        mm_callback_info_schedule (info);
    else {
        g_clear_error (&info->error);

        /* READ BINARY of EFspn (Service Provider Name) ETSI 51.011 section 10.3.11 */
        mm_at_serial_port_queue_command_cached (port,
                                                "+CRSM=176,28486,0,0,17",
                                                3,
                                                get_spn_done,
                                                info);
    }
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
#define SAVED_ERROR_TAG "error"

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
    MMGenericGsmPrivate *priv;
    MMSerialPort *port;
    GError *saved_error;

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    /* Clear the pin check timeout to ensure that it won't ever get a
     * stale MMCallbackInfo if the modem got removed.  We'll reschedule it here
     * anyway if needed.
     */
    if (priv->pin_check_timeout)
        g_source_remove (priv->pin_check_timeout);
    priv->pin_check_timeout = 0;

    /* Propagate the error to the info */
    if (error)
        info->error = g_error_copy (error);

    /* If the modem wasn't removed, and the modem isn't ready yet, ask it for
     * the current PIN status a few times since some devices take a bit to fully
     * enable themselves after a SIM PIN/PUK unlock.
     */
    if (info->error && !g_error_matches (info->error, MM_MODEM_ERROR, MM_MODEM_ERROR_REMOVED)) {
        if (priv->pin_check_tries < 4) {
            g_clear_error (&info->error);
            priv->pin_check_tries++;
            priv->pin_check_timeout = g_timeout_add_seconds (2, pin_puk_recheck_again, info);
            return;
        }
    }

    /* Otherwise, clean up and return the PIN check result */
    port = mm_callback_info_get_data (info, PIN_PORT_TAG);
    if (port)
        mm_serial_port_close (port);

    /* If we have a saved error from sending PIN/PUK, return that to callers */
    saved_error = mm_callback_info_get_data (info, SAVED_ERROR_TAG);
    if (saved_error) {
        if (!mm_modem_base_get_unlock_required (MM_MODEM_BASE (info->modem))) {
            /* Original unlock failed but the modem is actually unlocked, so
             * return success.  Sometimes happens if the modem doesn't allow
             * CPIN="xxxx" when it's already unlocked and returns an error.
             * Do nothing.
             */
        } else {
            /* Unlock failed after recheck, return original error */
            g_clear_error (&info->error);
            info->error = g_error_copy (saved_error);
        }
    }

    mm_callback_info_schedule (info);
}

static void
send_puk_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        if (error->domain != MM_MOBILE_ERROR) {
            info->error = g_error_copy (error);
            mm_callback_info_schedule (info);
            mm_serial_port_close (MM_SERIAL_PORT (port));
            return;
        } else {
            /* Keep the real error around so we can send it back
             * when we're done rechecking CPIN status.
             */
            mm_callback_info_set_data (info, SAVED_ERROR_TAG,
                                       g_error_copy (error),
                                       (GDestroyNotify) g_error_free);
        }
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        if (error->domain != MM_MOBILE_ERROR) {
            info->error = g_error_copy (error);
            mm_callback_info_schedule (info);
            mm_serial_port_close (MM_SERIAL_PORT (port));
            return;
        } else {
            /* Keep the real error around so we can send it back
             * when we're done rechecking CPIN status.
             */
            mm_callback_info_set_data (info, SAVED_ERROR_TAG,
                                       g_error_copy (error),
                                       (GDestroyNotify) g_error_free);
        }
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

        old_status = gsm_reg_status (self, NULL);
        priv->reg_status[rs_type - 1] = status;
        if (gsm_reg_status (self, NULL) != old_status)
            changed = TRUE;
    }

    /* Don't clear oper code or oper num if at least one of CS or PS state
     * is home or roaming.
     */
    if (   priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[0] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING
        || priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        || priv->reg_status[1] == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {
        if (update_code && oper_code == NULL)
            update_code = FALSE;
        if (update_name && oper_name == NULL)
            update_name = FALSE;
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
                                                gsm_reg_status (self, NULL),
                                                priv->oper_code,
                                                priv->oper_name);
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

    if (operator) {
        /* Some modems (Option & HSO) return the operator name as a hexadecimal
         * string of the bytes of the operator name as encoded by the current
         * character set.
         */
        if (cur_charset == MM_MODEM_CHARSET_UCS2)
            operator = mm_charset_take_and_convert_to_utf8 (operator, MM_MODEM_CHARSET_UCS2);

        /* Ensure the operator name is valid UTF-8 so that we can send it
         * through D-Bus and such.
         */
        if (!g_utf8_validate (operator, -1, NULL)) {
            g_free (operator);
            operator = NULL;
        }
    }

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
    mm_info ("Disconnected because roaming is not allowed");
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

    mm_dbg ("%s registration state changed: %d",
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
    guint32 state = 0;
    gulong lac = 0, cell_id = 0;
    gint act = -1;
    gboolean cgreg = FALSE;
    GError *error = NULL;

    if (!mm_gsm_parse_creg_response (match_info, &state, &lac, &cell_id, &act, &cgreg, &error)) {
        mm_warn ("error parsing unsolicited registration: %s",
                 error && error->message ? error->message : "(unknown)");
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

    update_lac_ci (self, lac, cell_id, cgreg ? 1 : 0);

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
    guint32 status = 0;
    gulong lac = 0, ci = 0;
    gint act = -1;
    gboolean cgreg = FALSE, parsed;
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
    parsed = mm_gsm_parse_creg_response (match_info, &status, &lac, &ci, &act, &cgreg, error);
    g_match_info_free (match_info);
    if (parsed) {
        /* Success; update cached location information */
        update_lac_ci (self, lac, ci, cgreg ? 1 : 0);

        /* Only update access technology if it appeared in the CREG/CGREG response */
        if (act != -1)
            mm_generic_gsm_update_access_technology (self, etsi_act_to_mm_act (act));

        if (status >= 0) {
            /* Update cached registration status */
            reg_status_updated (self, cgreg_to_reg_type (cgreg), status, NULL);
        }
    }

    return parsed;
}

#define CS_ERROR_TAG "cs-error"
#define CS_DONE_TAG "cs-complete"
#define PS_ERROR_TAG "ps-error"
#define PS_DONE_TAG "ps-complete"

static void
check_reg_status_done (MMCallbackInfo *info)
{
    MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GError *cs_error, *ps_error;
    MMModemGsmNetworkRegStatus status;
    guint id;

    /* This function should only get called during the connect sequence when
     * polling for registration state, since explicit registration requests
     * from D-Bus clients are filled from the cached registration state.
     */
    g_return_if_fail (info == priv->pending_reg_info);

    /* Only process when both CS and PS checks are both done */
    if (   !mm_callback_info_get_data (info, CS_DONE_TAG)
        || !mm_callback_info_get_data (info, PS_DONE_TAG))
        return;

    /* If both CS and PS registration checks returned errors we fail */
    cs_error = mm_callback_info_get_data (info, CS_ERROR_TAG);
    ps_error = mm_callback_info_get_data (info, PS_ERROR_TAG);
    if (cs_error && ps_error) {
        /* Prefer the PS error */
        info->error = g_error_copy (ps_error);
        goto reg_done;
    }

    status = gsm_reg_status (self, NULL);
    if (   status != MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        && status != MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING
        && status != MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED) {

        /* Clear state that should be reset every poll */
        mm_callback_info_set_data (info, CS_DONE_TAG, NULL, NULL);
        mm_callback_info_set_data (info, CS_ERROR_TAG, NULL, NULL);
        mm_callback_info_set_data (info, PS_DONE_TAG, NULL, NULL);
        mm_callback_info_set_data (info, PS_ERROR_TAG, NULL, NULL);

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
generic_reg_status_done (MMCallbackInfo *info,
                         GString *response,
                         GError *error,
                         const char *error_tag,
                         const char *done_tag)
{
    MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
    GError *local = NULL;

    if (error)
        local = g_error_copy (error);
    else if (strlen (response->str)) {
        /* Unsolicited registration status handlers will usually process the
         * response for us, but just in case they don't, do that here.
         */
        if (handle_reg_status_response (self, response, &local) == TRUE)
            g_assert_no_error (local);
    }

    if (local)
        mm_callback_info_set_data (info, error_tag, local, (GDestroyNotify) g_error_free);
    mm_callback_info_set_data (info, done_tag, GUINT_TO_POINTER (1), NULL);
}

static void
get_ps_reg_status_done (MMAtSerialPort *port,
                        GString *response,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (mm_callback_info_check_modem_removed (info) == FALSE) {
        generic_reg_status_done (info, response, error, PS_ERROR_TAG, PS_DONE_TAG);
        check_reg_status_done (info);
    }
}

static void
get_cs_reg_status_done (MMAtSerialPort *port,
                        GString *response,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (mm_callback_info_check_modem_removed (info) == FALSE) {
        generic_reg_status_done (info, response, error, CS_ERROR_TAG, CS_DONE_TAG);
        check_reg_status_done (info);
    }
}

static void
get_registration_status (MMAtSerialPort *port, MMCallbackInfo *info)
{
    mm_at_serial_port_queue_command (port, "+CREG?", 10, get_cs_reg_status_done, info);
    mm_at_serial_port_queue_command (port, "+CGREG?", 10, get_ps_reg_status_done, info);
}

static void
register_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsmPrivate *priv;

    mm_callback_info_unref (info);

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    /* If the registration timed out (and thus pending_reg_info will be NULL)
     * and the modem eventually got around to sending the response for the
     * registration request then just ignore the response since the callback is
     * already called.
     */

    if (priv->pending_reg_info) {
        g_warn_if_fail (info == priv->pending_reg_info);
        if (error) {
            g_clear_error (&info->error);
            info->error = g_error_copy (error);
        }

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
    } else if (reg_is_idle (gsm_reg_status (self, NULL)) || priv->manual_reg) {
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
     * and call the callback from get_[cs|ps]_reg_status_done() in response to
     * the polled response.  The registration timeout will only be triggered
     * when the +COPS response is never received.
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
              gsm_reg_status (MM_GENERIC_GSM (info->modem), NULL),
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
    MMGenericGsmPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

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
    gint cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (modem));

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

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        /* Reset old state since the operation failed */
        prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_GSM_PREV_STATE_TAG));
        mm_modem_set_state (MM_MODEM (info->modem),
                            prev_state,
                            MM_MODEM_STATE_REASON_NONE);
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
    MMCallbackInfo *info = (MMCallbackInfo *)user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    mm_callback_info_schedule (info);
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter.  Other errors are hard errors.
         */
        if (   !g_error_matches (error, MM_MODEM_CONNECT_ERROR, MM_MODEM_CONNECT_ERROR_NO_CARRIER)
            && !g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_FLASH_FAILED)) {
            info->error = g_error_copy (error);
            mm_callback_info_schedule (info);
            return;
        }
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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

            if (cid == 0) {
                /* Choose something */
                cid = 1;
            }

            g_match_info_free (match_info);
            g_regex_unref (r);
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        /* Some Android phones don't support querying existing PDP contexts,
         * but will accept setting the APN.  So if CGDCONT? isn't supported,
         * just ignore that error and hope for the best. (bgo #637327)
         */
        if (g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_NOT_SUPPORTED) == FALSE)
            info->error = g_error_copy (error);
    } else if (g_str_has_prefix (response->str, "+CGDCONT:")) {
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
    priv->signal_emit_timestamp = time (NULL);
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

    priv->signal_update_timestamp = time (NULL);

    if (priv->signal_quality == quality)
        return;

    priv->signal_quality = quality;

    /* Some modems will send unsolcited signal quality changes quite often,
     * so rate-limit them to every few seconds.  Track the last time we
     * emitted signal quality so that we send the signal immediately if there
     * haven't been any updates in a while.
     */
    if (!priv->signal_quality_id) {
        if (priv->signal_emit_timestamp > 0) {
            time_t curtime;
            long int diff;

            curtime = time (NULL);
            diff = curtime - priv->signal_emit_timestamp;
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

#define CIND_TAG "+CIND:"

static void
get_cind_signal_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;
    GByteArray *indicators;
    guint quality;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
        priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        indicators = mm_parse_cind_query_response (response->str, &info->error);
        if (indicators) {
            if (indicators->len >= priv->signal_ind) {
                quality = g_array_index (indicators, guint8, priv->signal_ind);
                quality = CLAMP (quality, 0, 5) * 20;
                mm_generic_gsm_update_signal_quality (MM_GENERIC_GSM (info->modem), quality);
                mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
            }
            g_byte_array_free (indicators, TRUE);
        }
    }

    mm_callback_info_schedule (info);
}

static void
get_csq_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply;
    gboolean parsed = FALSE;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    reply = response->str;
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
    if (port) {
        /* Prefer +CIND if the modem supports it, fall back to +CSQ otherwise */
        if (priv->signal_ind)
            mm_at_serial_port_queue_command (port, "+CIND?", 3, get_cind_signal_done, info);
        else
            mm_at_serial_port_queue_command (port, "+CSQ", 3, get_csq_done, info);
    } else {
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

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        gboolean tried_no_quotes = !!mm_callback_info_get_data (info, TRIED_NO_QUOTES_TAG);
        MMModemCharset charset = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "charset"));
        char *command;

        if (tried_no_quotes) {
            info->error = g_error_copy (error);
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

    if (!(priv->charsets & charset) || !utils_check_for_single_value (charset)) {
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
sms_send_invoke (MMCallbackInfo *info)
{
    MMModemGsmSmsSendFn callback = (MMModemGsmSmsSendFn) info->callback;

    callback (MM_MODEM_GSM_SMS (info->modem),
              mm_callback_info_get_data (info, "indexes"),
              info->error,
              info->user_data);
}

static void
free_indexes (gpointer data)
{
    g_array_free ((GArray *) data, TRUE);
}

static void
sms_send_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p, *pdu;
    unsigned long num;
    GArray *indexes = NULL;
    guint32 idx = 0;
    guint cmgs_pdu_size;
    char *command;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

        /* If there was an error sending in text mode the retry with the PDU;
         * text mode is pretty dumb on most devices and often fails.  Later we'll
         * just use text mode exclusively.
         */
        pdu = mm_callback_info_get_data (info, "pdu");
        if (priv->sms_pdu_mode == FALSE && priv->sms_pdu_supported && pdu) {
            cmgs_pdu_size = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "cmgs-pdu-size"));
            g_assert (cmgs_pdu_size);
            command = g_strdup_printf ("+CMGS=%d\r%s\x1a", cmgs_pdu_size, pdu);
            mm_at_serial_port_queue_command (port, command, 10, sms_send_done, info);
            g_free (command);

            /* Clear the PDU data so we don't keep getting here */
            mm_callback_info_set_data (info, "pdu", NULL, NULL);
            return;
        }

        /* Otherwise it's a hard error */
        info->error = g_error_copy (error);
    } else {
        /* If the response happens to have a ">" in it from the interactive
         * handling of the CMGS command, skip it.
         */
        p = strchr (response->str, '+');
        if (p && *p) {
            /* Check for the message index */
            p = mm_strip_tag (p, "+CMGS:");
            if (p && *p) {
                errno = 0;
                num = strtoul (p, NULL, 10);
                if ((num < G_MAXUINT32) && (errno == 0))
                    idx = (guint32) num;
            }
        }
        indexes = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 1);
        g_array_append_val (indexes, idx);
        mm_callback_info_set_data (info, "indexes", indexes, free_indexes);
    }
    mm_callback_info_schedule (info);
}

static void
sms_send (MMModemGsmSms *modem,
          const char *number,
          const char *text,
          const char *smsc,
          guint validity,
          guint class,
          MMModemGsmSmsSendFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    char *command;
    MMAtSerialPort *port;
    guint8 *pdu;
    guint pdulen = 0, msgstart = 0;
    char *hex;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      sms_send_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    port = mm_generic_gsm_get_best_at_port (self, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Always create a PDU since we might need it for fallback from text mode */
    pdu = sms_create_submit_pdu (number, text, smsc, validity, class, &pdulen, &msgstart, &info->error);
    if (!pdu) {
        mm_callback_info_schedule (info);
        return;
    }

    hex = utils_bin2hexstr (pdu, pdulen);
    g_free (pdu);
    if (hex == NULL) {
        g_set_error_literal (&info->error,
                             MM_MODEM_ERROR,
                             MM_MODEM_ERROR_GENERAL,
                             "Not enough memory to send SMS PDU");
        mm_callback_info_schedule (info);
        return;
    }

    mm_callback_info_set_data (info, "pdu", hex, g_free);
    mm_callback_info_set_data (info, "cmgs-pdu-size", GUINT_TO_POINTER (pdulen - msgstart), NULL);
    if (priv->sms_pdu_mode) {
        /* CMGS length is the size of the PDU without SMSC information */
        command = g_strdup_printf ("+CMGS=%d\r%s\x1a", pdulen - msgstart, hex);
    } else
        command = g_strdup_printf ("+CMGS=\"%s\"\r%s\x1a", number, text);

    mm_at_serial_port_queue_command (port, command, 10, sms_send_done, info);
    g_free (command);
}

static void
sms_get_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);
    GHashTable *properties;
    int rv, status, tpdu_len;
    guint idx;
    char pdu[SMS_MAX_PDU_LEN + 1];
    gboolean look_for_complete;

    idx = priv->sms_fetch_pending;
    priv->sms_fetch_pending = 0;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    /* 344 == SMS_MAX_PDU_LEN */
    rv = sscanf (response->str, "+CMGR: %d,,%d %344s",
                 &status, &tpdu_len, pdu);
    if (rv != 3) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Failed to parse CMGR response (parsed %d items)",
                                   rv);
        goto out;
    }

    properties = sms_parse_pdu (pdu, &info->error);
    if (!properties) {
        goto out;
    }

    g_hash_table_insert (properties, "index", simple_uint_value (idx));
    sms_cache_insert (info->modem, properties, idx);

    look_for_complete = GPOINTER_TO_UINT (mm_callback_info_get_data(info,
                                                           "complete-sms-only"));

    if (look_for_complete == TRUE) {
        /*
         * If this is a standalone message, or the key part of a
         * multipart message, pass it along, otherwise report that there's
         * no such message.
         */
        properties = sms_cache_lookup_full (info->modem, properties,
                                            &info->error);
    }
    if (properties)
        mm_callback_info_set_data (info, GS_HASH_TAG, properties,
                                   (GDestroyNotify) g_hash_table_unref);

out:
    mm_callback_info_schedule (info);
}

static void
sms_get_invoke (MMCallbackInfo *info)
{
    MMModemGsmSmsGetFn callback = (MMModemGsmSmsGetFn) info->callback;

    callback (MM_MODEM_GSM_SMS (info->modem),
              (GHashTable *) mm_callback_info_get_data (info, GS_HASH_TAG),
              info->error, info->user_data);
}

static void
sms_get (MMModemGsmSms *modem,
         guint idx,
         MMModemGsmSmsGetFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;
    MMGenericGsmPrivate *priv =
            MM_GENERIC_GSM_GET_PRIVATE (MM_GENERIC_GSM (modem));
    GHashTable *properties;
    GError *error = NULL;

    properties = g_hash_table_lookup (priv->sms_contents, GUINT_TO_POINTER (idx));
    if (properties != NULL) {
        g_hash_table_ref (properties);
        properties = sms_cache_lookup_full (MM_MODEM (modem), properties, &error);
        if (properties == NULL) {
            error = g_error_new (MM_MODEM_ERROR,
                                 MM_MODEM_ERROR_GENERAL,
                                 "No SMS found");
        }
        callback (modem, properties, error, user_data);
        if (properties != NULL)
            g_hash_table_unref (properties);
        g_error_free (error);
        return;
    }

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      sms_get_invoke,
                                      G_CALLBACK (callback),
                                      user_data);
    mm_callback_info_set_data (info,
                               "complete-sms-only",
                               GUINT_TO_POINTER (TRUE),
                               NULL);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    command = g_strdup_printf ("+CMGR=%d", idx);
    priv->sms_fetch_pending = idx;
    mm_at_serial_port_queue_command (port, command, 10, sms_get_done, info);
}

typedef struct {
    MMGenericGsmPrivate *priv;
    MMCallbackInfo *info;
    SMSMultiPartMessage *mpm;
    int deleting;
} SMSDeleteProgress;

static void
sms_delete_done (MMAtSerialPort *port,
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
sms_delete_multi_next (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    SMSDeleteProgress *progress = (SMSDeleteProgress *)user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (progress->info))
        goto done;

    if (error)
        progress->info->error = g_error_copy (error);

    for (progress->deleting++ ;
         progress->deleting < progress->mpm->numparts ;
         progress->deleting++)
        if (progress->mpm->parts[progress->deleting] != 0)
            break;
    if (progress->deleting < progress->mpm->numparts) {
        GHashTable *properties;
        char *command;
        guint idx;

        idx = progress->mpm->parts[progress->deleting];
        command = g_strdup_printf ("+CMGD=%d", idx);
        mm_at_serial_port_queue_command (port, command, 10,
                                         sms_delete_multi_next, progress);
        properties = g_hash_table_lookup (progress->priv->sms_contents, GUINT_TO_POINTER (idx));
        g_hash_table_remove (progress->priv->sms_contents, GUINT_TO_POINTER (idx));
        g_hash_table_remove (progress->priv->sms_present, GUINT_TO_POINTER (idx));
        g_hash_table_unref (properties);
        return;
    }

    mm_callback_info_schedule (progress->info);
done:
    g_free (progress->mpm->parts);
    g_free (progress->mpm);
    g_free (progress);
}

static void
sms_delete (MMModemGsmSms *modem,
            guint idx,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (MM_GENERIC_GSM (modem));
    GHashTable *properties;
    MMAtSerialResponseFn next_callback;
    GValue *ref;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    properties = g_hash_table_lookup (priv->sms_contents, GINT_TO_POINTER (idx));
    if (properties == NULL) {
        /*
         * TODO(njw): This assumes our cache is valid. If we doubt this, we should just
         * run the delete anyway and let that return the nonexistent-message error.
         */
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "No SMS to delete");
        mm_callback_info_schedule (info);
        return;
    }

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        g_hash_table_remove (priv->sms_contents, GINT_TO_POINTER (idx));
        g_hash_table_unref (properties);
        return;
    }

    user_data = info;
    next_callback = sms_delete_done;
    ref = g_hash_table_lookup (properties, "concat-reference");
    if (ref != NULL) {
        SMSMultiPartMessage *mpm;
        SMSDeleteProgress *progress;
        guint refnum;

        refnum = g_value_get_uint (ref);
        mpm = g_hash_table_lookup (priv->sms_parts, GUINT_TO_POINTER (refnum));
        if (mpm == NULL) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Internal error - no part array for multipart SMS");
            mm_callback_info_schedule (info);
            g_hash_table_remove (priv->sms_contents, GINT_TO_POINTER (idx));
            g_hash_table_unref (properties);
            return;
        }
        /* Only allow the delete operation on the main index number. */
        if (idx != mpm->index) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "No SMS to delete");
            mm_callback_info_schedule (info);
            return;
        }

        g_hash_table_remove (priv->sms_parts, GUINT_TO_POINTER (refnum));
        progress = g_malloc0 (sizeof(*progress));
        progress->priv = priv;
        progress->info = info;
        progress->mpm = mpm;
        for (progress->deleting = 0 ;
             progress->deleting < mpm->numparts ;
             progress->deleting++)
            if (mpm->parts[progress->deleting] != 0)
                break;
        user_data = progress;
        next_callback = sms_delete_multi_next;
        idx = progress->mpm->parts[progress->deleting];
        properties = g_hash_table_lookup (priv->sms_contents, GINT_TO_POINTER (idx));
    }
    g_hash_table_remove (priv->sms_contents, GUINT_TO_POINTER (idx));
    g_hash_table_remove (priv->sms_present, GUINT_TO_POINTER (idx));
    g_hash_table_unref (properties);

    command = g_strdup_printf ("+CMGD=%d", idx);
    mm_at_serial_port_queue_command (port, command, 10, next_callback,
                                     user_data);
}

static gboolean
pdu_parse_cmgl (MMGenericGsm *self, const char *response, GError **error)
{
    int rv, status, tpdu_len, offset;
    GHashTable *properties;

    while (*response) {
        int idx;
        char pdu[SMS_MAX_PDU_LEN + 1];

        rv = sscanf (response, "+CMGL: %d,%d,,%d %344s %n",
                     &idx, &status, &tpdu_len, pdu, &offset);
        if (4 != rv) {
            g_set_error (error,
                         MM_MODEM_ERROR,
                         MM_MODEM_ERROR_GENERAL,
                         "Failed to parse CMGL response: expected 4 results got %d", rv);
            mm_err("Couldn't parse response to SMS LIST (%d)", rv);
            return FALSE;
        }
        response += offset;

        properties = sms_parse_pdu (pdu, NULL);
        if (properties) {
            g_hash_table_insert (properties, "index", simple_uint_value (idx));
            sms_cache_insert (MM_MODEM (self), properties, idx);
            /* The cache holds a reference, so we don't need it anymore */
            g_hash_table_unref (properties);
        }
    }

    return TRUE;
}

static gboolean
get_match_uint (GMatchInfo *m, guint match_index, guint *out_val)
{
    char *s;
    unsigned long num;

    g_return_val_if_fail (out_val != NULL, FALSE);

    s = g_match_info_fetch (m, match_index);
    g_return_val_if_fail (s != NULL, FALSE);

    errno = 0;
    num = strtoul (s, NULL, 10);
    g_free (s);

    if (num <= 1000 && errno == 0) {
        *out_val = (guint) num;
        return TRUE;
    }
    return FALSE;
}

static char *
get_match_string_unquoted (GMatchInfo *m, guint match_index)
{
    char *s, *p, *q, *ret = NULL;

    q = s = g_match_info_fetch (m, match_index);
    g_return_val_if_fail (s != NULL, FALSE);

    /* remove quotes */
    if (*q == '"')
        q++;
    p = strchr (q, '"');
    if (p)
        *p = '\0';
    if (*q)
        ret = g_strdup (q);
    g_free (s);
    return ret;
}

static gboolean
text_parse_cmgl (MMGenericGsm *self, const char *response, GError **error)
{
    MMGenericGsmPrivate *priv;
    GRegex *r;
    GMatchInfo *match_info = NULL;

    priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    /* +CMGL: <index>,<stat>,<oa/da>,[alpha],<scts><CR><LF><data><CR><LF> */
    r = g_regex_new ("\\+CMGL:\\s*(\\d+)\\s*,\\s*([^,]*),\\s*([^,]*),\\s*([^,]*),\\s*([^\\r\\n]*)\\r\\n([^\\r\\n]*)", 0, 0, NULL);
    g_assert (r);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to parse CMGL response");
        mm_err("Couldn't parse response to SMS LIST");
        g_regex_unref (r);
        return FALSE;
    }

    while (g_match_info_matches (match_info)) {
        GHashTable *properties;
        guint matches, idx;
        char *number = NULL, *timestamp, *text, *ucs2_text;
        gsize ucs2_len = 0;
        GByteArray *data;

        matches = g_match_info_get_match_count (match_info);
        if (matches != 7) {
            mm_dbg ("Failed to match entire CMGL response (count %d)", matches);
            goto next;
        }

        if (!get_match_uint (match_info, 1, &idx)) {
            mm_dbg ("Failed to convert message index");
            goto next;
        }

        /* <stat is ignored for now> */

        /* Get and parse number */
        number = get_match_string_unquoted (match_info, 3);
        if (!number) {
            mm_dbg ("Failed to get message sender number");
            goto next;
        }
        number = mm_charset_take_and_convert_to_utf8 (number,
                                                      priv->cur_charset);

        /* Get and parse timestamp (always expected in ASCII) */
        timestamp = get_match_string_unquoted (match_info, 5);

        /* Get and parse text */
        text = mm_charset_take_and_convert_to_utf8 (g_match_info_fetch (match_info, 6),
                                                    priv->cur_charset);

        /* The raw SMS data can only be GSM, UCS2, or unknown (8-bit), so we
         * need to convert to UCS2 here.
         */
        ucs2_text = g_convert (text, -1, "UCS-2BE//TRANSLIT", "UTF-8", NULL, &ucs2_len, NULL);
        g_assert (ucs2_text);
        data = g_byte_array_sized_new (ucs2_len);
        g_byte_array_append (data, (const guint8 *) ucs2_text, ucs2_len);
        g_free (ucs2_text);

        properties = sms_properties_hash_new (NULL,
                                              number,
                                              timestamp,
                                              text,
                                              data,
                                              2,   /* DCS = UCS2 */
                                              0);  /* class */
        g_assert (properties);

        g_free (number);
        g_free (timestamp);
        g_free (text);
        g_byte_array_free (data, TRUE);

        g_hash_table_insert (properties, "index", simple_uint_value (idx));
        sms_cache_insert (MM_MODEM (self), properties, idx);
        /* The cache holds a reference, so we don't need it anymore */
        g_hash_table_unref (properties);

next:
        g_match_info_next (match_info, NULL);
    }
    g_match_info_free (match_info);

    g_regex_unref (r);
    return TRUE;
}

static void
free_list_results (gpointer data)
{
    GPtrArray *results = (GPtrArray *) data;

    g_ptr_array_foreach (results, (GFunc) g_hash_table_unref, NULL);
    g_ptr_array_free (results, TRUE);
}

static void
sms_list_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *self = MM_GENERIC_GSM (info->modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GHashTableIter iter;
    GHashTable *properties = NULL;
    GPtrArray *results = NULL;
    gboolean success;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    if (priv->sms_pdu_mode)
        success = pdu_parse_cmgl (self, response->str, &info->error);
    else
        success = text_parse_cmgl (self, response->str, &info->error);

    if (success) {
        results = g_ptr_array_new ();

        /* Add all the complete messages to the results */
        g_hash_table_iter_init (&iter, priv->sms_contents);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &properties)) {
            g_hash_table_ref (properties);
            g_clear_error (&info->error);
            properties = sms_cache_lookup_full (info->modem, properties, NULL);
            if (properties)
                g_ptr_array_add (results, properties);
        }

        if (results)
            mm_callback_info_set_data (info, "list-sms", results, free_list_results);
    }

    mm_callback_info_schedule (info);
}

static void
sms_list_invoke (MMCallbackInfo *info)
{
    MMModemGsmSmsListFn callback = (MMModemGsmSmsListFn) info->callback;

    callback (MM_MODEM_GSM_SMS (info->modem),
              (GPtrArray *) mm_callback_info_get_data (info, "list-sms"),
              info->error, info->user_data);
}

static void
sms_list (MMModemGsmSms *modem,
          MMModemGsmSmsListFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      sms_list_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    if (MM_GENERIC_GSM_GET_PRIVATE (modem)->sms_pdu_mode)
        command = g_strdup_printf ("+CMGL=4");
    else
        command = g_strdup_printf ("+CMGL=\"ALL\"");
    mm_at_serial_port_queue_command (port, command, 10, sms_list_done, info);
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
/* MMModemGsmUssd interface */

static void
ussd_update_state (MMGenericGsm *self, MMModemGsmUssdState new_state)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (new_state != priv->ussd_state) {
        priv->ussd_state = new_state;
        g_object_notify (G_OBJECT (self), MM_MODEM_GSM_USSD_STATE);
    }
}

void
mm_generic_gsm_ussd_cleanup (MMGenericGsm *self)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);

    if (priv->pending_ussd_info) {
        /* And schedule the callback */
        g_clear_error (&priv->pending_ussd_info->error);
        priv->pending_ussd_info->error = g_error_new_literal (MM_MODEM_ERROR,
                                                              MM_MODEM_ERROR_GENERAL,
                                                              "USSD session terminated without reply.");
        mm_callback_info_schedule (priv->pending_ussd_info);
        priv->pending_ussd_info = NULL;
    }

    ussd_update_state (self, MM_MODEM_GSM_USSD_STATE_IDLE);

    g_free (priv->ussd_network_request);
    priv->ussd_network_request = NULL;
    g_object_notify (G_OBJECT (self), MM_MODEM_GSM_USSD_NETWORK_REQUEST);

    g_free (priv->ussd_network_notification);
    priv->ussd_network_notification = NULL;
    g_object_notify (G_OBJECT (self), MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION);
}

static char *
decode_ussd_response (MMGenericGsm *self,
                      const char *reply,
                      MMModemCharset cur_charset)
{
    char **items, **iter, *p;
    char *str = NULL;
    gint encoding = -1;
    char *decoded;

    /* Look for the first ',' */
    p = strchr (reply, ',');
    if (p == NULL)
        return NULL;

    items = g_strsplit_set (p + 1, " ,", -1);
    for (iter = items; iter && *iter; iter++) {
        if (*iter[0] == '\0')
            continue;
        if (str == NULL)
            str = *iter;
        else if (encoding == -1) {
            encoding = atoi (*iter);
            mm_dbg ("USSD data coding scheme %d", encoding);
            break;  /* All done */
        }
    }

    if (!str)
        return NULL;

    /* Strip quotes */
    if (str[0] == '"')
        str++;
    p = strchr (str, '"');
    if (p)
        *p = '\0';

    decoded = mm_modem_gsm_ussd_decode (MM_MODEM_GSM_USSD (self), str, cur_charset);
    g_strfreev (items);
    return decoded;
}

static char*
ussd_encode (MMModemGsmUssd *modem, const char* command, guint *scheme)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    GByteArray *ussd_command = g_byte_array_new();
    gboolean success;
    char *hex = NULL;

    /* encode to cur_charset */
    success = mm_modem_charset_byte_array_append (ussd_command, command, FALSE,
                                                  priv->cur_charset);
    g_warn_if_fail (success == TRUE);
    if (!success)
        goto out;

    *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;
    /* convert to hex representation */
    hex = utils_bin2hexstr (ussd_command->data, ussd_command->len);

 out:
    g_byte_array_free (ussd_command, TRUE);
    return hex;
}

static char*
ussd_decode (MMModemGsmUssd *modem, const char* reply, guint scheme)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    char *converted;

    converted = mm_modem_charset_hex_to_utf8 (reply, priv->cur_charset);
    return converted;
}

static void
cusd_received (MMAtSerialPort *port,
               GMatchInfo *info,
               gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (user_data);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GError *error = NULL;
    gint status;
    MMModemGsmUssdState ussd_state = MM_MODEM_GSM_USSD_STATE_IDLE;
    char *reply = NULL, *converted;

    reply = g_match_info_fetch (info, 1);
    if (!reply || !isdigit (*reply)) {
        mm_warn ("Recieved invalid USSD response: '%s'", reply ? reply : "(none)");
        g_free (reply);
        return;
    }

    status = g_ascii_digit_value (*reply);
    switch (status) {
    case 0: /* no further action required */
        converted = decode_ussd_response (self, reply, priv->cur_charset);
        if (priv->pending_ussd_info) {
            /* Response to the user's request */
            mm_callback_info_set_result (priv->pending_ussd_info, converted, g_free);
        } else {
            /* Network-initiated USSD-Notify */
            g_free (priv->ussd_network_notification);
            priv->ussd_network_notification = converted;
            g_object_notify (G_OBJECT (self), MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION);
        }
        break;
    case 1: /* further action required */
        ussd_state = MM_MODEM_GSM_USSD_STATE_USER_RESPONSE;
        converted = decode_ussd_response (self, reply, priv->cur_charset);
        if (priv->pending_ussd_info) {
            mm_callback_info_set_result (priv->pending_ussd_info, converted, g_free);
        } else {
            /* Network-initiated USSD-Request */
            g_free (priv->ussd_network_request);
            priv->ussd_network_request = converted;
            g_object_notify (G_OBJECT (self), MM_MODEM_GSM_USSD_NETWORK_REQUEST);
        }
        break;
    case 2:
        error = g_error_new_literal (MM_MODEM_ERROR,
                                     MM_MODEM_ERROR_GENERAL,
                                     "USSD terminated by network.");
        break;
    case 4:
        error = g_error_new_literal (MM_MODEM_ERROR,
                                     MM_MODEM_ERROR_GENERAL,
                                     "Operation not supported.");
        break;
    default:
        error = g_error_new (MM_MODEM_ERROR,
                             MM_MODEM_ERROR_GENERAL,
                             "Unhandled USSD reply %d", status);
        break;
    }

    ussd_update_state (self, ussd_state);

    if (priv->pending_ussd_info) {
        if (error)
            priv->pending_ussd_info->error = g_error_copy (error);
        mm_callback_info_schedule (priv->pending_ussd_info);
        priv->pending_ussd_info = NULL;
    }

    g_clear_error (&error);
    g_free (reply);
}

static void
ussd_send_done (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsmPrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_GENERIC_GSM_GET_PRIVATE (info->modem);

    if (error) {
        /* Some immediate error happened when sending the USSD request */
        info->error = g_error_copy (error);
        priv->pending_ussd_info = NULL;
        mm_callback_info_schedule (info);

        ussd_update_state (MM_GENERIC_GSM (info->modem), MM_MODEM_GSM_USSD_STATE_IDLE);
    }

    /* Otherwise if no error wait for the response to show up via the
     * unsolicited response code.
     */
}

static void
ussd_send (MMModemGsmUssd *modem,
           const char *command,
           MMModemStringFn callback,
           gpointer user_data)
{
    MMCallbackInfo *info;
    char *atc_command;
    char *hex;
    guint scheme = 0;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);
    MMAtSerialPort *port;

    g_warn_if_fail (priv->pending_ussd_info == NULL);

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Cache the callback info since the response is an unsolicited one */
    priv->pending_ussd_info = info;

    hex = mm_modem_gsm_ussd_encode (MM_MODEM_GSM_USSD (modem), command, &scheme);
    if (!hex) {
        info->error =  g_error_new (MM_MODEM_ERROR,
                                    MM_MODEM_ERROR_GENERAL,
                                    "Failed to encode USSD command '%s'",
                                    command);
        mm_callback_info_schedule (info);
        return;
    }
    atc_command = g_strdup_printf ("+CUSD=1,\"%s\",%d", hex, scheme);
    g_free (hex);

    mm_at_serial_port_queue_command (port, atc_command, 10, ussd_send_done, info);
    g_free (atc_command);

    ussd_update_state (MM_GENERIC_GSM (modem), MM_MODEM_GSM_USSD_STATE_ACTIVE);
}

static void
ussd_initiate (MMModemGsmUssd *modem,
               const char *command,
               MMModemStringFn callback,
               gpointer user_data)
{
    MMCallbackInfo *info;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->ussd_state != MM_MODEM_GSM_USSD_STATE_IDLE) {
        info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "USSD session already active.");
        mm_callback_info_schedule (info);
    } else {
        ussd_send (modem, command, callback, user_data);
    }
}

static void
ussd_respond (MMModemGsmUssd *modem,
              const char *command,
              MMModemStringFn callback,
              gpointer user_data)
{
    MMCallbackInfo *info;
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->ussd_state != MM_MODEM_GSM_USSD_STATE_USER_RESPONSE) {
        info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "No active USSD session, cannot respond.");
        mm_callback_info_schedule (info);
    } else {
        ussd_send (modem, command, callback, user_data);
    }
}

static void
ussd_cancel_done (MMAtSerialPort *port,
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

    ussd_update_state (MM_GENERIC_GSM (info->modem), MM_MODEM_GSM_USSD_STATE_IDLE);
}

static void
ussd_cancel (MMModemGsmUssd *modem,
             MMModemFn callback,
             gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+CUSD=2", 10, ussd_cancel_done, info);
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
    char *data_device;

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    g_object_get (G_OBJECT (modem), MM_MODEM_DATA_DEVICE, &data_device, NULL);
    mm_dbg ("(%s): simple connect state %d", data_device, state);
    g_free (data_device);

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
                status = gsm_reg_status (MM_GENERIC_GSM (modem), NULL);
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
    GHashTableIter iter;
    gpointer key, value;
    char *data_device;

    /* List simple connect properties when debugging */
    g_object_get (G_OBJECT (simple), MM_MODEM_DATA_DEVICE, &data_device, NULL);
    g_hash_table_iter_init (&iter, properties);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        char *val_str;

        val_str = g_strdup_value_contents ((GValue *) value);
        mm_dbg ("(%s): %s => %s", data_device, (const char *) key, val_str);
        g_free (val_str);
    }
    g_free (data_device);

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
    gboolean error_no_network = FALSE;

    /* Treat "no network" as zero strength */
    if (g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_NO_NETWORK)) {
        error_no_network = TRUE;
        result = 0;
    }

    if (!error || error_no_network) {
        properties = (GHashTable *) mm_callback_info_get_data (info, SS_HASH_TAG);
        g_hash_table_insert (properties, "signal_quality", simple_uint_value (result));
    } else {
        g_clear_error (&info->error);
        info->error = g_error_copy (error);
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
    } else {
        g_clear_error (&info->error);
        info->error = g_error_copy (error);
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

    /* Do nothing if modem removed */
    if (!modem || mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        g_clear_error (&info->error);
        info->error = g_error_copy (error);
    } else {
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

static gboolean
gsm_lac_ci_available (MMGenericGsm *self, guint32 *out_idx)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMModemGsmNetworkRegStatus status;
    guint idx;

    /* Must be registered, and have operator code, LAC and CI before GSM_LAC_CI is valid */
    status = gsm_reg_status (self, &idx);
    if (out_idx)
        *out_idx = idx;

    if (   status != MM_MODEM_GSM_NETWORK_REG_STATUS_HOME
        && status != MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING)
        return FALSE;

    if (!priv->oper_code || !strlen (priv->oper_code))
        return FALSE;

    if (!priv->lac[idx] || !priv->cell_id[idx])
        return FALSE;

    return TRUE;
}

static void
update_lac_ci (MMGenericGsm *self, gulong lac, gulong ci, guint idx)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    gboolean changed = FALSE;

    if (lac != priv->lac[idx]) {
        priv->lac[idx] = lac;
        changed = TRUE;
    }

    if (ci != priv->cell_id[idx]) {
        priv->cell_id[idx] = ci;
        changed = TRUE;
    }

    if (changed && gsm_lac_ci_available (self, NULL) && priv->loc_enabled && priv->loc_signal)
        g_object_notify (G_OBJECT (self), MM_MODEM_LOCATION_LOCATION);
}

static void
destroy_gvalue (gpointer data)
{
    GValue *value = (GValue *) data;

    g_value_unset (value);
    g_slice_free (GValue, value);
}

static GHashTable *
make_location_hash (MMGenericGsm *self, GError **error)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GHashTable *locations = NULL;
    guint32 reg_idx = 0;
    GValue *val;
    char mcc[4] = { 0, 0, 0, 0 };
    char mnc[4] = { 0, 0, 0, 0 };

    if (priv->loc_caps == MM_MODEM_LOCATION_CAPABILITY_UNKNOWN) {
        g_set_error_literal (error,
                             MM_MODEM_ERROR,
                             MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                             "Modem has no location capabilities");
        return NULL;
    }

    locations = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                       NULL, destroy_gvalue);

    if (!gsm_lac_ci_available (self, &reg_idx))
        return locations;

    memcpy (mcc, priv->oper_code, 3);
    /* Not all modems report 6-digit MNCs */
    memcpy (mnc, priv->oper_code + 3, 2);
    if (strlen (priv->oper_code) == 6)
        mnc[2] = priv->oper_code[5];

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_STRING);
    g_value_take_string (val, g_strdup_printf ("%s,%s,%lX,%lX",
                                               mcc,
                                               mnc,
                                               priv->lac[reg_idx],
                                               priv->cell_id[reg_idx]));
    g_hash_table_insert (locations,
                         GUINT_TO_POINTER (MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI),
                         val);

    return locations;
}

static void
location_enable (MMModemLocation *modem,
                 gboolean loc_enable,
                 gboolean signal_location,
                 MMModemFn callback,
                 gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    MMCallbackInfo *info;

    if (loc_enable != priv->loc_enabled) {
        priv->loc_enabled = loc_enable;
        g_object_notify (G_OBJECT (modem), MM_MODEM_LOCATION_ENABLED);
    }

    if (signal_location != priv->loc_signal) {
        priv->loc_signal = signal_location;
        g_object_notify (G_OBJECT (modem), MM_MODEM_LOCATION_SIGNALS_LOCATION);
    }

    if (loc_enable && signal_location && gsm_lac_ci_available (self, NULL))
        g_object_notify (G_OBJECT (modem), MM_MODEM_LOCATION_LOCATION);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_schedule (info);
}

static void
location_get (MMModemLocation *modem,
              MMModemLocationGetFn callback,
              gpointer user_data)
{
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (self);
    GHashTable *locations = NULL;
    GError *error = NULL;

    if (priv->loc_caps == MM_MODEM_LOCATION_CAPABILITY_UNKNOWN) {
        error = g_error_new_literal (MM_MODEM_ERROR,
                                     MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                     "Modem has no location capabilities");
    } else if (priv->loc_enabled)
        locations = make_location_hash (self, &error);
    else
        locations = g_hash_table_new (g_direct_hash, g_direct_equal);

    callback (modem, locations, error, user_data);
    if (locations)
        g_hash_table_destroy (locations);
    g_clear_error (&error);
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
modem_location_init (MMModemLocation *class)
{
    class->enable = location_enable;
    class->get_location = location_get;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imei = get_imei;
    class->get_imsi = get_imsi;
    class->get_operator_id = get_operator_id;
    class->get_spn = get_spn;
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
    class->get = sms_get;
    class->delete = sms_delete;
    class->list = sms_list;
}

static void
modem_gsm_ussd_init (MMModemGsmUssd *class)
{
    class->initiate = ussd_initiate;
    class->respond = ussd_respond;
    class->cancel = ussd_cancel;
    class->encode = ussd_encode;
    class->decode = ussd_decode;
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
    priv->sms_present = g_hash_table_new (g_direct_hash, g_direct_equal);
    priv->sms_contents = g_hash_table_new (g_direct_hash, g_direct_equal);
    priv->sms_parts = g_hash_table_new (g_direct_hash, g_direct_equal);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_NETWORK_ALLOWED_MODE,
                                                    NULL,
                                                    MM_MODEM_GSM_NETWORK_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY,
                                                    NULL,
                                                    MM_MODEM_GSM_NETWORK_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_LOCATION_CAPABILITIES,
                                                    "Capabilities",
                                                    MM_MODEM_LOCATION_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_LOCATION_ENABLED,
                                                    "Enabled",
                                                    MM_MODEM_LOCATION_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_LOCATION_SIGNALS_LOCATION,
                                                    NULL,
                                                    MM_MODEM_LOCATION_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_LOCATION_LOCATION,
                                                    NULL,
                                                    MM_MODEM_LOCATION_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_USSD_STATE,
                                                    "State",
                                                    MM_MODEM_GSM_USSD_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION,
                                                    "NetworkNotification",
                                                    MM_MODEM_GSM_USSD_DBUS_INTERFACE);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_GSM_USSD_NETWORK_REQUEST,
                                                    "NetworkRequest",
                                                    MM_MODEM_GSM_USSD_DBUS_INTERFACE);

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
    case MM_GENERIC_GSM_PROP_SIM_IDENTIFIER:
    case MM_GENERIC_GSM_PROP_LOC_CAPABILITIES:
    case MM_GENERIC_GSM_PROP_LOC_ENABLED:
    case MM_GENERIC_GSM_PROP_LOC_SIGNAL:
    case MM_GENERIC_GSM_PROP_LOC_LOCATION:
    case MM_GENERIC_GSM_PROP_USSD_STATE:
    case MM_GENERIC_GSM_PROP_USSD_NETWORK_REQUEST:
    case MM_GENERIC_GSM_PROP_USSD_NETWORK_NOTIFICATION:
    case MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static const char *
ussd_state_to_string (MMModemGsmUssdState ussd_state)
{
    switch (ussd_state) {
    case MM_MODEM_GSM_USSD_STATE_IDLE:
        return "idle";
    case MM_MODEM_GSM_USSD_STATE_ACTIVE:
        return "active";
    case MM_MODEM_GSM_USSD_STATE_USER_RESPONSE:
        return "user-response";
    default:
        break;
    }

    g_warning ("Unknown GSM USSD state %d", ussd_state);
    return "unknown";
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (object);
    GHashTable *locations = NULL;

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
    case MM_GENERIC_GSM_PROP_SIM_IDENTIFIER:
        g_value_set_string (value, priv->simid);
        break;
    case MM_GENERIC_GSM_PROP_LOC_CAPABILITIES:
        g_value_set_uint (value, priv->loc_caps);
        break;
    case MM_GENERIC_GSM_PROP_LOC_ENABLED:
        g_value_set_boolean (value, priv->loc_enabled);
        break;
    case MM_GENERIC_GSM_PROP_LOC_SIGNAL:
        g_value_set_boolean (value, priv->loc_signal);
        break;
    case MM_GENERIC_GSM_PROP_LOC_LOCATION:
        /* We don't allow property accesses unless location change signalling
         * is enabled, for security reasons.
         */
        if (priv->loc_enabled && priv->loc_signal)
            locations = make_location_hash (MM_GENERIC_GSM (object), NULL);
        else
            locations = g_hash_table_new (g_direct_hash, g_direct_equal);
        g_value_take_boxed (value, locations);
        break;
    case MM_GENERIC_GSM_PROP_USSD_STATE:
        g_value_set_string (value, ussd_state_to_string (priv->ussd_state));
        break;
    case MM_GENERIC_GSM_PROP_USSD_NETWORK_REQUEST:
        g_value_set_string (value, priv->ussd_network_request);
        break;
    case MM_GENERIC_GSM_PROP_USSD_NETWORK_NOTIFICATION:
        g_value_set_string (value, priv->ussd_network_notification);
        break;
    case MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD:
        /* By default, try to set XOFF/XON flow control */
        g_value_set_string (value, "+IFC=1,1");
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
    mm_generic_gsm_ussd_cleanup (MM_GENERIC_GSM (object));

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
    g_free (priv->simid);
    g_hash_table_destroy (priv->sms_present);
    g_hash_table_destroy (priv->sms_contents);
    g_hash_table_destroy (priv->sms_parts);

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
    klass->get_sim_iccid = real_get_sim_iccid;

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

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SIM_IDENTIFIER,
                                      MM_MODEM_GSM_CARD_SIM_IDENTIFIER);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_LOC_CAPABILITIES,
                                      MM_MODEM_LOCATION_CAPABILITIES);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_LOC_ENABLED,
                                      MM_MODEM_LOCATION_ENABLED);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_LOC_SIGNAL,
                                      MM_MODEM_LOCATION_SIGNALS_LOCATION);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_LOC_LOCATION,
                                      MM_MODEM_LOCATION_LOCATION);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_USSD_STATE,
                                      MM_MODEM_GSM_USSD_STATE);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_USSD_NETWORK_NOTIFICATION,
                                      MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_USSD_NETWORK_REQUEST,
                                      MM_MODEM_GSM_USSD_NETWORK_REQUEST);

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

    g_object_class_install_property
        (object_class, MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD,
         g_param_spec_string (MM_GENERIC_GSM_FLOW_CONTROL_CMD,
                              "FlowControlCommand",
                              "Flow control configuration command (errors ignored)",
                              "+IFC=1,1",
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
