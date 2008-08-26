/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_HSO_H
#define MM_PLUGIN_HSO_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_HSO			(mm_plugin_hso_get_type ())
#define MM_PLUGIN_HSO(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_HSO, MMPluginHso))
#define MM_PLUGIN_HSO_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_HSO, MMPluginHsoClass))
#define MM_IS_PLUGIN_HSO(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_HSO))
#define MM_IS_PLUGIN_HSO_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_HSO))
#define MM_PLUGIN_HSO_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_HSO, MMPluginHsoClass))

typedef struct {
    GObject parent;
} MMPluginHso;

typedef struct {
    GObjectClass parent;
} MMPluginHsoClass;

GType mm_plugin_hso_get_type (void);

#endif /* MM_PLUGIN_HSO_H */
