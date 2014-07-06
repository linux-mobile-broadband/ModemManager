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
 * Copyright (C) 2013 Altair Semiconductor
 *
 * Author: Ori Inbar <ori.inbar@altair-semi.com>
 */

#ifndef MM_BROADBAND_BEARER_ALTAIR_LTE_H
#define MM_BROADBAND_BEARER_ALTAIR_LTE_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-altair-lte.h"

#define MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE            (mm_broadband_bearer_altair_lte_get_type ())
#define MM_BROADBAND_BEARER_ALTAIR_LTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE, MMBroadbandBearerAltairLte))
#define MM_BROADBAND_BEARER_ALTAIR_LTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE, MMBroadbandBearerAltairLteClass))
#define MM_IS_BROADBAND_BEARER_ALTAIR_LTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE))
#define MM_IS_BROADBAND_BEARER_ALTAIR_LTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE))
#define MM_BROADBAND_BEARER_ALTAIR_LTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE, MMBroadbandBearerAltairLteClass))

typedef struct _MMBroadbandBearerAltairLte MMBroadbandBearerAltairLte;
typedef struct _MMBroadbandBearerAltairLteClass MMBroadbandBearerAltairLteClass;

struct _MMBroadbandBearerAltairLte {
    MMBroadbandBearer parent;
};

struct _MMBroadbandBearerAltairLteClass {
    MMBroadbandBearerClass parent;
};

GType mm_broadband_bearer_altair_lte_get_type (void);

/* Default 3GPP bearer creation implementation */
void          mm_broadband_bearer_altair_lte_new        (MMBroadbandModemAltairLte *modem,
                                                         MMBearerProperties *properties,
                                                         GCancellable *cancellable,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);
MMBaseBearer *mm_broadband_bearer_altair_lte_new_finish (GAsyncResult *res,
                                                         GError **error);

#endif /* MM_BROADBAND_BEARER_ALTAIR_LTE_H */
