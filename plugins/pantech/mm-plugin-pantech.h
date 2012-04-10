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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PLUGIN_PANTECH_H
#define MM_PLUGIN_PANTECH_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_PANTECH            (mm_plugin_pantech_get_type ())
#define MM_PLUGIN_PANTECH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_PANTECH, MMPluginPantech))
#define MM_PLUGIN_PANTECH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_PANTECH, MMPluginPantechClass))
#define MM_IS_PLUGIN_PANTECH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_PANTECH))
#define MM_IS_PLUGIN_PANTECH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_PANTECH))
#define MM_PLUGIN_PANTECH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_PANTECH, MMPluginPantechClass))

typedef struct {
    MMPlugin parent;
} MMPluginPantech;

typedef struct {
    MMPluginClass parent;
} MMPluginPantechClass;

GType mm_plugin_pantech_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_PANTECH_H */
