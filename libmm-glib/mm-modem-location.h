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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_LOCATION_H_
#define _MM_MODEM_LOCATION_H_

#include <ModemManager.h>
#include <mm-gdbus-modem.h>

#include "mm-modem-location-3gpp.h"

G_BEGIN_DECLS

typedef MmGdbusModemLocation      MMModemLocation;
#define MM_TYPE_MODEM_LOCATION(o) MM_GDBUS_TYPE_MODEM_LOCATION (o)
#define MM_MODEM_LOCATION(o)      MM_GDBUS_MODEM_LOCATION(o)
#define MM_IS_MODEM_LOCATION(o)   MM_GDBUS_IS_MODEM_LOCATION(o)

const gchar *mm_modem_location_get_path (MMModemLocation *self);
gchar       *mm_modem_location_dup_path (MMModemLocation *self);

MMModemLocationSource mm_modem_location_get_capabilities (MMModemLocation *self);
gboolean              mm_modem_location_get_enabled      (MMModemLocation *self);

void     mm_modem_location_enable        (MMModemLocation *self,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gboolean mm_modem_location_enable_finish (MMModemLocation *self,
                                          GAsyncResult *res,
                                          GError **error);
gboolean mm_modem_location_enable_sync   (MMModemLocation *self,
                                          GCancellable *cancellable,
                                          GError **error);

void     mm_modem_location_disable        (MMModemLocation *self,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_modem_location_disable_finish (MMModemLocation *self,
                                           GAsyncResult *res,
                                           GError **error);
gboolean mm_modem_location_disable_sync   (MMModemLocation *self,
                                           GCancellable *cancellable,
                                           GError **error);

void                 mm_modem_location_get_3gpp        (MMModemLocation *self,
                                                        GCancellable *cancellable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
MMModemLocation3gpp *mm_modem_location_get_3gpp_finish (MMModemLocation *self,
                                                        GAsyncResult *res,
                                                        GError **error);
MMModemLocation3gpp *mm_modem_location_get_3gpp_sync   (MMModemLocation *self,
                                                        GCancellable *cancellable,
                                                        GError **error);
G_END_DECLS

#endif /* _MM_MODEM_LOCATION_H_ */
