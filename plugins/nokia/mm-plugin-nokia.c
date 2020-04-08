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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-nokia.h"
#include "mm-broadband-modem-nokia.h"

G_DEFINE_TYPE (MMPluginNokia, mm_plugin_nokia, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom commands for AT probing */

static const MMPortProbeAtCommand custom_at_probe[] = {
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { "ATE1 E0", 3, mm_port_probe_response_processor_is_at },
    { NULL }
};

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_nokia_new (uid,
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
    static const guint16 vendor_ids[] = { 0x0421, 0 };
    static const gchar *vendor_strings[] = { "nokia", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOKIA,
                      MM_PLUGIN_NAME,                   MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,     subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,     vendor_ids,
                      MM_PLUGIN_ALLOWED_VENDOR_STRINGS, vendor_strings,
                      MM_PLUGIN_CUSTOM_AT_PROBE,        custom_at_probe,
                      MM_PLUGIN_ALLOWED_SINGLE_AT,      TRUE, /* only 1 AT port expected! */
                      MM_PLUGIN_FORBIDDEN_ICERA,        TRUE, /* No Nokia/Icera modems */
                      NULL));
}

static void
mm_plugin_nokia_init (MMPluginNokia *self)
{
}

static void
mm_plugin_nokia_class_init (MMPluginNokiaClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
