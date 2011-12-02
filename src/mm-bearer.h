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

#define MM_BEARER_PATH           "bearer-path"
#define MM_BEARER_CONNECTION     "bearer-connection"
#define MM_BEARER_MODEM          "bearer-modem"
#define MM_BEARER_CAPABILITY     "bearer-capability"

/* same names as the ones used in DBus properties */
#define MM_BEARER_CONNECTION_APN      "apn"
#define MM_BEARER_CONNECTION_IP_TYPE  "ip-type"
#define MM_BEARER_CONNECTION_USER     "user"
#define MM_BEARER_CONNECTION_PASSWORD "password"
#define MM_BEARER_CONNECTION_NUMBER   "number"

struct _MMBearer {
    MmGdbusBearerSkeleton parent;
    MMBearerPrivate *priv;
};

struct _MMBearerClass {
    MmGdbusBearerSkeletonClass parent;
};

GType mm_bearer_get_type (void);

MMBearer *mm_bearer_new (MMBaseModem *modem,
                         GVariant *properties,
                         MMModemCapability capability,
                         GError **error);

#endif /* MM_BEARER_H */
