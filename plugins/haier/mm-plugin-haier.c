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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-broadband-modem.h"
#include "mm-plugin-haier.h"

G_DEFINE_TYPE (MMPluginHaier, mm_plugin_haier, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_new (sysfs_path,
                                                  drivers,
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
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    port = mm_port_probe_peek_port (probe);

    /* Look for port type hints */
    if (mm_port_probe_is_at (probe) && g_udev_device_get_property_as_boolean (port, "ID_MM_HAIER_PORT_TYPE_MODEM")) {
        mm_dbg ("HAIER: AT port '%s/%s' flagged as primary",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = (MM_PORT_SERIAL_AT_FLAG_PRIMARY | MM_PORT_SERIAL_AT_FLAG_PPP);
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_parent_path (probe),
                                    mm_port_probe_get_port_type (probe),
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint16 vendor_ids[] = { 0x201e, 0 };

    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HAIER,
                                    MM_PLUGIN_NAME,               "Haier",
                                    MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                                    MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                                    MM_PLUGIN_ALLOWED_AT,         TRUE,
                                    NULL));
}

static void
mm_plugin_haier_init (MMPluginHaier *self)
{
}

static void
mm_plugin_haier_class_init (MMPluginHaierClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
