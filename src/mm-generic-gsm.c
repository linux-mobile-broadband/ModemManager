/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mm-generic-gsm.h"
#include "mm-modem-gsm-card.h"
#include "mm-modem-gsm-network.h"
#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-parsers.h"

static gpointer mm_generic_gsm_parent_class = NULL;

#define MM_GENERIC_GSM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_GENERIC_GSM, MMGenericGsmPrivate))

typedef struct {
    char *driver;
    char *data_device;
    char *oper_code;
    char *oper_name;
    MMModemGsmNetworkRegStatus reg_status;
    guint32 signal_quality;
    guint32 cid;
} MMGenericGsmPrivate;

static void get_registration_status (MMSerial *serial, MMCallbackInfo *info);
static void read_operator_done (MMSerial *serial,
                                GString *response,
                                GError *error,
                                gpointer user_data);

MMModem *
mm_generic_gsm_new (const char *serial_device, const char *driver)
{
    g_return_val_if_fail (serial_device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_GSM,
                                   MM_SERIAL_DEVICE, serial_device,
                                   MM_MODEM_DRIVER, driver,
                                   NULL));
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

void
mm_generic_gsm_set_reg_status (MMGenericGsm *modem,
                               MMModemGsmNetworkRegStatus status)
{
    MMGenericGsmPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    priv = MM_GENERIC_GSM_GET_PRIVATE (modem);

    if (priv->reg_status != status) {
        priv->reg_status = status;

        if (status == MM_MODEM_GSM_NETWORK_REG_STATUS_HOME ||
            status == MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING) {
            mm_serial_queue_command (MM_SERIAL (modem), "+COPS=3,2;+COPS?", 3, read_operator_done, GINT_TO_POINTER (0));
            mm_serial_queue_command (MM_SERIAL (modem), "+COPS=3,0;+COPS?", 3, read_operator_done, GINT_TO_POINTER (1));
            mm_modem_gsm_network_get_signal_quality (MM_MODEM_GSM_NETWORK (modem), NULL, NULL);
        } else {
            g_free (priv->oper_code);
            g_free (priv->oper_name);
            priv->oper_code = priv->oper_name = NULL;

            mm_modem_gsm_network_registration_info (MM_MODEM_GSM_NETWORK (modem), priv->reg_status,
                                                    priv->oper_code, priv->oper_name);
        }
    }
}

static void
pin_check_done (MMSerial *serial,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (g_str_has_prefix (response->str, "+CPIN: ")) {
        const char *str = response->str + 7;

        if (g_str_has_prefix (str, "READY"))
            parsed = TRUE;
        else if (g_str_has_prefix (str, "SIM PIN"))
            info->error = mm_mobile_error_for_code (MM_MOBILE_ERROR_SIM_PIN);
        else if (g_str_has_prefix (str, "SIM PUK"))
            info->error = mm_mobile_error_for_code (MM_MOBILE_ERROR_SIM_PUK);
        /* FIXME: There's more exotic ones that are not handled right now */
    }

    if (!info->error && !parsed)
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse PIN request results");

    mm_callback_info_schedule (info);
}

void
mm_generic_gsm_check_pin (MMGenericGsm *modem,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    g_return_if_fail (MM_IS_GENERIC_GSM (modem));

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "+CPIN?", 3, pin_check_done, info);
}

/*****************************************************************************/

static void
enable_done (MMSerial *serial,
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
init_done (MMSerial *serial,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Disable unsolicited registration state changes, these will mess up our response parser */
        mm_serial_queue_command (serial, "+CREG=0", 5, NULL, NULL);
        mm_serial_queue_command (serial, "+CFUN=1", 5, enable_done, user_data);
    }
}

static void
enable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "Z E0 V1 X4 &C1 +CMEE=1", 3, init_done, user_data);
}

static void
disable_done (MMSerial *serial,
              GString *response,
              GError *error,
              gpointer user_data)
{
    mm_serial_close (serial);
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
disable_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_serial_queue_command (serial, "+CFUN=0", 5, disable_done, user_data);
}

