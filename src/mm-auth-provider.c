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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <ModemManager.h>
#include "mm-errors-types.h"
#include "mm-log-object.h"
#include "mm-utils.h"
#include "mm-auth-provider.h"

#if defined WITH_POLKIT
# include <polkit/polkit.h>
#endif

struct _MMAuthProvider {
    GObject parent;
#if defined WITH_POLKIT
    PolkitAuthority *authority;
#endif
};

struct _MMAuthProviderClass {
    GObjectClass parent;
};

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMAuthProvider, mm_auth_provider, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

/*****************************************************************************/

gboolean
mm_auth_provider_authorize_finish (MMAuthProvider  *self,
                                   GAsyncResult    *res,
                                   GError        **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#if defined WITH_POLKIT

typedef struct {
    PolkitSubject         *subject;
    gchar                 *authorization;
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

static void
check_authorization_ready (PolkitAuthority *authority,
                           GAsyncResult    *res,
                           GTask           *task)
{
    PolkitAuthorizationResult *pk_result;
    GError                    *error = NULL;
    AuthorizeContext          *ctx;

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
#endif

void
mm_auth_provider_authorize (MMAuthProvider        *self,
                            GDBusMethodInvocation *invocation,
                            const gchar           *authorization,
                            GCancellable          *cancellable,
                            GAsyncReadyCallback    callback,
                            gpointer               user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

#if defined WITH_POLKIT
    {
        AuthorizeContext *ctx;

        /* When creating the object, we actually allowed errors when looking for the
         * authority. If that is the case, we'll just forbid any incoming
         * authentication request */
        if (!self->authority) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "PolicyKit authorization error: 'authority not found'");
            g_object_unref (task);
            return;
        }

        ctx = g_new (AuthorizeContext, 1);
        ctx->invocation = g_object_ref (invocation);
        ctx->authorization = g_strdup (authorization);
        ctx->subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (ctx->invocation));
        g_task_set_task_data (task, ctx, (GDestroyNotify)authorize_context_free);

        polkit_authority_check_authorization (self->authority,
                                              ctx->subject,
                                              authorization,
                                              NULL, /* details */
                                              POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                              cancellable,
                                              (GAsyncReadyCallback)check_authorization_ready,
                                              task);
    }
#else
    /* Just create the result and complete it */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
#endif
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("auth-provider");
}

/*****************************************************************************/

static void
mm_auth_provider_init (MMAuthProvider *self)
{
#if defined WITH_POLKIT
    {
        GError *error = NULL;

        self->authority = polkit_authority_get_sync (NULL, &error);
        if (!self->authority) {
            /* NOTE: we failed to create the polkit authority, but we still create
             * our AuthProvider. Every request will fail, though. */
            mm_obj_warn (self, "failed to create PolicyKit authority: '%s'",
                         error ? error->message : "unknown");
            g_clear_error (&error);
        }
    }
#endif
}

static void
dispose (GObject *object)
{
#if defined WITH_POLKIT
    g_clear_object (&(MM_AUTH_PROVIDER (object)->authority));
#endif

    G_OBJECT_CLASS (mm_auth_provider_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_auth_provider_class_init (MMAuthProviderClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    object_class->dispose = dispose;
}

MM_DEFINE_SINGLETON_GETTER (MMAuthProvider, mm_auth_provider_get, MM_TYPE_AUTH_PROVIDER)
