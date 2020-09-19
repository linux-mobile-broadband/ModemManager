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

#ifndef MM_BEARER_QMI_H
#define MM_BEARER_QMI_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-bearer.h"
#include "mm-broadband-modem-qmi.h"

#define MM_TYPE_BEARER_QMI            (mm_bearer_qmi_get_type ())
#define MM_BEARER_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_QMI, MMBearerQmi))
#define MM_BEARER_QMI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_QMI, MMBearerQmiClass))
#define MM_IS_BEARER_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_QMI))
#define MM_IS_BEARER_QMI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_QMI))
#define MM_BEARER_QMI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_QMI, MMBearerQmiClass))

typedef struct _MMBearerQmi MMBearerQmi;
typedef struct _MMBearerQmiClass MMBearerQmiClass;
typedef struct _MMBearerQmiPrivate MMBearerQmiPrivate;

struct _MMBearerQmi {
    MMBaseBearer parent;
    MMBearerQmiPrivate *priv;
};

struct _MMBearerQmiClass {
    MMBaseBearerClass parent;
};

GType mm_bearer_qmi_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerQmi, g_object_unref)

/* QMI bearer creation implementation.
 * NOTE it is *not* a broadband bearer, so not async-initable */
MMBaseBearer *mm_bearer_qmi_new (MMBroadbandModemQmi *modem,
                                 MMBearerProperties *config);

#endif /* MM_BEARER_QMI_H */
