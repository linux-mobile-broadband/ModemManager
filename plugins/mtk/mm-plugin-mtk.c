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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-plugin-mtk.h"
#include "mm-broadband-modem-mtk.h"

G_DEFINE_TYPE (MMPluginMtk, mm_plugin_mtk, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/

/* MTK done */
static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_mtk_new (sysfs_path,
                                                      drivers,
                                                      mm_plugin_get_name (self),
                                                      vendor,
                                                      product));
}

/* MTK done */
static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    GUdevDevice *port;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    port = mm_port_probe_peek_port (probe);

    if (mm_port_probe_is_at (probe)) {
        /* Get port type from udev */
        if (g_udev_device_get_property_as_boolean (port, "ID_MM_MTK_AT_PORT")) {
            mm_dbg ("MTK: AT port '%s/%s' flagged as secondary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
        } else if (g_udev_device_get_property_as_boolean (port, "ID_MM_MTK_MODEM_PORT")) {
            mm_dbg ("MTK: Modem port '%s/%s' flagged as primary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
        }
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
    static const gchar *udev_tags[]={
        "ID_MM_MTK_TAGGED",
        NULL};

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_MTK,
                      MM_PLUGIN_NAME,               "MTK",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_UDEV_TAGS,  udev_tags,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      NULL));
}

static void
mm_plugin_mtk_init (MMPluginMtk *self)
{
}

static void
mm_plugin_mtk_class_init (MMPluginMtkClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
