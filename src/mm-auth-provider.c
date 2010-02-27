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

enum {
	REQUEST_ADDED,
	REQUEST_REMOVED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


/*****************************************************************************/

GObject *
mm_auth_provider_new (void)
{
    return g_object_new (MM_TYPE_AUTH_PROVIDER, NULL);
}

/*****************************************************************************/

struct MMAuthRequest {
    guint32 refcount;
    guint32 id;
    char *auth;
    GObject *instance;

    MMAuthResult result;

    MMAuthRequestCb callback;
    gpointer callback_data;
    GDestroyNotify notify;
};

static MMAuthRequest *
mm_auth_request_new (const char *authorization,
                     GObject *instance,
                     MMAuthRequestCb callback,
                     gpointer callback_data,
                     GDestroyNotify notify)
{
    static guint32 id = 1;
    MMAuthRequest *req;

    g_return_val_if_fail (authorization != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    req = g_malloc0 (sizeof (MMAuthRequest));
    req->id = id++;
    req->refcount = 1;
    req->auth = g_strdup (authorization);
    req->instance = instance;
    req->callback = callback;
    req->callback_data = callback_data;
    req->notify = notify;

    return req;
}

MMAuthRequest *
mm_auth_request_ref (MMAuthRequest *req)
{
    g_return_val_if_fail (req != NULL, NULL);
    g_return_val_if_fail (req->refcount > 0, NULL);

    req->refcount++;
    return req;
}

void
mm_auth_request_unref (MMAuthRequest *req)
{
    g_return_if_fail (req != NULL);
    g_return_if_fail (req->refcount > 0);

    req->refcount--;
    if (req->refcount == 0) {
        g_free (req->auth);
        memset (req, 0, sizeof (MMAuthRequest));
        g_free (req);
    }
}

guint32
mm_auth_request_get_id (MMAuthRequest *req)
{
    g_return_val_if_fail (req != NULL, 0);
    g_return_val_if_fail (req->refcount > 0, 0);

    return req->id;
}

const char *
mm_auth_request_get_authorization (MMAuthRequest *req)
{
    g_return_val_if_fail (req != NULL, 0);
    g_return_val_if_fail (req->refcount > 0, 0);

    return req->auth;
}

/*****************************************************************************/

MMAuthRequest *
mm_auth_provider_get_request (MMAuthProvider *provider, guint32 reqid)
{
    MMAuthProviderPrivate *priv;

    g_return_val_if_fail (provider != NULL, NULL);
    g_return_val_if_fail (MM_IS_AUTH_PROVIDER (provider), NULL);
    g_return_val_if_fail (reqid > 0, NULL);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);
    return (MMAuthRequest *) g_hash_table_lookup (priv->requests, GUINT_TO_POINTER (reqid));
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
        req = (MMAuthRequest *) value;
        if (req->result != MM_AUTH_RESULT_UNKNOWN) {
            req->callback (req->instance, req->id, req->result, req->callback_data);

            /* Let the caller clean up the request's callback data */
            if (req->notify)
                req->notify (req->callback_data);

            remove = g_slist_prepend (remove, req);
        }
    }

    /* And remove those requests from our pending request list */
    while (remove) {
        req = (MMAuthRequest *) remove->data;
        g_signal_emit (self, signals[REQUEST_REMOVED], 0, req->instance, req->id);
        g_hash_table_remove (priv->requests, GUINT_TO_POINTER (req->id));
        remove = g_slist_remove (remove, req);
    }

    return FALSE;
}

void
mm_auth_provider_finish_request (MMAuthProvider *provider,
                                 guint32 reqid,
                                 MMAuthResult result)
{
    MMAuthProviderPrivate *priv;
    MMAuthRequest *req;

    g_return_if_fail (provider != NULL);
    g_return_if_fail (MM_IS_AUTH_PROVIDER (provider));
    g_return_if_fail (reqid > 0);
    g_return_if_fail (result != MM_AUTH_RESULT_UNKNOWN);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);
    req = (MMAuthRequest *) g_hash_table_lookup (priv->requests, GUINT_TO_POINTER (reqid));
    g_return_if_fail (req != NULL);

    req->result = result;

    if (priv->process_id == 0)
        priv->process_id = g_idle_add (process_complete_requests, provider);
}

