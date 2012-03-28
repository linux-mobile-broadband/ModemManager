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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#ifndef _MM_MODEM_LOCATION_H_
#define _MM_MODEM_LOCATION_H_

#include <ModemManager.h>
#include <libmm-common.h>

G_BEGIN_DECLS

typedef MmGdbusModemLocation      MMModemLocation;
#define MM_TYPE_MODEM_LOCATION(o) MM_GDBUS_TYPE_MODEM_LOCATION (o)
#define MM_MODEM_LOCATION(o)      MM_GDBUS_MODEM_LOCATION(o)
#define MM_IS_MODEM_LOCATION(o)   MM_GDBUS_IS_MODEM_LOCATION(o)

const gchar *mm_modem_location_get_path (MMModemLocation *self);
gchar       *mm_modem_location_dup_path (MMModemLocation *self);

MMModemLocationSource mm_modem_location_get_capabilities (MMModemLocation *self);
MMModemLocationSource mm_modem_location_get_enabled      (MMModemLocation *self);
gboolean              mm_modem_location_signals_location (MMModemLocation *self);

void     mm_modem_location_setup        (MMModemLocation *self,
                                         MMModemLocationSource sources,
                                         gboolean signal_location,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
gboolean mm_modem_location_setup_finish (MMModemLocation *self,
                                         GAsyncResult *res,
                                         GError **error);
gboolean mm_modem_location_setup_sync   (MMModemLocation *self,
                                         MMModemLocationSource sources,
                                         gboolean signal_location,
                                         GCancellable *cancellable,
                                         GError **error);

void            mm_modem_location_get_3gpp        (MMModemLocation *self,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
MMLocation3gpp *mm_modem_location_get_3gpp_finish (MMModemLocation *self,
                                                   GAsyncResult *res,
                                                   GError **error);
MMLocation3gpp *mm_modem_location_get_3gpp_sync   (MMModemLocation *self,
                                                   GCancellable *cancellable,
                                                   GError **error);

void               mm_modem_location_get_gps_nmea        (MMModemLocation *self,
                                                          GCancellable *cancellable,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);
MMLocationGpsNmea *mm_modem_location_get_gps_nmea_finish (MMModemLocation *self,
                                                          GAsyncResult *res,
                                                          GError **error);
MMLocationGpsNmea *mm_modem_location_get_gps_nmea_sync   (MMModemLocation *self,
                                                          GCancellable *cancellable,
                                                          GError **error);

void              mm_modem_location_get_gps_raw        (MMModemLocation *self,
                                                        GCancellable *cancellable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
MMLocationGpsRaw *mm_modem_location_get_gps_raw_finish (MMModemLocation *self,
                                                        GAsyncResult *res,
                                                        GError **error);
MMLocationGpsRaw *mm_modem_location_get_gps_raw_sync   (MMModemLocation *self,
                                                        GCancellable *cancellable,
                                                        GError **error);

void     mm_modem_location_get_full        (MMModemLocation *self,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_modem_location_get_full_finish (MMModemLocation *self,
                                            GAsyncResult *res,
                                            MMLocation3gpp **location_3gpp,
                                            MMLocationGpsNmea **location_gps_nmea,
                                            MMLocationGpsRaw **location_gps_raw,
                                            GError **error);
gboolean mm_modem_location_get_full_sync   (MMModemLocation *self,
                                            MMLocation3gpp **location_3gpp,
                                            MMLocationGpsNmea **location_gps_nmea,
                                            MMLocationGpsRaw **location_gps_raw,
                                            GCancellable *cancellable,
                                            GError **error);

G_END_DECLS

#endif /* _MM_MODEM_LOCATION_H_ */
