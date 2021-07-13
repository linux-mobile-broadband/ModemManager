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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BROADBAND_BEARER_ICERA_H
#define MM_BROADBAND_BEARER_ICERA_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"

#define MM_TYPE_BROADBAND_BEARER_ICERA            (mm_broadband_bearer_icera_get_type ())
#define MM_BROADBAND_BEARER_ICERA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_ICERA, MMBroadbandBearerIcera))
#define MM_BROADBAND_BEARER_ICERA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_ICERA, MMBroadbandBearerIceraClass))
#define MM_IS_BROADBAND_BEARER_ICERA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_ICERA))
#define MM_IS_BROADBAND_BEARER_ICERA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_ICERA))
#define MM_BROADBAND_BEARER_ICERA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_ICERA, MMBroadbandBearerIceraClass))

#define MM_BROADBAND_BEARER_ICERA_DEFAULT_IP_METHOD "broadband-bearer-icera-default-ip-method"

typedef struct _MMBroadbandBearerIcera MMBroadbandBearerIcera;
typedef struct _MMBroadbandBearerIceraClass MMBroadbandBearerIceraClass;
typedef struct _MMBroadbandBearerIceraPrivate MMBroadbandBearerIceraPrivate;

struct _MMBroadbandBearerIcera {
    MMBroadbandBearer parent;
    MMBroadbandBearerIceraPrivate *priv;
};

struct _MMBroadbandBearerIceraClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_icera_get_type (void);

/* Default bearer creation implementation */
void      mm_broadband_bearer_icera_new            (MMBroadbandModem *modem,
                                                    MMBearerIpMethod ip_method,
                                                    MMBearerProperties *config,
                                                    GCancellable *cancellable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
MMBaseBearer *mm_broadband_bearer_icera_new_finish (GAsyncResult *res,
                                                    GError **error);

gint mm_broadband_bearer_icera_get_connecting_profile_id (MMBroadbandBearerIcera *self);

#endif /* MM_BROADBAND_BEARER_ICERA_H */
