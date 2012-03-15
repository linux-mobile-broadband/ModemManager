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

G_DEFINE_TYPE (MMPluginHso, mm_plugin_hso, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/

static MMBaseModem *
grab_port (MMPluginBase *base,
           MMBaseModem *existing,
           MMPortProbe *probe,
           GError **error)
{
    MMBaseModem *modem = NULL;
    GUdevDevice *port;
    const gchar *name, *subsys, *sysfs_path;
    guint16 vendor = 0, product = 0;
    MMAtPortFlag pflags = MM_AT_PORT_FLAG_NONE;
    gchar *devfile;

    port = mm_port_probe_get_port (probe); /* transfer none */
    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Could not get modem product ID");
        return NULL;
    }

    /* Build proper devfile path */
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
            return NULL;
        }
    }

    sysfs_path = mm_port_probe_get_port_physdev (probe);

    /* Detect AT port types */
    if (g_str_equal (subsys, "tty")) {
        gchar *hsotype_path;
        gchar *contents = NULL;

        hsotype_path = g_build_filename (sysfs_path, "hsotype", NULL);
        if (g_file_get_contents (hsotype_path, &contents, NULL, NULL)) {
            if (g_str_has_prefix (contents, "Control"))
                pflags = MM_AT_PORT_FLAG_PRIMARY;
            else if (g_str_has_prefix (contents, "Application"))
                pflags = MM_AT_PORT_FLAG_SECONDARY;  /* secondary */
            g_free (contents);
        }
        g_free (hsotype_path);
    }

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_hso_new (sysfs_path,
                                                           mm_port_probe_get_port_driver (probe),
                                                           mm_plugin_get_name (MM_PLUGIN (base)),
                                                           vendor,
                                                           product));

    if (!mm_base_modem_grab_port (existing ? existing : modem,
                                  subsys,
                                  name,
                                  mm_port_probe_get_port_type (probe),
                                  pflags,
                                  error)) {
        if (modem)
            g_object_unref (modem);
        return NULL;
    }

    return existing ? existing : modem;
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const gchar *drivers[] = { "hso", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_HSO,
                      MM_PLUGIN_BASE_NAME, "Option High-Speed",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_DRIVERS, drivers,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      NULL));
}

static void
mm_plugin_hso_init (MMPluginHso *self)
{
}

static void
mm_plugin_hso_class_init (MMPluginHsoClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}
