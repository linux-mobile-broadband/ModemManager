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

#include <polkit/polkit.h>

#include <config.h>

#include <ModemManager.h>
#include "mm-errors-types.h"

#include "mm-log.h"
#include "mm-auth-provider-polkit.h"

G_DEFINE_TYPE (MMAuthProviderPolkit, mm_auth_provider_polkit, MM_TYPE_AUTH_PROVIDER)

struct _MMAuthProviderPolkitPrivate {
    PolkitAuthority *authority;
};

/*****************************************************************************/

MMAuthProvider *
mm_auth_provider_polkit_new (void)
{
    return g_object_new (MM_TYPE_AUTH_PROVIDER_POLKIT, NULL);
}

/*****************************************************************************/

typedef struct {
    PolkitSubject *subject;
    gchar *authorization;
    GDBusMethodInvocation *invocation;
} AuthorizeContext;

static void
authorize_context_free (AuthorizeContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->subject);
    g_free (ctx->authorization);
    g_free (ctx);
}

static gboolean
authorize_finish (MMAuthProvider *self,
                     GAsyncResult *res,
                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
check_authorization_ready (PolkitAuthority *authority,
                           GAsyncResult *res,
                           GTask *task)
{
    PolkitAuthorizationResult *pk_result;
    GError *error = NULL;
    AuthorizeContext *ctx;

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    pk_result = polkit_authority_check_authorization_finish (authority, res, &error);
    if (!pk_result) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "PolicyKit authorization failed: '%s'",
                                 error->message);
        g_error_free (error);
    } else {
        if (polkit_authorization_result_get_is_authorized (pk_result))
            /* Good! */
            g_task_return_boolean (task, TRUE);
        else if (polkit_authorization_result_get_is_challenge (pk_result))
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNAUTHORIZED,
                                     "PolicyKit authorization failed: challenge needed for '%s'",
                                     ctx->authorization);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNAUTHORIZED,
                                     "PolicyKit authorization failed: not authorized for '%s'",
                                     ctx->authorization);
        g_object_unref (pk_result);
    }

    g_object_unref (task);
}

static void
authorize (MMAuthProvider *self,
           GDBusMethodInvocation *invocation,
           const gchar *authorization,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    MMAuthProviderPolkit *polkit = MM_AUTH_PROVIDER_POLKIT (self);
    AuthorizeContext *ctx;
    GTask *task;

    /* When creating the object, we actually allowed errors when looking for the
     * authority. If that is the case, we'll just forbid any incoming
     * authentication request */
    if (!polkit->priv->authority) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 authorize,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "PolicyKit authorization error: "
                                 "'authority not found'");
        return;
    }

    ctx = g_new (AuthorizeContext, 1);
    ctx->invocation = g_object_ref (invocation);
    ctx->authorization = g_strdup (authorization);
    ctx->subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (ctx->invocation));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)authorize_context_free);

    polkit_authority_check_authorization (polkit->priv->authority,
                                          ctx->subject,
                                          authorization,
                                          NULL, /* details */
                                          POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                          cancellable,
                                          (GAsyncReadyCallback)check_authorization_ready,
                                          task);
}

/*****************************************************************************/

static void
mm_auth_provider_polkit_init (MMAuthProviderPolkit *self)
{
    GError *error = NULL;

    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_AUTH_PROVIDER_POLKIT,
                                              MMAuthProviderPolkitPrivate);

    self->priv->authority = polkit_authority_get_sync (NULL, &error);
    if (!self->priv->authority) {
        /* NOTE: we failed to create the polkit authority, but we still create
         * our AuthProvider. Every request will fail, though. */
        mm_warn ("failed to create PolicyKit authority: '%s'",
                 error ? error->message : "unknown");
        g_clear_error (&error);
    }
}

static void
dispose (GObject *object)
{
    g_clear_object (&(MM_AUTH_PROVIDER_POLKIT (object)->priv->authority));

    G_OBJECT_CLASS (mm_auth_provider_polkit_parent_class)->dispose (object);
}

static void
mm_auth_provider_polkit_class_init (MMAuthProviderPolkitClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    MMAuthProviderClass *auth_provider_class = MM_AUTH_PROVIDER_CLASS (class);

    g_type_class_add_private (class, sizeof (MMAuthProviderPolkitPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    auth_provider_class->authorize = authorize;
    auth_provider_class->authorize_finish = authorize_finish;
}
