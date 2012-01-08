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

#include "mm-bearer-properties.h"

void
mm_bearer_properties_set_apn (MMBearerProperties *self,
                              const gchar *apn)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_apn (self, apn);
}

void
mm_bearer_properties_set_user (MMBearerProperties *self,
                               const gchar *user)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_user (self, user);
}

void
mm_bearer_properties_set_password (MMBearerProperties *self,
                                   const gchar *password)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_password (self, password);
}

void
mm_bearer_properties_set_ip_type (MMBearerProperties *self,
                                  const gchar *ip_type)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_ip_type (self, ip_type);
}

void
mm_bearer_properties_set_allow_roaming (MMBearerProperties *self,
                                        gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_allow_roaming (self, allow_roaming);
}

void
mm_bearer_properties_set_number (MMBearerProperties *self,
                                 const gchar *number)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_number (self, number);
}

void
mm_bearer_properties_set_rm_protocol (MMBearerProperties *self,
                                      MMModemCdmaRmProtocol protocol)
{
    g_return_if_fail (MM_IS_BEARER_PROPERTIES (self));

    mm_common_bearer_properties_set_rm_protocol (self, protocol);
}

const gchar *
mm_bearer_properties_get_apn (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_common_bearer_properties_get_apn (self);
}

gchar *
mm_bearer_properties_dup_apn (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return g_strdup (mm_common_bearer_properties_get_apn (self));
}

const gchar *
mm_bearer_properties_get_user (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_common_bearer_properties_get_user (self);
}

gchar *
mm_bearer_properties_dup_user (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return g_strdup (mm_common_bearer_properties_get_user (self));
}

const gchar *
mm_bearer_properties_get_password (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_common_bearer_properties_get_password (self);
}

gchar *
mm_bearer_properties_dup_password (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return g_strdup (mm_common_bearer_properties_get_password (self));
}

const gchar *
mm_bearer_properties_get_ip_type (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_common_bearer_properties_get_ip_type (self);
}

gchar *
mm_bearer_properties_dup_ip_type (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return g_strdup (mm_common_bearer_properties_get_ip_type (self));
}

gboolean
mm_bearer_properties_get_allow_roaming (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), FALSE);

    return mm_common_bearer_properties_get_allow_roaming (self);
}

const gchar *
mm_bearer_properties_get_number (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return mm_common_bearer_properties_get_number (self);
}

gchar *
mm_bearer_properties_dup_number (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), NULL);

    return g_strdup (mm_common_bearer_properties_get_number (self));
}

MMModemCdmaRmProtocol
mm_bearer_properties_get_rm_protocol (MMBearerProperties *self)
{
    g_return_val_if_fail (MM_IS_BEARER_PROPERTIES (self), MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN);

    return mm_common_bearer_properties_get_rm_protocol (self);
}

/*****************************************************************************/

MMBearerProperties *
mm_bearer_properties_new_from_string (const gchar *str,
                                      GError **error)
{
    return mm_common_bearer_properties_new_from_string (str, error);
}

MMBearerProperties *
mm_bearer_properties_new (void)
{
    return mm_common_bearer_properties_new ();
}
