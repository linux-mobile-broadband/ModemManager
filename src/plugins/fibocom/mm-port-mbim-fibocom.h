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
 * Copyright (C) 2024 Google, Inc.
 */

#ifndef MM_PORT_MBIM_FIBOCOM_H
#define MM_PORT_MBIM_FIBOCOM_H

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libmbim-glib.h>

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif

#include "mm-port-mbim.h"

#define MM_TYPE_PORT_MBIM_FIBOCOM            (mm_port_mbim_fibocom_get_type ())
#define MM_PORT_MBIM_FIBOCOM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_MBIM_FIBOCOM, MMPortMbimFibocom))
#define MM_PORT_MBIM_FIBOCOM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_MBIM_FIBOCOM, MMPortMbimFibocomClass))
#define MM_IS_PORT_MBIM_FIBOCOM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_MBIM_FIBOCOM))
#define MM_IS_PORT_MBIM_FIBOCOM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_MBIM_FIBOCOM))
#define MM_PORT_MBIM_FIBOCOM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_MBIM_FIBOCOM, MMPortMbimFibocomClass))

typedef struct _MMPortMbimFibocom MMPortMbimFibocom;
typedef struct _MMPortMbimFibocomClass MMPortMbimFibocomClass;
typedef struct _MMPortMbimFibocomPrivate MMPortMbimFibocomPrivate;

struct _MMPortMbimFibocom {
    MMPortMbim parent;
    MMPortMbimFibocomPrivate *priv;
};

struct _MMPortMbimFibocomClass {
    MMPortMbimClass parent;
};

GType mm_port_mbim_fibocom_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortMbimFibocom, g_object_unref)

MMPortMbimFibocom *mm_port_mbim_fibocom_new (const gchar  *name,
                                             MMPortSubsys  subsys);

#endif /* MM_PORT_MBIM_FIBOCOM_H */
