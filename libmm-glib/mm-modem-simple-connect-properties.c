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

#include "mm-modem-simple-connect-properties.h"

void
mm_modem_simple_connect_properties_set_pin (MMModemSimpleConnectProperties *self,
                                            const gchar *pin)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_pin (self, pin);
}

void
mm_modem_simple_connect_properties_set_operator_id (MMModemSimpleConnectProperties *self,
                                                    const gchar *operator_id)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_operator_id (self, operator_id);
}

void
mm_modem_simple_connect_properties_set_allowed_bands (MMModemSimpleConnectProperties *self,
                                                      const MMModemBand *bands,
                                                      guint n_bands)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_allowed_bands (self, bands, n_bands);
}

void
mm_modem_simple_connect_properties_set_allowed_modes (MMModemSimpleConnectProperties *self,
                                                      MMModemMode allowed,
                                                      MMModemMode preferred)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_allowed_modes (self, allowed, preferred);
}

void
mm_modem_simple_connect_properties_set_apn (MMModemSimpleConnectProperties *self,
                                            const gchar *apn)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_apn (self, apn);
}

void
mm_modem_simple_connect_properties_set_user (MMModemSimpleConnectProperties *self,
                                             const gchar *user)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_user (self, user);
}

void
mm_modem_simple_connect_properties_set_password (MMModemSimpleConnectProperties *self,
                                                 const gchar *password)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_password (self, password);
}

void
mm_modem_simple_connect_properties_set_ip_type (MMModemSimpleConnectProperties *self,
                                                const gchar *ip_type)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_ip_type (self, ip_type);
}

void
mm_modem_simple_connect_properties_set_allow_roaming (MMModemSimpleConnectProperties *self,
                                                      gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_allow_roaming (self, allow_roaming);
}

void
mm_modem_simple_connect_properties_set_number (MMModemSimpleConnectProperties *self,
                                               const gchar *number)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES (self));

    mm_common_connect_properties_set_number (self, number);
}

/*****************************************************************************/

MMModemSimpleConnectProperties *
mm_modem_simple_connect_properties_new_from_string (const gchar *str,
                                                    GError **error)
{
    return mm_common_connect_properties_new_from_string (str, error);
}

MMModemSimpleConnectProperties *
mm_modem_simple_connect_properties_new (void)
{
    return mm_common_connect_properties_new ();
}
