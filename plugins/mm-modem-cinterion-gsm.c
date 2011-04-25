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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"
#include "mm-modem-cinterion-gsm.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMModemCinterionGsm, mm_modem_cinterion_gsm, MM_TYPE_GENERIC_GSM);

#define MM_MODEM_CINTERION_GSM_GET_PRIVATE(o)                           \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_CINTERION_GSM, MMModemCinterionGsmPrivate))

typedef struct {
    /* Flag to know if we should try AT^SIND or not to get psinfo */
    gboolean sind_psinfo;

    /* Supported networks */
    gboolean only_geran;
    gboolean only_utran;
    gboolean both_geran_utran;
} MMModemCinterionGsmPrivate;

MMModem *
mm_modem_cinterion_gsm_new (const char *device,
                            const char *driver,
                            const char *plugin,
                            guint32 vendor,
                            guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_CINTERION_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

static MMModemGsmAccessTech
get_access_technology_from_smong_gprs_status (const gchar *gprs_status,
                                              GError **error)
{
    if (strlen (gprs_status) == 1) {
        switch (gprs_status[0]) {
        case '0':
            return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
        case '1':
        case '2':
            return MM_MODEM_GSM_ACCESS_TECH_GPRS;
        case '3':
        case '4':
            return MM_MODEM_GSM_ACCESS_TECH_EDGE;
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_MODEM_ERROR,
                 MM_MODEM_ERROR_GENERAL,
                 "Couldn't get network capabilities, "
                 "invalid GPRS status value: '%s'",
                 gprs_status);
    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static MMModemGsmAccessTech
get_access_technology_from_psinfo (const gchar *psinfo,
                                   GError **error)
{
    if (strlen (psinfo) == 1) {
        switch (psinfo[0]) {
        case '0':
            return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
        case '1':
        case '2':
            return MM_MODEM_GSM_ACCESS_TECH_GPRS;
        case '3':
        case '4':
            return MM_MODEM_GSM_ACCESS_TECH_EDGE;
        case '5':
        case '6':
            return MM_MODEM_GSM_ACCESS_TECH_UMTS;
        case '7':
        case '8':
            return MM_MODEM_GSM_ACCESS_TECH_HSDPA;
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_MODEM_ERROR,
                 MM_MODEM_ERROR_GENERAL,
                 "Couldn't get network capabilities, "
                 "invalid psinfo value: '%s'",
                 psinfo);
    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static void
get_smong_cb (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    /* The AT^SMONG command returns a cell info table, where the second
     * column identifies the "GPRS status", which is exactly what we want.
     * So we'll try to read that second number in the values row.
     *
     * AT^SMONG
     * GPRS Monitor
     * BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC    # Cell #
     * 0776  1  -      -   214   03  2    00      01
     * OK
     */
    regex = g_regex_new (".*GPRS Monitor\\r\\n"
                         "BCCH\\s*G.*\\r\\n"
                         "(\\d*)\\s*(\\d*)\\s*", 0, 0, NULL);
    if (g_regex_match_full (regex, response->str, response->len, 0, 0, &match_info, NULL)) {
        gchar *gprs_status;

        gprs_status = g_match_info_fetch (match_info, 2);
        act = get_access_technology_from_smong_gprs_status (gprs_status, &info->error);
        g_free (gprs_status);

        /* We'll default to use SMONG then */
        priv->sind_psinfo = FALSE;
    } else {
        g_set_error (&info->error,
                     MM_MODEM_ERROR,
                     MM_MODEM_ERROR_GENERAL,
                     "Couldn't get network capabilities, invalid SMONG reply: '%s'",
                     response->str);

        /* We'll reset here the flag to try to use SIND/psinfo the next time */
        priv->sind_psinfo = TRUE;
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    mm_callback_info_schedule (info);
}

static void
get_sind_cb (MMAtSerialPort *port,
             GString *response,
             GError *error,
             gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    /* The AT^SIND? command replies a list of several different indicators.
     * We will only look for 'psinfo' which is the one which may tell us
     * the available network access technology. Note that only 3G-enabled
     * devices seem to have this indicator.
     *
     * AT+SIND?
     * ^SIND: battchg,1,1
     * ^SIND: signal,1,99
     * ...
     */
    regex = g_regex_new ("\\r\\n\\^SIND:\\s*psinfo,\\s*(\\d*),\\s*(\\d*)", 0, 0, NULL);
    if (g_regex_match_full (regex, response->str, response->len, 0, 0, &match_info, NULL)) {
        gchar *ind_value;

        ind_value = g_match_info_fetch (match_info, 2);
        act = get_access_technology_from_psinfo (ind_value, &info->error);
        g_free (ind_value);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    /* If there was no 'psinfo' indicator, we'll try AT^SMONG and read the cell
     * info table. */
    mm_at_serial_port_queue_command (port, "^SMONG", 3, get_smong_cb, info);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (gsm);
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    if (priv->sind_psinfo) {
        mm_at_serial_port_queue_command (port, "^SIND?", 3, get_sind_cb, info);
    } else {
        mm_at_serial_port_queue_command (port, "^SMONG", 3, get_smong_cb, info);
    }
}

static void
enable_complete (MMGenericGsm *gsm,
                 GError *error,
                 MMCallbackInfo *info)
{
    /* Do NOT chain up parent do_enable_power_up_done(), as it actually ignores
     * all errors. */

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}


static void
get_supported_networks_cb (MMAtSerialPort *port,
                           GString *response,
                           GError *error,
                           gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);
    GError *inner_error = NULL;

    if (error) {
        enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    /* Note: Documentation says that AT+WS46=? is replied with '+WS46:' followed
     * by a list of supported network modes between parenthesis, but the EGS5
     * used to test this didn't use the 'WS46:' prefix. Also, more than one
     * numeric ID may appear in the list, that's why they are checked
     * separately. * */

    if (strstr (response->str, "12") != NULL) {
        mm_dbg ("Device allows 2G-only network mode");
        priv->only_geran = TRUE;
    }

    if (strstr (response->str, "22") != NULL) {
        mm_dbg ("Device allows 3G-only network mode");
        priv->only_utran = TRUE;
    }

    if (strstr (response->str, "25") != NULL) {
        mm_dbg ("Device allows 2G/3G network mode");
        priv->both_geran_utran = TRUE;
    }

    /* If no expected ID found, error */
    if (!priv->only_geran &&
        !priv->only_utran &&
        !priv->both_geran_utran) {
        mm_warn ("Invalid list of supported networks: '%s'",
                 response->str);
        inner_error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Invalid list of supported networks: '%s'",
                                   response->str);
    }

    enable_complete (MM_GENERIC_GSM (info->modem), inner_error, info);
    if (inner_error)
        g_error_free (inner_error);
}

static void
do_enable_power_up_done (MMGenericGsm *gsm,
                         GString *response,
                         GError *error,
                         MMCallbackInfo *info)
{
    MMAtSerialPort *port;
    GError *inner_error = NULL;

    if (error) {
        enable_complete (gsm, error, info);
        return;
    }

    /* Get port */
    port = mm_generic_gsm_get_best_at_port (gsm, &inner_error);
    if (!port) {
        enable_complete (gsm, inner_error, info);
        g_error_free (inner_error);
        return;
    }

    /* List supported networks */
    mm_at_serial_port_queue_command (port, "+WS46=?", 3, get_supported_networks_cb, info);
}

/*****************************************************************************/

static void
mm_modem_cinterion_gsm_init (MMModemCinterionGsm *self)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (self);

    /* Set defaults */
    priv->sind_psinfo = TRUE; /* Initially, always try to get psinfo */
}

static void
mm_modem_cinterion_gsm_class_init (MMModemCinterionGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemCinterionGsmPrivate));

    gsm_class->do_enable_power_up_done = do_enable_power_up_done;
    gsm_class->get_access_technology = get_access_technology;
}

