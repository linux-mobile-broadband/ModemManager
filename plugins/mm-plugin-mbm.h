/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 Ericsson AB
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef MM_PLUGIN_MBM_H
#define MM_PLUGIN_MBM_H

#include "mm-plugin-base.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_MBM             (mm_plugin_mbm_get_type ())
#define MM_PLUGIN_MBM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_MBM, MMPluginMbm))
#define MM_PLUGIN_MBM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_MBM, MMPluginMbmClass))
#define MM_IS_PLUGIN_MBM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_MBM))
#define MM_IS_PLUGIN_MBM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_MBM))
#define MM_PLUGIN_MBM_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_MBM, MMPluginMbmClass))

typedef struct {
    MMPluginBase parent;
} MMPluginMbm;

typedef struct {
    MMPluginBaseClass parent;
} MMPluginMbmClass;

GType mm_plugin_mbm_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_MBM_H */
