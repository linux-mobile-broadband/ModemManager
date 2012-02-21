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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#include "mm-auth-provider.h"

G_DEFINE_TYPE (MMAuthProvider, mm_auth_provider, G_TYPE_OBJECT)

/*****************************************************************************/

MMAuthProvider *
mm_auth_provider_new (void)
{
    return g_object_new (MM_TYPE_AUTH_PROVIDER, NULL);
}

/*****************************************************************************/

gboolean
mm_auth_provider_authorize_finish (MMAuthProvider *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_AUTH_PROVIDER (self), FALSE);

    return MM_AUTH_PROVIDER_GET_CLASS (self)->authorize_finish (self, res, error);
}

void
mm_auth_provider_authorize (MMAuthProvider *self,
                            GDBusMethodInvocation *invocation,
                            const gchar *authorization,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_AUTH_PROVIDER (self));

    MM_AUTH_PROVIDER_GET_CLASS (self)->authorize (self,
                                                  invocation,
                                                  authorization,
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/*****************************************************************************/

static gboolean
authorize_finish (MMAuthProvider *self,
                  GAsyncResult *res,
                  GError **error)
{
    /* Null auth; everything passes */
    return TRUE;
}

static void
authorize (MMAuthProvider *self,
           GDBusMethodInvocation *invocation,
           const gchar *authorization,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Just create the result and complete it */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        authorize);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

static void
mm_auth_provider_init (MMAuthProvider *self)
{
}

static void
mm_auth_provider_class_init (MMAuthProviderClass *class)
{
    /* Virtual methods */
    class->authorize = authorize;
    class->authorize_finish = authorize_finish;
}
