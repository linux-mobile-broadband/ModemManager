/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_PLUGIN_H
#define MM_PLUGIN_H

#include <glib-object.h>
#include <libhal.h>
#include <mm-modem.h>

#define MM_PLUGIN_MAJOR_VERSION 1
#define MM_PLUGIN_MINOR_VERSION 0

#define MM_TYPE_PLUGIN      (mm_plugin_get_type ())
#define MM_PLUGIN(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PLUGIN, MMPlugin))
#define MM_IS_PLUGIN(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PLUGIN))
#define MM_PLUGIN_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_PLUGIN, MMPlugin))

typedef struct _MMPlugin MMPlugin;

typedef MMPlugin *(*MMPluginCreateFunc) (void);

struct _MMPlugin {
    GTypeInterface g_iface;

    /* Methods */
    const char *(*get_name) (MMPlugin *self);

    char **(*list_supported_udis) (MMPlugin *self,
                                   LibHalContext *hal_ctx);

    gboolean (*supports_udi) (MMPlugin *self,
                              LibHalContext *hal_ctx,
                              const char *udi);

    MMModem *(*create_modem) (MMPlugin *self,
                              LibHalContext *hal_ctx,
                              const char *udi);
};

GType mm_plugin_get_type (void);

const char *mm_plugin_get_name (MMPlugin *plugin);

char **mm_plugin_list_supported_udis (MMPlugin *plugin,
                                      LibHalContext *hal_ctx);

gboolean mm_plugin_supports_udi (MMPlugin *plugin,
                                 LibHalContext *hal_ctx,
                                 const char *udi);

MMModem *mm_plugin_create_modem (MMPlugin *plugin,
                                 LibHalContext *hal_ctx,
                                 const char *udi);

#endif /* MM_PLUGIN_H */
