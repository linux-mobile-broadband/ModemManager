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
 * Copyright (C) 2008 - 2010 Dan Williams <dcbw@redhat.com>
 */

#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>

#include <gmodule.h>

#include <libmm-common.h>

#include "mm-plugin-generic.h"
#include "mm-broadband-modem.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginGeneric, mm_plugin_generic, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/

static MMBaseModem *
grab_port (MMPluginBase *base,
           MMBaseModem *existing,
           MMPortProbe *probe,
           GError **error)
{
    GUdevDevice *port;
    MMBaseModem *modem = NULL;
    const gchar *name, *subsys, *devfile, *driver;
    guint16 vendor = 0, product = 0;

    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);

    /* The generic plugin cannot do anything with non-AT and non-QCDM ports */
    if (!mm_port_probe_is_at (probe) &&
        !mm_port_probe_is_qcdm (probe)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Ignoring non-AT/non-QCDM ports");
        return NULL;
    }

    driver = mm_port_probe_get_port_driver (probe);
    port = mm_port_probe_get_port (probe);

    /* Check device file of the port, we expect one */
    devfile = g_udev_device_get_device_file (port);
    if (!devfile) {
        if (!driver || !g_str_equal (driver, "bluetooth")) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Could not get port's sysfs file.");
            return NULL;
        }

        mm_warn ("%s: (%s/%s) WARNING: missing udev 'device' file",
                 mm_plugin_get_name (MM_PLUGIN (base)),
                 subsys,
                 name);
    }

    /* Vendor and Product IDs are really optional, we'll just warn if they
     * cannot get loaded */
    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product))
        mm_warn ("Could not get modem vendor/product ID");

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_new (mm_port_probe_get_port_physdev (probe),
                                                       driver,
                                                       mm_plugin_get_name (MM_PLUGIN (base)),
                                                       vendor,
                                                       product));

    if (!mm_base_modem_grab_port (existing ? existing : modem,
                                  subsys,
                                  name,
                                  mm_port_probe_get_port_type (probe),
                                  MM_AT_PORT_FLAG_NONE,
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
    static const gchar *subsystems[] = { "tty", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_GENERIC,
                      MM_PLUGIN_BASE_NAME, MM_PLUGIN_GENERIC_NAME,
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      MM_PLUGIN_BASE_ALLOWED_QCDM, TRUE,
                      NULL));
}

static void
mm_plugin_generic_init (MMPluginGeneric *self)
{
}

static void
mm_plugin_generic_class_init (MMPluginGenericClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}
