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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BROADBAND_BEARER_UBLOX_H
#define MM_BROADBAND_BEARER_UBLOX_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-modem-helpers-ublox.h"

#define MM_TYPE_BROADBAND_BEARER_UBLOX            (mm_broadband_bearer_ublox_get_type ())
#define MM_BROADBAND_BEARER_UBLOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_UBLOX, MMBroadbandBearerUblox))
#define MM_BROADBAND_BEARER_UBLOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_UBLOX, MMBroadbandBearerUbloxClass))
#define MM_IS_BROADBAND_BEARER_UBLOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_UBLOX))
#define MM_IS_BROADBAND_BEARER_UBLOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_UBLOX))
#define MM_BROADBAND_BEARER_UBLOX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_UBLOX, MMBroadbandBearerUbloxClass))

#define MM_BROADBAND_BEARER_UBLOX_USB_PROFILE     "broadband-bearer-ublox-usb-profile"
#define MM_BROADBAND_BEARER_UBLOX_NETWORKING_MODE "broadband-bearer-ublox-networking-mode"

typedef struct _MMBroadbandBearerUblox MMBroadbandBearerUblox;
typedef struct _MMBroadbandBearerUbloxClass MMBroadbandBearerUbloxClass;
typedef struct _MMBroadbandBearerUbloxPrivate MMBroadbandBearerUbloxPrivate;

struct _MMBroadbandBearerUblox {
    MMBroadbandBearer parent;
    MMBroadbandBearerUbloxPrivate *priv;
};

struct _MMBroadbandBearerUbloxClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_ublox_get_type (void);

void          mm_broadband_bearer_ublox_new        (MMBroadbandModem       *modem,
                                                    MMUbloxUsbProfile       profile,
                                                    MMUbloxNetworkingMode   mode,
                                                    MMBearerProperties     *config,
                                                    GCancellable           *cancellable,
                                                    GAsyncReadyCallback     callback,
                                                    gpointer                user_data);
MMBaseBearer *mm_broadband_bearer_ublox_new_finish (GAsyncResult           *res,
                                                    GError                **error);

#endif /* MM_BROADBAND_BEARER_UBLOX_H */
