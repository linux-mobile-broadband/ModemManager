/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2011 Google Inc.
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>

#include "mm-plugin-cinterion.h"
#include "mm-broadband-modem-cinterion.h"
#include "mm-errors-types.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginCinterion, mm_plugin_cinterion, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

static MMBaseModem *
grab_port (MMPluginBase *base,
           MMBaseModem *existing,
           MMPortProbe *probe,
           GError **error)
{
    MMBaseModem *modem = NULL;
    const gchar *name, *subsys, *driver;
    guint16 vendor = 0, product = 0;

    /* The Cinterion plugin cannot do anything with non-AT ports */
    if (!mm_port_probe_is_at (probe)) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Ignoring non-AT port");
        return NULL;
    }

    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);
    driver = mm_port_probe_get_port_driver (probe);

    /* Try to get Product IDs from udev. Note that it is not an error
     * if we can't get them in our case, as we also support serial
     * modems. */
    mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product);

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_cinterion_new (mm_port_probe_get_port_physdev (probe),
                                                                 driver,
                                                                 mm_plugin_get_name (MM_PLUGIN (base)),
                                                                 vendor,
                                                                 product));

    if (!mm_base_modem_grab_port (existing ? existing : modem,
                                  subsys,
                                  name,
                                  MM_PORT_TYPE_UNKNOWN)) {
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
    static const gchar *vendor_strings[] = { "cinterion", "siemens", NULL };
    static const guint16 vendor_ids[] = { 0x1e2d, 0x0681, 0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_CINTERION,
                      MM_PLUGIN_BASE_NAME, "Cinterion",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_VENDOR_STRINGS, vendor_strings,
                      MM_PLUGIN_BASE_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      MM_PLUGIN_BASE_SORT_LAST, TRUE,
                      NULL));
}

static void
mm_plugin_cinterion_init (MMPluginCinterion *self)
{
}

static void
mm_plugin_cinterion_class_init (MMPluginCinterionClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}
