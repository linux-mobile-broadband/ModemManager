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

#include <string.h>

#include "mm-marshal.h"
#include "mm-auth-provider.h"

GObject *mm_auth_provider_new (void);

G_DEFINE_TYPE (MMAuthProvider, mm_auth_provider, G_TYPE_OBJECT)

#define MM_AUTH_PROVIDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_AUTH_PROVIDER, MMAuthProviderPrivate))

typedef struct {
    GHashTable *requests;
    guint process_id;
} MMAuthProviderPrivate;

enum {
    PROP_0,
    PROP_NAME,
    LAST_PROP
};

/*****************************************************************************/

GObject *
mm_auth_provider_new (void)
{
    return g_object_new (MM_TYPE_AUTH_PROVIDER, NULL);
}

/*****************************************************************************/

static void
remove_requests (MMAuthProvider *self, GSList *remove)
{
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);
    MMAuthRequest *req;

    while (remove) {
        req = MM_AUTH_REQUEST (remove->data);
        g_hash_table_remove (priv->requests, req);
        remove = g_slist_remove (remove, req);
    }
}

void
mm_auth_provider_cancel_request (MMAuthProvider *provider, MMAuthRequest *req)
{
    MMAuthProviderPrivate *priv;

    g_return_if_fail (provider != NULL);
    g_return_if_fail (MM_IS_AUTH_PROVIDER (provider));
    g_return_if_fail (req != NULL);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);

    g_return_if_fail (g_hash_table_lookup (priv->requests, req) != NULL);
    g_hash_table_remove (priv->requests, req);
}

void
mm_auth_provider_cancel_for_owner (MMAuthProvider *self, GObject *owner)
{
    MMAuthProviderPrivate *priv;
    GHashTableIter iter;
    MMAuthRequest *req;
    gpointer value;
    GSList *remove = NULL;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AUTH_PROVIDER (self));

    /* Find all requests from this owner */
    priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);
    g_hash_table_iter_init (&iter, priv->requests);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        req = MM_AUTH_REQUEST (value);
        if (mm_auth_request_get_owner (req) == owner)
            remove = g_slist_prepend (remove, req);
    }

    /* And cancel/remove them */
    remove_requests (self, remove);
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
    return (MMAuthRequest *) mm_auth_request_new (0, 
                                                  authorization,
                                                  owner,
                                                  context,
                                                  callback,
                                                  callback_data,
                                                  notify);
}

static gboolean
process_complete_requests (gpointer user_data)
{
    MMAuthProvider *self = MM_AUTH_PROVIDER (user_data);
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);
    GHashTableIter iter;
    gpointer value;
    GSList *remove = NULL;
    MMAuthRequest *req;

    priv->process_id = 0;

    /* Call finished request's callbacks */
    g_hash_table_iter_init (&iter, priv->requests);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        req = MM_AUTH_REQUEST (value);

        if (mm_auth_request_get_authorization (req) != MM_AUTH_RESULT_UNKNOWN) {
            mm_auth_request_callback (req);
            remove = g_slist_prepend (remove, req);
        }
    }

    /* And remove those requests from our pending request list */
    remove_requests (self, remove);

    return FALSE;
}

static void
auth_result_cb (MMAuthRequest *req, gpointer user_data)
{
    MMAuthProvider *self = MM_AUTH_PROVIDER (user_data);
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);

    /* Process results from an idle handler */
    if (priv->process_id == 0)
        priv->process_id = g_idle_add (process_complete_requests, self);
}

#define RESULT_SIGID_TAG "result-sigid"

MMAuthRequest *
mm_auth_provider_request_auth (MMAuthProvider *self,
                               const char *authorization,
                               GObject *owner,
                               DBusGMethodInvocation *context,
                               MMAuthRequestCb callback,
                               gpointer callback_data,
                               GDestroyNotify notify,
                               GError **error)
{
    MMAuthProviderPrivate *priv;
    MMAuthRequest *req;
    guint32 sigid;

    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (MM_IS_AUTH_PROVIDER (self), 0);
    g_return_val_if_fail (authorization != NULL, 0);
    g_return_val_if_fail (callback != NULL, 0);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);

    req = MM_AUTH_PROVIDER_GET_CLASS (self)->create_request (self,
                                                             authorization,
                                                             owner,
                                                             context,
                                                             callback,
                                                             callback_data,
                                                             notify);
    g_assert (req);

    sigid = g_signal_connect (req, "result", G_CALLBACK (auth_result_cb), self);
    g_object_set_data (G_OBJECT (req), RESULT_SIGID_TAG, GUINT_TO_POINTER (sigid));

    g_hash_table_insert (priv->requests, req, req);
    if (!mm_auth_request_authenticate (req, error)) {
        /* Error */
        g_hash_table_remove (priv->requests, req);
        return NULL;
    }

    return req;
}

/*****************************************************************************/

static void
dispose_auth_request (gpointer data)
{
    MMAuthRequest *req = MM_AUTH_REQUEST (data);
    guint sigid;

    sigid = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (req), RESULT_SIGID_TAG));
    if (sigid)
        g_signal_handler_disconnect (req, sigid);
    mm_auth_request_dispose (req);
    g_object_unref (req);
}

static void
mm_auth_provider_init (MMAuthProvider *self)
{
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);

    priv->requests = g_hash_table_new_full (g_direct_hash,
                                            g_direct_equal,
                                            NULL,
                                            dispose_auth_request);
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

#define NULL_PROVIDER "open"

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, NULL_PROVIDER);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (object);

    if (priv->process_id)
        g_source_remove (priv->process_id);
    g_hash_table_destroy (priv->requests);

    G_OBJECT_CLASS (mm_auth_provider_parent_class)->dispose (object);
}

static void
mm_auth_provider_class_init (MMAuthProviderClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    mm_auth_provider_parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (MMAuthProviderPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    class->create_request = real_create_request;

    /* Properties */
    g_object_class_install_property (object_class, PROP_NAME,
         g_param_spec_string (MM_AUTH_PROVIDER_NAME,
                              "Name",
                              "Provider name",
                              NULL_PROVIDER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

