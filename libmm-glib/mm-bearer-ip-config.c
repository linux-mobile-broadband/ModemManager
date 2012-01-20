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
 * Copyright (C) 2012 Google, Inc.
 */

#include "mm-bearer-ip-config.h"

MMBearerIpMethod
mm_bearer_ip_config_get_method (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), MM_BEARER_IP_METHOD_UNKNOWN);

    return mm_common_bearer_ip_config_get_method (self);
}

const gchar *
mm_bearer_ip_config_get_address (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return mm_common_bearer_ip_config_get_address (self);
}

guint
mm_bearer_ip_config_get_prefix (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), 0);

    return mm_common_bearer_ip_config_get_prefix (self);
}

const gchar **
mm_bearer_ip_config_get_dns (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return mm_common_bearer_ip_config_get_dns (self);
}

const gchar *
mm_bearer_ip_config_get_gateway (MMBearerIpConfig *self)
{
    g_return_val_if_fail (MM_IS_BEARER_IP_CONFIG (self), NULL);

    return mm_common_bearer_ip_config_get_gateway (self);
}
