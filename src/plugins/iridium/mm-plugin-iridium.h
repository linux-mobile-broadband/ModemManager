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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef MM_PLUGIN_IRIDIUM_H
#define MM_PLUGIN_IRIDIUM_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_IRIDIUM            (mm_plugin_iridium_get_type ())
#define MM_PLUGIN_IRIDIUM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_IRIDIUM, MMPluginIridium))
#define MM_PLUGIN_IRIDIUM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_IRIDIUM, MMPluginIridiumClass))
#define MM_IS_PLUGIN_IRIDIUM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_IRIDIUM))
#define MM_IS_PLUGIN_IRIDIUM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_IRIDIUM))
#define MM_PLUGIN_IRIDIUM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_IRIDIUM, MMPluginIridiumClass))

typedef struct {
    MMPlugin parent;
} MMPluginIridium;

typedef struct {
    MMPluginClass parent;
} MMPluginIridiumClass;

GType mm_plugin_iridium_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_IRIDIUM_H */
