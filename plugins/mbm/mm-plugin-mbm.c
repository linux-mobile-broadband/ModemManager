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
 * Copyright (C) 2008 Ericsson AB
 * Copyright (C) 2012 Lanedo GmbH
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-plugin-mbm.h"
#include "mm-broadband-modem-mbm.h"

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginMbm, mm_plugin_mbm, MM_TYPE_PLUGIN)

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
#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_dbg ("MBIM-powered Ericsson modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (sysfs_path,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_mbm_new (sysfs_path,
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
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;
    GUdevDevice *port;
    MMPortType port_type;

    port_type = mm_port_probe_get_port_type (probe);
    port = mm_port_probe_peek_port (probe);

    if (g_udev_device_get_property_as_boolean (port, "ID_MM_ERICSSON_MBM_GPS_PORT")) {
        mm_dbg ("(%s/%s) Port flagged as GPS",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        port_type = MM_PORT_TYPE_GPS;
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_parent_path (probe),
                                    port_type,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const gchar *udev_tags[] = {
        "ID_MM_ERICSSON_MBM",
        NULL
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_MBM,
                      MM_PLUGIN_NAME,               "Ericsson MBM",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_UDEV_TAGS,  udev_tags,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      NULL));
}

static void
mm_plugin_mbm_init (MMPluginMbm *self)
{
}

static void
mm_plugin_mbm_class_init (MMPluginMbmClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;

}
