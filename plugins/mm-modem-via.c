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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-modem-via.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMModemVia, mm_modem_via, MM_TYPE_GENERIC_CDMA)

MMModem *
mm_modem_via_new (const char *device,
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

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_VIA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   MM_GENERIC_CDMA_REGISTRATION_TRY_CSS, FALSE,
                                   MM_GENERIC_CDMA_REGISTRATION_TRY_CAD, FALSE,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/*****************************************************************************/

static void
handle_evdo_quality_change (MMAtSerialPort *port,
                            GMatchInfo *match_info,
                            gpointer user_data)
{
    char *str;
    long unsigned int quality = 0;

    str = g_match_info_fetch (match_info, 1);

    errno = 0;
    quality = strtoul (str, NULL, 10);
    if (errno == 0) {
        quality = CLAMP (quality, 0, 100);
        mm_dbg ("EVDO signal quality: %ld", quality);
        mm_generic_cdma_update_evdo_quality (MM_GENERIC_CDMA (user_data), (guint32) quality);
    }
    g_free (str);
}

/*****************************************************************************/

static void
sysinfo_done (MMAtSerialPort *port,
              GString *response,
              GError *error,
              gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    const char *reply;
    MMModemCdmaRegistrationState reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    guint32 val = 0;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        /* Leave superclass' reg state alone if AT^SYSINFO isn't supported */
        goto done;
    }

    reply = mm_strip_tag (response->str, "^SYSINFO:");

    /* Format is "<srv_status>,<srv_domain>,<roam_status>,<sys_mode>,<sim_state>" */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!r) {
        mm_warn ("Via: ^SYSINFO parse regex creation failed.");
        goto done;
    }

    g_regex_match (r, reply, 0, &match_info);
    g_regex_unref (r);

    if (g_match_info_get_match_count (match_info) < 6) {
        mm_warn ("Via: ^SYSINFO parse failed.");
        goto done;
    }

    if (mm_uint_from_match_item (match_info, 1, &val)) {
        if (val == 2) {
            /* Service available, check roaming state */
            val = 0;
            if (mm_uint_from_match_item (match_info, 3, &val)) {
                if (val == 0)
                    reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                else if (val == 1)
                    reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
            }
        }
    }

    /* Check service type */
    val = 0;
    if (mm_uint_from_match_item (match_info, 4, &val)) {
        if (val == 2)  /* CDMA */
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
        else if (val == 4)  /* HDR */
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);
        else if (val == 8) {  /* Hybrid */
            mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
            mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, reg_state);
        }
    } else {
        /* Say we're registered to something even though sysmode parsing failed */
        mm_generic_cdma_query_reg_state_set_callback_1x_state (info, reg_state);
    }

done:
    if (match_info)
        g_match_info_free (match_info);

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

static void
port_grabbed (MMGenericCdma *cdma,
              MMPort *port,
              MMAtPortFlags pflags,
              gpointer user_data)
{
    GRegex *regex;

    if (MM_IS_AT_SERIAL_PORT (port)) {
        /* EVDO signal strength changed */
        regex = g_regex_new ("\\r\\n\\^HRSSILVL:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, handle_evdo_quality_change, cdma, NULL);
        g_regex_unref (regex);

        /* Access technology change */
        regex = g_regex_new ("\\r\\n\\^MODE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* EVDO data dormancy */
        regex = g_regex_new ("\\r\\n\\+DOSESSION:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\^SIMST:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+VPON:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+CREG:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Roaming indicator (reportedly unreliable) */
        regex = g_regex_new ("\\r\\n\\+VROM:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+VSER:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+CIEV:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+VPUP:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);
    }
}

/*****************************************************************************/

static void
mm_modem_via_init (MMModemVia *self)
{
}

static void
mm_modem_via_class_init (MMModemViaClass *klass)
{
    MMGenericCdmaClass *cdma_class = MM_GENERIC_CDMA_CLASS (klass);

    mm_modem_via_parent_class = g_type_class_peek_parent (klass);

    cdma_class->query_registration_state = query_registration_state;
    cdma_class->port_grabbed = port_grabbed;
}

