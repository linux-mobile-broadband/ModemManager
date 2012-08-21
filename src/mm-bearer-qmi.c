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
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-bearer-qmi.h"
#include "mm-utils.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerQmi, mm_bearer_qmi, MM_TYPE_BEARER);

struct _MMBearerQmiPrivate {
    gpointer dummy;
};

/*****************************************************************************/

MMBearer *
mm_bearer_qmi_new (MMBroadbandModemQmi *modem,
                   MMBearerProperties *config)
{
    MMBearer *bearer;

    /* The Qmi bearer inherits from MMBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_QMI,
                           MM_BEARER_MODEM, modem,
                           MM_BEARER_CONFIG, config,
                           NULL);

    /* Only export valid bearers */
    mm_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_qmi_init (MMBearerQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_QMI,
                                              MMBearerQmiPrivate);
}

static void
mm_bearer_qmi_class_init (MMBearerQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerQmiPrivate));
}
