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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#include "mm-plugin-novatel.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginNovatel, mm_plugin_novatel, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* Custom commands for AT probing */

/* We need to explicitly flip secondary ports to AT mode.
 * We also use this command also for checking AT support in the current port.
 */
static const MMPortProbeAtCommand custom_at_probe[] = {
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { NULL }
};

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
    return MM_BASE_MODEM (mm_broadband_modem_novatel_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", NULL };
    static const guint16 vendors[] = { 0x1410, /* Novatel */
                                       0x413c, /* Dell */
                                       0 };
    static const mm_uint16_pair forbidden_products[] = { { 0x1410, 0x9010 }, /* Novatel E362 */
                                                         {0, 0} };
    static const gchar *drivers[] = { "option1", "option", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOVATEL,
                      MM_PLUGIN_NAME,                  "Novatel",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,    subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,       drivers,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,    vendors,
                      MM_PLUGIN_FORBIDDEN_PRODUCT_IDS, forbidden_products,
                      MM_PLUGIN_ALLOWED_AT,            TRUE,
                      MM_PLUGIN_CUSTOM_AT_PROBE,       custom_at_probe,
                      MM_PLUGIN_ALLOWED_QCDM,          TRUE,
                      NULL));
}

static void
mm_plugin_novatel_init (MMPluginNovatel *self)
{
}

static void
mm_plugin_novatel_class_init (MMPluginNovatelClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
