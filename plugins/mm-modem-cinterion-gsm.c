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
#include <ctype.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"
#include "mm-modem-cinterion-gsm.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);

G_DEFINE_TYPE_EXTENDED (MMModemCinterionGsm, mm_modem_cinterion_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init))

/* Mask of all bands supported in 2G devices */
#define ALL_2G_BANDS          \
    (MM_MODEM_GSM_BAND_EGSM | \
     MM_MODEM_GSM_BAND_DCS |  \
     MM_MODEM_GSM_BAND_PCS |  \
     MM_MODEM_GSM_BAND_G850)

/* Mask of all bands supported in 3G devices (including some 2G bands) */
#define ALL_3G_BANDS           \
    (MM_MODEM_GSM_BAND_EGSM |  \
     MM_MODEM_GSM_BAND_DCS |   \
     MM_MODEM_GSM_BAND_PCS |   \
     MM_MODEM_GSM_BAND_G850 |  \
     MM_MODEM_GSM_BAND_U2100 | \
     MM_MODEM_GSM_BAND_U1900 | \
     MM_MODEM_GSM_BAND_U850)

#define MM_MODEM_CINTERION_GSM_GET_PRIVATE(o)                           \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_CINTERION_GSM, MMModemCinterionGsmPrivate))

typedef struct {
    /* Flag to know if we should try AT^SIND or not to get psinfo */
    gboolean sind_psinfo;

    /* Supported networks */
    gboolean only_geran;
    gboolean only_utran;
    gboolean both_geran_utran;

    /* Current allowed mode */
    MMModemGsmAllowedMode allowed_mode;

    /* Bitmask for currently active bands */
    guint32 current_bands;
} MMModemCinterionGsmPrivate;

/* Setup relationship between the band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    gchar *cinterion_band;
    guint32 mm_band_mask;
} CinterionBand2G;
/* Table checked in both MC75i (GPRS/EDGE) and EGS5 (GPRS) references.
 * Note that the modem's configuration is also based on a bitmask, but as we
 * will just support some of the combinations, we just use strings for them.
 */
static const CinterionBand2G bands_2g[] = {
    { "1",  MM_MODEM_GSM_BAND_EGSM  },
    { "2",  MM_MODEM_GSM_BAND_DCS   },
    { "4",  MM_MODEM_GSM_BAND_PCS   },
    { "8",  MM_MODEM_GSM_BAND_G850  },
    { "3",  (MM_MODEM_GSM_BAND_EGSM | MM_MODEM_GSM_BAND_DCS) },
    { "5",  (MM_MODEM_GSM_BAND_EGSM | MM_MODEM_GSM_BAND_PCS) },
    { "10", (MM_MODEM_GSM_BAND_G850 | MM_MODEM_GSM_BAND_DCS) },
    { "12", (MM_MODEM_GSM_BAND_G850 | MM_MODEM_GSM_BAND_PCS) },
    { "15", ALL_2G_BANDS }
};

/* Setup relationship between the 3G band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    guint32 cinterion_band_flag;
    guint32 mm_band_flag;
} CinterionBand3G;
/* Table checked in HC25 (3G) reference. This table includes both 2G and 3G
 * frequencies. Depending on which one is configured, one access technology or
 * the other will be used. This may conflict with the allowed mode configuration
 * set, so you shouldn't for example set 3G frequency bands, and then use a
 * 2G-only allowed mode. */
static const CinterionBand3G bands_3g[] = {
    { (1 << 0), MM_MODEM_GSM_BAND_EGSM  },
    { (1 << 1), MM_MODEM_GSM_BAND_DCS   },
    { (1 << 2), MM_MODEM_GSM_BAND_PCS   },
    { (1 << 3), MM_MODEM_GSM_BAND_G850  },
    { (1 << 4), MM_MODEM_GSM_BAND_U2100 },
    { (1 << 5), MM_MODEM_GSM_BAND_U1900 },
    { (1 << 6), MM_MODEM_GSM_BAND_U850  }
};

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

static void
convert_str_from_ucs2 (gchar **str)
{
    const char *p;
    char *converted;
    size_t len;

    p = *str;
    len = strlen (p);

    /* Len needs to be a multiple of 4 for UCS2 */
    if ((len < 4) || ((len % 4) != 0))
        return;

    while (*p) {
        if (!isxdigit (*p++))
            return;
    }

    converted = mm_modem_charset_hex_to_utf8 (*str, MM_MODEM_CHARSET_UCS2);
    if (converted) {
        g_free (*str);
        *str = converted;
    }
}

