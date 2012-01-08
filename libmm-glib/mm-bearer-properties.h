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

#ifndef MM_BEARER_PROPERTIES_H
#define MM_BEARER_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonBearerProperties     MMBearerProperties;
#define MM_TYPE_BEARER_PROPERTIES(o) MM_TYPE_COMMON_BEARER_PROPERTIES (o)
#define MM_BEARER_PROPERTIES(o)      MM_COMMON_BEARER_PROPERTIES(o)
#define MM_IS_BEARER_PROPERTIES(o)   MM_IS_COMMON_BEARER_PROPERTIES(o)

MMBearerProperties *mm_bearer_properties_new (void);
MMBearerProperties *mm_bearer_properties_new_from_string (
    const gchar *str,
    GError **error);

void mm_bearer_properties_set_apn (
    MMBearerProperties *properties,
    const gchar *apn);
void mm_bearer_properties_set_user (
    MMBearerProperties *properties,
    const gchar *user);
void mm_bearer_properties_set_password (
    MMBearerProperties *properties,
    const gchar *password);
void mm_bearer_properties_set_ip_type (
    MMBearerProperties *properties,
    const gchar *ip_type);
void mm_bearer_properties_set_allow_roaming (
    MMBearerProperties *properties,
    gboolean allow_roaming);
void mm_bearer_properties_set_number (
    MMBearerProperties *properties,
    const gchar *number);
void mm_bearer_properties_set_rm_protocol (
    MMBearerProperties *properties,
    MMModemCdmaRmProtocol protocol);

const gchar *mm_bearer_properties_get_apn (
    MMBearerProperties *properties);
gchar *mm_bearer_properties_dup_apn (
    MMBearerProperties *properties);
const gchar *mm_bearer_properties_get_user (
    MMBearerProperties *properties);
gchar *mm_bearer_properties_dup_user (
    MMBearerProperties *properties);
const gchar *mm_bearer_properties_get_password (
    MMBearerProperties *properties);
gchar *mm_bearer_properties_dup_password (
    MMBearerProperties *properties);
const gchar *mm_bearer_properties_get_ip_type (
    MMBearerProperties *properties);
gchar *mm_bearer_properties_dup_ip_type (
    MMBearerProperties *properties);
gboolean mm_bearer_properties_get_allow_roaming (
    MMBearerProperties *properties);
const gchar *mm_bearer_properties_get_number (
    MMBearerProperties *properties);
gchar *mm_bearer_properties_dup_number (
    MMBearerProperties *properties);
MMModemCdmaRmProtocol mm_bearer_properties_get_rm_protocol (
    MMBearerProperties *properties);

G_END_DECLS

#endif /* MM_BEARER_PROPERTIES_H */