static void
enable (MMModem *modem,
        gboolean enable,
        MMModemFn callback,
        gpointer user_data)
{
    MMCallbackInfo *info;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    info = mm_callback_info_new (modem, callback, user_data);

    if (!enable) {
        if (mm_serial_is_connected (MM_SERIAL (modem)))
            mm_serial_flash (MM_SERIAL (modem), 1000, disable_flash_done, info);
        else
            disable_flash_done (MM_SERIAL (modem), info);
    } else {
        if (mm_serial_open (MM_SERIAL (modem), &info->error))
            mm_serial_flash (MM_SERIAL (modem), 100, enable_flash_done, info);

        if (info->error)
            mm_callback_info_schedule (info);
    }
}

static void
get_string_done (MMSerial *serial,
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
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "+CGSN", 3, get_string_done, info);
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "+CIMI", 3, get_string_done, info);
}

static void
gsm_card_info_invoke (MMCallbackInfo *info)
{
    MMModemGsmCardInfoFn callback = (MMModemGsmCardInfoFn) info->callback;

    callback (MM_MODEM_GSM_CARD (info->modem),
              (char *) mm_callback_info_get_data (info, "card-info-manufacturer"),
              (char *) mm_callback_info_get_data (info, "card-info-model"),
              (char *) mm_callback_info_get_data (info, "card-info-version"),
              info->error, info->user_data);
}

static void
get_version_done (MMSerial *serial,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (!error)
        mm_callback_info_set_data (info, "card-info-version", g_strdup (response->str), g_free);
    else if (!info->error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
get_model_done (MMSerial *serial,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (!error)
        mm_callback_info_set_data (info, "card-info-model", g_strdup (response->str), g_free);
    else if (!info->error)
        info->error = g_error_copy (error);
}

static void
get_manufacturer_done (MMSerial *serial,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (!error)
        mm_callback_info_set_data (info, "card-info-manufacturer", g_strdup (response->str), g_free);
    else
        info->error = g_error_copy (error);
}

static void
get_card_info (MMModemGsmCard *modem,
               MMModemGsmCardInfoFn callback,
               gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      gsm_card_info_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_serial_queue_command (MM_SERIAL (modem), "+CGMI", 3, get_manufacturer_done, info);
    mm_serial_queue_command (MM_SERIAL (modem), "+CGMM", 3, get_model_done, info);
    mm_serial_queue_command (MM_SERIAL (modem), "+CGMR", 3, get_version_done, info);
}

static void
send_puk_done (MMSerial *serial,
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
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin);
    mm_serial_queue_command (MM_SERIAL (modem), command, 3, send_puk_done, info);
    g_free (command);
}

static void
send_pin_done (MMSerial *serial,
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
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPIN=\"%s\"", pin);
    mm_serial_queue_command (MM_SERIAL (modem), command, 3, send_pin_done, info);
    g_free (command);
}

static void
enable_pin_done (MMSerial *serial,
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
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CLCK=\"SC\",%d,\"%s\"", enabled ? 1 : 0, pin);
    mm_serial_queue_command (MM_SERIAL (modem), command, 3, enable_pin_done, info);
    g_free (command);
}

static void
change_pin_done (MMSerial *serial,
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
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    command = g_strdup_printf ("+CPWD=\"SC\",\"%s\",\"%s\"", old_pin, new_pin);
    mm_serial_queue_command (MM_SERIAL (modem), command, 3, change_pin_done, info);
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
read_operator_done (MMSerial *serial,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    if (!error) {
        char *oper;

        oper = parse_operator (response->str);
        if (oper) {
            MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (serial);

            if (GPOINTER_TO_INT (user_data) == 0) {
                g_free (priv->oper_code);
                priv->oper_code = oper;
            } else {
                g_free (priv->oper_name);
                priv->oper_name = oper;

                mm_modem_gsm_network_registration_info (MM_MODEM_GSM_NETWORK (serial), priv->reg_status,
                                                        priv->oper_code, priv->oper_name);
            }
        }
    }
}

static gboolean
reg_status_again (gpointer data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) data;
    guint32 counter;

    counter = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "reg-status-counter"));
    if (counter > 60) {
        /* That's 60 seconds */
        info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                           MM_MOBILE_ERROR_NETWORK_TIMEOUT,
                                           "Registration timed out");
        mm_callback_info_schedule (info);
    } else {
        mm_callback_info_set_data (info, "reg-status-counter",
                                   GUINT_TO_POINTER (++counter), NULL);
        get_registration_status (MM_SERIAL (info->modem), info);
    }

    return TRUE;
}

