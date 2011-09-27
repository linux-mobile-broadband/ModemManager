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

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "mm-modem-x22x-gsm.h"
#include "mm-at-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMModemX22xGsm, mm_modem_x22x_gsm, MM_TYPE_GENERIC_GSM)

MMModem *
mm_modem_x22x_gsm_new (const char *device,
                       const char *driver,
                       const char *plugin,
                       guint32 vendor,
                       guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_X22X_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/*****************************************************************************/

static gboolean
parse_syssel_response (GString *response,
                       MMModemGsmAllowedMode *out_mode,
                       GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    char *str;
    gint mode = -1;
    gboolean success = FALSE;

    g_return_val_if_fail (response != NULL, FALSE);
    g_return_val_if_fail (out_mode != NULL, FALSE);

    r = g_regex_new ("\\+SYSSEL:\\s*(\\d),(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    if (!r) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Internal error parsing mode/tech response");
        return FALSE;
    }

    if (!g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, NULL)) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to parse mode/tech response");
        goto out;
    }

    str = g_match_info_fetch (match_info, 3);
    mode = atoi (str);
    g_free (str);

    if (mode < 0 || mode > 2) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to parse mode/tech response");
        goto out;
    }

    if (out_mode) {
        if (mode == 0)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        else if (mode == 1)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (mode == 2)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
        else
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
    }
    success = TRUE;

out:
    g_match_info_free (match_info);
    g_regex_unref (r);
    return success;
}

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
        parse_syssel_response (response, &mode, &info->error);
        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
    }

    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+SYSSEL?", 3, get_allowed_mode_done, info);
}

static void
set_allowed_mode_done (MMAtSerialPort *port,
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
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command = NULL;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        command = "+SYSSEL=,1,0";
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        command = "+SYSSEL=,2,0";
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        command = "+SYSSEL=,0,0";
        break;
    }

    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
}

static void
get_act_request_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error)
        info->error = g_error_copy (error);
    else {
        p = mm_strip_tag (response->str, "+SSND:");
        act = mm_gsm_string_to_access_tech (p);
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *modem,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (modem, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+SSND?", 3, get_act_request_done, info);
}

/*****************************************************************************/

static void
mm_modem_x22x_gsm_init (MMModemX22xGsm *self)
{
}

static void
mm_modem_x22x_gsm_class_init (MMModemX22xGsmClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_x22x_gsm_parent_class = g_type_class_peek_parent (klass);

    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

