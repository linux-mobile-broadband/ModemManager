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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"
#include "mm-modem-iridium-gsm.h"
#include "mm-log.h"

/* NOTE:
 *  We are simulating here the Iridium modem as if it were a pure GSM device
 *  (even if it of course isn't). This is because the Iridium modems implement
 *  GSM-like AT commands and therefore we can base the Iridium plugin on the
 *  generic GSM implementation. So:
 *  - We report Access Technology as pure GSM.
 *  - We allow only 2G-related allowed modes.
 *
 */

static void modem_gsm_network_init (MMModemGsmNetwork *gsm_network_class);

G_DEFINE_TYPE_EXTENDED (MMModemIridiumGsm, mm_modem_iridium_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_NETWORK, modem_gsm_network_init))


#define MM_MODEM_IRIDIUM_GSM_GET_PRIVATE(o)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_IRIDIUM_GSM, MMModemIridiumGsmPrivate))

typedef struct {
    /* Current allowed mode */
    MMModemGsmAllowedMode allowed_mode;
} MMModemIridiumGsmPrivate;

MMModem *
mm_modem_iridium_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin,
                          guint32 vendor,
                          guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_IRIDIUM_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   MM_MODEM_BASE_MAX_TIMEOUTS, 3,
                                   NULL));
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMModemIridiumGsmPrivate *priv = MM_MODEM_IRIDIUM_GSM_GET_PRIVATE (gsm);
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    /* Allow only 2G-related allowed modes */
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
                                   "not supported");
        break;
    }

    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMModemIridiumGsmPrivate *priv = MM_MODEM_IRIDIUM_GSM_GET_PRIVATE (gsm);
    MMCallbackInfo *info;

    /* Just return cached value */
    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);
    mm_callback_info_set_result (info,
                                 GUINT_TO_POINTER (priv->allowed_mode),
                                 NULL);
    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);
    mm_callback_info_set_result (info,
                                 GUINT_TO_POINTER (MM_MODEM_GSM_ACCESS_TECH_GSM),
                                 NULL);
    mm_callback_info_schedule (info);
}

static void
get_csqf_done (MMAtSerialPort *port,
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

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

    if (response && strstr (response->str, "+CSQ:")) {
        /* Got valid reply */
        const char *str;
        int quality;

        str = strstr (response->str, "+CSQ:") + 5;

        /* Skip possible whitespaces after '+CSQF:' and before the response */
        while (*str == ' ')
            str++;

        if (sscanf (str, "%d", &quality)) {
            /* Normalize the quality. <rssi> is NOT given in dBs,
             * given as a relative value between 0 and 5 */
            quality = CLAMP (quality, 0, 5) * 100 / 5;

            mm_generic_gsm_update_signal_quality (MM_GENERIC_GSM (info->modem), quality);
            mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
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
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    MMModemGsmNetwork *parent_iface;

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), NULL);
    if (!port) {
        /* Let the superclass handle it, will return cached value */
        parent_iface = g_type_interface_peek_parent (MM_MODEM_GSM_NETWORK_GET_INTERFACE (modem));
        parent_iface->get_signal_quality (modem, callback, user_data);
        return;
    }

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    /* The iridium modem may have a huge delay to get signal quality if we pass
     * AT+CSQ, so we'll default to use AT+CSQF, which is a fast version that
     * returns right away the last signal quality value retrieved */
    mm_at_serial_port_queue_command (port, "+CSQF", 3, get_csqf_done, info);
}

static void
get_sim_iccid (MMGenericGsm *modem,
               MMModemStringFn callback,
               gpointer callback_data)
{
    /* There seems to be no way of getting an ICCID/IMSI subscriber ID within
     * the Iridium AT command set, so we just skip this. */
    callback (MM_MODEM (modem), "", NULL, callback_data);
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
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
        /* No need for any special power up command */
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD:
        /* Enable RTS/CTS flow control.
         * Other available values:
         *   AT&K0: Disable flow control
         *   AT&K3: RTS/CTS
         *   AT&K4: XOFF/XON
         *   AT&K6: Both RTS/CTS and XOFF/XON
         */
        g_value_set_string (value, "&K3");
        break;
    case MM_GENERIC_GSM_PROP_SMS_INDICATION_ENABLE_CMD:
        /* AT+CNMO=<mode>,[<mt>[,<bm>[,<ds>[,<bfr>]]]]
         *  but <bm> can only be 0,
         *  and <ds> can only be either 0 or 1
         *
         * Note: Modem may return +CMS ERROR:322, which indicates Memory Full,
         * not a big deal
         */
        g_value_set_string (value, "+CNMI=2,1,0,0,1");
        break;
    case MM_GENERIC_GSM_PROP_SMS_STORAGE_LOCATION_CMD:
        /* AT=CPMS=<mem1>[,<mem2>[,<mem3>]]
         *  Only "SM" is allowed in all 3 message storages
         *
         * Note: Modem may return +CMS ERROR:322, which indicates Memory Full,
         * not a big deal
         */
        g_value_set_string (value, "+CPMS=\"SM\",\"SM\",\"SM\"");
        break;
    default:
        break;
    }
}

/*****************************************************************************/

static void
modem_gsm_network_init (MMModemGsmNetwork *network_class)
{
    network_class->get_signal_quality = get_signal_quality;
}

static void
mm_modem_iridium_gsm_init (MMModemIridiumGsm *self)
{
    MMModemIridiumGsmPrivate *priv = MM_MODEM_IRIDIUM_GSM_GET_PRIVATE (self);

    /* Set defaults */
    priv->allowed_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
}

static void
mm_modem_iridium_gsm_class_init (MMModemIridiumGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemIridiumGsmPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_POWER_UP_CMD,
                                      MM_GENERIC_GSM_POWER_UP_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_FLOW_CONTROL_CMD,
                                      MM_GENERIC_GSM_FLOW_CONTROL_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SMS_INDICATION_ENABLE_CMD,
                                      MM_GENERIC_GSM_SMS_INDICATION_ENABLE_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_SMS_STORAGE_LOCATION_CMD,
                                      MM_GENERIC_GSM_SMS_STORAGE_LOCATION_CMD);

    gsm_class->get_access_technology = get_access_technology;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_sim_iccid = get_sim_iccid;
}

