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

G_DEFINE_TYPE (MMPluginGeneric, mm_plugin_generic, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar *driver,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_new (sysfs_path,
                                                  driver,
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
    /* The generic plugin cannot do anything with non-AT and non-QCDM ports */
    if (!mm_port_probe_is_at (probe) &&
        !mm_port_probe_is_qcdm (probe)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Ignoring non-AT/non-QCDM ports");
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
    static const gchar *subsystems[] = { "tty", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_GENERIC,
                      MM_PLUGIN_NAME,               MM_PLUGIN_GENERIC_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      NULL));
}

static void
mm_plugin_generic_init (MMPluginGeneric *self)
{
}

static void
mm_plugin_generic_class_init (MMPluginGenericClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
