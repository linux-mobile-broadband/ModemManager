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

#ifndef MM_AUTH_PROVIDER_POLKIT_H
#define MM_AUTH_PROVIDER_POLKIT_H

#include "mm-auth-provider.h"

#define MM_TYPE_AUTH_PROVIDER_POLKIT            (mm_auth_provider_polkit_get_type ())
#define MM_AUTH_PROVIDER_POLKIT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AUTH_PROVIDER_POLKIT, MMAuthProviderPolkit))
#define MM_AUTH_PROVIDER_POLKIT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AUTH_PROVIDER_POLKIT, MMAuthProviderPolkitClass))
#define MM_IS_AUTH_PROVIDER_POLKIT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AUTH_PROVIDER_POLKIT))
#define MM_IS_AUTH_PROVIDER_POLKIT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AUTH_PROVIDER_POLKIT))
#define MM_AUTH_PROVIDER_POLKIT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AUTH_PROVIDER_POLKIT, MMAuthProviderPolkitClass))

typedef struct _MMAuthProviderPolkit MMAuthProviderPolkit;
typedef struct _MMAuthProviderPolkitClass MMAuthProviderPolkitClass;
typedef struct _MMAuthProviderPolkitPrivate MMAuthProviderPolkitPrivate;

struct _MMAuthProviderPolkit {
    MMAuthProvider parent;
    MMAuthProviderPolkitPrivate *priv;
};

struct _MMAuthProviderPolkitClass {
    MMAuthProviderClass parent;
};

GType mm_auth_provider_polkit_get_type (void);

MMAuthProvider *mm_auth_provider_polkit_new (void);

#endif /* MM_AUTH_PROVIDER_POLKIT_H */
