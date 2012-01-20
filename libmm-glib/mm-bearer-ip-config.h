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

#ifndef MM_BEARER_IP_CONFIG_H
#define MM_BEARER_IP_CONFIG_H

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonBearerIpConfig      MMBearerIpConfig;
#define MM_TYPE_BEARER_IP_CONFIG(o) MM_TYPE_COMMON_BEARER_IP_CONFIG (o)
#define MM_BEARER_IP_CONFIG(o)      MM_COMMON_BEARER_IP_CONFIG(o)
#define MM_IS_BEARER_IP_CONFIG(o)   MM_IS_COMMON_BEARER_IP_CONFIG(o)

MMBearerIpMethod   mm_bearer_ip_config_get_method  (MMBearerIpConfig *self);
const gchar       *mm_bearer_ip_config_get_address (MMBearerIpConfig *self);
guint              mm_bearer_ip_config_get_prefix  (MMBearerIpConfig *self);
const gchar      **mm_bearer_ip_config_get_dns     (MMBearerIpConfig *self);
const gchar       *mm_bearer_ip_config_get_gateway (MMBearerIpConfig *self);

G_END_DECLS

#endif /* MM_BEARER_IP_CONFIG_H */
