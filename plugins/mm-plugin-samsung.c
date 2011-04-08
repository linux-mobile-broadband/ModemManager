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
 * Copyright (c) 2011 Samsung Electronics, Inc.,
 */

#include <string.h>
#include <gmodule.h>
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-samsung.h"
#include "mm-modem-samsung-gsm.h"

G_DEFINE_TYPE (MMPluginSamsung, mm_plugin_samsung, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_SAMSUNG,
                                    MM_PLUGIN_BASE_NAME, "Samsung",
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

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    const char *subsys, *name;
    guint16 vendor = 0, product = 0;

    port = mm_plugin_base_supports_task_get_port (task);

    subsys = g_udev_device_get_subsystem (port);
    g_assert (subsys);
    name = g_udev_device_get_name (port);
    g_assert (name);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* Vendor ID check */
    if (vendor != 0x04e8 && vendor != 0x1983)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* The ethernet ports are obviously supported and don't need probing */
    if (!strcmp (subsys, "net")) {
        mm_plugin_base_supports_task_complete (task, 10);
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

    /* Otherwise kick off a probe */
    if (mm_plugin_base_probe_port (base, task, 0, NULL))
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
    guint32 caps;
    const char *name, *subsys, *sysfs_path;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    if (caps & CAP_CDMA) {
        g_set_error (error, 0, 0, "Only GSM modems are currently supported by this plugin.");
        return NULL;
    }

    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        modem = mm_modem_samsung_gsm_new (sysfs_path,
                                          mm_plugin_base_supports_task_get_driver (task),
                                          mm_plugin_get_name (MM_PLUGIN (base)));

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, MM_PORT_TYPE_UNKNOWN, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else {
        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, MM_PORT_TYPE_UNKNOWN, NULL, error))
            return NULL;
    }

    return modem;
}

static void
mm_plugin_samsung_init (MMPluginSamsung *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_samsung_class_init (MMPluginSamsungClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}

