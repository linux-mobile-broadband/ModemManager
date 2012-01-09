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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_BEARER_CDMA_H
#define MM_BEARER_CDMA_H

#include <glib.h>
#include <glib-object.h>

#include "mm-bearer.h"
#include "mm-iface-modem-cdma.h"

#define MM_TYPE_BEARER_CDMA            (mm_bearer_cdma_get_type ())
#define MM_BEARER_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_CDMA, MMBearerCdma))
#define MM_BEARER_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_CDMA, MMBearerCdmaClass))
#define MM_IS_BEARER_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_CDMA))
#define MM_IS_BEARER_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_CDMA))
#define MM_BEARER_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_CDMA, MMBearerCdmaClass))

#define MM_BEARER_CDMA_RM_PROTOCOL "bearer-cdma-rm-protocol"

/* Prefix for all CDMA bearer object paths */
#define MM_DBUS_BEARER_CDMA_PREFIX MM_DBUS_BEARER_PREFIX "/CDMA"

typedef struct _MMBearerCdma MMBearerCdma;
typedef struct _MMBearerCdmaClass MMBearerCdmaClass;
typedef struct _MMBearerCdmaPrivate MMBearerCdmaPrivate;

struct _MMBearerCdma {
    MMBearer parent;
    MMBearerCdmaPrivate *priv;
};

struct _MMBearerCdmaClass {
    MMBearerClass parent;
};

GType mm_bearer_cdma_get_type (void);

/* Getter for an unique CDMA Bearer path */
gchar *mm_bearer_cdma_new_unique_path (void);

/* Default CDMA bearer creation implementation */
void mm_bearer_cdma_new (MMIfaceModemCdma *modem,
                         MMCommonBearerProperties *properties,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
MMBearer *mm_bearer_cdma_new_finish (MMIfaceModemCdma *modem,
                                     GAsyncResult *res,
                                     GError **error);

guint mm_bearer_cdma_get_rm_protocol (MMBearerCdma *self);

#endif /* MM_BEARER_CDMA_H */
