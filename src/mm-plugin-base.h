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

#ifndef MM_PLUGIN_BASE_H
#define MM_PLUGIN_BASE_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>

#include "mm-modem.h"

#define MM_TYPE_PLUGIN_BASE            (mm_plugin_base_get_type ())
#define MM_PLUGIN_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_BASE, MMPluginBase))
#define MM_PLUGIN_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))
#define MM_IS_PLUGIN_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_BASE))
#define MM_IS_PLUBIN_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_BASE))
#define MM_PLUGIN_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_BASE, MMPluginBaseClass))

typedef struct _MMPluginBase MMPluginBase;
typedef struct _MMPluginBaseClass MMPluginBaseClass;

struct _MMPluginBase {
    GObject parent;
};

struct _MMPluginBaseClass {
    GObjectClass parent;
};

GType mm_plugin_base_get_type (void);

gboolean mm_plugin_base_add_modem (MMPluginBase *self,
                                   MMModem *modem);

MMModem *mm_plugin_base_find_modem (MMPluginBase *self,
                                    const char *master_device);

#endif /* MM_PLUGIN_BASE_H */

