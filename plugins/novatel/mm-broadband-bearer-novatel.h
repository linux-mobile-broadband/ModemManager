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
 * Author: Nathan Williams <njw@google.com>
 *
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_BROADBAND_BEARER_NOVATEL_H
#define MM_BROADBAND_BEARER_NOVATEL_H

#include <glib.h>
#include <glib-object.h>

#include <libmm-common.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-novatel.h"

#define MM_TYPE_BROADBAND_BEARER_NOVATEL            (mm_broadband_bearer_novatel_get_type ())
#define MM_BROADBAND_BEARER_NOVATEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_NOVATEL, MMBroadbandBearerNovatel))
#define MM_BROADBAND_BEARER_NOVATEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_NOVATEL, MMBroadbandBearerNovatelClass))
#define MM_IS_BROADBAND_BEARER_NOVATEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_NOVATEL))
#define MM_IS_BROADBAND_BEARER_NOVATEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_NOVATEL))
#define MM_BROADBAND_BEARER_NOVATEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_NOVATEL, MMBroadbandBearerNovatelClass))

#define MM_BROADBAND_BEARER_NOVATEL_USER         "broadband-bearer-novatel-user"
#define MM_BROADBAND_BEARER_NOVATEL_PASSWORD     "broadband-bearer-novatel-password"

typedef struct _MMBroadbandBearerNovatel MMBroadbandBearerNovatel;
typedef struct _MMBroadbandBearerNovatelClass MMBroadbandBearerNovatelClass;
typedef struct _MMBroadbandBearerNovatelPrivate MMBroadbandBearerNovatelPrivate;

struct _MMBroadbandBearerNovatel {
    MMBroadbandBearer parent;
    MMBroadbandBearerNovatelPrivate *priv;
};

struct _MMBroadbandBearerNovatelClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_novatel_get_type (void);

/* Default 3GPP bearer creation implementation */
void mm_broadband_bearer_novatel_new (MMBroadbandModemNovatel *modem,
                                      MMBearerProperties *properties,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
MMBearer *mm_broadband_bearer_novatel_new_finish (GAsyncResult *res,
                                                  GError **error);


#endif /* MM_BROADBAND_BEARER_NOVATEL_H */
