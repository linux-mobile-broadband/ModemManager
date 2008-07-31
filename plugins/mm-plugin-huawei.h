/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_HUAWEI_H
#define MM_PLUGIN_HUAWEI_H

#include "mm-plugin.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_PLUGIN_HUAWEI			(mm_plugin_huawei_get_type ())
#define MM_PLUGIN_HUAWEI(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN_HUAWEI, MMPluginHuawei))
#define MM_PLUGIN_HUAWEI_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PLUGIN_HUAWEI, MMPluginHuaweiClass))
#define MM_IS_PLUGIN_HUAWEI(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN_HUAWEI))
#define MM_IS_PLUGIN_HUAWEI_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PLUGIN_HUAWEI))
#define MM_PLUGIN_HUAWEI_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PLUGIN_HUAWEI, MMPluginHuaweiClass))

typedef struct {
    GObject parent;
} MMPluginHuawei;

typedef struct {
    GObjectClass parent;
} MMPluginHuaweiClass;

GType mm_plugin_huawei_get_type (void);

#endif /* MM_PLUGIN_HUAWEI_H */
