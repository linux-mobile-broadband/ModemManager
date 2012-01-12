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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-modem-simple-status-properties.h"

MMModemState
mm_modem_simple_status_properties_get_state (MMModemSimpleStatusProperties *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self),
                          MM_MODEM_STATE_UNKNOWN);

    return mm_common_simple_properties_get_state (self);
}

guint32
mm_modem_simple_status_properties_get_signal_quality (MMModemSimpleStatusProperties *self,
                                                      gboolean *recent)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self), 0);

    return mm_common_simple_properties_get_signal_quality (self, recent);
}

void
mm_modem_simple_status_properties_get_bands (MMModemSimpleStatusProperties *self,
                                             const MMModemBand **bands,
                                             guint *n_bands)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self));

    return mm_common_simple_properties_get_bands (self, bands, n_bands);
}

MMModemAccessTechnology
mm_modem_simple_status_properties_get_access_technologies (MMModemSimpleStatusProperties *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self),
                          MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return mm_common_simple_properties_get_access_technologies (self);
}

MMModem3gppRegistrationState
mm_modem_simple_status_properties_get_registration_state (MMModemSimpleStatusProperties *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self),
                          MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN);

    return mm_common_simple_properties_get_3gpp_registration_state (self);
}

const gchar *
mm_modem_simple_status_properties_get_operator_code (MMModemSimpleStatusProperties *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self), NULL);

    return mm_common_simple_properties_get_3gpp_operator_code (self);
}

const gchar *
mm_modem_simple_status_properties_get_operator_name (MMModemSimpleStatusProperties *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE_STATUS_PROPERTIES (self), NULL);

    return mm_common_simple_properties_get_3gpp_operator_name (self);
}
