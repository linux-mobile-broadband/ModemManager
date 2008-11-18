/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_SIERRA_H
#define MM_PLUGIN_SIERRA_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_SIERRA            (mm_plugin_sierra_get_type ())
#define MM_PLUGIN_SIERRA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_SIERRA, MMPluginSierra))
#define MM_PLUGIN_SIERRA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_SIERRA, MMPluginSierraClass))
#define MM_IS_PLUGIN_SIERRA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_SIERRA))
#define MM_IS_PLUGIN_SIERRA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_SIERRA))
#define MM_PLUGIN_SIERRA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_SIERRA, MMPluginSierraClass))

typedef struct {
    GObject parent;
} MMPluginSierra;

typedef struct {
    GObjectClass parent;
} MMPluginSierraClass;

GType mm_plugin_sierra_get_type (void);

#endif /* MM_PLUGIN_SIERRA_H */
