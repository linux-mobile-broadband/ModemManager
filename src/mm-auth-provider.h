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
#include <dbus/dbus-glib-lowlevel.h>

#include "mm-auth-request.h"

/* Authorizations */
#define MM_AUTHORIZATION_DEVICE_INFO    "org.freedesktop.ModemManager.Device.Info"
#define MM_AUTHORIZATION_DEVICE_CONTROL "org.freedesktop.ModemManager.Device.Control"
#define MM_AUTHORIZATION_CONTACTS       "org.freedesktop.ModemManager.Contacts"
#define MM_AUTHORIZATION_SMS            "org.freedesktop.ModemManager.SMS"
#define MM_AUTHORIZATION_USSD           "org.freedesktop.ModemManager.USSD"
#define MM_AUTHORIZATION_LOCATION       "org.freedesktop.ModemManager.Location"
/******************/


#define MM_TYPE_AUTH_PROVIDER            (mm_auth_provider_get_type ())
#define MM_AUTH_PROVIDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_PROVIDER, MMAuthProvider))
#define MM_AUTH_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))
#define MM_IS_AUTH_PROVIDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_PROVIDER))
#define MM_IS_AUTH_PROVIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_PROVIDER))
#define MM_AUTH_PROVIDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_PROVIDER, MMAuthProviderClass))

#define MM_AUTH_PROVIDER_NAME "name"

typedef struct {
    GObject parent;
} MMAuthProvider;

typedef struct {
    GObjectClass parent;

    MMAuthRequest * (*create_request) (MMAuthProvider *provider,
                                       const char *authorization,
                                       GObject *owner,
                                       DBusGMethodInvocation *context,
                                       MMAuthRequestCb callback,
                                       gpointer callback_data,
                                       GDestroyNotify notify);
} MMAuthProviderClass;

GType mm_auth_provider_get_type (void);

/* Don't do anything clever from the notify callback... */
MMAuthRequest *mm_auth_provider_request_auth (MMAuthProvider *provider,
                                              const char *authorization,
                                              GObject *owner,
                                              DBusGMethodInvocation *context,
                                              MMAuthRequestCb callback,
                                              gpointer callback_data,
                                              GDestroyNotify notify,
                                              GError **error);

void mm_auth_provider_cancel_for_owner (MMAuthProvider *provider,
                                        GObject *owner);

/* Subclass API */

/* To get an auth provider instance, implemented in mm-auth-provider-factory.c */
MMAuthProvider *mm_auth_provider_get (void);

/* schedules the request's completion */
void mm_auth_provider_finish_request (MMAuthProvider *provider,
                                      MMAuthRequest *req,
                                      MMAuthResult result);

void mm_auth_provider_cancel_request (MMAuthProvider *provider, MMAuthRequest *req);

#endif /* MM_AUTH_PROVIDER_H */

