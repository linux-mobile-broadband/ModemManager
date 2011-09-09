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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PORT_PROBE_H
#define MM_PORT_PROBE_H

#include <glib.h>
#include <glib-object.h>

#define MM_TYPE_PORT_PROBE            (mm_port_probe_get_type ())
#define MM_PORT_PROBE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_PROBE, MMPortProbe))
#define MM_PORT_PROBE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_PROBE, MMPortProbeClass))
#define MM_IS_PORT_PROBE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_PROBE))
#define MM_IS_PLUBIN_PROBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_PROBE))
#define MM_PORT_PROBE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_PROBE, MMPortProbeClass))

typedef struct _MMPortProbe MMPortProbe;
typedef struct _MMPortProbeClass MMPortProbeClass;
typedef struct _MMPortProbePrivate MMPortProbePrivate;

struct _MMPortProbe {
    GObject parent;
    MMPortProbePrivate *priv;
};

struct _MMPortProbeClass {
    GObjectClass parent;
};

GType mm_port_probe_get_type (void);

MMPortProbe *mm_port_probe_new (void);

#endif /* MM_PORT_PROBE_H */

