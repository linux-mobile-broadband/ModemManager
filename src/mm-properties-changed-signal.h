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
 * Copyright (C) 2007 - 2008 Novell, Inc.
 * Copyright (C) 2008 - 2010 Red Hat, Inc.
 */

#ifndef _MM_PROPERTIES_CHANGED_SIGNAL_H_
#define _MM_PROPERTIES_CHANGED_SIGNAL_H_

#include <glib-object.h>

#define MM_TYPE_PROPERTIES_CHANGED      (mm_properties_changed_get_type ())
#define MM_PROPERTIES_CHANGED(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PROPERTIES_CHANGED, MMPropertiesChanged))
#define MM_IS_PROPERTIES_CHANGED(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PROPERTIES_CHANGED))
#define MM_PROPERTIES_CHANGED_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_PROPERTIES_CHANGED, MMPropertiesChanged))

typedef struct {
    GTypeInterface g_iface;
} MMPropertiesChanged;

GType mm_properties_changed_get_type (void);

void mm_properties_changed_signal_enable (GObjectClass *object_class);

void mm_properties_changed_signal_register_property (GObject *object,
                                                     const char *gobject_property,
                                                     const char *real_property,
                                                     const char *interface);

#endif /* _MM_PROPERTIES_CHANGED_SIGNAL_H_ */
