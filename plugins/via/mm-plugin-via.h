/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2012 Red Hat, Inc.
 */

#ifndef MM_PLUGIN_VIA_H
#define MM_PLUGIN_VIA_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_VIA            (mm_plugin_via_get_type ())
#define MM_PLUGIN_VIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_VIA, MMPluginVia))
#define MM_PLUGIN_VIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_VIA, MMPluginViaClass))
#define MM_IS_PLUGIN_VIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_VIA))
#define MM_IS_PLUGIN_VIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_VIA))
#define MM_PLUGIN_VIA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_VIA, MMPluginViaClass))

typedef struct {
    MMPlugin parent;
} MMPluginVia;

typedef struct {
    MMPluginClass parent;
} MMPluginViaClass;

GType mm_plugin_via_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_VIA_H */
