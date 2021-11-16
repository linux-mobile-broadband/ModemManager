/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-location-3gpp.h"
#include "mm-location-gps-nmea.h"
#include "mm-location-gps-raw.h"
#include "mm-location-cdma-bs.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_LOCATION            (mm_modem_location_get_type ())
#define MM_MODEM_LOCATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_LOCATION, MMModemLocation))
#define MM_MODEM_LOCATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_LOCATION, MMModemLocationClass))
#define MM_IS_MODEM_LOCATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_LOCATION))
#define MM_IS_MODEM_LOCATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_LOCATION))
#define MM_MODEM_LOCATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_LOCATION, MMModemLocationClass))

typedef struct _MMModemLocation MMModemLocation;
typedef struct _MMModemLocationClass MMModemLocationClass;
typedef struct _MMModemLocationPrivate MMModemLocationPrivate;

/**
 * MMModemLocation:
 *
 * The #MMModemLocation structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemLocation {
    /*< private >*/
    MmGdbusModemLocationProxy  parent;
    MMModemLocationPrivate    *priv;
};

struct _MMModemLocationClass {
    /*< private >*/
    MmGdbusModemLocationProxyClass parent;
};

GType mm_modem_location_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemLocation, g_object_unref)

const gchar *mm_modem_location_get_path (MMModemLocation *self);
gchar       *mm_modem_location_dup_path (MMModemLocation *self);

MMModemLocationSource mm_modem_location_get_capabilities (MMModemLocation *self);

MMModemLocationSource mm_modem_location_get_enabled      (MMModemLocation *self);

gboolean              mm_modem_location_signals_location (MMModemLocation *self);

MMModemLocationAssistanceDataType mm_modem_location_get_supported_assistance_data (MMModemLocation *self);

const gchar *mm_modem_location_get_supl_server (MMModemLocation *self);
gchar       *mm_modem_location_dup_supl_server (MMModemLocation *self);

const gchar **mm_modem_location_get_assistance_data_servers (MMModemLocation *self);
gchar       **mm_modem_location_dup_assistance_data_servers (MMModemLocation *self);

guint mm_modem_location_get_gps_refresh_rate (MMModemLocation *self);

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

void     mm_modem_location_set_supl_server        (MMModemLocation *self,
                                                   const gchar *supl,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
gboolean mm_modem_location_set_supl_server_finish (MMModemLocation *self,
                                                   GAsyncResult *res,
                                                   GError **error);
gboolean mm_modem_location_set_supl_server_sync   (MMModemLocation *self,
                                                   const gchar *supl,
                                                   GCancellable *cancellable,
                                                   GError **error);

void     mm_modem_location_inject_assistance_data        (MMModemLocation      *self,
                                                          const guint8         *data,
                                                          gsize                 data_size,
                                                          GCancellable         *cancellable,
                                                          GAsyncReadyCallback   callback,
                                                          gpointer              user_data);
gboolean mm_modem_location_inject_assistance_data_finish (MMModemLocation      *self,
                                                          GAsyncResult         *res,
                                                          GError              **error);
gboolean mm_modem_location_inject_assistance_data_sync   (MMModemLocation      *self,
                                                          const guint8         *data,
                                                          gsize                 data_size,
                                                          GCancellable         *cancellable,
                                                          GError              **error);

void     mm_modem_location_set_gps_refresh_rate        (MMModemLocation *self,
                                                        guint rate,
                                                        GCancellable *cancellable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
gboolean mm_modem_location_set_gps_refresh_rate_finish (MMModemLocation *self,
                                                        GAsyncResult *res,
                                                        GError **error);
gboolean mm_modem_location_set_gps_refresh_rate_sync   (MMModemLocation *self,
                                                        guint rate,
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

void              mm_modem_location_get_cdma_bs        (MMModemLocation *self,
                                                        GCancellable *cancellable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer user_data);
MMLocationCdmaBs *mm_modem_location_get_cdma_bs_finish (MMModemLocation *self,
                                                        GAsyncResult *res,
                                                        GError **error);
MMLocationCdmaBs *mm_modem_location_get_cdma_bs_sync   (MMModemLocation *self,
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
                                            MMLocationCdmaBs **location_cdma_bs,
                                            GError **error);
gboolean mm_modem_location_get_full_sync   (MMModemLocation *self,
                                            MMLocation3gpp **location_3gpp,
                                            MMLocationGpsNmea **location_gps_nmea,
                                            MMLocationGpsRaw **location_gps_raw,
                                            MMLocationCdmaBs **location_cdma_bs,
                                            GCancellable *cancellable,
                                            GError **error);

MMLocation3gpp    *mm_modem_location_peek_signaled_3gpp     (MMModemLocation *self);
MMLocation3gpp    *mm_modem_location_get_signaled_3gpp      (MMModemLocation *self);
MMLocationGpsNmea *mm_modem_location_peek_signaled_gps_nmea (MMModemLocation *self);
MMLocationGpsNmea *mm_modem_location_get_signaled_gps_nmea  (MMModemLocation *self);
MMLocationGpsRaw  *mm_modem_location_peek_signaled_gps_raw  (MMModemLocation *self);
MMLocationGpsRaw  *mm_modem_location_get_signaled_gps_raw   (MMModemLocation *self);
MMLocationCdmaBs  *mm_modem_location_peek_signaled_cdma_bs  (MMModemLocation *self);
MMLocationCdmaBs  *mm_modem_location_get_signaled_cdma_bs   (MMModemLocation *self);

G_END_DECLS

#endif /* _MM_MODEM_LOCATION_H_ */
