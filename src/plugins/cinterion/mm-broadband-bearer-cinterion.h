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
 * Copyright (C) 2016 Trimble Navigation Limited
 * Author: Matthew Stanger <Matthew_Stanger@trimble.com>
 */

#ifndef MM_BROADBAND_BEARER_CINTERION_H
#define MM_BROADBAND_BEARER_CINTERION_H

#include <glib.h>
#include <glib-object.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-cinterion.h"

#define MM_TYPE_BROADBAND_BEARER_CINTERION                (mm_broadband_bearer_cinterion_get_type ())
#define MM_BROADBAND_BEARER_CINTERION(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_CINTERION, MMBroadbandBearerCinterion))
#define MM_BROADBAND_BEARER_CINTERION_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_CINTERION, MMBroadbandBearerCinterionClass))
#define MM_IS_BROADBAND_BEARER_CINTERION(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_CINTERION))
#define MM_IS_BROADBAND_BEARER_CINTERION_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_CINTERION))
#define MM_BROADBAND_BEARER_CINTERION_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_CINTERION, MMBroadbandBearerCinterionClass))

typedef struct _MMBroadbandBearerCinterion      MMBroadbandBearerCinterion;
typedef struct _MMBroadbandBearerCinterionClass MMBroadbandBearerCinterionClass;

struct _MMBroadbandBearerCinterion {
    MMBroadbandBearer parent;
};

struct _MMBroadbandBearerCinterionClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_cinterion_get_type (void);

void          mm_broadband_bearer_cinterion_new        (MMBroadbandModemCinterion  *modem,
                                                        MMBearerProperties         *config,
                                                        GCancellable               *cancellable,
                                                        GAsyncReadyCallback         callback,
                                                        gpointer                    user_data);
MMBaseBearer *mm_broadband_bearer_cinterion_new_finish (GAsyncResult               *res,
                                                        GError                    **error);

#endif /* MM_BROADBAND_BEARER_CINTERION_H */
