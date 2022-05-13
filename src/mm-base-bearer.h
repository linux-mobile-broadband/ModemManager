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

 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2015 Azimut Electronics
 * Copyright (C) 2011 - 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BASE_BEARER_H
#define MM_BASE_BEARER_H

#include <glib.h>
#include <glib-object.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem.h"

/*****************************************************************************/
/* Helpers to implement connect() */

typedef struct _MMBearerConnectResult MMBearerConnectResult;
MMBearerConnectResult *mm_bearer_connect_result_new              (MMPort *data,
                                                                  MMBearerIpConfig *ipv4_config,
                                                                  MMBearerIpConfig *ipv6_config);
void                   mm_bearer_connect_result_unref            (MMBearerConnectResult *result);
MMBearerConnectResult *mm_bearer_connect_result_ref              (MMBearerConnectResult *result);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerConnectResult, mm_bearer_connect_result_unref)

MMPort                *mm_bearer_connect_result_peek_data        (MMBearerConnectResult *result);
MMBearerIpConfig      *mm_bearer_connect_result_peek_ipv4_config (MMBearerConnectResult *result);
MMBearerIpConfig      *mm_bearer_connect_result_peek_ipv6_config (MMBearerConnectResult *result);

/* by default, if none specified, multiplexed=FALSE */
void                   mm_bearer_connect_result_set_multiplexed  (MMBearerConnectResult *result,
                                                                  gboolean               multiplexed);
gboolean               mm_bearer_connect_result_get_multiplexed  (MMBearerConnectResult *result);

/* profile id, if known */
void                   mm_bearer_connect_result_set_profile_id   (MMBearerConnectResult *result,
                                                                  gint                   profile_id);
gint                   mm_bearer_connect_result_get_profile_id   (MMBearerConnectResult *result);

/* speed, for stats */
void                   mm_bearer_connect_result_set_uplink_speed   (MMBearerConnectResult *result,
                                                                    guint64                speed);
guint64                mm_bearer_connect_result_get_uplink_speed   (MMBearerConnectResult *result);
void                   mm_bearer_connect_result_set_downlink_speed (MMBearerConnectResult *result,
                                                                    guint64                speed);
guint64                mm_bearer_connect_result_get_downlink_speed (MMBearerConnectResult *result);

/*****************************************************************************/

/* Default timeout values to be used in the steps of a connection or
 * disconnection attempt that may take long to complete. Note that the actual
 * connection attempt from the user may have a different timeout, but we don't
 * really fully care about that, it's a problem to consider in the user side.
 * In the daemon itself, what we want and require is to be in sync with the
 * state of the modem. */
#define MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT    180
#define MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT 120

/*****************************************************************************/

#define MM_TYPE_BASE_BEARER            (mm_base_bearer_get_type ())
#define MM_BASE_BEARER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_BEARER, MMBaseBearer))
#define MM_BASE_BEARER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_BEARER, MMBaseBearerClass))
#define MM_IS_BASE_BEARER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_BEARER))
#define MM_IS_BASE_BEARER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_BEARER))
#define MM_BASE_BEARER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_BEARER, MMBaseBearerClass))

typedef struct _MMBaseBearer MMBaseBearer;
typedef struct _MMBaseBearerClass MMBaseBearerClass;
typedef struct _MMBaseBearerPrivate MMBaseBearerPrivate;

#define MM_BASE_BEARER_PATH       "bearer-path"
#define MM_BASE_BEARER_CONNECTION "bearer-connection"
#define MM_BASE_BEARER_MODEM      "bearer-modem"
#define MM_BASE_BEARER_STATUS     "bearer-status"
#define MM_BASE_BEARER_CONFIG     "bearer-config"

typedef enum { /*< underscore_name=mm_bearer_status >*/
    MM_BEARER_STATUS_DISCONNECTED,
    MM_BEARER_STATUS_DISCONNECTING,
    MM_BEARER_STATUS_CONNECTING,
    MM_BEARER_STATUS_CONNECTED,
} MMBearerStatus;

typedef enum { /*< underscore_name=mm_bearer_connection_status >*/
    MM_BEARER_CONNECTION_STATUS_UNKNOWN,
    MM_BEARER_CONNECTION_STATUS_DISCONNECTED,
    MM_BEARER_CONNECTION_STATUS_DISCONNECTING,
    MM_BEARER_CONNECTION_STATUS_CONNECTED,
    MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED,
} MMBearerConnectionStatus;

