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
 * GNU General Public License for more details.
 *
 * Copyright (C) 2019 James Wah
 * Copyright (C) 2020 Marinus Enzinger <marinus@enzingerm.de>
 * Copyright (C) 2023 Shane Parslow
 * Copyright (c) 2024 Thomas Vogt
 */

#ifndef MM_BEARER_XMM7360_H
#define MM_BEARER_XMM7360_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-bearer.h"
#include "mm-broadband-modem-xmm7360.h"

#define MM_TYPE_BEARER_XMM7360            (mm_bearer_xmm7360_get_type ())
#define MM_BEARER_XMM7360(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_XMM7360, MMBearerXmm7360))
#define MM_BEARER_XMM7360_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_XMM7360, MMBearerXmm7360Class))
#define MM_IS_BEARER_XMM7360(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_XMM7360))
#define MM_IS_BEARER_XMM7360_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_XMM7360))
#define MM_BEARER_XMM7360_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_XMM7360, MMBearerXmm7360Class))

typedef struct _MMBearerXmm7360 MMBearerXmm7360;
typedef struct _MMBearerXmm7360Class MMBearerXmm7360Class;
typedef struct _MMBearerXmm7360Private MMBearerXmm7360Private;

struct _MMBearerXmm7360 {
    MMBaseBearer parent;
    MMBearerXmm7360Private *priv;
};

struct _MMBearerXmm7360Class {
    MMBaseBearerClass parent;
};

GType mm_bearer_xmm7360_get_type (void);

MMBaseBearer *mm_bearer_xmm7360_new (MMBroadbandModemXmm7360 *modem,
                                     MMBearerProperties *config);

#endif /* MM_BEARER_XMM7360_H */
