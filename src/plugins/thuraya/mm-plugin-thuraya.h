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
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
 */

#ifndef MM_PLUGIN_THURAYA_H
#define MM_PLUGIN_THURAYA_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_THURAYA            (mm_plugin_thuraya_get_type ())
#define MM_PLUGIN_THURAYA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_THURAYA, MMPluginThuraya))
#define MM_PLUGIN_THURAYA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_THURAYA, MMPluginThurayaClass))
#define MM_IS_PLUGIN_THURAYA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_THURAYA))
#define MM_IS_PLUGIN_THURAYA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_THURAYA))
#define MM_PLUGIN_THURAYA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_THURAYA, MMPluginThurayaClass))

typedef struct {
    MMPlugin parent;
} MMPluginThuraya;

typedef struct {
    MMPluginClass parent;
} MMPluginThurayaClass;

GType mm_plugin_thuraya_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_THURAYA_H */
