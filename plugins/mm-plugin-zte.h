/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_ZTE_H
#define MM_PLUGIN_ZTE_H

#include "mm-plugin.h"

#define MM_TYPE_PLUGIN_ZTE            (mm_plugin_zte_get_type ())
#define MM_PLUGIN_ZTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_ZTE, MMPluginZte))
#define MM_PLUGIN_ZTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_ZTE, MMPluginZteClass))
#define MM_IS_PLUGIN_ZTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_ZTE))
#define MM_IS_PLUGIN_ZTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_ZTE))
#define MM_PLUGIN_ZTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_ZTE, MMPluginZteClass))

typedef struct {
    GObject parent;
} MMPluginZte;

typedef struct {
    GObjectClass parent;
} MMPluginZteClass;

GType mm_plugin_zte_get_type (void);

G_MODULE_EXPORT MMPlugin *mm_plugin_create (void);

#endif /* MM_PLUGIN_ZTE_H */
