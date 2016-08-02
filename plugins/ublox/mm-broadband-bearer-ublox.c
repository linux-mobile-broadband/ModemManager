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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer-ublox.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-ublox-enums-types.h"
#include "mm-modem-helpers-ublox.h"

G_DEFINE_TYPE (MMBroadbandBearerUblox, mm_broadband_bearer_ublox, MM_TYPE_BROADBAND_BEARER)

enum {
    PROP_0,
    PROP_USB_PROFILE,
    PROP_NETWORKING_MODE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerUbloxPrivate {
    MMUbloxUsbProfile     profile;
    MMUbloxNetworkingMode mode;
};

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_ublox_new_finish (GAsyncResult  *res,
                                      GError       **error)
{
    GObject *source;
    GObject *bearer;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_ublox_new (MMBroadbandModem      *modem,
                               MMUbloxUsbProfile      profile,
                               MMUbloxNetworkingMode  mode,
                               MMBearerProperties    *config,
                               GCancellable          *cancellable,
                               GAsyncReadyCallback    callback,
                               gpointer               user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_UBLOX,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        MM_BROADBAND_BEARER_UBLOX_USB_PROFILE, profile,
        MM_BROADBAND_BEARER_UBLOX_NETWORKING_MODE, mode,
        NULL);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMBroadbandBearerUblox *self = MM_BROADBAND_BEARER_UBLOX (object);

    switch (prop_id) {
    case PROP_USB_PROFILE:
        self->priv->profile = g_value_get_enum (value);
        break;
    case PROP_NETWORKING_MODE:
        self->priv->mode = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerUblox *self = MM_BROADBAND_BEARER_UBLOX (object);

    switch (prop_id) {
    case PROP_USB_PROFILE:
        g_value_set_enum (value, self->priv->profile);
        break;
    case PROP_NETWORKING_MODE:
        g_value_set_enum (value, self->priv->mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_ublox_init (MMBroadbandBearerUblox *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_UBLOX,
                                              MMBroadbandBearerUbloxPrivate);

    /* Defaults */
    self->priv->profile = MM_UBLOX_USB_PROFILE_UNKNOWN;
    self->priv->mode    = MM_UBLOX_NETWORKING_MODE_UNKNOWN;
}

static void
mm_broadband_bearer_ublox_class_init (MMBroadbandBearerUbloxClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerUbloxPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    properties[PROP_USB_PROFILE] =
        g_param_spec_enum (MM_BROADBAND_BEARER_UBLOX_USB_PROFILE,
                           "USB profile",
                           "USB profile in use",
                           MM_TYPE_UBLOX_USB_PROFILE,
                           MM_UBLOX_USB_PROFILE_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_USB_PROFILE, properties[PROP_USB_PROFILE]);

    properties[PROP_NETWORKING_MODE] =
        g_param_spec_enum (MM_BROADBAND_BEARER_UBLOX_NETWORKING_MODE,
                           "Networking mode",
                           "Networking mode in use",
                           MM_TYPE_UBLOX_NETWORKING_MODE,
                           MM_UBLOX_NETWORKING_MODE_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_NETWORKING_MODE, properties[PROP_NETWORKING_MODE]);
}
