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
#include "mm-plugin-simtech.h"
#include "mm-broadband-modem-simtech.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

G_DEFINE_TYPE (MMPluginSimtech, mm_plugin_simtech, MM_TYPE_PLUGIN)

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
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered SimTech modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_simtech_new (sysfs_path,
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
    MMPortType ptype;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    port = mm_port_probe_peek_port (probe);
    ptype = mm_port_probe_get_port_type (probe);

    if (mm_port_probe_is_at (probe)) {
        /* Look for port type hints; just probing can't distinguish which port should
         * be the data/primary port on these devices.  We have to tag them based on
         * what the Windows .INF files say the port layout should be.
         */
        if (g_udev_device_get_property_as_boolean (port, "ID_MM_SIMTECH_PORT_TYPE_MODEM")) {
            mm_dbg ("Simtech: AT port '%s/%s' flagged as primary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
        } else if (g_udev_device_get_property_as_boolean (port, "ID_MM_SIMTECH_PORT_TYPE_AUX")) {
            mm_dbg ("Simtech: AT port '%s/%s' flagged as secondary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
        }

        /* If the port was tagged by the udev rules but isn't a primary or secondary,
         * then ignore it to guard against race conditions if a device just happens
         * to show up with more than two AT-capable ports.
         */
        if (pflags == MM_PORT_SERIAL_AT_FLAG_NONE &&
            g_udev_device_get_property_as_boolean (port, "ID_MM_SIMTECH_TAGGED"))
            ptype = MM_PORT_TYPE_IGNORED;
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_parent_path (probe),
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const guint16 vendor_ids[] = { 0x1e0e, /* A-Link (for now) */
                                          0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SIMTECH,
                      MM_PLUGIN_NAME,               "SimTech",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      NULL));
}

static void
mm_plugin_simtech_init (MMPluginSimtech *self)
{
}

static void
mm_plugin_simtech_class_init (MMPluginSimtechClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
