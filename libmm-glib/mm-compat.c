/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <gio/gio.h>

#include "mm-compat.h"

#ifndef MM_DISABLE_DEPRECATED

/*****************************************************************************/

void
mm_simple_connect_properties_set_number (MMSimpleConnectProperties *self,
                                         const gchar               *number)
{
    /* NO-OP */
}

const gchar *
mm_simple_connect_properties_get_number (MMSimpleConnectProperties *self)
{
    /* NO-OP */
    return NULL;
}

/*****************************************************************************/

void
mm_bearer_properties_set_number (MMBearerProperties *self,
                                 const gchar        *number)
{
    /* NO-OP */
}

const gchar *
mm_bearer_properties_get_number (MMBearerProperties *self)
{
    /* NO-OP */
    return NULL;
}

/*****************************************************************************/

void
mm_call_properties_set_direction (MMCallProperties *self,
                                  MMCallDirection   direction)
{
    /* NO-OP */
}

MMCallDirection
mm_call_properties_get_direction (MMCallProperties *self)
{
    /* NO-OP */
    return MM_CALL_DIRECTION_UNKNOWN;
}


void
mm_call_properties_set_state (MMCallProperties *self,
                              MMCallState       state)
{
    /* NO-OP */
}


MMCallState
mm_call_properties_get_state (MMCallProperties *self)
{
    /* NO-OP */
    return MM_CALL_STATE_UNKNOWN;
}

void
mm_call_properties_set_state_reason (MMCallProperties *self,
                                     MMCallStateReason state_reason)
{
    /* NO-OP */
}

MMCallStateReason
mm_call_properties_get_state_reason (MMCallProperties *self)
{
    /* NO-OP */
    return MM_CALL_STATE_REASON_UNKNOWN;
}

/*****************************************************************************/

gchar *
mm_location_gps_nmea_build_full (MMLocationGpsNmea *self)
{
    g_auto(GStrv) traces = NULL;

    traces = mm_location_gps_nmea_get_traces (self);
    return (traces ? g_strjoinv ("\r\n", traces) : g_strdup (""));
}

/*****************************************************************************/

MMModem3gppSubscriptionState
mm_modem_3gpp_get_subscription_state (MMModem3gpp *self)
{
    return MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN;
}

/*****************************************************************************/

gboolean
mm_modem_get_pending_network_initiated_sessions (MMModemOma                           *self,
                                                 MMOmaPendingNetworkInitiatedSession **sessions,
                                                 guint                                *n_sessions)
{
    return mm_modem_oma_get_pending_network_initiated_sessions (self, sessions, n_sessions);
}

gboolean
mm_modem_peek_pending_network_initiated_sessions (MMModemOma                                 *self,
                                                  const MMOmaPendingNetworkInitiatedSession **sessions,
                                                  guint                                      *n_sessions)
{
    return mm_modem_oma_peek_pending_network_initiated_sessions (self, sessions, n_sessions);
}

#endif /* MM_DISABLE_DEPRECATED */
