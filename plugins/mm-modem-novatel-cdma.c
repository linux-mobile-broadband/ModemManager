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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "mm-modem-novatel-cdma.h"
#include "mm-modem-helpers.h"
#include "libqcdm/src/commands.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void modem_cdma_init (MMModemCdma *cdma_class);

G_DEFINE_TYPE_EXTENDED (MMModemNovatelCdma, mm_modem_novatel_cdma, MM_TYPE_GENERIC_CDMA, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_CDMA, modem_cdma_init))


MMModem *
mm_modem_novatel_cdma_new (const char *device,
                           const char *driver,
                           const char *plugin,
                           gboolean evdo_rev0,
                           gboolean evdo_revA,
                           guint32 vendor,
                           guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOVATEL_CDMA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   NULL));
}

/*****************************************************************************/

static void
parent_csq_done (MMModem *modem,
                 guint32 result,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    if (error)
        info->error = g_error_copy (error);
    else
        mm_callback_info_set_result (info, GUINT_TO_POINTER (result), NULL);
    mm_callback_info_schedule (info);
}

static int
get_one_qual (const char *reply, const char *tag)
{
    int qual = -1;
    const char *p;
    long int dbm;
    gboolean success = FALSE;

    p = strstr (reply, tag);
    if (!p)
        return -1;

    /* Skip the tag */
    p += strlen (tag);

    /* Skip spaces */
    while (isspace (*p))
        p++;

    errno = 0;
    dbm = strtol (p, NULL, 10);
    if (errno == 0) {
        if (*p == '-') {
            /* Some cards appear to use RX0/RX1 and output RSSI in negative dBm */
            if (dbm < 0)
                success = TRUE;
        } else if (isdigit (*p) && (dbm > 0) && (dbm < 115)) {
            /* S720 appears to use "1x RSSI" and print RSSI in dBm without '-' */
            dbm *= -1;
            success = TRUE;
        }
    }

    if (success) {
        dbm = CLAMP (dbm, -113, -51);
        qual = 100 - ((dbm + 51) * 100 / (-113 + 51));
    }

    return qual;
}

static void
get_rssi_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemCdma *parent_iface;
    int qual;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        /* Fallback to parent's method */
        parent_iface = g_type_interface_peek_parent (MM_MODEM_CDMA_GET_INTERFACE (info->modem));
        parent_iface->get_signal_quality (MM_MODEM_CDMA (info->modem), parent_csq_done, info);
        return;
    }

    /* Parse the signal quality */
    qual = get_one_qual (response->str, "RX0=");
    if (qual < 0)
        qual = get_one_qual (response->str, "1x RSSI=");
    if (qual < 0)
        qual = get_one_qual (response->str, "RX1=");

    if (qual >= 0) {
        mm_callback_info_set_result (info, GUINT_TO_POINTER ((guint32) qual), NULL);
        mm_generic_cdma_update_cdma1x_quality (MM_GENERIC_CDMA (info->modem), (guint32) qual);
    } else {
        info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                    "%s", "Could not parse signal quality results");
    }

    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMModemCdma *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    MMModemCdma *parent_iface;

    port = mm_generic_cdma_get_best_at_port (MM_GENERIC_CDMA (modem), NULL);
    if (!port) {
        /* Let the superclass handle it */
        parent_iface = g_type_interface_peek_parent (MM_MODEM_CDMA_GET_INTERFACE (modem));
        parent_iface->get_signal_quality (MM_MODEM_CDMA (modem), callback, user_data);
        return;
    }

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    /* Many Novatel CDMA cards don't report CSQ in standard 0 - 31 and the CSQ
     * reply doesn't appear to be in positive dBm either; instead try the custom
     * Novatel command for it.
     */
    mm_at_serial_port_queue_command (port, "$NWRSSI", 3, get_rssi_done, info);
}

/*****************************************************************************/

static void
parse_modem_snapshot (MMCallbackInfo *info, QCDMResult *result)
{
    MMModemCdmaRegistrationState evdo_state, cdma1x_state, new_state;
    guint8 eri = 0;

    g_return_if_fail (info != NULL);
    g_return_if_fail (result != NULL);

    evdo_state = mm_generic_cdma_query_reg_state_get_callback_evdo_state (info);
    cdma1x_state = mm_generic_cdma_query_reg_state_get_callback_1x_state (info);

    /* Roaming? */
    if (qcdm_result_get_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI, &eri)) {
        char *str;
        gboolean roaming = FALSE;

        str = g_strdup_printf ("%u", eri);
        if (mm_cdma_parse_eri (str, &roaming, NULL, NULL)) {
            new_state = roaming ? MM_MODEM_CDMA_REGISTRATION_STATE_HOME : MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
            if (cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
                mm_generic_cdma_query_reg_state_set_callback_1x_state (info, new_state);
            if (evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
                mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, new_state);
        }
        g_free (str);
    }
}

static void
reg_nwsnap_6500_cb (MMQcdmSerialPort *port,
                    GByteArray *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    QCDMResult *result;

    if (!error) {
        result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const char *) response->data, response->len, NULL);
        if (result) {
            parse_modem_snapshot (info, result);
            qcdm_result_unref (result);
        }
    }
    mm_callback_info_schedule (info);
}

static void
reg_nwsnap_6800_cb (MMQcdmSerialPort *port,
                    GByteArray *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    QCDMResult *result;
    GByteArray *nwsnap;

    if (error)
        goto done;

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const char *) response->data, response->len, &info->error);
    if (!result) {
        g_clear_error (&info->error);

        /* Try for MSM6500 */
        nwsnap = g_byte_array_sized_new (25);
        nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6500, NULL);
        g_assert (nwsnap->len);
        mm_qcdm_serial_port_queue_command (port, nwsnap, 3, reg_nwsnap_6500_cb, info);
        return;
    }

    parse_modem_snapshot (info, result);
    qcdm_result_unref (result);

done:
    mm_callback_info_schedule (info);
}

static void
query_registration_state (MMGenericCdma *cdma,
                          MMModemCdmaRegistrationState cur_cdma_state,
                          MMModemCdmaRegistrationState cur_evdo_state,
                          MMModemCdmaRegistrationStateFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;
    MMQcdmSerialPort *port;
    GByteArray *nwsnap;

    info = mm_generic_cdma_query_reg_state_callback_info_new (cdma, cur_cdma_state, cur_evdo_state, callback, user_data);

    port = mm_generic_cdma_get_best_qcdm_port (cdma, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Try MSM6800 first since newer cards use that */
    nwsnap = g_byte_array_sized_new (25);
    nwsnap->len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new ((char *) nwsnap->data, 25, QCDM_NW_CHIPSET_6800, NULL);
    g_assert (nwsnap->len);
    mm_qcdm_serial_port_queue_command (port, nwsnap, 3, reg_nwsnap_6800_cb, info);
}

/*****************************************************************************/

static void
modem_cdma_init (MMModemCdma *cdma_class)
{
    cdma_class->get_signal_quality = get_signal_quality;
}

static void
mm_modem_novatel_cdma_init (MMModemNovatelCdma *self)
{
}

static void
mm_modem_novatel_cdma_class_init (MMModemNovatelCdmaClass *klass)
{
    MMGenericCdmaClass *generic_class = MM_GENERIC_CDMA_CLASS (klass);

    mm_modem_novatel_cdma_parent_class = g_type_class_peek_parent (klass);

    generic_class->query_registration_state = query_registration_state;
}

