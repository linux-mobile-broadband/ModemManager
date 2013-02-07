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
    int cdma_qual = 0, evdo_qual = 0;
    int composite_qual = 0;

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

    /* When the modem is using CDMA 1x, the RSSI will be prefixed with
     * "1x RSSI".  When using EVDO, the RSSI will be prefixed with "RX0".
     */

    evdo_qual = get_one_qual (response->str, "RX0=");
    if (evdo_qual >= 0) {
        mm_generic_cdma_update_evdo_quality (MM_GENERIC_CDMA (info->modem), (guint32) evdo_qual);
        composite_qual = evdo_qual;
    } else {
        cdma_qual = get_one_qual (response->str, "1x RSSI=");
        if (cdma_qual >= 0) {
            mm_generic_cdma_update_cdma1x_quality (MM_GENERIC_CDMA (info->modem), (guint32) cdma_qual);
            composite_qual = cdma_qual;
        }
    }

    if (composite_qual >= 0)
        mm_callback_info_set_result (info, GUINT_TO_POINTER ((guint32) composite_qual), NULL);
    else {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse signal quality results");
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
parse_modem_eri (MMCallbackInfo *info, QcdmResult *result)
{
    MMModemCdmaRegistrationState evdo_state, cdma1x_state, new_state;
    guint8 indicator_id = 0, icon_id = 0, icon_mode = 0;

    g_return_if_fail (info != NULL);
    g_return_if_fail (result != NULL);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_INDICATOR_ID, &indicator_id);
    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_ID, &icon_id);
    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_MODE, &icon_mode);

    /* We use the "Icon ID" (also called the "Icon Index") because if it is 1,
     * the device is never roaming.  Any operator-defined IDs (greater than 2)
     * may or may not be roaming, but that's operator-defined and we don't
     * know anything about them.
     *
     * Indicator ID:
     * 0 appears to be "not roaming", contrary to standard ERI values
     * >= 1 appears to be the actual ERI value, which may or may not be
     *      roaming depending on the operator's custom ERI list
     *
     * Icon ID:
     * 0 = roaming indicator on
     * 1 = roaming indicator off
     * 2 = roaming indicator flash
     *
     * Icon Mode:
     * 0 = normal
     * 1 = flash  (only used with Icon ID >= 2)
     *
     * Roaming example:
     *    Roam:         160
     *    Indicator ID: 160
     *    Icon ID:      3
     *    Icon Mode:    0
     *    Call Prompt:  1
     *
     * Home example:
     *    Roam:         0
     *    Indicator ID: 0
     *    Icon ID:      1
     *    Icon Mode:    0
     *    Call Prompt:  1
     */
    if (icon_id == 1)
        new_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
    else
        new_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;

    cdma1x_state = mm_generic_cdma_query_reg_state_get_callback_1x_state (info);
    if (cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        mm_generic_cdma_query_reg_state_set_callback_1x_state (info, new_state);

    evdo_state = mm_generic_cdma_query_reg_state_get_callback_evdo_state (info);
    if (evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, new_state);
}

static void
reg_eri_6500_cb (MMQcdmSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    QcdmResult *result;

    if (!error) {
        result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result ((const char *) response->data, response->len, NULL);
        if (result) {
            parse_modem_eri (info, result);
            qcdm_result_unref (result);
        }
    }
    mm_callback_info_schedule (info);
}

static void
reg_eri_6800_cb (MMQcdmSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    QcdmResult *result;
    GByteArray *nweri;

    if (error)
        goto done;

    /* Parse the response */
    result = qcdm_cmd_nw_subsys_eri_result ((const char *) response->data, response->len, NULL);
    if (!result) {
        /* Try for MSM6500 */
        nweri = g_byte_array_sized_new (25);
        nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6500);
        g_assert (nweri->len);
        mm_qcdm_serial_port_queue_command (port, nweri, 3, reg_eri_6500_cb, info);
        return;
    }

    parse_modem_eri (info, result);
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
    GByteArray *nweri;

    info = mm_generic_cdma_query_reg_state_callback_info_new (cdma, cur_cdma_state, cur_evdo_state, callback, user_data);

    port = mm_generic_cdma_get_best_qcdm_port (cdma, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    /* Try MSM6800 first since newer cards use that */
    nweri = g_byte_array_sized_new (25);
    nweri->len = qcdm_cmd_nw_subsys_eri_new ((char *) nweri->data, 25, QCDM_NW_CHIPSET_6800);
    g_assert (nweri->len);
    mm_qcdm_serial_port_queue_command (port, nweri, 3, reg_eri_6800_cb, info);
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

