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

#ifndef MM_AUTH_PROVIDER_H
#define MM_AUTH_PROVIDER_H

#include <glib-object.h>

/* Authorizations */
#define MM_AUTHORIZATION_DEVICE   "org.freedesktop.ModemManager.Device"
#define MM_AUTHORIZATION_CONTACTS "org.freedesktop.ModemManager.Contacts"
#define MM_AUTHORIZATION_SMS      "org.freedesktop.ModemManager.SMS"
/******************/


#define MM_TYPE_AUTH_PROVIDER            (mm_auth_provider_get_type ())
#define MM_AUTH_PROVIDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_PROVIDER, MMAuthProvider))
#define MM_AUTH_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))
#define MM_IS_AUTH_PROVIDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_PROVIDER))
#define MM_IS_AUTH_PROVIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_PROVIDER))
#define MM_AUTH_PROVIDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))

#define MM_AUTH_PROVIDER_NAME "name"

typedef enum MMAuthResult {
    MM_AUTH_RESULT_UNKNOWN = 0,
    MM_AUTH_RESULT_INTERNAL_FAILURE,
    MM_AUTH_RESULT_NOT_AUTHORIZED,
    MM_AUTH_RESULT_CHALLENGE,
    MM_AUTH_RESULT_AUTHORIZED
} MMAuthResult;

typedef struct MMAuthRequest MMAuthRequest;
typedef struct _MMAuthProvider MMAuthProvider;
typedef struct _MMAuthProviderClass MMAuthProviderClass;

typedef void (*MMAuthRequestCb) (GObject *instance,
                                 guint32 reqid,
                                 MMAuthResult result,
                                 gpointer user_data);

struct _MMAuthProvider {
    GObject parent;
};

struct _MMAuthProviderClass {
    GObjectClass parent;

    gboolean (*request_auth) (MMAuthProvider *provider,
                              MMAuthRequest *req,
                              GError **error);
};

GType mm_auth_provider_get_type (void);

guint32 mm_auth_provider_request_auth (MMAuthProvider *provider,
                                       const char *authorization,
                                       GObject *instance,
                                       MMAuthRequestCb callback,
                                       gpointer callback_data,
                                       GDestroyNotify notify,
                                       GError **error);

/* To get an auth provider instance, implemented in mm-auth-provider-factory.c */
MMAuthProvider *mm_auth_provider_get (void);

/* For subclasses only */
MMAuthRequest *mm_auth_provider_get_request      (MMAuthProvider *provider, guint32 reqid);
MMAuthRequest *mm_auth_request_ref               (MMAuthRequest *req);
void           mm_auth_request_unref             (MMAuthRequest *req);
guint32        mm_auth_request_get_id            (MMAuthRequest *req);
const char *   mm_auth_request_get_authorization (MMAuthRequest *req);

/* Normal API */

/* schedules the request's completion */
void mm_auth_provider_finish_request (MMAuthProvider *provider,
                                      guint32 reqid,
                                      MMAuthResult result);

void mm_auth_provider_cancel_request (MMAuthProvider *provider, guint32 reqid);

const char *mm_auth_provider_get_authorization_for_id (MMAuthProvider *provider,
                                                       guint32 reqid);

#endif /* MM_AUTH_PROVIDER_H */

