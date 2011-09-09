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
 * Copyright (C) 2011 - Aleksander Morgado <aleksander@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>

#include "mm-port-probe.h"
#include "mm-errors.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPortProbe, mm_port_probe, G_TYPE_OBJECT)

struct _MMPortProbePrivate {
    gpointer dummy;
};

MMPortProbe *
mm_port_probe_new (void)
{
    MMPortProbe *self;

    self = MM_PORT_PROBE (g_object_new (MM_TYPE_PORT_PROBE, NULL));

    return self;
}

static void
mm_port_probe_init (MMPortProbe *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),                  \
                                              MM_TYPE_PORT_PROBE,      \
                                              MMPortProbePrivate);
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (mm_port_probe_parent_class)->finalize (object);
}

static void
mm_port_probe_class_init (MMPortProbeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortProbePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
