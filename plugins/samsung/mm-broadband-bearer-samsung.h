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

#ifndef MM_BROADBAND_BEARER_SAMSUNG_H
#define MM_BROADBAND_BEARER_SAMSUNG_H

#include <glib.h>
#include <glib-object.h>

#include <libmm-common.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-samsung.h"

#define MM_TYPE_BROADBAND_BEARER_SAMSUNG            (mm_broadband_bearer_samsung_get_type ())
#define MM_BROADBAND_BEARER_SAMSUNG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_SAMSUNG, MMBroadbandBearerSamsung))
#define MM_BROADBAND_BEARER_SAMSUNG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_SAMSUNG, MMBroadbandBearerSamsungClass))
#define MM_IS_BROADBAND_BEARER_SAMSUNG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_SAMSUNG))
#define MM_IS_BROADBAND_BEARER_SAMSUNG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_SAMSUNG))
#define MM_BROADBAND_BEARER_SAMSUNG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_SAMSUNG, MMBroadbandBearerSamsungClass))

#define MM_BROADBAND_BEARER_SAMSUNG_USER         "broadband-bearer-samsung-user"
#define MM_BROADBAND_BEARER_SAMSUNG_PASSWORD     "broadband-bearer-samsung-password"

typedef struct _MMBroadbandBearerSamsung MMBroadbandBearerSamsung;
typedef struct _MMBroadbandBearerSamsungClass MMBroadbandBearerSamsungClass;
typedef struct _MMBroadbandBearerSamsungPrivate MMBroadbandBearerSamsungPrivate;

struct _MMBroadbandBearerSamsung {
    MMBroadbandBearer parent;
    MMBroadbandBearerSamsungPrivate *priv;
};

struct _MMBroadbandBearerSamsungClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_samsung_get_type (void);

/* Default 3GPP bearer creation implementation */
void mm_broadband_bearer_samsung_new (MMBroadbandModemSamsung *modem,
                                      MMBearerProperties *properties,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
MMBearer *mm_broadband_bearer_samsung_new_finish (GAsyncResult *res,
                                                  GError **error);

#endif /* MM_BROADBAND_BEARER_SAMSUNG_H */
