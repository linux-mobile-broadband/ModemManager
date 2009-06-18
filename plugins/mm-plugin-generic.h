/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_GENERIC_H
#define MM_PLUGIN_GENERIC_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_GENERIC			(mm_plugin_generic_get_type ())
#define MM_PLUGIN_GENERIC(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_GENERIC, MMPluginGeneric))
#define MM_PLUGIN_GENERIC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_GENERIC, MMPluginGenericClass))
#define MM_IS_PLUGIN_GENERIC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_GENERIC))
#define MM_IS_PLUGIN_GENERIC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_GENERIC))
#define MM_PLUGIN_GENERIC_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_GENERIC, MMPluginGenericClass))

typedef struct {
    GObject parent;
} MMPluginGeneric;

typedef struct {
    GObjectClass parent;
} MMPluginGenericClass;

GType mm_plugin_generic_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_GENERIC_H */
