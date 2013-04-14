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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BEARER_MBIM_H
#define MM_BEARER_MBIM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer.h"
#include "mm-broadband-modem-mbim.h"

#define MM_TYPE_BEARER_MBIM            (mm_bearer_mbim_get_type ())
#define MM_BEARER_MBIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_MBIM, MMBearerMbim))
#define MM_BEARER_MBIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_MBIM, MMBearerMbimClass))
#define MM_IS_BEARER_MBIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_MBIM))
#define MM_IS_BEARER_MBIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_MBIM))
#define MM_BEARER_MBIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_MBIM, MMBearerMbimClass))

#define MM_BEARER_MBIM_SESSION_ID "bearer-mbim-session-id"

typedef struct _MMBearerMbim MMBearerMbim;
typedef struct _MMBearerMbimClass MMBearerMbimClass;
typedef struct _MMBearerMbimPrivate MMBearerMbimPrivate;

struct _MMBearerMbim {
    MMBearer parent;
    MMBearerMbimPrivate *priv;
};

struct _MMBearerMbimClass {
    MMBearerClass parent;
};

GType mm_bearer_mbim_get_type (void);

/* MBIM bearer creation implementation.
 * NOTE it is *not* a broadband bearer, so not async-initable */
MMBearer *mm_bearer_mbim_new (MMBroadbandModemMbim *modem,
                              MMBearerProperties *config,
                              guint32 session_id);

guint32 mm_bearer_mbim_get_session_id (MMBearerMbim *self);

#endif /* MM_BEARER_MBIM_H */
