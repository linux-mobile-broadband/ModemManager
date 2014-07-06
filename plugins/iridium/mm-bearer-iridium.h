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
 * Copyright (C) 2012 Ammonit Measurement GmbH.
 */

#ifndef MM_BEARER_IRIDIUM_H
#define MM_BEARER_IRIDIUM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-bearer.h"
#include "mm-broadband-modem-iridium.h"

#define MM_TYPE_BEARER_IRIDIUM            (mm_bearer_iridium_get_type ())
#define MM_BEARER_IRIDIUM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_IRIDIUM, MMBearerIridium))
#define MM_BEARER_IRIDIUM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_IRIDIUM, MMBearerIridiumClass))
#define MM_IS_BEARER_IRIDIUM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_IRIDIUM))
#define MM_IS_BEARER_IRIDIUM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_IRIDIUM))
#define MM_BEARER_IRIDIUM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_IRIDIUM, MMBearerIridiumClass))

typedef struct _MMBearerIridium MMBearerIridium;
typedef struct _MMBearerIridiumClass MMBearerIridiumClass;

struct _MMBearerIridium {
    MMBaseBearer parent;
};

struct _MMBearerIridiumClass {
    MMBaseBearerClass parent;
};

GType mm_bearer_iridium_get_type (void);

/* Iridium bearer creation implementation.
 * NOTE it is *not* a broadband bearer, so not async-initable */
MMBaseBearer *mm_bearer_iridium_new (MMBroadbandModemIridium *modem,
                                     MMBearerProperties *config);

#endif /* MM_BEARER_IRIDIUM_H */