static void
get_2g_band_done (MMAtSerialPort *port,
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
        MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);
        guint32 mm_band = MM_MODEM_GSM_BAND_UNKNOWN;
        GRegex *regex;
        GMatchInfo *match_info = NULL;

        /* The AT^SCFG? command replies a list of several different config
         * values. We will only look for 'Radio/Band".
         *
         * AT+SCFG="Radio/Band"
         * ^SCFG: "Radio/Band","0031","0031"
         *
         * Note that "0031" is a UCS2-encoded string, as we configured UCS2 as
         * character set to use.
         */
        regex = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\s*\"(.*)\",\\s*\"(.*)\"", 0, 0, NULL);
        if (regex &&
            g_regex_match_full (regex, response->str, response->len, 0, 0, &match_info, NULL)) {
            gchar *current;

            /* The first number given is the current band configuration, the
             * second number given is the allowed band configuration, which we
             * don't really need to get here. */
            current = g_match_info_fetch (match_info, 1);
            if (current) {
                guint i;

                /* If in UCS2, convert to UTF-8 */
                if (mm_generic_gsm_get_charset (MM_GENERIC_GSM (info->modem)) == MM_MODEM_CHARSET_UCS2)
                    convert_str_from_ucs2 (&current);

                for (i = 0; i < G_N_ELEMENTS (bands_2g); i++) {
                    if (strcmp (bands_2g[i].cinterion_band, current) == 0) {
                        mm_band = bands_2g[i].mm_band_mask;
                        break;
                    }
                }

                g_free (current);
            }
        }

        if (mm_band == MM_MODEM_GSM_BAND_UNKNOWN) {
            g_set_error (&info->error,
                         MM_MODEM_ERROR,
                         MM_MODEM_ERROR_GENERAL,
                         "Couldn't get bands configuration");
        } else {
            priv->current_bands = mm_band;
            mm_callback_info_set_result (info, GUINT_TO_POINTER (mm_band), NULL);
        }

        if (regex)
            g_regex_unref (regex);
        if (match_info)
            g_match_info_free (match_info);
    }

    mm_callback_info_schedule (info);
}

static void
get_3g_band_done (MMAtSerialPort *port,
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
        MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);
        guint32 mm_band = 0;
        GRegex *regex;
        GMatchInfo *match_info = NULL;

        /* The AT^SCFG? command replies a list of several different config
         * values. We will only look for 'Radio/Band".
         *
         * AT+SCFG="Radio/Band"
         * ^SCFG: "Radio/Band",127
         *
         * Note that in this case, the <rba> replied is a number, not a string.
         */
        regex = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\s*(\\d*)", 0, 0, NULL);
        if (regex &&
            g_regex_match_full (regex, response->str, response->len, 0, 0, &match_info, NULL)) {
            gchar *current;

            current = g_match_info_fetch (match_info, 1);
            if (current) {
                guint32 current_int;
                guint i;

                current_int = (guint32) atoi (current);

                for (i = 0; i < G_N_ELEMENTS (bands_3g); i++) {
                    if (current_int & bands_3g[i].cinterion_band_flag)
                        mm_band |= bands_3g[i].mm_band_flag;
                }

                g_free (current);
            }
        }

        if (mm_band == 0) {
            g_set_error (&info->error,
                         MM_MODEM_ERROR,
                         MM_MODEM_ERROR_GENERAL,
                         "Couldn't get bands configuration");
        } else {
            priv->current_bands = mm_band;
            mm_callback_info_set_result (info, GUINT_TO_POINTER (mm_band), NULL);
        }

        if (regex)
            g_regex_unref (regex);
        if (match_info)
            g_match_info_free (match_info);
    }

    mm_callback_info_schedule (info);
}

static void
get_band (MMModemGsmNetwork *self,
          MMModemUIntFn callback,
          gpointer user_data)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (self);
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    /* If results are already cached, return them */
    if (priv->current_bands > 0) {
        mm_callback_info_set_result (info, GUINT_TO_POINTER (priv->current_bands), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    /* Otherwise ask the modem */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Query the currently used Radio/Band. The query command is the same for
     * both 2G and 3G devices, but the reply reader is different. */
    mm_at_serial_port_queue_command (port,
                                     "AT^SCFG=\"Radio/Band\"",
                                     3,
                                     ((!priv->only_utran && !priv->both_geran_utran) ?
                                      get_2g_band_done :
                                      get_3g_band_done),
                                     info);
}

static void
set_band_done (MMAtSerialPort *port,
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
        MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);

        priv->current_bands = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "new-band"));
    }

    mm_callback_info_schedule (info);
}

