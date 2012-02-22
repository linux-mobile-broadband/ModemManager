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
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <string.h>
#include <gmodule.h>

#include "mm-plugin-samsung.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-samsung.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginSamsung, mm_plugin_samsung, MM_TYPE_PLUGIN_BASE)

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

    mm_dbg(" existing %p", existing);
    /* The Samsung plugin uses AT and net ports */
    if (!mm_port_probe_is_at (probe) &&
        !g_str_equal (mm_port_probe_get_port_subsys (probe), "net")) {
        g_set_error (error, 0, 0, "Ignoring non-AT/net port");
        return NULL;
    }

    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);
    driver = mm_port_probe_get_port_driver (probe);
    mm_dbg("subsys %s name %s driver %s", subsys, name, driver);

    /* Try to get Product IDs from udev. */
    mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product);
    mm_dbg("vendor 0x%04x product 0x%04x", vendor, product);

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_samsung_new (mm_port_probe_get_port_physdev (probe),
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
        mm_dbg("mm_base_modem_grab_port failed; releasing");
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
    static const mm_uint16_pair products[] = { { 0x04e8, 0x6872},
                                               { 0x04e8, 0x6906},
                                               {0, 0} };
    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SAMSUNG,
                      MM_PLUGIN_BASE_NAME, "Samsung",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_PRODUCT_IDS, products,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      NULL));
}

static void
mm_plugin_samsung_init (MMPluginSamsung *self)
{
}

static void
mm_plugin_samsung_class_init (MMPluginSamsungClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}
