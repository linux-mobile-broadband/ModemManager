/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_OPTION_H
#define MM_PLUGIN_OPTION_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_OPTION            (mm_plugin_option_get_type ())
#define MM_PLUGIN_OPTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_OPTION, MMPluginOption))
#define MM_PLUGIN_OPTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_OPTION, MMPluginOptionClass))
#define MM_IS_PLUGIN_OPTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_OPTION))
#define MM_IS_PLUGIN_OPTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_OPTION))
#define MM_PLUGIN_OPTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_OPTION, MMPluginOptionClass))

typedef struct {
    GObject parent;
} MMPluginOption;

typedef struct {
    GObjectClass parent;
} MMPluginOptionClass;

GType mm_plugin_option_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_OPTION_H */
