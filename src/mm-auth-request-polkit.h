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

#ifndef MM_AUTH_REQUEST_POLKIT_H
#define MM_AUTH_REQUEST_POLKIT_H

#include <glib-object.h>
#include <polkit/polkit.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "mm-auth-request.h"

#define MM_TYPE_AUTH_REQUEST_POLKIT            (mm_auth_request_polkit_get_type ())
#define MM_AUTH_REQUEST_POLKIT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_REQUEST_POLKIT, MMAuthRequestPolkit))
#define MM_AUTH_REQUEST_POLKIT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_REQUEST_POLKIT, MMAuthRequestPolkitClass))
#define MM_IS_AUTH_REQUEST_POLKIT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_REQUEST_POLKIT))
#define MM_IS_AUTH_REQUEST_POLKIT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_REQUEST_POLKIT))
#define MM_AUTH_REQUEST_POLKIT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_REQUEST_POLKIT, MMAuthRequestPolkitClass))

typedef struct {
    MMAuthRequest parent;
} MMAuthRequestPolkit;

typedef struct {
    MMAuthRequestClass parent;
} MMAuthRequestPolkitClass;

GType mm_auth_request_polkit_get_type (void);

GObject *mm_auth_request_polkit_new (PolkitAuthority *authority,
                                     const char *authorization,
                                     GObject *owner,
                                     DBusGMethodInvocation *context,
                                     MMAuthRequestCb callback,
                                     gpointer callback_data,
                                     GDestroyNotify notify);

void mm_auth_request_polkit_cancel (MMAuthRequestPolkit *self);

#endif /* MM_AUTH_REQUEST_POLKIT_H */

