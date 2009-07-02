/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 Ericsson AB
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <string.h>
#include <gmodule.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-mbm.h"
#include "mm-modem-mbm.h"

G_DEFINE_TYPE (MMPluginMbm, mm_plugin_mbm, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_MBM,
                                    MM_PLUGIN_BASE_NAME, "Ericsson MBM",
                                    NULL));
}

/*****************************************************************************/

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
    GUdevDevice *port, *physdev;
    guint32 cached = 0, level;
    const char *driver, *subsys;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);
    subsys = g_udev_device_get_subsystem (port);
    g_assert (subsys);

    if (strcmp (subsys, "tty") && strcmp (subsys, "net"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    driver = mm_plugin_base_supports_task_get_driver (task);
    if (!driver)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    physdev = mm_plugin_base_supports_task_get_physdev (task);
    g_assert (physdev);
    if (!g_udev_device_get_property_as_boolean (physdev, "ID_MM_ERICSSON_MBM"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (!strcmp (subsys, "net")) {
        mm_plugin_base_supports_task_complete (task, 10);
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

    if (mm_plugin_base_get_cached_port_capabilities (base, port, &cached)) {
        level = get_level_for_capabilities (cached);
        if (level) {
            mm_plugin_base_supports_task_complete (task, level);
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
        }
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    /* Otherwise kick off a probe */
    if (mm_plugin_base_probe_port (base, task, NULL))
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;

    return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
}

static MMModem *
grab_port (MMPluginBase *base,
           MMModem *existing,
           MMPluginBaseSupportsTask *task,
           GError **error)
{
    GUdevDevice *port = NULL, *physdev = NULL;
    MMModem *modem = NULL;
    const char *name, *subsys, *sysfs_path;
    guint32 caps;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    physdev = mm_plugin_base_supports_task_get_physdev (task);
    g_assert (physdev);
    sysfs_path = g_udev_device_get_sysfs_path (physdev);
    if (!sysfs_path) {
        g_set_error (error, 0, 0, "Could not get port's physical device sysfs path.");
        return NULL;
    }

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    if (!(caps & MM_PLUGIN_BASE_PORT_CAP_GSM) && strcmp (subsys, "net"))
        return NULL;

    if (!existing) {
        modem = mm_modem_mbm_new (sysfs_path,
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

/*****************************************************************************/

static void
mm_plugin_mbm_init (MMPluginMbm *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_mbm_class_init (MMPluginMbmClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