static void
set_2g_band (MMModemGsmNetwork *self,
             MMModemGsmBand band,
             MMAtSerialPort *port,
             MMCallbackInfo *info)
{
    const gchar *cinterion_band = NULL;
    gchar *cinterion_band_ucs2 = NULL;
    gchar *cmd;
    guint i;

    /* If we get ANY, reset to all-2G bands to get the proper value */
    if (band == MM_MODEM_GSM_BAND_ANY)
        band = ALL_2G_BANDS;

    /* Loop looking for correct allowed masks */
    for (i = 0; i < G_N_ELEMENTS (bands_2g); i++) {
        if (bands_2g[i].mm_band_mask == band) {
            cinterion_band = bands_2g[i].cinterion_band;
            break;
        }
    }

    /* If we didn't find a match, set an error */
    if (!cinterion_band) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Wrong band mask: '%u'", band);
        mm_callback_info_schedule (info);
        return;
    }

    if (mm_generic_gsm_get_charset (MM_GENERIC_GSM (info->modem)) == MM_MODEM_CHARSET_UCS2)
        cinterion_band_ucs2 = mm_modem_charset_utf8_to_hex (cinterion_band, MM_MODEM_CHARSET_UCS2);

    mm_callback_info_set_data (info,
                               "new-band",
                               GUINT_TO_POINTER ((guint)band),
                               NULL);
    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rbp>,<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing double-quote enclosed strings here!
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",\"%s\",\"%s\"",
                           cinterion_band_ucs2 ? cinterion_band_ucs2 : cinterion_band,
                           cinterion_band_ucs2 ? cinterion_band_ucs2 : cinterion_band);
    mm_at_serial_port_queue_command (port, cmd, 3, set_band_done, info);
    g_free (cmd);
    g_free (cinterion_band_ucs2);
}

static void
set_3g_band (MMModemGsmNetwork *self,
             MMModemGsmBand band,
             MMAtSerialPort *port,
             MMCallbackInfo *info)
{
    guint32 cinterion_band = 0;
    gchar *cmd;
    guint i;

    /* If we get ANY, reset to all-3G bands to get the proper value */
    if (band == MM_MODEM_GSM_BAND_ANY)
        band = ALL_3G_BANDS;

    /* Loop looking for correct allowed masks */
    for (i = 0; i < G_N_ELEMENTS (bands_3g); i++) {
        if (band & bands_3g[i].mm_band_flag) {
            cinterion_band |= bands_3g[i].cinterion_band_flag;
        }
    }

    /* If we didn't find a match, set an error */
    if (!cinterion_band) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Wrong band mask: '%u'", band);
        mm_callback_info_schedule (info);
        return;
    }

    mm_callback_info_set_data (info,
                               "new-band",
                               GUINT_TO_POINTER ((guint)band),
                               NULL);
    /* Following the setup:
     *  AT^SCFG="Radion/Band",<rba>
     * We will set the preferred band equal to the allowed band, so that we force
     * the modem to connect at that specific frequency only. Note that we will be
     * passing a number here!
     */
    cmd = g_strdup_printf ("^SCFG=\"Radio/Band\",%u", cinterion_band);
    mm_at_serial_port_queue_command (port, cmd, 3, set_band_done, info);
    g_free (cmd);
}

static void
set_band (MMModemGsmNetwork *self,
          MMModemGsmBand band,
          MMModemFn callback,
          gpointer user_data)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (self);
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    /* Are we trying to change the band to the same bands currently
     * being used? if so, we're done */
    if (priv->current_bands == band) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Otherwise ask the modem */
    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    if (!priv->only_utran &&
        !priv->both_geran_utran)
        set_2g_band (self, band, port, info);
    else
        set_3g_band (self, band, port, info);
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
    MMModemCinterionGsmPrivate *priv;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    GMatchInfo *match_info = NULL;
    GRegex *regex;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
        mm_callback_info_schedule (info);
        return;
    }

    priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (gsm);
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    /* We just return cached value. For 3G devices we could try to ask the
     * modem for the Access technology being used, and base 3G-only or 2G-only
     * replies on that, but we wouldn't be covering all possible allowed modes
     * anyway. */
    mm_callback_info_set_result (info,
                                 GUINT_TO_POINTER (priv->allowed_mode),
                                 NULL);
    mm_callback_info_schedule (info);
}

