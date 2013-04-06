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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_PORT_H
#define MM_PORT_H

#include <config.h>
#include <glib.h>
#include <glib-object.h>

typedef enum { /*< underscore_name=mm_port_subsys >*/
    MM_PORT_SUBSYS_UNKNOWN = 0x0,
    MM_PORT_SUBSYS_TTY,
    MM_PORT_SUBSYS_NET,
    MM_PORT_SUBSYS_USB,

    MM_PORT_SUBSYS_LAST = MM_PORT_SUBSYS_USB /*< skip >*/
} MMPortSubsys;

typedef enum { /*< underscore_name=mm_port_type >*/
    MM_PORT_TYPE_UNKNOWN = 0x0,
    MM_PORT_TYPE_IGNORED,
    MM_PORT_TYPE_NET,
    MM_PORT_TYPE_AT,
    MM_PORT_TYPE_QCDM,
    MM_PORT_TYPE_GPS,
    MM_PORT_TYPE_QMI,
    MM_PORT_TYPE_MBIM,
    MM_PORT_TYPE_LAST = MM_PORT_TYPE_MBIM /*< skip >*/
} MMPortType;

#define MM_TYPE_PORT            (mm_port_get_type ())
#define MM_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT, MMPort))
#define MM_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT, MMPortClass))
#define MM_IS_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT))
#define MM_IS_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT))
#define MM_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT, MMPortClass))

#define MM_PORT_DEVICE         "device"
#define MM_PORT_SUBSYS         "subsys"
#define MM_PORT_TYPE           "type"
#define MM_PORT_CARRIER_DETECT "carrier-detect"
#define MM_PORT_CONNECTED      "connected"

typedef struct _MMPort MMPort;
typedef struct _MMPortClass MMPortClass;

struct _MMPort {
    GObject parent;
};

struct _MMPortClass {
    GObjectClass parent;
};

GType mm_port_get_type (void);

const char * mm_port_get_device         (MMPort *self);

MMPortSubsys mm_port_get_subsys         (MMPort *self);

MMPortType   mm_port_get_port_type      (MMPort *self);

gboolean     mm_port_get_carrier_detect (MMPort *self);

gboolean     mm_port_get_connected      (MMPort *self);

void         mm_port_set_connected      (MMPort *self, gboolean connected);

#endif /* MM_PORT_H */