static void
reg_status_remove (gpointer data)
{
    g_source_remove (GPOINTER_TO_UINT (data));
}

static void
get_reg_status_done (MMSerial *serial,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *reply = response->str;
    guint32 id;
    gboolean done = FALSE;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    if (g_str_has_prefix (reply, "+CREG: ")) {
        /* Got valid reply */
        int n, stat;

        if (sscanf (reply + 7, "%d,%d", &n, &stat)) {
            MMModemGsmNetworkRegStatus status;

            switch (stat) {
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

            mm_generic_gsm_set_reg_status (MM_GENERIC_GSM (serial), status);

            switch (status) {
            case MM_MODEM_GSM_NETWORK_REG_STATUS_HOME:
            case MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING:
                /* Done */
                done = TRUE;
                break;
            case MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE:
                /* Huh? Stupid card, we told it to register, pretend it returned SEARCHING
                 (hoping it will eventually start searching) */
                /* fall through */
            case MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING:
                /* Wait more until the timeout expires. */
                id = GPOINTER_TO_INT (mm_callback_info_get_data (info, "reg-status-timeout"));
                if (!id) {
                    id = g_timeout_add (1000, reg_status_again, info);
                    mm_callback_info_set_data (info, "reg-status-timeout", GUINT_TO_POINTER (id),
                                               reg_status_remove);
                }
                break;
            case MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED:
                info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                                   MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED,
                                                   "Network no allowed");
                break;
            case MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN:
            default:
                info->error = g_error_new_literal (MM_MODEM_ERROR,
                                                   MM_MODEM_ERROR_GENERAL,
                                                   "Unknown network status");
                break;
            }
        }
    } else {
        g_debug ("unknown response: %s", reply);
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the response");
    }

 out:
    if (done || info->error)
        mm_callback_info_schedule (info);
}

static void
get_registration_status (MMSerial *serial, MMCallbackInfo *info)
{
    mm_serial_queue_command (serial, "+CREG?", 3, get_reg_status_done, info);
}

static void
register_done (MMSerial *serial,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else
        get_registration_status (serial, info);
}

static void
do_register (MMModemGsmNetwork *modem,
             const char *network_id,
             MMModemFn callback,
             gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);

    if (network_id)
        command = g_strdup_printf ("+COPS=1,2,\"%s\"", network_id);
    else
        command = g_strdup ("+COPS=0,,");

    mm_serial_queue_command (MM_SERIAL (modem), command, 60, register_done, info);
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

static void
connect_report_done (MMSerial *serial,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (!error && g_str_has_prefix (response->str, "+CEER: ")) {
        g_free (info->error->message);
        info->error->message = g_strdup (response->str + 7); /* skip the "+CEER: " */
    }
    
    mm_callback_info_schedule (info);
}

static void
connect_done (MMSerial *serial,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        /* Try to get more information why it failed */
        mm_serial_queue_command (serial, "+CEER", 3, connect_report_done, info);
    } else
        /* Done */
        mm_callback_info_schedule (info);
}

