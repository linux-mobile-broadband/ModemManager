/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_NOVATEL_H
#define MM_PLUGIN_NOVATEL_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_NOVATEL            (mm_plugin_novatel_get_type ())
#define MM_PLUGIN_NOVATEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_NOVATEL, MMPluginNovatel))
#define MM_PLUGIN_NOVATEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_NOVATEL, MMPluginNovatelClass))
#define MM_IS_PLUGIN_NOVATEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_NOVATEL))
#define MM_IS_PLUGIN_NOVATEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_NOVATEL))
#define MM_PLUGIN_NOVATEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_NOVATEL, MMPluginNovatelClass))

typedef struct {
    GObject parent;
} MMPluginNovatel;

typedef struct {
    GObjectClass parent;
} MMPluginNovatelClass;

GType mm_plugin_novatel_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_NOVATEL_H */
