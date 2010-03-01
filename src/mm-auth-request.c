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

#include "mm-auth-request.h"

G_DEFINE_TYPE (MMAuthRequest, mm_auth_request, G_TYPE_OBJECT)

#define MM_AUTH_REQUEST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_AUTH_REQUEST, MMAuthRequestPrivate))

typedef struct {
    GObject *owner;
    char *auth;
    DBusGMethodInvocation *context;
    MMAuthRequestCb callback;
    gpointer callback_data;

    MMAuthResult result;
} MMAuthRequestPrivate;

/*****************************************************************************/

GObject *
mm_auth_request_new (GType atype,
                     const char *authorization,
                     GObject *owner,
                     DBusGMethodInvocation *context,
                     MMAuthRequestCb callback,
                     gpointer callback_data,
                     GDestroyNotify notify)
{
    GObject *obj;
    MMAuthRequestPrivate *priv;

    g_return_val_if_fail (authorization != NULL, NULL);
    g_return_val_if_fail (owner != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    obj = g_object_new (atype ? atype : MM_TYPE_AUTH_REQUEST, NULL);
    if (obj) {
        priv = MM_AUTH_REQUEST_GET_PRIVATE (obj);
        priv->owner = owner; /* not reffed */
        priv->context = context;
        priv->auth = g_strdup (authorization);
        priv->callback = callback;
        priv->callback_data = callback_data;

        g_object_set_data_full (obj, "caller-data", callback_data, notify);
    }

    return obj;
}

/*****************************************************************************/

const char *
mm_auth_request_get_authorization (MMAuthRequest *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_AUTH_REQUEST (self), NULL);

    return MM_AUTH_REQUEST_GET_PRIVATE (self)->auth;
}

GObject *
mm_auth_request_get_owner (MMAuthRequest *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_AUTH_REQUEST (self), NULL);

    return MM_AUTH_REQUEST_GET_PRIVATE (self)->owner;
}

MMAuthResult
mm_auth_request_get_result (MMAuthRequest *self)
{
    g_return_val_if_fail (self != NULL, MM_AUTH_RESULT_UNKNOWN);
    g_return_val_if_fail (MM_IS_AUTH_REQUEST (self), MM_AUTH_RESULT_UNKNOWN);

    return MM_AUTH_REQUEST_GET_PRIVATE (self)->result;
}

void
mm_auth_request_set_result (MMAuthRequest *self, MMAuthResult result)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AUTH_REQUEST (self));
    g_return_if_fail (result != MM_AUTH_RESULT_UNKNOWN);

    MM_AUTH_REQUEST_GET_PRIVATE (self)->result = result;
}

gboolean
mm_auth_request_authenticate (MMAuthRequest *self, GError **error)
{
    return MM_AUTH_REQUEST_GET_CLASS (self)->authenticate (self, error);
}

void
mm_auth_request_callback (MMAuthRequest *self)
{
    MMAuthRequestPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AUTH_REQUEST (self));

    priv = MM_AUTH_REQUEST_GET_PRIVATE (self);
    g_warn_if_fail (priv->result != MM_AUTH_RESULT_UNKNOWN);

    if (priv->callback)
        priv->callback (self, priv->owner, priv->context, priv->callback_data);
}

void
mm_auth_request_dispose (MMAuthRequest *self)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AUTH_REQUEST (self));

    if (MM_AUTH_REQUEST_GET_CLASS (self)->dispose)
        MM_AUTH_REQUEST_GET_CLASS (self)->dispose (self);
}

/*****************************************************************************/

static gboolean
real_authenticate (MMAuthRequest *self, GError **error)
{
    /* Null auth; everything passes */
    mm_auth_request_set_result (self, MM_AUTH_RESULT_AUTHORIZED);
    g_signal_emit_by_name (self, "result");
    return TRUE;
}

/*****************************************************************************/

static void
mm_auth_request_init (MMAuthRequest *self)
{
}

static void
dispose (GObject *object)
{
    MMAuthRequestPrivate *priv = MM_AUTH_REQUEST_GET_PRIVATE (object);

    g_free (priv->auth);

    G_OBJECT_CLASS (mm_auth_request_parent_class)->dispose (object);
}

static void
mm_auth_request_class_init (MMAuthRequestClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    mm_auth_request_parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (MMAuthRequestPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    class->authenticate = real_authenticate;

    g_signal_new ("result",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0, G_TYPE_NONE);
}