void
mm_auth_provider_cancel_request (MMAuthProvider *provider, guint32 reqid)
{
    MMAuthProviderPrivate *priv;
    MMAuthRequest *req;

    g_return_if_fail (provider != NULL);
    g_return_if_fail (MM_IS_AUTH_PROVIDER (provider));
    g_return_if_fail (reqid > 0);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);

    req = (MMAuthRequest *) g_hash_table_lookup (priv->requests, GUINT_TO_POINTER (reqid));
    g_return_if_fail (req != NULL);

    /* Let the caller clean up the request's callback data */
    if (req->notify)
        req->notify (req->callback_data);

    /* We don't signal removal here as it's assumed the caller
     * handles that itself instead of by the signal.
     */
    g_hash_table_remove (priv->requests, GUINT_TO_POINTER (reqid));
}

const char *
mm_auth_provider_get_authorization_for_id (MMAuthProvider *provider, guint32 reqid)
{
    MMAuthProviderPrivate *priv;
    MMAuthRequest *req;

    g_return_val_if_fail (provider != NULL, NULL);
    g_return_val_if_fail (MM_IS_AUTH_PROVIDER (provider), NULL);
    g_return_val_if_fail (reqid > 0, NULL);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);
    req = (MMAuthRequest *) g_hash_table_lookup (priv->requests, GUINT_TO_POINTER (reqid));
    g_return_val_if_fail (req != NULL, NULL);

    return mm_auth_request_get_authorization (req);
}

/*****************************************************************************/

static gboolean
real_request_auth (MMAuthProvider *provider,
                   MMAuthRequest *req,
                   GError **error)
{
    /* This class provides null authentication; all requests pass */
    mm_auth_provider_finish_request (provider,
                                     mm_auth_request_get_id (req),
                                     MM_AUTH_RESULT_AUTHORIZED);
    return TRUE;
}

guint32
mm_auth_provider_request_auth (MMAuthProvider *provider,
                               const char *authorization,
                               GObject *instance,
                               MMAuthRequestCb callback,
                               gpointer callback_data,
                               GDestroyNotify notify,
                               GError **error)
{
    MMAuthProviderPrivate *priv;
    MMAuthRequest *req;

    g_return_val_if_fail (provider != NULL, 0);
    g_return_val_if_fail (MM_IS_AUTH_PROVIDER (provider), 0);
    g_return_val_if_fail (authorization != NULL, 0);
    g_return_val_if_fail (callback != NULL, 0);

    priv = MM_AUTH_PROVIDER_GET_PRIVATE (provider);

    req = mm_auth_request_new (authorization, instance, callback, callback_data, notify);
    g_assert (req);

    g_hash_table_insert (priv->requests, GUINT_TO_POINTER (req->id), req);
    g_signal_emit (provider, signals[REQUEST_ADDED], 0, instance, req->id);

    if (!MM_AUTH_PROVIDER_GET_CLASS (provider)->request_auth (provider, req, error)) {
        /* Error */
        g_signal_emit (provider, signals[REQUEST_REMOVED], 0, instance, req->id);

        /* Let the caller clean up the request's callback data */
        if (req->notify)
            req->notify (req->callback_data);

        g_hash_table_remove (priv->requests, GUINT_TO_POINTER (req->id));
        return 0;
    }

    return req->id;
}

/*****************************************************************************/

static void
mm_auth_provider_init (MMAuthProvider *self)
{
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (self);

    priv->requests = g_hash_table_new_full (g_direct_hash,
                                            g_direct_equal,
                                            NULL,
                                            (GDestroyNotify) mm_auth_request_unref);
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
finalize (GObject *object)
{
    MMAuthProviderPrivate *priv = MM_AUTH_PROVIDER_GET_PRIVATE (object);

    if (priv->process_id)
        g_source_remove (priv->process_id);
    g_hash_table_destroy (priv->requests);

    G_OBJECT_CLASS (mm_auth_provider_parent_class)->finalize (object);
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
    object_class->finalize = finalize;
    class->request_auth = real_request_auth;

    /* Properties */
    g_object_class_install_property (object_class, PROP_NAME,
         g_param_spec_string (MM_AUTH_PROVIDER_NAME,
                              "Name",
                              "Provider name",
                              NULL_PROVIDER,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
	signals[REQUEST_ADDED] =
		g_signal_new ("request-added",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              mm_marshal_VOID__POINTER_UINT,
		              G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

	signals[REQUEST_REMOVED] =
		g_signal_new ("request-removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              mm_marshal_VOID__POINTER_UINT,
		              G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
}

