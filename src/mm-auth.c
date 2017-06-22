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

#include <string.h>

#include "config.h"

#include "mm-auth.h"
#include "mm-auth-provider.h"

#if defined WITH_POLKIT
# include "mm-auth-provider-polkit.h"
#endif

static MMAuthProvider *authp = NULL;

MMAuthProvider *
mm_auth_get_provider (void)
{
    if (!authp) {
#if defined WITH_POLKIT
        authp = mm_auth_provider_polkit_new ();
#else
        authp = mm_auth_provider_new ();
#endif
    }

    g_assert (authp);

    /* We'll keep the refcount of this object controlled, in order to have
     * clean shutdowns */
    return g_object_ref (authp);
}

void
mm_auth_shutdown (void)
{
    /* Clear the last reference of the auth provider if it was ever set */
    g_clear_object (&authp);
}
