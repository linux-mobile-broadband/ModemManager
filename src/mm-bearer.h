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
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2011 - 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BEARER_H
#define MM_BEARER_H

#include <glib.h>
#include <glib-object.h>

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
MMPort                *mm_bearer_connect_result_peek_data        (MMBearerConnectResult *result);
MMBearerIpConfig      *mm_bearer_connect_result_peek_ipv4_config (MMBearerConnectResult *result);
MMBearerIpConfig      *mm_bearer_connect_result_peek_ipv6_config (MMBearerConnectResult *result);

/*****************************************************************************/

#define MM_TYPE_BEARER            (mm_bearer_get_type ())
#define MM_BEARER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER, MMBearer))
#define MM_BEARER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER, MMBearerClass))
#define MM_IS_BEARER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER))
#define MM_IS_BEARER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER))
#define MM_BEARER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER, MMBearerClass))

typedef struct _MMBearer MMBearer;
typedef struct _MMBearerClass MMBearerClass;
typedef struct _MMBearerPrivate MMBearerPrivate;

#define MM_BEARER_PATH       "bearer-path"
#define MM_BEARER_CONNECTION "bearer-connection"
#define MM_BEARER_MODEM      "bearer-modem"
#define MM_BEARER_STATUS     "bearer-status"
#define MM_BEARER_CONFIG     "bearer-config"
#define MM_BEARER_DEFAULT_IP_FAMILY "bearer-deafult-ip-family"

typedef enum { /*< underscore_name=mm_bearer_status >*/
    MM_BEARER_STATUS_DISCONNECTED,
    MM_BEARER_STATUS_DISCONNECTING,
    MM_BEARER_STATUS_CONNECTING,
    MM_BEARER_STATUS_CONNECTED,
} MMBearerStatus;

struct _MMBearer {
    MmGdbusBearerSkeleton parent;
    MMBearerPrivate *priv;
};

struct _MMBearerClass {
    MmGdbusBearerSkeletonClass parent;

    /* Connect this bearer */
    void (* connect) (MMBearer *bearer,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    MMBearerConnectResult * (* connect_finish) (MMBearer *bearer,
                                                GAsyncResult *res,
                                                GError **error);

    /* Disconnect this bearer */
    void (* disconnect) (MMBearer *bearer,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* disconnect_finish) (MMBearer *bearer,
                                    GAsyncResult *res,
                                    GError **error);

    /* Report disconnection */
    void (* report_disconnection) (MMBearer *bearer);
};

GType mm_bearer_get_type (void);

void         mm_bearer_export   (MMBearer *self);

const gchar        *mm_bearer_get_path    (MMBearer *bearer);
MMBearerStatus      mm_bearer_get_status  (MMBearer *bearer);
MMBearerProperties *mm_bearer_peek_config (MMBearer *self);
MMBearerProperties *mm_bearer_get_config  (MMBearer *self);
MMBearerIpFamily mm_bearer_get_default_ip_family (MMBearer *self);


void     mm_bearer_connect        (MMBearer *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_bearer_connect_finish (MMBearer *self,
                                   GAsyncResult *res,
                                   GError **error);

void     mm_bearer_disconnect        (MMBearer *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean mm_bearer_disconnect_finish (MMBearer *self,
                                      GAsyncResult *res,
                                      GError **error);

void mm_bearer_disconnect_force (MMBearer *self);

void mm_bearer_report_disconnection (MMBearer *self);

#endif /* MM_BEARER_H */
