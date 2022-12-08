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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_PLUGIN_HAIER_H
#define MM_PLUGIN_HAIER_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_HAIER            (mm_plugin_haier_get_type ())
#define MM_PLUGIN_HAIER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_HAIER, MMPluginHaier))
#define MM_PLUGIN_HAIER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_HAIER, MMPluginHaierClass))
#define MM_IS_PLUGIN_HAIER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_HAIER))
#define MM_IS_PLUGIN_HAIER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_HAIER))
#define MM_PLUGIN_HAIER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_HAIER, MMPluginHaierClass))

typedef struct {
    MMPlugin parent;
} MMPluginHaier;

typedef struct {
    MMPluginClass parent;
} MMPluginHaierClass;

GType mm_plugin_haier_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_HAIER_H */
