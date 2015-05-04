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

#ifndef MM_AUTH_PROVIDER_H
#define MM_AUTH_PROVIDER_H

#include <gio/gio.h>

#define MM_TYPE_AUTH_PROVIDER            (mm_auth_provider_get_type ())
#define MM_AUTH_PROVIDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_PROVIDER, MMAuthProvider))
#define MM_AUTH_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))
#define MM_IS_AUTH_PROVIDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_PROVIDER))
#define MM_IS_AUTH_PROVIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_PROVIDER))
#define MM_AUTH_PROVIDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))

/* Authorizations */
#define MM_AUTHORIZATION_MANAGER_CONTROL "org.freedesktop.ModemManager1.Control"
#define MM_AUTHORIZATION_DEVICE_CONTROL  "org.freedesktop.ModemManager1.Device.Control"
#define MM_AUTHORIZATION_CONTACTS        "org.freedesktop.ModemManager1.Contacts"
#define MM_AUTHORIZATION_MESSAGING       "org.freedesktop.ModemManager1.Messaging"
#define MM_AUTHORIZATION_VOICE           "org.freedesktop.ModemManager1.Voice"
#define MM_AUTHORIZATION_USSD            "org.freedesktop.ModemManager1.USSD"
#define MM_AUTHORIZATION_LOCATION        "org.freedesktop.ModemManager1.Location"
#define MM_AUTHORIZATION_FIRMWARE        "org.freedesktop.ModemManager1.Firmware"

typedef struct _MMAuthProvider MMAuthProvider;
typedef struct _MMAuthProviderClass MMAuthProviderClass;

struct _MMAuthProvider {
    GObject parent;
};

struct _MMAuthProviderClass {
    GObjectClass parent;

    /* Perform authorization checks in this request (async).
     * Returns TRUE if authorized, FALSE if error is set. */
    void (* authorize) (MMAuthProvider *self,
                        GDBusMethodInvocation *invocation,
                        const gchar *authorization,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    gboolean (* authorize_finish) (MMAuthProvider *self,
                                   GAsyncResult *res,
                                   GError **error);
};

GType mm_auth_provider_get_type (void);

MMAuthProvider *mm_auth_provider_new (void);

void mm_auth_provider_authorize (MMAuthProvider *self,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *authorization,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_auth_provider_authorize_finish (MMAuthProvider *self,
                                            GAsyncResult *res,
                                            GError **error);

#endif /* MM_AUTH_PROVIDER_H */
