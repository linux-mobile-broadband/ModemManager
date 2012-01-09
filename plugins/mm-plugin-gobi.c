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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#include <string.h>
#include <gmodule.h>

#include <mm-errors-types.h>

#include "mm-plugin-gobi.h"
#include "mm-broadband-modem-gobi.h"

G_DEFINE_TYPE (MMPluginGobi, mm_plugin_gobi, MM_TYPE_PLUGIN_BASE)

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
    const gchar *name, *subsys, *driver;
    guint16 vendor = 0, product = 0;

    /* The Gobi plugin only handles AT and QCDM ports (for now) */
    if (!mm_port_probe_is_at (probe) && !mm_port_probe_is_qcdm (probe)) {
        g_set_error (error, 0, 0, "Ignoring non-AT/non-QCDM port");
        return NULL;
    }

    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);
    driver = mm_port_probe_get_port_driver (probe);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_gobi_new (mm_port_probe_get_port_physdev (probe),
                                                            driver,
                                                            mm_plugin_get_name (MM_PLUGIN (base)),
                                                            vendor,
                                                            product));

    if (!mm_base_modem_grab_port (existing ? existing : modem,
                                  subsys,
                                  name,
                                  (mm_port_probe_is_qcdm (probe) ?
                                   MM_PORT_TYPE_QCDM :
                                   MM_PORT_TYPE_UNKNOWN))) {
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
    static const gchar *drivers[] = { "qcserial", NULL };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_GOBI,
                      MM_PLUGIN_BASE_NAME, "Gobi",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_DRIVERS, drivers,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      MM_PLUGIN_BASE_ALLOWED_QCDM, TRUE,
                      NULL));
}

static void
mm_plugin_gobi_init (MMPluginGobi *self)
{
}

static void
mm_plugin_gobi_class_init (MMPluginGobiClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}

