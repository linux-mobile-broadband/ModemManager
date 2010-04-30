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
                           gboolean evdo_revA)
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

    p = strstr (reply, tag);
    if (!p)
        return -1;

    /* Skip the tag */
    p += strlen (tag);

    /* Skip spaces */
    while (isspace (*p))
        p++;
    if (*p == '-') {
        long int dbm;

        errno = 0;
        dbm = strtol (p, NULL, 10);
        if (dbm < 0 && errno == 0) {
            dbm = CLAMP (dbm, -113, -51);
            qual = 100 - ((dbm + 51) * 100 / (-113 + 51));
        }
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

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* Fallback to parent's method */
            g_clear_error (&info->error);
            parent_iface = g_type_interface_peek_parent (MM_MODEM_CDMA_GET_INTERFACE (info->modem));
            parent_iface->get_signal_quality (MM_MODEM_CDMA (info->modem), parent_csq_done, info);
        } else
            mm_callback_info_schedule (info);

        return;
    }

    /* Parse the signal quality */
    qual = get_one_qual (response->str, "RX0=");
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
    mm_modem_novatel_cdma_parent_class = g_type_class_peek_parent (klass);
}

