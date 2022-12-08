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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 *
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef MM_PLUGIN_WAVECOM_H
#define MM_PLUGIN_WAVECOM_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_WAVECOM            (mm_plugin_wavecom_get_type ())
#define MM_PLUGIN_WAVECOM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_WAVECOM, MMPluginWavecom))
#define MM_PLUGIN_WAVECOM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_WAVECOM, MMPluginWavecomClass))
#define MM_IS_PLUGIN_WAVECOM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_WAVECOM))
#define MM_IS_PLUGIN_WAVECOM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_WAVECOM))
#define MM_PLUGIN_WAVECOM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_WAVECOM, MMPluginWavecomClass))

typedef struct {
    MMPlugin parent;
} MMPluginWavecom;

typedef struct {
    MMPluginClass parent;
} MMPluginWavecomClass;

GType mm_plugin_wavecom_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_WAVECOM_H */
