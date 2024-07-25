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
 * Copyright (C) 2024 Quectel Wireless Solution,Co.,Ltd.
 */

#ifndef MM_PORT_MBIM_QUECTEL_H
#define MM_PORT_MBIM_QUECTEL_H

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libmbim-glib.h>

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif

#include "mm-port-mbim.h"

#define MM_TYPE_PORT_MBIM_QUECTEL            (mm_port_mbim_quectel_get_type ())
#define MM_PORT_MBIM_QUECTEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_MBIM_QUECTEL, MMPortMbimQuectel))
#define MM_PORT_MBIM_QUECTEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_MBIM_QUECTEL, MMPortMbimQuectelClass))
#define MM_IS_PORT_MBIM_QUECTEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_MBIM_QUECTEL))
#define MM_IS_PORT_MBIM_QUECTEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_MBIM_QUECTEL))
#define MM_PORT_MBIM_QUECTEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_MBIM_QUECTEL, MMPortMbimQuectelClass))

typedef struct _MMPortMbimQuectel MMPortMbimQuectel;
typedef struct _MMPortMbimQuectelClass MMPortMbimQuectelClass;
typedef struct _MMPortMbimQuectelPrivate MMPortMbimQuectelPrivate;

struct _MMPortMbimQuectel {
    MMPortMbim parent;
    MMPortMbimQuectelPrivate *priv;
};

struct _MMPortMbimQuectelClass {
    MMPortMbimClass parent;
};

GType mm_port_mbim_quectel_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortMbimQuectel, g_object_unref)

MMPortMbimQuectel *mm_port_mbim_quectel_new (const gchar  *name,
                                             MMPortSubsys  subsys);

#endif /* MM_PORT_MBIM_QUECTEL_H */
