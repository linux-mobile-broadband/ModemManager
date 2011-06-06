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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-zte.h"
#include "mm-modem-zte.h"
#include "mm-generic-cdma.h"

G_DEFINE_TYPE (MMPluginZte, mm_plugin_zte, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_ZTE,
                                    MM_PLUGIN_BASE_NAME, "ZTE",
                                    NULL));
}

/*****************************************************************************/

#define CAP_CDMA (MM_PLUGIN_BASE_PORT_CAP_IS707_A | \
                  MM_PLUGIN_BASE_PORT_CAP_IS707_P | \
                  MM_PLUGIN_BASE_PORT_CAP_IS856 | \
                  MM_PLUGIN_BASE_PORT_CAP_IS856_A)

static guint32
get_level_for_capabilities (guint32 capabilities)
{
    if (capabilities & MM_PLUGIN_BASE_PORT_CAP_GSM)
        return 10;
    if (capabilities & CAP_CDMA)
        return 10;
    if (capabilities & MM_PLUGIN_BASE_PORT_CAP_QCDM)
        return 10;
    return 0;
}

static void
probe_result (MMPluginBase *base,
              MMPluginBaseSupportsTask *task,
              guint32 capabilities,
              gpointer user_data)
{
    mm_plugin_base_supports_task_complete (task, get_level_for_capabilities (capabilities));
}

static gboolean
custom_init_response_cb (MMPluginBaseSupportsTask *task,
                         GString *response,
                         GError *error,
                         guint32 tries,
                         gboolean *out_stop,
                         guint32 *out_level,
                         gpointer user_data)
{
    if (error)
        return tries <= 4 ? TRUE : FALSE;

    /* No error, assume success */
    return FALSE;
}

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    guint32 cached = 0, level;
    guint16 vendor = 0;
    const char *subsys, *name;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, NULL))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (vendor != 0x19d2)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (!strcmp (subsys, "net")) {
        /* If we don't know the modem's type yet, defer grabbing the port
         * until we know the type.
         */
        if (!existing)
            return MM_PLUGIN_SUPPORTS_PORT_DEFER;

        mm_plugin_base_supports_task_complete (task, 10);
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

    if (strcmp (subsys, "tty"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (mm_plugin_base_get_cached_port_capabilities (base, port, &cached)) {
        level = get_level_for_capabilities (cached);
        if (level) {
            mm_plugin_base_supports_task_complete (task, level);
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
        }
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    /* Otherwise kick off a probe */

    /* Many ZTE devices will flood the port with "Message waiting" indications
     * and eventually fill up the serial buffer and crash.  We need to turn off
     * that indicator.  See NetworkManager commits
     * 	1235f71b20c92cded4abd976ccc5010649aae1a0 and
     * 	f38ad328acfdc6ce29dd1380602c546b064161ae for more details.
     */
    mm_plugin_base_supports_task_add_custom_init_command (task,
                                                          "ATE0+CPMS?",
                                                          3,
                                                          custom_init_response_cb,
                                                          NULL);

    if (mm_plugin_base_probe_port (base, task, 100000, NULL))
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;

    return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
}

static MMModem *
grab_port (MMPluginBase *base,
           MMModem *existing,
           MMPluginBaseSupportsTask *task,
           GError **error)
{
    GUdevDevice *port = NULL;
    MMModem *modem = NULL;
    const char *name, *subsys, *sysfs_path;
    guint32 caps;
    MMPortType ptype = MM_PORT_TYPE_UNKNOWN;
    guint16 vendor = 0, product = 0;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    /* Look for port type hints */
    if (g_udev_device_get_property_as_boolean (port, "ID_MM_ZTE_PORT_TYPE_MODEM"))
        ptype = MM_PORT_TYPE_PRIMARY;
    else if (g_udev_device_get_property_as_boolean (port, "ID_MM_ZTE_PORT_TYPE_AUX"))
        ptype = MM_PORT_TYPE_SECONDARY;

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        if (caps & MM_PLUGIN_BASE_PORT_CAP_GSM) {
            modem = mm_modem_zte_new (sysfs_path,
                                      mm_plugin_base_supports_task_get_driver (task),
                                      mm_plugin_get_name (MM_PLUGIN (base)),
                                      vendor,
                                      product);
        } else if (caps & CAP_CDMA) {
            modem = mm_generic_cdma_new (sysfs_path,
                                         mm_plugin_base_supports_task_get_driver (task),
                                         mm_plugin_get_name (MM_PLUGIN (base)),
                                         !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856),
                                         !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856_A),
                                         vendor,
                                         product);
        }

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, ptype, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else if (get_level_for_capabilities (caps) || (!strcmp (subsys, "net"))) {
        if (caps & MM_PLUGIN_BASE_PORT_CAP_QCDM)
            ptype = MM_PORT_TYPE_QCDM;

        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, NULL, error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

static void
mm_plugin_zte_init (MMPluginZte *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_zte_class_init (MMPluginZteClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
