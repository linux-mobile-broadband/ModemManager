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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-huawei-cdma.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemHuaweiCdma, mm_modem_huawei_cdma, MM_TYPE_GENERIC_CDMA, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))


MMModem *
mm_modem_huawei_cdma_new (const char *device,
                         const char *driver,
                         const char *plugin,
                         gboolean evdo_rev0,
                         gboolean evdo_revA,
                         guint32 vendor,
                         guint32 product)
{
    gboolean try_css = TRUE;

    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    /* Don't use AT+CSS on EVDO-capable hardware for determining registration
     * status, because often the device will have only an EVDO connection and
     * AT+CSS won't necessarily report EVDO registration status, only 1X.
     */
    if (evdo_rev0 || evdo_revA)
        try_css = FALSE;

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_HUAWEI_CDMA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   MM_GENERIC_CDMA_REGISTRATION_TRY_CSS, try_css,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/* Unsolicited message handlers */

static gint
parse_quality (const char *str, const char *detail)
{
    long int quality = 0;

    errno = 0;
    quality = strtol (str, NULL, 10);
    if (errno == 0) {
        quality = CLAMP (quality, 0, 100);
        mm_dbg ("%s: %ld", detail, quality);
        return (gint) quality;
    }
    return -1;
}

static void
handle_1x_quality_change (MMAtSerialPort *port,
                          GMatchInfo *match_info,
                          gpointer user_data)
{
    MMModemHuaweiCdma *self = MM_MODEM_HUAWEI_CDMA (user_data);
    char *str;
    gint quality;

    str = g_match_info_fetch (match_info, 1);
    quality = parse_quality (str, "1X signal quality");
    g_free (str);

    if (quality >= 0)
        mm_generic_cdma_update_cdma1x_quality (MM_GENERIC_CDMA (self), (guint32) quality);
}

static void
handle_evdo_quality_change (MMAtSerialPort *port,
                            GMatchInfo *match_info,
                            gpointer user_data)
{
    MMModemHuaweiCdma *self = MM_MODEM_HUAWEI_CDMA (user_data);
    char *str;
    gint quality;

    str = g_match_info_fetch (match_info, 1);
    quality = parse_quality (str, "EVDO signal quality");
    g_free (str);

    if (quality >= 0)
        mm_generic_cdma_update_evdo_quality (MM_GENERIC_CDMA (self), (guint32) quality);
}

/*****************************************************************************/

static const char *
strip_response (const char *resp, const char *cmd)
{
    const char *p = resp;

    if (p) {
        if (!strncmp (p, cmd, strlen (cmd)))
            p += strlen (cmd);
        while (*p == ' ')
            p++;
    }
    return p;
}

static gboolean
uint_from_match_item (GMatchInfo *match_info, guint32 num, guint32 *val)
{
    long int tmp;
    char *str;
    gboolean success = FALSE;

    str = g_match_info_fetch (match_info, num);
    g_return_val_if_fail (str != NULL, FALSE);

    errno = 0;
    tmp = strtol (str, NULL, 10);
    if (errno == 0 && tmp >= 0 && tmp <= G_MAXUINT) {
        *val = (guint32) tmp;
        success = TRUE;
    }
    g_free (str);
    return success;
}

static void
sysinfo_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GRegex *r;
    GMatchInfo *match_info;
    const char *reply;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        /* Leave superclass' reg state alone if AT^SYSINFO isn't supported */
        goto done;
    }

    reply = strip_response (response->str, "^SYSINFO:");

    /* Format is "<srv_status>,<srv_domain>,<roam_status>,<sys_mode>,<sim_state>" */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!r) {
        mm_warn ("Huawei: ^SYSINFO parse regex creation failed.");
        goto done;
    }

    g_regex_match (r, reply, 0, &match_info);
    if (g_match_info_get_match_count (match_info) >= 5) {
        MMModemCdmaRegistrationState reg_state;
        guint32 val = 0;

        /* At this point the generic code already knows we've been registered */
        reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;

        if (uint_from_match_item (match_info, 1, &val)) {
            if (val == 2) {
                /* Service available, check roaming state */
                val = 0;
                if (uint_from_match_item (match_info, 3, &val)) {
                    if (val == 0)
                        reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                    else if (val == 1)
                        reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
                }
            }
        }

        /* Check service type */
        val = 0;
        if (uint_from_match_item (match_info, 4, &val)) {
            if (val == 2)
                mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
            else if (val == 4)
                mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);
            else if (val == 8) {
                mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
                mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);
            }
        } else {
            /* Say we're registered to something even though sysmode parsing failed */
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
        }
    } else
        mm_warn ("Huawei: failed to parse ^SYSINFO response.");

    g_match_info_free (match_info);
    g_regex_unref (r);

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
    MMAtSerialPort *port;

    info = mm_generic_cdma_query_reg_state_callback_info_new (cdma, cur_cdma_state, cur_evdo_state, callback, user_data);

    port = mm_generic_cdma_get_best_at_port (cdma, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "^SYSINFO", 3, sysinfo_done, info);
}

/*****************************************************************************/

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMPort *port = NULL;
    GRegex *regex;

	port = mm_generic_cdma_grab_port (MM_GENERIC_CDMA (modem), subsys, name, suggested_type, user_data, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        gboolean evdo0 = FALSE, evdoA = FALSE;

        g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);

        /* 1x signal level */
        regex = g_regex_new ("\\r\\n\\^RSSILVL:(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, handle_1x_quality_change, modem, NULL);
        g_regex_unref (regex);

        g_object_get (G_OBJECT (modem),
                      MM_GENERIC_CDMA_EVDO_REV0, &evdo0,
                      MM_GENERIC_CDMA_EVDO_REVA, &evdoA,
                      NULL);

        if (evdo0 || evdoA) {
            /* EVDO signal level */
            regex = g_regex_new ("\\r\\n\\^HRSSILVL:(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, handle_evdo_quality_change, modem, NULL);
            g_regex_unref (regex);
        }
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_huawei_cdma_init (MMModemHuaweiCdma *self)
{
}

static void
mm_modem_huawei_cdma_class_init (MMModemHuaweiCdmaClass *klass)
{
    MMGenericCdmaClass *cdma_class = MM_GENERIC_CDMA_CLASS (klass);

    mm_modem_huawei_cdma_parent_class = g_type_class_peek_parent (klass);

    cdma_class->query_registration_state = query_registration_state;
}

