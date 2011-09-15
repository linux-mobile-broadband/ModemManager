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

#include "mm-plugin-generic.h"
#include "mm-generic-gsm.h"
#include "mm-generic-cdma.h"
#include "mm-errors.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginGeneric, mm_plugin_generic, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/

static MMModem *
grab_port (MMPluginBase *base,
           MMModem *existing,
           MMPortProbe *probe,
           GError **error)
{
    GUdevDevice *port;
    MMModem *modem = NULL;
    const gchar *name, *subsys, *devfile, *physdev, *driver;
    guint32 caps;
    guint16 vendor = 0, product = 0;

    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);
    driver = mm_port_probe_get_port_driver (probe);
    port = mm_port_probe_get_port (probe);
    g_assert (port);

    devfile = g_udev_device_get_device_file (port);
    if (!devfile) {
        if (!driver || strcmp (driver, "bluetooth")) {
            g_set_error (error, 0, 0, "Could not get port's sysfs file.");
            return NULL;
        }

        mm_warn ("%s: (%s/%s) WARNING: missing udev 'device' file",
                 mm_plugin_get_name (MM_PLUGIN (base)),
                 subsys,
                 name);
    }

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    caps = mm_port_probe_get_capabilities (probe);
    physdev = mm_port_probe_get_port_physdev (probe);
    if (!existing) {
        if (caps & MM_PORT_PROBE_CAPABILITY_CDMA) {
            modem = mm_generic_cdma_new (physdev,
                                         driver,
                                         mm_plugin_get_name (MM_PLUGIN (base)),
                                         !!(caps & MM_PORT_PROBE_CAPABILITY_IS856),
                                         !!(caps & MM_PORT_PROBE_CAPABILITY_IS856_A),
                                         vendor,
                                         product);
        } else if (caps & MM_PORT_PROBE_CAPABILITY_GSM) {
            modem = mm_generic_gsm_new (physdev,
                                        driver,
                                        mm_plugin_get_name (MM_PLUGIN (base)),
                                        vendor,
                                        product);
        }

        if (modem) {
            if (!mm_modem_grab_port (modem,
                                     subsys,
                                     name,
                                     MM_PORT_TYPE_UNKNOWN,
                                     NULL,
                                     error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else if (caps) {
        MMPortType ptype = MM_PORT_TYPE_UNKNOWN;

        if (mm_port_probe_is_qcdm (probe))
            ptype = MM_PORT_TYPE_QCDM;

        modem = existing;
        if (!mm_modem_grab_port (modem,
                                 subsys,
                                 name,
                                 ptype,
                                 NULL,
                                 error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *name = MM_PLUGIN_GENERIC_NAME;
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint32 capabilities = MM_PORT_PROBE_CAPABILITY_GSM_OR_CDMA;
    static const gboolean qcdm = TRUE;

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_GENERIC,
                      MM_PLUGIN_BASE_NAME, name,
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_CAPABILITIES, capabilities,
                      MM_PLUGIN_BASE_ALLOWED_QCDM, qcdm,
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

