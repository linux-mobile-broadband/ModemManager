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
 */

#ifndef MM_BEARER_H
#define MM_BEARER_H

#include <glib.h>
#include <glib-object.h>

#include <mm-gdbus-bearer.h>
#include "mm-base-modem.h"

#define MM_TYPE_BEARER            (mm_bearer_get_type ())
#define MM_BEARER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER, MMBearer))
#define MM_BEARER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER, MMBearerClass))
#define MM_IS_BEARER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER))
#define MM_IS_BEARER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER))
#define MM_BEARER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER, MMBearerClass))

typedef struct _MMBearer MMBearer;
typedef struct _MMBearerClass MMBearerClass;
typedef struct _MMBearerPrivate MMBearerPrivate;

#define MM_BEARER_PATH               "bearer-path"
#define MM_BEARER_CONNECTION         "bearer-connection"
#define MM_BEARER_MODEM              "bearer-modem"
#define MM_BEARER_CONNECTION_ALLOWED "bearer-connection-allowed"

/* Prefix for all bearer object paths */
#define MM_DBUS_BEARER_PREFIX MM_DBUS_PATH "/Bearers"

struct _MMBearer {
    MmGdbusBearerSkeleton parent;
    MMBearerPrivate *priv;
};

struct _MMBearerClass {
    MmGdbusBearerSkeletonClass parent;

    /* Connect this bearer */
    void (* connect) (MMBearer *bearer,
                      const gchar *number,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    gboolean (* connect_finish) (MMBearer *bearer,
                                 GAsyncResult *res,
                                 GError **error);

    /* Disconnect this bearer */
    void (* disconnect) (MMBearer *bearer,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* disconnect_finish) (MMBearer *bearer,
                                    GAsyncResult *res,
                                    GError **error);
};

GType mm_bearer_get_type (void);

const gchar *mm_bearer_get_path (MMBearer *bearer);

void mm_bearer_expose_properties (MMBearer *bearer,
                                  const gchar *first_property_name,
                                  ...);

void mm_bearer_set_connection_allowed   (MMBearer *bearer);
void mm_bearer_set_connection_forbidden (MMBearer *bearer);

#endif /* MM_BEARER_H */
