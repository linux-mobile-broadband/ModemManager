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

#ifndef MM_PLUGIN_GENERIC_H
#define MM_PLUGIN_GENERIC_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_GENERIC            (mm_plugin_generic_get_type ())
#define MM_PLUGIN_GENERIC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_GENERIC, MMPluginGeneric))
#define MM_PLUGIN_GENERIC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_GENERIC, MMPluginGenericClass))
#define MM_IS_PLUGIN_GENERIC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_GENERIC))
#define MM_IS_PLUGIN_GENERIC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_GENERIC))
#define MM_PLUGIN_GENERIC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_GENERIC, MMPluginGenericClass))

typedef struct {
    MMPlugin parent;
} MMPluginGeneric;

typedef struct {
    MMPluginClass parent;
} MMPluginGenericClass;

GType mm_plugin_generic_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_GENERIC_H */
