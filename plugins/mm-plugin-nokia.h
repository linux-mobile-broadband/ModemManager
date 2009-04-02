/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_NOKIA_H
#define MM_PLUGIN_NOKIA_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_NOKIA            (mm_plugin_nokia_get_type ())
#define MM_PLUGIN_NOKIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_NOKIA, MMPluginNokia))
#define MM_PLUGIN_NOKIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_NOKIA, MMPluginNokiaClass))
#define MM_IS_PLUGIN_NOKIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_NOKIA))
#define MM_IS_PLUGIN_NOKIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_NOKIA))
#define MM_PLUGIN_NOKIA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_NOKIA, MMPluginNokiaClass))

typedef struct {
    GObject parent;
} MMPluginNokia;

typedef struct {
    GObjectClass parent;
} MMPluginNokiaClass;

GType mm_plugin_nokia_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_NOKIA_H */