static void
connect (MMModem *modem,
         const char *number,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;
    char *command;
    guint32 cid = mm_generic_gsm_get_cid (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

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

    mm_serial_queue_command (MM_SERIAL (modem), command, 60, connect_done, info);
    g_free (command);
}

static void
disconnect_flash_done (MMSerial *serial, gpointer user_data)
{
    mm_callback_info_schedule ((MMCallbackInfo *) user_data);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMCallbackInfo *info;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    info = mm_callback_info_new (modem, callback, user_data);
    mm_serial_flash (MM_SERIAL (modem), 1000, disconnect_flash_done, info);
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
destroy_scan_data (gpointer data)
{
    GPtrArray *results = (GPtrArray *) data;

    g_ptr_array_foreach (results, (GFunc) g_hash_table_destroy, NULL);
    g_ptr_array_free (results, TRUE);
}

static void
scan_done (MMSerial *serial,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;

    if (error)
        info->error = g_error_copy (error);
    else if (!strncmp (reply, "+COPS: ", 7)) {
        /* Got valid reply */
        GPtrArray *results;
        GRegex *r;
        GMatchInfo *match_info;
        GError *err = NULL;

        reply += 7;

        /* Pattern without crazy escaping using | for matching: (|\d|,"|.+|","|.+|","|.+|",|\d|) */
        r = g_regex_new ("\\((\\d),\"(.+)\",\"(.+)\",\"(.+)\",(\\d)\\)", G_REGEX_UNGREEDY, 0, &err);
        if (err) {
            g_error ("Invalid regular expression: %s", err->message);
            g_error_free (err);
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                               "Could not parse scan results.");
            goto out;
        }

        results = g_ptr_array_new ();

        g_regex_match (r, reply, 0, &match_info);
        while (g_match_info_matches (match_info)) {
            GHashTable *hash;

            hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
            g_hash_table_insert (hash, g_strdup ("status"), g_match_info_fetch (match_info, 1));
            g_hash_table_insert (hash, g_strdup ("operator-long"), g_match_info_fetch (match_info, 2));
            g_hash_table_insert (hash, g_strdup ("operator-short"), g_match_info_fetch (match_info, 3));
            g_hash_table_insert (hash, g_strdup ("operator-num"), g_match_info_fetch (match_info, 4));

            g_ptr_array_add (results, hash);
            g_match_info_next (match_info, NULL);
        }

        mm_callback_info_set_data (info, "scan-results", results, destroy_scan_data);
        g_match_info_free (match_info);
        g_regex_unref (r);
    } else
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse scan results.");

 out:
    mm_callback_info_schedule (info);
}

static void
scan (MMModemGsmNetwork *modem,
      MMModemGsmNetworkScanFn callback,
      gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      gsm_network_scan_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_serial_queue_command (MM_SERIAL (modem), "+COPS=?", 60, scan_done, info);
}

/* SetApn */

static void
set_apn_done (MMSerial *serial,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else
        mm_generic_gsm_set_cid (MM_GENERIC_GSM (serial),
                                GPOINTER_TO_UINT (mm_callback_info_get_data (info, "cid")));

    mm_callback_info_schedule (info);
}

static void
cid_range_read (MMSerial *serial,
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

        r = g_regex_new ("\\+CGDCONT: \\((\\d+)-(\\d+)\\),\"(\\S+)\"",
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

        command = g_strdup_printf ("+CGDCONT=%d, \"IP\", \"%s\"", cid, apn);
        mm_serial_queue_command (serial, command, 3, set_apn_done, info);
        g_free (command);
    }
}

static void
existing_apns_read (MMSerial *serial,
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

        r = g_regex_new ("\\+CGDCONT: (\\d+)\\s*,\"(\\S+)\",\"(\\S+)\",\"(\\S+)\"",
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
                    mm_generic_gsm_set_cid (MM_GENERIC_GSM (serial), (guint32) num_cid);
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
    } else
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse the response");

    if (found || info->error)
        mm_callback_info_schedule (info);
    else
        /* APN not configured on the card. Get the allowed CID range */
        mm_serial_queue_command (serial, "+CGDCONT=?", 3, cid_range_read, info);
}

static void
set_apn (MMModemGsmNetwork *modem,
         const char *apn,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_set_data (info, "apn", g_strdup (apn), g_free);

    /* Start by searching if the APN is already in card */
    mm_serial_queue_command (MM_SERIAL (modem), "+CGDCONT?", 3, existing_apns_read, info);
}

/* GetSignalQuality */

static void
get_signal_quality_done (MMSerial *serial,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;

    if (error)
        info->error = g_error_copy (error);
    else if (!strncmp (reply, "+CSQ: ", 6)) {
        /* Got valid reply */
        int quality;
        int ber;

        reply += 6;

        if (sscanf (reply, "%d,%d", &quality, &ber)) {
            /* 99 means unknown */
            if (quality != 99)
                /* Normalize the quality */
                quality = quality * 100 / 31;

            MM_GENERIC_GSM_GET_PRIVATE (serial)->signal_quality = quality;
            mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
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
    MMCallbackInfo *info;

    if (mm_serial_is_connected (MM_SERIAL (modem))) {
        g_message ("Returning saved signal quality %d", MM_GENERIC_GSM_GET_PRIVATE (modem)->signal_quality);
        callback (MM_MODEM (modem), MM_GENERIC_GSM_GET_PRIVATE (modem)->signal_quality, NULL, user_data);
        return;
    }

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_queue_command (MM_SERIAL (modem), "+CSQ", 3, get_signal_quality_done, info);
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
        if (state == SIMPLE_STATE_CHECK_PIN && g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_SIM_PIN))
            need_pin = TRUE;
        else {
            info->error = g_error_copy (error);
            goto out;
        }
    }

    switch (state) {
    case SIMPLE_STATE_BEGIN:
        state = SIMPLE_STATE_ENABLE;
        mm_modem_enable (modem, TRUE, simple_state_machine, info);
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
    modem_class->enable = enable;
    modem_class->connect = connect;
    modem_class->disconnect = disconnect;
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imei = get_imei;
    class->get_imsi = get_imsi;
    class->get_info = get_card_info;
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
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
    class->get_status = simple_get_status;
}

static void
mm_generic_gsm_init (MMGenericGsm *self)
{
    mm_serial_set_response_parser (MM_SERIAL (self),
                                   mm_serial_parser_v1_parse,
                                   mm_serial_parser_v1_new (),
                                   mm_serial_parser_v1_destroy);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMGenericGsmPrivate *priv = MM_GENERIC_GSM_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_DRIVER:
        /* Construct only */
        priv->driver = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_DATA_DEVICE:
        g_free (priv->data_device);
        priv->data_device = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_TYPE:
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
        if (priv->data_device)
            g_value_set_string (value, priv->data_device);
        else
            g_value_set_string (value, mm_serial_get_device (MM_SERIAL (object)));
        break;
    case MM_MODEM_PROP_DRIVER:
        g_value_set_string (value, MM_GENERIC_GSM_GET_PRIVATE (object)->driver);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_GSM);
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

    g_free (priv->driver);
    g_free (priv->data_device);
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

    /* Properties */
    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DRIVER,
                                      MM_MODEM_DRIVER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);
}

