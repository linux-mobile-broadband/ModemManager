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

#include <glib.h>
#include <gio/gio.h>

#include "mm-auth-request-polkit.h"

G_DEFINE_TYPE (MMAuthRequestPolkit, mm_auth_request_polkit, MM_TYPE_AUTH_REQUEST)

#define MM_AUTH_REQUEST_POLKIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_AUTH_REQUEST_POLKIT, MMAuthRequestPolkitPrivate))

typedef struct {
    PolkitAuthority *authority;
    GCancellable *cancellable;
    PolkitSubject *subject;
} MMAuthRequestPolkitPrivate;

/*****************************************************************************/

GObject *
mm_auth_request_polkit_new (PolkitAuthority *authority,
                            const char *authorization,
                            GObject *owner,
                            DBusGMethodInvocation *context,
                            MMAuthRequestCb callback,
                            gpointer callback_data,
                            GDestroyNotify notify)
{
    GObject *obj;
    MMAuthRequestPolkitPrivate *priv;
    char *sender;

    g_return_val_if_fail (authorization != NULL, NULL);
    g_return_val_if_fail (owner != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);
    g_return_val_if_fail (context != NULL, NULL);

    obj = mm_auth_request_new (MM_TYPE_AUTH_REQUEST_POLKIT,
                               authorization,
                               owner,
                               context,
                               callback,
                               callback_data,
                               notify);
    if (obj) {
        priv = MM_AUTH_REQUEST_POLKIT_GET_PRIVATE (obj);
        priv->authority = authority;
        priv->cancellable = g_cancellable_new ();

        sender = dbus_g_method_get_sender (context);
        priv->subject = polkit_system_bus_name_new (sender);
    	g_free (sender);
    }

    return obj;
}

/*****************************************************************************/

static void
pk_auth_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
    MMAuthRequestPolkit *self = user_data;
    MMAuthRequestPolkitPrivate *priv;
	PolkitAuthorizationResult *pk_result;
	GError *error = NULL;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AUTH_REQUEST_POLKIT (self));

    priv = MM_AUTH_REQUEST_POLKIT_GET_PRIVATE (self);
    if (!g_cancellable_is_cancelled (priv->cancellable)) {
    	pk_result = polkit_authority_check_authorization_finish (priv->authority,
    	                                                         result,
    	                                                         &error);
    	if (error) {
            mm_auth_request_set_result (MM_AUTH_REQUEST (self), MM_AUTH_RESULT_INTERNAL_FAILURE);
            g_warning ("%s: PolicyKit authentication error: (%d) %s",
                       __func__,
                       error ? error->code : -1,
                       error && error->message ? error->message : "(unknown)");
    	} else if (polkit_authorization_result_get_is_authorized (pk_result))
            mm_auth_request_set_result (MM_AUTH_REQUEST (self), MM_AUTH_RESULT_AUTHORIZED);
    	else if (polkit_authorization_result_get_is_challenge (pk_result))
            mm_auth_request_set_result (MM_AUTH_REQUEST (self), MM_AUTH_RESULT_CHALLENGE);
        else
            mm_auth_request_set_result (MM_AUTH_REQUEST (self), MM_AUTH_RESULT_NOT_AUTHORIZED);

        g_signal_emit_by_name (self, "result");
    }
    
    g_object_unref (self);
}

static gboolean
real_authenticate (MMAuthRequest *self, GError **error)
{
    MMAuthRequestPolkitPrivate *priv;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_AUTH_REQUEST_POLKIT (self), FALSE);

    /* We ref ourselves across the polkit call, because we can't get
     * disposed of while the call is still in-progress, and even if we
     * cancel ourselves we'll still get the callback.
     */
    g_object_ref (self);

    priv = MM_AUTH_REQUEST_POLKIT_GET_PRIVATE (self);
    polkit_authority_check_authorization (priv->authority,
                                          priv->subject,
                                          mm_auth_request_get_authorization (MM_AUTH_REQUEST (self)),
                                          NULL,
                                          POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                          priv->cancellable,
                                          pk_auth_cb,
                                          self);
    return TRUE;
}

static void
real_dispose (MMAuthRequest *req)
{
    g_return_if_fail (req != NULL);
    g_return_if_fail (MM_IS_AUTH_REQUEST_POLKIT (req));

    g_cancellable_cancel (MM_AUTH_REQUEST_POLKIT_GET_PRIVATE (req)->cancellable);
}

/*****************************************************************************/

static void
mm_auth_request_polkit_init (MMAuthRequestPolkit *self)
{
}

static void
dispose (GObject *object)
{
    MMAuthRequestPolkitPrivate *priv = MM_AUTH_REQUEST_POLKIT_GET_PRIVATE (object);

	g_object_unref (priv->cancellable);
	g_object_unref (priv->subject);

    G_OBJECT_CLASS (mm_auth_request_polkit_parent_class)->dispose (object);
}

static void
mm_auth_request_polkit_class_init (MMAuthRequestPolkitClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    MMAuthRequestClass *ar_class = MM_AUTH_REQUEST_CLASS (class);

    mm_auth_request_polkit_parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (MMAuthRequestPolkitPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    ar_class->authenticate = real_authenticate;
    ar_class->dispose = real_dispose;
}

