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
 * Copyright (C) 2011 Samsung Electronics, Inc.
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-broadband-bearer-samsung.h"
#include "mm-error-helpers.h"

G_DEFINE_TYPE (MMBroadbandBearerSamsung, mm_broadband_bearer_samsung, MM_TYPE_BROADBAND_BEARER_ICERA);

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_samsung_new_finish (GAsyncResult *res,
                                        GError **error)
{
    GObject *source;
    GObject *bearer;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
}

void
mm_broadband_bearer_samsung_new (MMBroadbandModem *modem,
                                 MMBearerProperties *config,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_SAMSUNG,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM,  modem,
        MM_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_samsung_init (MMBroadbandBearerSamsung *self)
{
}

static void
mm_broadband_bearer_samsung_class_init (MMBroadbandBearerSamsungClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    /* Clean the parent Icera method to get the IP config, as this modem wants
     * the default DHCP */
    broadband_bearer_class->get_ip_config_3gpp = NULL;
    broadband_bearer_class->get_ip_config_3gpp_finish = NULL;
}