GType
mm_generic_gsm_get_type (void)
{
    static GType generic_gsm_type = 0;

    if (G_UNLIKELY (generic_gsm_type == 0)) {
        static const GTypeInfo generic_gsm_type_info = {
            sizeof (MMGenericGsmClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_generic_gsm_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMGenericGsm),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_generic_gsm_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };
        
        static const GInterfaceInfo modem_gsm_card_info = {
            (GInterfaceInitFunc) modem_gsm_card_init
        };

        static const GInterfaceInfo modem_gsm_network_info = {
            (GInterfaceInitFunc) modem_gsm_network_init
        };

        static const GInterfaceInfo modem_simple_info = {
            (GInterfaceInitFunc) modem_simple_init
        };

        generic_gsm_type = g_type_register_static (MM_TYPE_SERIAL, "MMGenericGsm", &generic_gsm_type_info, 0);

        g_type_add_interface_static (generic_gsm_type, MM_TYPE_MODEM, &modem_iface_info);
        g_type_add_interface_static (generic_gsm_type, MM_TYPE_MODEM_GSM_CARD, &modem_gsm_card_info);
        g_type_add_interface_static (generic_gsm_type, MM_TYPE_MODEM_GSM_NETWORK, &modem_gsm_network_info);
        g_type_add_interface_static (generic_gsm_type, MM_TYPE_MODEM_SIMPLE, &modem_simple_info);
    }

    return generic_gsm_type;
}
