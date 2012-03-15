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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-hso.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-utils.h"

G_DEFINE_TYPE (MMBroadbandBearerHso, mm_broadband_bearer_hso, MM_TYPE_BROADBAND_BEARER);

enum {
    PROP_0,
    PROP_USER,
    PROP_PASSWORD,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerHsoPrivate {
    gchar *user;
    gchar *password;
};

/*****************************************************************************/

static gboolean
cmp_properties (MMBearer *self,
                MMBearerProperties *properties)
{
    MMBroadbandBearerHso *hso = MM_BROADBAND_BEARER_HSO (self);

    return ((mm_broadband_bearer_get_allow_roaming (MM_BROADBAND_BEARER (self)) ==
             mm_bearer_properties_get_allow_roaming (properties)) &&
            (!g_strcmp0 (mm_broadband_bearer_get_ip_type (MM_BROADBAND_BEARER (self)),
                         mm_bearer_properties_get_ip_type (properties))) &&
            (!g_strcmp0 (mm_broadband_bearer_get_3gpp_apn (MM_BROADBAND_BEARER (self)),
                         mm_bearer_properties_get_apn (properties))) &&
            (!g_strcmp0 (hso->priv->user,
                         mm_bearer_properties_get_user (properties))) &&
            (!g_strcmp0 (hso->priv->password,
                         mm_bearer_properties_get_password (properties))));
}

static MMBearerProperties *
expose_properties (MMBearer *self)
{
    MMBroadbandBearerHso *hso = MM_BROADBAND_BEARER_HSO (self);
    MMBearerProperties *properties;

    properties = mm_bearer_properties_new ();
    mm_bearer_properties_set_apn (properties,
                                  mm_broadband_bearer_get_3gpp_apn (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_ip_type (properties,
                                      mm_broadband_bearer_get_ip_type (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_allow_roaming (properties,
                                            mm_broadband_bearer_get_allow_roaming (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_user (properties, hso->priv->user);
    mm_bearer_properties_set_password (properties, hso->priv->user);
    return properties;
}

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_hso_new_finish (GAsyncResult *res,
                                    GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
}

void
mm_broadband_bearer_hso_new (MMBroadbandModemHso *modem,
                             MMBearerProperties *properties,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_HSO,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BROADBAND_BEARER_3GPP_APN,      mm_bearer_properties_get_apn (properties),
        MM_BROADBAND_BEARER_IP_TYPE,       mm_bearer_properties_get_ip_type (properties),
        MM_BROADBAND_BEARER_ALLOW_ROAMING, mm_bearer_properties_get_allow_roaming (properties),
        MM_BROADBAND_BEARER_HSO_USER,      mm_bearer_properties_get_user (properties),
        MM_BROADBAND_BEARER_HSO_PASSWORD,  mm_bearer_properties_get_password (properties),
        NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    switch (prop_id) {
    case PROP_USER:
        g_free (self->priv->user);
        self->priv->user = g_value_dup_string (value);
        break;
    case PROP_PASSWORD:
        g_free (self->priv->password);
        self->priv->password = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    switch (prop_id) {
    case PROP_USER:
        g_value_set_string (value, self->priv->user);
        break;
    case PROP_PASSWORD:
        g_value_set_string (value, self->priv->password);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_hso_init (MMBroadbandBearerHso *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_HSO,
                                              MMBroadbandBearerHsoPrivate);
}

static void
finalize (GObject *object)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    g_free (self->priv->user);
    g_free (self->priv->password);

    G_OBJECT_CLASS (mm_broadband_bearer_hso_parent_class)->finalize (object);
}

static void
mm_broadband_bearer_hso_class_init (MMBroadbandBearerHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    bearer_class->cmp_properties = cmp_properties;
    bearer_class->expose_properties = expose_properties;

    properties[PROP_USER] =
        g_param_spec_string (MM_BROADBAND_BEARER_HSO_USER,
                             "User",
                             "Username to use to authenticate the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_USER, properties[PROP_USER]);

    properties[PROP_PASSWORD] =
        g_param_spec_string (MM_BROADBAND_BEARER_HSO_PASSWORD,
                             "Password",
                             "Password to use to authenticate the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PASSWORD, properties[PROP_PASSWORD]);
}
