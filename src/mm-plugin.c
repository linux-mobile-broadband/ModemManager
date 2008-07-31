/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "mm-plugin.h"

const char *
mm_plugin_get_name (MMPlugin *plugin)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), NULL);

    return MM_PLUGIN_GET_INTERFACE (plugin)->get_name (plugin);
}

char **
mm_plugin_list_supported_udis (MMPlugin *plugin,
                               LibHalContext *hal_ctx)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), NULL);
    g_return_val_if_fail (hal_ctx != NULL, NULL);

    return MM_PLUGIN_GET_INTERFACE (plugin)->list_supported_udis (plugin, hal_ctx);
}

gboolean
mm_plugin_supports_udi (MMPlugin *plugin,
                        LibHalContext *hal_ctx,
                        const char *udi)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), FALSE);
    g_return_val_if_fail (hal_ctx != NULL, FALSE);
    g_return_val_if_fail (udi != NULL, FALSE);

    return MM_PLUGIN_GET_INTERFACE (plugin)->supports_udi (plugin, hal_ctx, udi);
}

MMModem *
mm_plugin_create_modem (MMPlugin *plugin,
                        LibHalContext *hal_ctx,
                        const char *udi)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), NULL);
    g_return_val_if_fail (hal_ctx != NULL, NULL);
    g_return_val_if_fail (udi != NULL, NULL);

    return MM_PLUGIN_GET_INTERFACE (plugin)->create_modem (plugin, hal_ctx, udi);
}


/*****************************************************************************/

static void
mm_plugin_init (gpointer g_iface)
{
}

GType
mm_plugin_get_type (void)
{
    static GType plugin_type = 0;

    if (!G_UNLIKELY (plugin_type)) {
        const GTypeInfo plugin_info = {
            sizeof (MMPlugin), /* class_size */
            mm_plugin_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        plugin_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMPlugin",
                                             &plugin_info, 0);

        g_type_interface_add_prerequisite (plugin_type, G_TYPE_OBJECT);
    }

    return plugin_type;
}
