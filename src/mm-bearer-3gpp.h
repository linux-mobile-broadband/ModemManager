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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_BEARER_3GPP_H
#define MM_BEARER_3GPP_H

#include <glib.h>
#include <glib-object.h>

#include "mm-bearer.h"
#include "mm-base-modem.h"

#define MM_TYPE_BEARER_3GPP            (mm_bearer_3gpp_get_type ())
#define MM_BEARER_3GPP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_3GPP, MMBearer3gpp))
#define MM_BEARER_3GPP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_3GPP, MMBearer3gppClass))
#define MM_IS_BEARER_3GPP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_3GPP))
#define MM_IS_BEARER_3GPP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_3GPP))
#define MM_BEARER_3GPP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_3GPP, MMBearer3gppClass))

#define MM_BEARER_3GPP_CID           "bearer-3gpp-cid"
#define MM_BEARER_3GPP_APN           "bearer-3gpp-apn"
#define MM_BEARER_3GPP_IP_TYPE       "bearer-3gpp-ip-type"
#define MM_BEARER_3GPP_ALLOW_ROAMING "bearer-3gpp-allow-roaming"

/* Prefix for all 3GPP bearer object paths */
#define MM_DBUS_BEARER_3GPP_PREFIX MM_DBUS_BEARER_PREFIX "/3GPP"

typedef struct _MMBearer3gpp MMBearer3gpp;
typedef struct _MMBearer3gppClass MMBearer3gppClass;
typedef struct _MMBearer3gppPrivate MMBearer3gppPrivate;

struct _MMBearer3gpp {
    MMBearer parent;
    MMBearer3gppPrivate *priv;
};

struct _MMBearer3gppClass {
    MMBearerClass parent;
};

GType mm_bearer_3gpp_get_type (void);

/* Default 3GPP bearer creation implementation */
MMBearer *mm_bearer_3gpp_new (MMBaseModem *modem,
                              const gchar *apn,
                              const gchar *ip_type,
                              gboolean allow_roaming);

const gchar *mm_bearer_3gpp_get_apn           (MMBearer3gpp *self);
const gchar *mm_bearer_3gpp_get_ip_type       (MMBearer3gpp *self);
gboolean     mm_bearer_3gpp_get_allow_roaming (MMBearer3gpp *self);

#endif /* MM_BEARER_3GPP_H */
