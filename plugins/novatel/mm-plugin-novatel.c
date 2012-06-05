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

#include "mm-plugin-novatel.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginNovatel, mm_plugin_novatel, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

static MMBaseModem *
create_modem (MMPluginBase *plugin,
              const gchar *sysfs_path,
              const gchar *driver,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_novatel_new (sysfs_path,
                                                          driver,
                                                          mm_plugin_get_name (MM_PLUGIN (plugin)),
                                                          vendor,
                                                          product));
}

static gboolean
grab_port (MMPluginBase *base,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    /* The Novatel plugin uses AT and net ports */
    if (!mm_port_probe_is_at (probe) &&
        !g_str_equal (mm_port_probe_get_port_subsys (probe), "net")) {
        g_set_error (error, 0, 0, "Ignoring non-AT/net port");
        return FALSE;
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_port_type (probe),
                                    MM_AT_PORT_FLAG_NONE,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const mm_uint16_pair products[] = { { 0x1410, 0x9010 }, /* Novatel E362 */
                                               {0, 0} };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOVATEL,
                      MM_PLUGIN_BASE_NAME, "Novatel",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_PRODUCT_IDS, products,
                      MM_PLUGIN_BASE_ALLOWED_SINGLE_AT, TRUE,
                      NULL));
}

static void
mm_plugin_novatel_init (MMPluginNovatel *self)
{
}

static void
mm_plugin_novatel_class_init (MMPluginNovatelClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->create_modem = create_modem;
    pb_class->grab_port = grab_port;
}