struct _MMBaseBearer {
    MmGdbusBearerSkeleton parent;
    MMBaseBearerPrivate *priv;
};

struct _MMBaseBearerClass {
    MmGdbusBearerSkeletonClass parent;

    /* Connect this bearer */
    void (* connect) (MMBaseBearer *bearer,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    MMBearerConnectResult * (* connect_finish) (MMBaseBearer *bearer,
                                                GAsyncResult *res,
                                                GError **error);

    /* Disconnect this bearer */
    void (* disconnect) (MMBaseBearer *bearer,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* disconnect_finish) (MMBaseBearer *bearer,
                                    GAsyncResult *res,
                                    GError **error);

    /* Monitor connection status:
     *
     * Only CONNECTED or DISCONNECTED should be reported here; this method
     * is used to poll for connection status once the connection has been
     * established.
     *
     * This method will return MM_CORE_ERROR_UNSUPPORTED if the polling
     * is not required (i.e. if we can safely rely on async indications
     * sent by the modem).
     */
    void (* load_connection_status) (MMBaseBearer *bearer,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    MMBearerConnectionStatus (* load_connection_status_finish) (MMBaseBearer *bearer,
                                                                GAsyncResult *res,
                                                                GError **error);

#if defined WITH_SUSPEND_RESUME

    /* Reload connection status:
     *
     * This method should return the exact connection status of the bearer, and
     * the check must always be performed (if supported). This method should not
     * return MM_CORE_ERROR_UNSUPPORTED as a way to skip the operation, as in
     * this case the connection monitoring is required during the quick
     * suspend/resume synchronization.
     *
     * It is up to each protocol/plugin whether providing the same method here
     * and in load_connection_status() makes sense.
     */
    void (* reload_connection_status) (MMBaseBearer *bearer,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    MMBearerConnectionStatus (* reload_connection_status_finish) (MMBaseBearer *bearer,
                                                                  GAsyncResult *res,
                                                                  GError **error);

#endif

    /* Reload statistics */
    void (* reload_stats) (MMBaseBearer *bearer,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    gboolean (* reload_stats_finish) (MMBaseBearer *bearer,
                                      guint64 *bytes_rx,
                                      guint64 *bytes_tx,
                                      GAsyncResult *res,
                                      GError **error);

    /* Report connection status of this bearer */
    void (* report_connection_status) (MMBaseBearer             *bearer,
                                       MMBearerConnectionStatus  status,
                                       const GError             *connection_error);
};

GType mm_base_bearer_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseBearer, g_object_unref)

void         mm_base_bearer_export   (MMBaseBearer *self);

const gchar        *mm_base_bearer_get_path       (MMBaseBearer *self);
MMBearerStatus      mm_base_bearer_get_status     (MMBaseBearer *self);
MMBearerProperties *mm_base_bearer_peek_config    (MMBaseBearer *self);
MMBearerProperties *mm_base_bearer_get_config     (MMBaseBearer *self);
gint                mm_base_bearer_get_profile_id (MMBaseBearer *self);
MMBearerApnType     mm_base_bearer_get_apn_type   (MMBaseBearer *self);

void     mm_base_bearer_connect        (MMBaseBearer *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_base_bearer_connect_finish (MMBaseBearer *self,
                                        GAsyncResult *res,
                                        GError **error);

void     mm_base_bearer_disconnect        (MMBaseBearer *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_base_bearer_disconnect_finish (MMBaseBearer *self,
                                           GAsyncResult *res,
                                           GError **error);

void mm_base_bearer_disconnect_force (MMBaseBearer *self);

void mm_base_bearer_report_connection_status_detailed (MMBaseBearer             *self,
                                                       MMBearerConnectionStatus  status,
                                                       const GError             *connection_error);

/* When unknown, just pass NULL */
#define mm_base_bearer_report_connection_status(self, status) mm_base_bearer_report_connection_status_detailed (self, status, NULL)

void mm_base_bearer_report_speeds (MMBaseBearer *self,
                                   guint64       uplink_speed,
                                   guint64       downlink_speed);

#if defined WITH_SUSPEND_RESUME

/* Sync Broadband Bearer (async) */
void     mm_base_bearer_sync        (MMBaseBearer *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
gboolean mm_base_bearer_sync_finish (MMBaseBearer *self,
                                     GAsyncResult *res,
                                     GError **error);

#endif

#endif /* MM_BASE_BEARER_H */
