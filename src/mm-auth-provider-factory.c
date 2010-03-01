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

#include "config.h"
#include "mm-auth-provider.h"

GObject *mm_auth_provider_new (void);

#ifdef WITH_POLKIT
#define IN_AUTH_PROVIDER_FACTORY_C
#include "mm-auth-provider-polkit.h"
#undef IN_AUTH_PROVIDER_FACTORY_C
#endif

MMAuthProvider *
mm_auth_provider_get (void)
{
    static MMAuthProvider *singleton;

    if (!singleton) {
#if WITH_POLKIT
        singleton = (MMAuthProvider *) mm_auth_provider_polkit_new ();
#else
        singleton = (MMAuthProvider *) mm_auth_provider_new ();
#endif
    }

    g_assert (singleton);
    return singleton;
}

