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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-bearer-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBearerCdma, mm_bearer_cdma, MM_TYPE_BEARER);

enum {
    PROP_0,
    PROP_RM_PROTOCOL,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerCdmaPrivate {
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;
};

/*****************************************************************************/

MMModemCdmaRmProtocol
mm_bearer_cdma_get_rm_protocol (MMBearerCdma *self)
{
    return self->priv->rm_protocol;
}

/*****************************************************************************/
/* CONNECT */

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return FALSE;
}

static void
connect (MMBearer *self,
         const gchar *number,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
}

/*****************************************************************************/
/* DISCONNECT */

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return FALSE;
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
}

/*****************************************************************************/

MMBearer *
mm_bearer_cdma_new (MMBaseModem *modem,
                    MMCommonBearerProperties *properties,
                    GError **error)
{
    static guint id = 0;
    MMBearerCdma *bearer;
    gchar *path;

    /* Create the object */
    bearer = g_object_new (MM_TYPE_BEARER_CDMA,
                           MM_BEARER_CDMA_RM_PROTOCOL, mm_common_bearer_properties_get_rm_protocol (properties),
                           MM_BEARER_ALLOW_ROAMING, mm_common_bearer_properties_get_allow_roaming (properties),
                           NULL);

    /* Set modem and path ONLY after having checked input properties, so that
     * we don't export invalid bearers. */
    path = g_strdup_printf (MM_DBUS_BEARER_CDMA_PREFIX "/%d", id++);
    g_object_set (bearer,
                  MM_BEARER_PATH,  path,
                  MM_BEARER_MODEM, modem,
                  NULL);
    g_free (path);

    return MM_BEARER (bearer);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearerCdma *self = MM_BEARER_CDMA (object);

    switch (prop_id) {
    case PROP_RM_PROTOCOL:
        self->priv->rm_protocol = g_value_get_enum (value);
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
    MMBearerCdma *self = MM_BEARER_CDMA (object);

    switch (prop_id) {
    case PROP_RM_PROTOCOL:
        g_value_set_enum (value, self->priv->rm_protocol);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_bearer_cdma_init (MMBearerCdma *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_CDMA,
                                              MMBearerCdmaPrivate);
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
}

static void
mm_bearer_cdma_class_init (MMBearerCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerCdmaPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;

    properties[PROP_RM_PROTOCOL] =
        g_param_spec_enum (MM_BEARER_CDMA_RM_PROTOCOL,
                           "Rm Protocol",
                           "Protocol to use in the Rm interface",
                           MM_TYPE_MODEM_CDMA_RM_PROTOCOL,
                           MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_RM_PROTOCOL, properties[PROP_RM_PROTOCOL]);
}
