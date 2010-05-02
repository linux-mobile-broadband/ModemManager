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
#include "mm-modem-novatel-gsm.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemNovatelGsm, mm_modem_novatel_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))


MMModem *
mm_modem_novatel_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOVATEL_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
dmat_callback2 (MMAtSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    mm_serial_port_close (MM_SERIAL_PORT (port));
}

static void
dmat_callback (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    if (error) {
        /* Try it again */
        if (mm_serial_port_open (MM_SERIAL_PORT (port), NULL))
            mm_at_serial_port_queue_command (port, "$NWDMAT=1", 2, dmat_callback2, NULL);
    }

    mm_serial_port_close (MM_SERIAL_PORT (port));
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port = NULL;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_AT_SERIAL_PORT (port) && (ptype == MM_PORT_TYPE_PRIMARY)) {
        /* Flip secondary ports to AT mode */
        if (mm_serial_port_open (MM_SERIAL_PORT (port), NULL))
            mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "$NWDMAT=1", 2, dmat_callback, NULL);
    }

    return !!port;
}

/*****************************************************************************/

static void
set_allowed_mode_done (MMAtSerialPort *port,
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
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    int nw_mode = 0;  /* 3G preferred */

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        nw_mode = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        nw_mode = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        break;
    }

    command = g_strdup_printf ("$NWRAT=%d,2", nw_mode);
    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static gboolean
parse_nwrat_response (GString *response,
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

    r = g_regex_new ("\\$NWRAT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
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

    str = g_match_info_fetch (match_info, 1);
    mode = atoi (str);
    g_free (str);

    g_match_info_free (match_info);

    if (mode < 0 || mode > 2) {
        g_set_error_literal (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Failed to parse mode/tech response");
        goto out;
    }

    if (out_mode) {
        if (mode == 0)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
        else if (mode == 1)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (mode == 2)
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
        else
            *out_mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
    }
    success = TRUE;

out:
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

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        parse_nwrat_response (response, &mode, &info->error);
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

    mm_at_serial_port_queue_command (port, "$NWRAT?", 3, get_allowed_mode_done, info);
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

    if (error)
        info->error = g_error_copy (error);
    else {
        p = mm_strip_tag (response->str, "$CNTI:");
        p = strchr (p, ',');
        if (p)
            act = mm_gsm_string_to_access_tech (p + 1);
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

    mm_at_serial_port_queue_command (port, "$CNTI=0", 3, get_act_request_done, info);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_novatel_gsm_init (MMModemNovatelGsm *self)
{
}

static void
mm_modem_novatel_gsm_class_init (MMModemNovatelGsmClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_novatel_gsm_parent_class = g_type_class_peek_parent (klass);

    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

