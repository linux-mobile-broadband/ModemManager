/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your hso) any later version.
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

#include <libmm-common.h>

#include "mm-private-boxed-types.h"
#include "mm-plugin-hso.h"
#include "mm-broadband-modem-hso.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginHso, mm_plugin_hso, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

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
    return MM_BASE_MODEM (mm_broadband_modem_hso_new (sysfs_path,
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
    const gchar *name, *subsys, *sysfs_path;
    MMAtPortFlag pflags = MM_AT_PORT_FLAG_NONE;
    gchar *devfile;
    MMPortType port_type;

    port = mm_port_probe_peek_port (probe);
    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);

    /* Build proper devfile path
     * TODO: Why do we need to do this? If this is useful, a comment should be
     * added explaining why; if it's not useful, let's get rid of it. */
    devfile = g_strdup (g_udev_device_get_device_file (port));
    if (!devfile) {
        if (g_str_equal (subsys, "net")) {
            /* Apparently 'hso' doesn't set up the right links for the netdevice,
             * and thus libgudev can't get the sysfs file path for it.
             */
            devfile = g_strdup_printf ("/sys/class/net/%s", name);
            if (!g_file_test (devfile, G_FILE_TEST_EXISTS)) {
                g_free (devfile);
                devfile = NULL;
            }
        }

        if (!devfile) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Could not get port's sysfs file.");
            return FALSE;
        }
    }
    g_free (devfile);

    port_type = mm_port_probe_get_port_type (probe);
    sysfs_path = g_udev_device_get_sysfs_path (port);

    /* Detect AT port types */
    if (g_str_equal (subsys, "tty")) {
        gchar *hsotype_path;
        gchar *contents = NULL;

        hsotype_path = g_build_filename (sysfs_path, "hsotype", NULL);
        if (g_file_get_contents (hsotype_path, &contents, NULL, NULL)) {
            if (g_str_has_prefix (contents, "Control"))
                pflags = MM_AT_PORT_FLAG_PRIMARY;
            else if (g_str_has_prefix (contents, "Application"))
                pflags = MM_AT_PORT_FLAG_SECONDARY;
            else if (g_str_has_prefix (contents, "GPS Control"))
                pflags = MM_AT_PORT_FLAG_GPS_CONTROL;
            else if (g_str_has_prefix (contents, "GPS")) {
                /* Not an AT port, but the port to grab GPS traces */
                g_assert (port_type == MM_PORT_TYPE_UNKNOWN);
                port_type = MM_PORT_TYPE_GPS;
            }
            g_free (contents);
        }
        g_free (hsotype_path);
    }

    return mm_base_modem_grab_port (modem,
                                    subsys,
                                    name,
                                    port_type,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const gchar *drivers[] = { "hso", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_HSO,
                      MM_PLUGIN_NAME,               "Option High-Speed",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      NULL));
}

static void
mm_plugin_hso_init (MMPluginHso *self)
{
}

static void
mm_plugin_hso_class_init (MMPluginHsoClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