static void
set_allowed_mode_cb (MMAtSerialPort *port,
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
        MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);

        priv->allowed_mode = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "new-mode"));
    }

    mm_callback_info_schedule (info);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (gsm);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    /* For dual 2G/3G devices... */
    if (priv->both_geran_utran) {
        GString *cmd;
        MMAtSerialPort *port;

        port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
        if (!port) {
            mm_callback_info_schedule (info);
            return;
        }

        /* We will try to simulate the possible allowed modes here. The
         * Cinterion devices do not seem to allow setting preferred access
         * technology in 3G devices, but they allow restricting to a given
         * one:
         * - 2G-only is forced by forcing GERAN RAT (AcT=0)
         * - 3G-only is forced by forcing UTRAN RAT (AcT=2)
         * - for the remaining ones, we default to automatic selection of RAT,
         *   which is based on the quality of the connection.
         */
        cmd = g_string_new ("+COPS=,,,");
        if (mode == MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY) {
            g_string_append (cmd, "2");
        } else if (mode == MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY) {
            g_string_append (cmd, "0");
        } /* else, no AcT given, defaults to Auto */

        mm_callback_info_set_data (info,
                                   "new-mode",
                                   GUINT_TO_POINTER (mode),
                                   NULL);

        mm_at_serial_port_queue_command (port, cmd->str, 3, set_allowed_mode_cb, info);
        g_string_free (cmd, TRUE);
        mm_callback_info_schedule (info);
        return;
    }

    /* For 3G-only devices, allow only 3G-related allowed modes */
    if (priv->only_utran) {
        switch (mode) {
        case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        case MM_MODEM_GSM_ALLOWED_MODE_ANY:
            priv->allowed_mode = mode;
            break;
        default:
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Cannot set desired allowed mode, "
                                       "not a 2G device");
            break;
        }
        mm_callback_info_schedule (info);
        return;
    }

    /* For 2G devices, allow only 2G-related allowed modes */
    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
        priv->allowed_mode = mode;
        break;
    default:
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Cannot set desired allowed mode, "
                                   "not a 3G device");
        break;
    }
    mm_callback_info_schedule (info);
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
    MMModemCinterionGsmPrivate *priv;
    GError *inner_error = NULL;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (info->modem);

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

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    /* Do nothing... see set_property() in parent, which also does nothing */
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    switch (prop_id) {
    case MM_GENERIC_GSM_PROP_SMS_INDICATION_ENABLE_CMD:
        /* AT+CNMO=<mode>,[<mt>[,<bm>[,<ds>[,<bfr>]]]]
         *  but <bfr> should be either not set, or equal to 1;
         *  and <ds> can be only either 0 or 2 (EGS5)
         */
        g_value_set_string (value, "+CNMI=2,1,2,2,1");
        break;
    case MM_GENERIC_GSM_PROP_SMS_STORAGE_LOCATION_CMD:
        /* AT=CPMS=<mem1>[,<mem2>[,<mem3>]]
         *  but <mem3> should be either not set, or equal to "SM" or "MT".
         * This <mem3> parameter is only meaningful if <mt>=2 in AT+CNMI
         * */
        g_value_set_string (value, "+CPMS=\"ME\",\"ME\"");
        break;
    default:
        break;
    }
}

/*****************************************************************************/

static void
modem_gsm_network_init (MMModemGsmNetwork *network_class)
{
    network_class->set_band = set_band;
    network_class->get_band = get_band;
}

static void
mm_modem_cinterion_gsm_init (MMModemCinterionGsm *self)
{
    MMModemCinterionGsmPrivate *priv = MM_MODEM_CINTERION_GSM_GET_PRIVATE (self);

    /* Set defaults */
    priv->sind_psinfo = TRUE; /* Initially, always try to get psinfo */
    priv->allowed_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
    priv->only_geran = FALSE;
    priv->only_utran = FALSE;
    priv->both_geran_utran = FALSE;
    priv->current_bands = 0; /* This is a bitmask, so empty */
}

static void
mm_modem_cinterion_gsm_class_init (MMModemCinterionGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemCinterionGsmPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SMS_INDICATION_ENABLE_CMD,
                                      MM_GENERIC_GSM_SMS_INDICATION_ENABLE_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SMS_STORAGE_LOCATION_CMD,
                                      MM_GENERIC_GSM_SMS_STORAGE_LOCATION_CMD);

    gsm_class->do_enable_power_up_done = do_enable_power_up_done;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

