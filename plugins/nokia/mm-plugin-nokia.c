/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <string.h>
#include <gmodule.h>

#include <libmm-common.h>

#include "mm-log.h"
#include "mm-plugin-nokia.h"
#include "mm-broadband-modem-nokia.h"

G_DEFINE_TYPE (MMPluginNokia, mm_plugin_nokia, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* CUSTOM INIT */

static const MMPortProbeAtCommand custom_init[] = {
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { NULL }
};

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar *driver,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_nokia_new (sysfs_path,
                                                        driver,
                                                        mm_plugin_get_name (self),
                                                        vendor,
                                                        product));
}

static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    GUdevDevice *port;
    MMAtPortFlag pflags = MM_AT_PORT_FLAG_NONE;

    /* The Nokia plugin cannot do anything with non-AT */
    if (!mm_port_probe_is_at (probe)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Ignoring non-AT port");
        return FALSE;
    }

    port = mm_port_probe_get_port (probe); /* transfer none */

    /* Look for port type hints */
    if (g_udev_device_get_property_as_boolean (port, "ID_MM_NOKIA_PORT_TYPE_MODEM"))
        pflags = MM_AT_PORT_FLAG_PRIMARY;
    else if (g_udev_device_get_property_as_boolean (port, "ID_MM_NOKIA_PORT_TYPE_AUX"))
        pflags = MM_AT_PORT_FLAG_SECONDARY;

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_port_type (probe),
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint16 vendor_ids[] = { 0x0421, 0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOKIA,
                      MM_PLUGIN_NAME,               "Nokia",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_CUSTOM_INIT,        custom_init,
                      MM_PLUGIN_ALLOWED_SINGLE_AT,  TRUE, /* only 1 AT port expected! */
                      NULL));
}

static void
mm_plugin_nokia_init (MMPluginNokia *self)
{
}

static void
mm_plugin_nokia_class_init (MMPluginNokiaClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
