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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 */

#ifndef MM_BROADBAND_BEARER_SIERRA_H
#define MM_BROADBAND_BEARER_SIERRA_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-sierra.h"

#define MM_TYPE_BROADBAND_BEARER_SIERRA            (mm_broadband_bearer_sierra_get_type ())
#define MM_BROADBAND_BEARER_SIERRA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_SIERRA, MMBroadbandBearerSierra))
#define MM_BROADBAND_BEARER_SIERRA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_SIERRA, MMBroadbandBearerSierraClass))
#define MM_IS_BROADBAND_BEARER_SIERRA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_SIERRA))
#define MM_IS_BROADBAND_BEARER_SIERRA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_SIERRA))
#define MM_BROADBAND_BEARER_SIERRA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_SIERRA, MMBroadbandBearerSierraClass))

typedef struct _MMBroadbandBearerSierra MMBroadbandBearerSierra;
typedef struct _MMBroadbandBearerSierraClass MMBroadbandBearerSierraClass;
typedef struct _MMBroadbandBearerSierraPrivate MMBroadbandBearerSierraPrivate;

struct _MMBroadbandBearerSierra {
    MMBroadbandBearer parent;
    MMBroadbandBearerSierraPrivate *priv;
};

struct _MMBroadbandBearerSierraClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_sierra_get_type (void);

/* Default 3GPP bearer creation implementation */
void mm_broadband_bearer_sierra_new (MMBroadbandModem *modem,
                                     MMBearerProperties *config,
                                     gboolean is_icera,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
MMBearer *mm_broadband_bearer_sierra_new_finish (GAsyncResult *res,
                                                 GError **error);

#endif /* MM_BROADBAND_BEARER_SIERRA_H */
