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

#ifndef MM_MODEM_SIMPLE_CONNECT_PROPERTIES_H
#define MM_MODEM_SIMPLE_CONNECT_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#include <libmm-common.h>

G_BEGIN_DECLS

typedef MMCommonConnectProperties                  MMModemSimpleConnectProperties;
#define MM_TYPE_MODEM_SIMPLE_CONNECT_PROPERTIES(o) MM_TYPE_COMMON_CONNECT_PROPERTIES (o)
#define MM_MODEM_SIMPLE_CONNECT_PROPERTIES(o)      MM_COMMON_CONNECT_PROPERTIES(o)
#define MM_IS_MODEM_SIMPLE_CONNECT_PROPERTIES(o)   MM_IS_COMMON_CONNECT_PROPERTIES(o)

MMModemSimpleConnectProperties *mm_modem_simple_connect_properties_new (void);
MMModemSimpleConnectProperties *mm_modem_simple_connect_properties_new_from_string (
    const gchar *str,
    GError **error);

void mm_modem_simple_connect_properties_set_pin (
    MMModemSimpleConnectProperties *properties,
    const gchar *pin);
void mm_modem_simple_connect_properties_set_operator_id (
    MMModemSimpleConnectProperties *properties,
    const gchar *operator_id);
void mm_modem_simple_connect_properties_set_bands (
    MMModemSimpleConnectProperties *properties,
    const MMModemBand *bands,
    guint n_bands);
void mm_modem_simple_connect_properties_set_allowed_modes (
    MMModemSimpleConnectProperties *properties,
    MMModemMode allowed,
    MMModemMode preferred);
void mm_modem_simple_connect_properties_set_apn (
    MMModemSimpleConnectProperties *properties,
    const gchar *apn);
void mm_modem_simple_connect_properties_set_user (
    MMModemSimpleConnectProperties *properties,
    const gchar *user);
void mm_modem_simple_connect_properties_set_password (
    MMModemSimpleConnectProperties *properties,
    const gchar *password);
void mm_modem_simple_connect_properties_set_ip_type (
    MMModemSimpleConnectProperties *properties,
    const gchar *ip_type);
void mm_modem_simple_connect_properties_set_allow_roaming (
    MMModemSimpleConnectProperties *properties,
    gboolean allow_roaming);
void mm_modem_simple_connect_properties_set_number (
    MMModemSimpleConnectProperties *properties,
    const gchar *number);

G_END_DECLS

#endif /* MM_MODEM_SIMPLE_CONNECT_PROPERTIES_H */
