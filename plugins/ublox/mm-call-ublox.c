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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-ublox.h"
#include "mm-call-ublox.h"

G_DEFINE_TYPE (MMCallUblox, mm_call_ublox, MM_TYPE_BASE_CALL)

struct _MMCallUbloxPrivate {
    GRegex *ucallstat_regex;
};

/*****************************************************************************/
/* In-call unsolicited events */

static void
ublox_ucallstat_received (MMPortSerialAt *port,
                          GMatchInfo     *match_info,
                          MMCallUblox    *self)
{
    static const gchar *call_stat_names[] = {
        [0] = "active",
        [1] = "hold",
        [2] = "dialling (MO)",
        [3] = "alerting (MO)",
        [4] = "ringing (MT)",
        [5] = "waiting (MT)",
        [6] = "disconnected",
        [7] = "connected",
    };
    guint id;
    guint stat;

    if (!mm_get_uint_from_match_info (match_info, 1, &id))
        return;

    if (!mm_get_uint_from_match_info (match_info, 2, &stat))
        return;

    if (stat < G_N_ELEMENTS (call_stat_names))
        mm_dbg ("u-blox call id '%u' state: '%s'", id, call_stat_names[stat]);
    else
        mm_dbg ("u-blox call id '%u' state unknown: '%u'", id, stat);

    switch (stat) {
    case 0:
        /* ringing to active */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (self)) == MM_CALL_STATE_RINGING_OUT)
            mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
        break;
    case 1:
        /* ignore hold state, we don't support this yet. */
        break;
    case 2:
        /* ignore dialing state, we already handle this in the parent */
        break;
    case 3:
        /* dialing to ringing */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (self)) == MM_CALL_STATE_DIALING)
            mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_RINGING_OUT, MM_CALL_STATE_REASON_OUTGOING_STARTED);
        break;
    case 4:
        /* ignore MT ringing state, we already handle this in the parent */
        break;
    case 5:
        /* ignore MT waiting state */
        break;
    case 6:
        mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
        break;
    case 7:
        /* ringing to active */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (self)) == MM_CALL_STATE_RINGING_OUT)
            mm_base_call_change_state (MM_BASE_CALL (self), MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
        break;
    }
}

static gboolean
common_setup_cleanup_unsolicited_events (MMCallUblox  *self,
                                         gboolean      enable,
                                         GError      **error)
{
    MMBaseModem    *modem = NULL;
    MMPortSerialAt *port;

    if (G_UNLIKELY (!self->priv->ucallstat_regex))
        self->priv->ucallstat_regex = g_regex_new ("\\r\\n\\+UCALLSTAT:\\s*(\\d+),(\\d+)\\r\\n",
						   G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    g_object_get (self,
                  MM_BASE_CALL_MODEM, &modem,
                  NULL);

    port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (modem));
    if (port) {
        mm_dbg ("%s +UCALLSTAT URC handler", enable ? "adding" : "removing");
        mm_port_serial_at_add_unsolicited_msg_handler (
            port,
            self->priv->ucallstat_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)ublox_ucallstat_received : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_object_unref (modem);
    return TRUE;
}

static gboolean
setup_unsolicited_events (MMBaseCall  *self,
                          GError     **error)
{
    return common_setup_cleanup_unsolicited_events (MM_CALL_UBLOX (self), TRUE, error);
}

static gboolean
cleanup_unsolicited_events (MMBaseCall  *self,
                            GError     **error)
{
    return common_setup_cleanup_unsolicited_events (MM_CALL_UBLOX (self), FALSE, error);
}

/*****************************************************************************/

MMBaseCall *
mm_call_ublox_new (MMBaseModem     *modem,
                   MMCallDirection  direction,
                   const gchar     *number)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_UBLOX,
                                       MM_BASE_CALL_MODEM, modem,
                                       "direction",        direction,
                                       "number",           number,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_ublox_init (MMCallUblox *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CALL_UBLOX, MMCallUbloxPrivate);
}

static void
finalize (GObject *object)
{
    MMCallUblox *self = MM_CALL_UBLOX (object);

    if (self->priv->ucallstat_regex)
        g_regex_unref (self->priv->ucallstat_regex);

    G_OBJECT_CLASS (mm_call_ublox_parent_class)->finalize (object);
}

static void
mm_call_ublox_class_init (MMCallUbloxClass *klass)
{
    GObjectClass    *object_class    = G_OBJECT_CLASS     (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallUbloxPrivate));

    object_class->finalize = finalize;

    base_call_class->setup_unsolicited_events   = setup_unsolicited_events;
    base_call_class->cleanup_unsolicited_events = cleanup_unsolicited_events;
}
