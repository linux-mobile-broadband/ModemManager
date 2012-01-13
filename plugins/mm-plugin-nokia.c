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
 * Copyright (C) 2011 Google, Inc.
 */

#include <string.h>
#include <gmodule.h>

#include <mm-errors-types.h>

#include "mm-plugin-nokia.h"
#include "mm-broadband-modem-nokia.h"

G_DEFINE_TYPE (MMPluginNokia, mm_plugin_nokia, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* CUSTOM INIT */

static gboolean
parse_init_last (const gchar *response,
                 const GError *error,
                 GValue *result,
                 GError **result_error)
{
    if (error) {
        *result_error = g_error_copy (error);
        return FALSE;
    }

    /* Otherwise, done. And also report that it's an AT port. */
    g_value_init (result, G_TYPE_BOOLEAN);
    g_value_set_boolean (result, TRUE);
    return TRUE;
}

static gboolean
parse_init (const gchar *response,
            const GError *error,
            GValue *result,
            GError **result_error)
{
    if (error) {
        /* On timeout, request to retry */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            return FALSE; /* Retry */
    }

    /* Otherwise, done. And also report that it's an AT port. */
    g_value_init (result, G_TYPE_BOOLEAN);
    g_value_set_boolean (result, TRUE);
    return TRUE;
}

static const MMPortProbeAtCommand custom_init[] = {
    { "ATE1 E0", parse_init },
    { "ATE1 E0", parse_init },
    { "ATE1 E0", parse_init_last },
    { NULL }
};

/*****************************************************************************/

static MMBaseModem *
grab_port (MMPluginBase *base,
           MMBaseModem *existing,
           MMPortProbe *probe,
           GError **error)
{
    MMBaseModem *modem = NULL;
    GUdevDevice *port;
    const gchar *name, *subsys, *driver;
    guint16 vendor = 0, product = 0;
    MMPortType ptype = MM_PORT_TYPE_UNKNOWN;

    /* The Nokia plugin cannot do anything with non-AT ports */
    if (!mm_port_probe_is_at (probe)) {
        g_set_error (error, 0, 0, "Ignoring non-AT port");
        return NULL;
    }

    port = mm_port_probe_get_port (probe); /* transfer none */
    subsys = mm_port_probe_get_port_subsys (probe);
    name = mm_port_probe_get_port_name (probe);
    driver = mm_port_probe_get_port_driver (probe);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    /* Look for port type hints */
    if (g_udev_device_get_property_as_boolean (port, "ID_MM_NOKIA_PORT_TYPE_MODEM"))
        ptype = MM_PORT_TYPE_PRIMARY;
    else if (g_udev_device_get_property_as_boolean (port, "ID_MM_NOKIA_PORT_TYPE_AUX"))
        ptype = MM_PORT_TYPE_SECONDARY;

    /* If this is the first port being grabbed, create a new modem object */
    if (!existing)
        modem = MM_BASE_MODEM (mm_broadband_modem_nokia_new (mm_port_probe_get_port_physdev (probe),
                                                             driver,
                                                             mm_plugin_get_name (MM_PLUGIN (base)),
                                                             vendor,
                                                             product));

    if (!mm_base_modem_grab_port (existing ? existing : modem,
                                  subsys,
                                  name,
                                  ptype)) {
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
    static const guint16 vendor_ids[] = { 0x0421, 0 };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOKIA,
                      MM_PLUGIN_BASE_NAME, "Nokia",
                      MM_PLUGIN_BASE_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_BASE_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_BASE_CUSTOM_INIT, custom_init,
                      MM_PLUGIN_BASE_ALLOWED_AT, TRUE,
                      NULL));
}

static void
mm_plugin_nokia_init (MMPluginNokia *self)
{
}

static void
mm_plugin_nokia_class_init (MMPluginNokiaClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->grab_port = grab_port;
}
