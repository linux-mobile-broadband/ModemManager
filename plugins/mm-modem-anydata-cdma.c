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

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-anydata-cdma.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemAnydataCdma, mm_modem_anydata_cdma, MM_TYPE_GENERIC_CDMA, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

MMModem *
mm_modem_anydata_cdma_new (const char *device,
                           const char *driver,
                           const char *plugin,
                           gboolean evdo_rev0,
                           gboolean evdo_revA)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_ANYDATA_CDMA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   MM_GENERIC_CDMA_REGISTRATION_TRY_CSS, FALSE,
                                   NULL));
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

static gboolean
int_from_match_item (GMatchInfo *match_info, guint32 num, gint *val)
{
    long int tmp;
    char *str;
    gboolean success = FALSE;

    str = g_match_info_fetch (match_info, num);
    g_return_val_if_fail (str != NULL, FALSE);

    errno = 0;
    tmp = strtol (str, NULL, 10);
    if (errno == 0 && tmp >= G_MININT && tmp <= G_MAXINT) {
        *val = (gint) tmp;
        success = TRUE;
    }
    g_free (str);
    return success;
}

static void
evdo_state_done (MMSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemCdmaRegistrationState reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    const char *reply;
    GRegex *r;
    GMatchInfo *match_info;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* If HSTATE returned an error, assume the device is not EVDO capable
             * or EVDO is not registered.
             */
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        }

        mm_callback_info_schedule (info);
        return;
    }

    reply = strip_response (response->str, "*HSTATE:");

    /* Format is "<at state>,<session state>,<channel>,<pn>,<EcIo>,<rssi>,..." */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*([^,\\)]*)\\s*,\\s*([^,\\)]*)\\s*,.*",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!r) {
        /* Parse error; warn about it and assume EVDO is not available */
        g_warning ("AnyData(%s): failed to create EVDO state regex: (%d) %s",
                   __func__,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_callback_info_schedule (info);
        return;
    }

    g_regex_match (r, reply, 0, &match_info);
    if (g_match_info_get_match_count (match_info) >= 6) {
        guint32 val = 0;
        gint dbm = 0;

        /* dBm is between -106 (worst) and -20.7 (best) */
        int_from_match_item (match_info, 6, &dbm);

        /* Parse the EVDO radio state */
        if (uint_from_match_item (match_info, 1, &val)) {
            switch (val) {
            case 3:  /* IDLE */
                /* If IDLE and the EVDO dBm is -105 or lower, assume no service.
                 * It may be that IDLE actually means NO SERVICE too; not sure.
                 */
                if (dbm > -105)
                    reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            case 4:  /* ACCESS */
            case 5:  /* CONNECT */
                reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            default:
                g_message ("ANYDATA: unknown *STATE (%d); assuming no service.", val);
                /* fall through */
            case 0:  /* NO SERVICE */
            case 1:  /* ACQUISITION */
            case 2:  /* SYNC */
                break;
            }
        }
    }

    mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);

    mm_callback_info_schedule (info);
}

static void
state_done (MMSerialPort *port,
            GString *response,
            GError *error,
            gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemCdmaRegistrationState reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    const char *reply;
    GRegex *r;
    GMatchInfo *match_info;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* Assume if we got this far, we're registered even if an error
             * occurred.  We're not sure if all AnyData CDMA modems support
             * the *STATE and *HSTATE commands.
             */
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED);
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        }

        mm_callback_info_schedule (info);
        return;
    }

    reply = strip_response (response->str, "*STATE:");

    /* Format is "<channel>,<pn>,<sid>,<nid>,<state>,<rssi>,..." */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*([^,\\)]*)\\s*,.*",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!r) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Could not parse sysinfo results (regex creation failed).");
        mm_callback_info_schedule (info);
        return;
    }

    g_regex_match (r, reply, 0, &match_info);
    if (g_match_info_get_match_count (match_info) >= 6) {
        guint32 val = 0;
        gint dbm = 0;

        /* dBm is between -106 (worst) and -20.7 (best) */
        int_from_match_item (match_info, 6, &dbm);

        /* Parse the 1x radio state */
        if (uint_from_match_item (match_info, 5, &val)) {
            switch (val) {
            case 1:  /* IDLE */
                /* If IDLE and the 1X dBm is -105 or lower, assume no service.
                 * It may be that IDLE actually means NO SERVICE too; not sure.
                 */
                if (dbm > -105)
                    reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            case 2:  /* ACCESS */
            case 3:  /* PAGING */
            case 4:  /* TRAFFIC */
                reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            default:
                g_message ("ANYDATA: unknown *STATE (%d); assuming no service.", val);
                /* fall through */
            case 0:  /* NO SERVICE */
                break;
            }
        }
    }

    mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);

    /* Try for EVDO state too */
    mm_serial_port_queue_command (port, "*HSTATE?", 3, evdo_state_done, info);
}

static void
query_registration_state (MMGenericCdma *cdma,
                          MMModemCdmaRegistrationStateFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary, *secondary, *port;

    port = primary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_PRIMARY);
    secondary = mm_generic_cdma_get_port (cdma, MM_PORT_TYPE_SECONDARY);

    info = mm_generic_cdma_query_reg_state_callback_info_new (cdma, callback, user_data);

    if (mm_port_get_connected (MM_PORT (primary))) {
        if (!secondary) {
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                               "Cannot get query registration state while connected");
            mm_callback_info_schedule (info);
            return;
        }

        /* Use secondary port if primary is connected */
        port = secondary;        
    }

    mm_serial_port_queue_command (port, "*STATE?", 3, state_done, info);
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
    if (port && MM_IS_SERIAL_PORT (port)) {
        /* Data state notifications */

        /* Data call has connected */
        regex = g_regex_new ("\\r\\n\\*ACTIVE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Data call disconnected */
        regex = g_regex_new ("\\r\\n\\*INACTIVE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Modem is now dormant */
        regex = g_regex_new ("\\r\\n\\*DORMANT:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Abnomral state notifications
         *
         * FIXME: set 1X/EVDO registration state to UNKNOWN when these
         * notifications are received?
         */

        /* Network acquisition fail */
        regex = g_regex_new ("\\r\\n\\*OFFLINE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Registration fail */
        regex = g_regex_new ("\\r\\n\\*REGREQ:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Authentication fail */
        regex = g_regex_new ("\\r\\n\\*AUTHREQ:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);
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
mm_modem_anydata_cdma_init (MMModemAnydataCdma *self)
{
}

static void
mm_modem_anydata_cdma_class_init (MMModemAnydataCdmaClass *klass)
{
    MMGenericCdmaClass *cdma_class = MM_GENERIC_CDMA_CLASS (klass);

    mm_modem_anydata_cdma_parent_class = g_type_class_peek_parent (klass);

    cdma_class->query_registration_state = query_registration_state;

#if 0
    /* FIXME: maybe use AT*SLEEP=0/1 to disable/enable slotted mode for powersave */
    cdma_class->post_enable = post_enable;
    cdma_class->post_enable = post_disable;
#endif
}

