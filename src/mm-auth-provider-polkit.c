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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#include <polkit/polkit.h>

#include "mm-auth-request-polkit.h"
#include "mm-auth-provider-polkit.h"

G_DEFINE_TYPE (MMAuthProviderPolkit, mm_auth_provider_polkit, MM_TYPE_AUTH_PROVIDER)

#define MM_AUTH_PROVIDER_POLKIT_GET_PRIVATE(o) \
     (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_AUTH_PROVIDER_POLKIT, MMAuthProviderPolkitPrivate))

typedef struct {
    PolkitAuthority *authority;
    guint auth_changed_id;
} MMAuthProviderPolkitPrivate;

enum {
    PROP_NAME = 1000,
};

/*****************************************************************************/

GObject *
mm_auth_provider_polkit_new (void)
{
    return g_object_new (MM_TYPE_AUTH_PROVIDER_POLKIT, NULL);
}

/*****************************************************************************/

static void
pk_authority_changed_cb (GObject *object, gpointer user_data)
{
    /* Let clients know they should re-check their authorization */
}

/*****************************************************************************/

static MMAuthRequest *
real_create_request (MMAuthProvider *provider,
                     const char *authorization,
                     GObject *owner,
                     DBusGMethodInvocation *context,
                     MMAuthRequestCb callback,
                     gpointer callback_data,
                     GDestroyNotify notify)
{
    MMAuthProviderPolkitPrivate *priv = MM_AUTH_PROVIDER_POLKIT_GET_PRIVATE (provider);

    return (MMAuthRequest *) mm_auth_request_polkit_new (priv->authority,
                                                         authorization,
                                                         owner,
                                                         context,
                                                         callback,
                                                         callback_data,
                                                         notify);
}

/*****************************************************************************/

static void
mm_auth_provider_polkit_init (MMAuthProviderPolkit *self)
{
    MMAuthProviderPolkitPrivate *priv = MM_AUTH_PROVIDER_POLKIT_GET_PRIVATE (self);

    priv->authority = polkit_authority_get ();
    if (priv->authority) {
        priv->auth_changed_id = g_signal_connect (priv->authority,
                                                  "changed",
                                                  G_CALLBACK (pk_authority_changed_cb),
                                                  self);
    } else
        g_warning ("%s: failed to create PolicyKit authority.", __func__);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_NAME:
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
    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, "polkit");
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMAuthProviderPolkit *self = MM_AUTH_PROVIDER_POLKIT (object);
    MMAuthProviderPolkitPrivate *priv = MM_AUTH_PROVIDER_POLKIT_GET_PRIVATE (self);

    if (priv->auth_changed_id) {
        g_signal_handler_disconnect (priv->authority, priv->auth_changed_id);
        priv->auth_changed_id = 0;
    }

    G_OBJECT_CLASS (mm_auth_provider_polkit_parent_class)->dispose (object);
}

static void
mm_auth_provider_polkit_class_init (MMAuthProviderPolkitClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    MMAuthProviderClass *ap_class = MM_AUTH_PROVIDER_CLASS (class);

    mm_auth_provider_polkit_parent_class = g_type_class_peek_parent (class);
    g_type_class_add_private (class, sizeof (MMAuthProviderPolkitPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    ap_class->create_request = real_create_request;

    /* Properties */
    g_object_class_override_property (object_class, PROP_NAME, MM_AUTH_PROVIDER_NAME);
}

