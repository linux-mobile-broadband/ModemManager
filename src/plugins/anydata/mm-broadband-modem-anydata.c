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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-serial-parsers.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-errors-types.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-anydata.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-cdma.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemAnydata, mm_broadband_modem_anydata, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init))

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */
typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

static gboolean
get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                        GAsyncResult *res,
                                        MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                        MMModemCdmaRegistrationState *detailed_evdo_state,
                                        GError **error)
{
    DetailedRegistrationStateResults *results;

    results = g_task_propagate_pointer (G_TASK (res), error);
    if (!results)
        return FALSE;

    *detailed_cdma1x_state = results->detailed_cdma1x_state;
    *detailed_evdo_state = results->detailed_evdo_state;
    g_free (results);
    return TRUE;
}

static void
hstate_ready (MMIfaceModemCdma *self,
              GAsyncResult *res,
              GTask *task)
{
    DetailedRegistrationStateResults *results;
    GError                           *error = NULL;
    const gchar                      *response;
    g_autoptr(GRegex)                 r = NULL;
    g_autoptr(GMatchInfo)             match_info = NULL;

    results = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Leave superclass' reg state alone if AT*HSTATE isn't supported */
        g_error_free (error);

        g_task_return_pointer (task, g_memdup (results, sizeof (*results)), g_free);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "*HSTATE:");

    /* Format is "<at state>,<session state>,<channel>,<pn>,<EcIo>,<rssi>,..." */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*([^,\\)]*)\\s*,\\s*([^,\\)]*)\\s*,.*",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, response, 0, &match_info);
    if (g_match_info_get_match_count (match_info) >= 6) {
        guint val = 0;
        gint dbm = 0;

        /* dBm is between -106 (worst) and -20.7 (best) */
        mm_get_int_from_match_info (match_info, 6, &dbm);

        /* Parse the EVDO radio state */
        if (mm_get_uint_from_match_info (match_info, 1, &val)) {
            switch (val) {
            case 3:  /* IDLE */
                /* If IDLE and the EVDO dBm is -105 or lower, assume no service.
                 * It may be that IDLE actually means NO SERVICE too; not sure.
                 */
                if (dbm > -105)
                    results->detailed_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            case 4:  /* ACCESS */
            case 5:  /* CONNECT */
                results->detailed_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            default:
                mm_obj_warn (self, "unknown *HSTATE (%d); assuming no service", val);
                /* fall through */
            case 0:  /* NO SERVICE */
            case 1:  /* ACQUISITION */
            case 2:  /* SYNC */
                break;
            }
        }
    }

    g_task_return_pointer (task, g_memdup (results, sizeof (*results)), g_free);
    g_object_unref (task);
}

static void
state_ready (MMIfaceModemCdma *self,
             GAsyncResult *res,
             GTask *task)
{
    DetailedRegistrationStateResults *results;
    GError                           *error = NULL;
    const gchar                      *response;
    g_autoptr(GRegex)                 r = NULL;
    g_autoptr(GMatchInfo)             match_info = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    results = g_task_get_task_data (task);
    response = mm_strip_tag (response, "*STATE:");

    /* Format is "<channel>,<pn>,<sid>,<nid>,<state>,<rssi>,..." */
    r = g_regex_new ("\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*([^,\\)]*)\\s*,.*",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, response, 0, &match_info);
    if (g_match_info_get_match_count (match_info) >= 6) {
        guint val = 0;
        gint dbm = 0;

        /* dBm is between -106 (worst) and -20.7 (best) */
        mm_get_int_from_match_info (match_info, 6, &dbm);

        /* Parse the 1x radio state */
        if (mm_get_uint_from_match_info (match_info, 5, &val)) {
            switch (val) {
            case 1:  /* IDLE */
                /* If IDLE and the 1X dBm is -105 or lower, assume no service.
                 * It may be that IDLE actually means NO SERVICE too; not sure.
                 */
                if (dbm > -105)
                    results->detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            case 2:  /* ACCESS */
            case 3:  /* PAGING */
            case 4:  /* TRAFFIC */
                results->detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
                break;
            default:
                mm_obj_warn (self, "unknown *HSTATE (%d); assuming no service", val);
                /* fall through */
            case 0:  /* NO SERVICE */
                break;
            }
        }
    }

    /* Try for EVDO state too */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*HSTATE?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)hstate_ready,
                              task);
}

static void
get_detailed_registration_state (MMIfaceModemCdma *self,
                                 MMModemCdmaRegistrationState cdma1x_state,
                                 MMModemCdmaRegistrationState evdo_state,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedRegistrationStateResults *results;
    GTask *task;

    results = g_new (DetailedRegistrationStateResults, 1);
    results->detailed_cdma1x_state = cdma1x_state;
    results->detailed_evdo_state = evdo_state;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, results, g_free);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*STATE?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)state_ready,
                              task);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
reset_finish (MMIfaceModem *self,
              GAsyncResult *res,
              GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
reset (MMIfaceModem *self,
       GAsyncReadyCallback callback,
       gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*RESET",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt    *ports[2];
    g_autoptr(GRegex)  active_regex = NULL;
    g_autoptr(GRegex)  inactive_regex = NULL;
    g_autoptr(GRegex)  dormant_regex = NULL;
    g_autoptr(GRegex)  offline_regex = NULL;
    g_autoptr(GRegex)  regreq_regex = NULL;
    g_autoptr(GRegex)  authreq_regex = NULL;
    guint              i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_anydata_parent_class)->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Data call has connected */
    active_regex = g_regex_new ("\\r\\n\\*ACTIVE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    /* Data call disconnected */
    inactive_regex = g_regex_new ("\\r\\n\\*INACTIVE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    /* Modem is now dormant */
    dormant_regex = g_regex_new ("\\r\\n\\*DORMANT:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    /* Network acquisition fail */
    offline_regex = g_regex_new ("\\r\\n\\*OFFLINE:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    /* Registration fail */
    regreq_regex = g_regex_new ("\\r\\n\\*REGREQ:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    /* Authentication fail */
    authreq_regex = g_regex_new ("\\r\\n\\*AUTHREQ:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    /* Now reset the unsolicited messages  */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* Data state notifications */
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), active_regex, NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), inactive_regex, NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), dormant_regex, NULL, NULL, NULL);

        /* Abnormal state notifications
         *
         * FIXME: set 1X/EVDO registration state to UNKNOWN when these
         * notifications are received?
         */
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), offline_regex, NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), regreq_regex, NULL, NULL, NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (MM_PORT_SERIAL_AT (ports[i]), authreq_regex, NULL, NULL, NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemAnydata *
mm_broadband_modem_anydata_new (const gchar *device,
                                const gchar *physdev,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_ANYDATA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer supports TTY only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, FALSE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_anydata_init (MMBroadbandModemAnydata *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->reset = reset;
    iface->reset_finish = reset_finish;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface->get_cdma1x_serving_system = NULL;
    iface->get_cdma1x_serving_system_finish = NULL;
    iface->get_detailed_registration_state = get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = get_detailed_registration_state_finish;
}

static void
mm_broadband_modem_anydata_class_init (MMBroadbandModemAnydataClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
