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

#ifndef MM_AUTH_REQUEST_H
#define MM_AUTH_REQUEST_H

#include <glib-object.h>
#include <dbus/dbus-glib-lowlevel.h>

#define MM_TYPE_AUTH_REQUEST            (mm_auth_request_get_type ())
#define MM_AUTH_REQUEST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_REQUEST, MMAuthRequest))
#define MM_AUTH_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_REQUEST, MMAuthRequestClass))
#define MM_IS_AUTH_REQUEST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_REQUEST))
#define MM_IS_AUTH_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_REQUEST))
#define MM_AUTH_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_REQUEST, MMAuthRequestClass))

typedef enum MMAuthResult {
    MM_AUTH_RESULT_UNKNOWN = 0,
    MM_AUTH_RESULT_INTERNAL_FAILURE,
    MM_AUTH_RESULT_NOT_AUTHORIZED,
    MM_AUTH_RESULT_CHALLENGE,
    MM_AUTH_RESULT_AUTHORIZED
} MMAuthResult;

typedef struct {
    GObject parent;
} MMAuthRequest;

typedef struct {
    GObjectClass parent;

    gboolean (*authenticate) (MMAuthRequest *self, GError **error);
    void     (*dispose)      (MMAuthRequest *self);
} MMAuthRequestClass;

GType mm_auth_request_get_type (void);

typedef void (*MMAuthRequestCb) (MMAuthRequest *req,
                                 GObject *owner,
                                 DBusGMethodInvocation *context,
                                 gpointer user_data);

GObject *mm_auth_request_new (GType atype,
                              const char *authorization,
                              GObject *owner,
                              DBusGMethodInvocation *context,
                              MMAuthRequestCb callback,
                              gpointer callback_data,
                              GDestroyNotify notify);

const char *           mm_auth_request_get_authorization (MMAuthRequest *req);
GObject *              mm_auth_request_get_owner         (MMAuthRequest *req);
MMAuthResult           mm_auth_request_get_result        (MMAuthRequest *req);
void                   mm_auth_request_set_result        (MMAuthRequest *req, MMAuthResult result);
gboolean               mm_auth_request_authenticate      (MMAuthRequest *req, GError **error);
void                   mm_auth_request_callback          (MMAuthRequest *req);
void                   mm_auth_request_dispose           (MMAuthRequest *req);

#endif /* MM_AUTH_REQUEST_H */

