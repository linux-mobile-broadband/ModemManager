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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-cinterion.h"
#include "mm-modem-cinterion-gsm.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginCinterion, mm_plugin_cinterion, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_CINTERION,
                                    MM_PLUGIN_BASE_NAME, "Cinterion",
                                    MM_PLUGIN_BASE_SORT_LAST, TRUE,
                                    NULL));
}

static guint32
get_level_for_capabilities (guint32 capabilities)
{
    if (capabilities & MM_PLUGIN_BASE_PORT_CAP_GSM)
        return 10;
    return 0;
}

static gboolean
check_vendor_cinterion (MMPluginBaseSupportsTask *task)
{
    MMPluginBase *base;
    GUdevDevice *port;
    const char *subsys, *name;
    guint16 vendor = 0;

    base = MM_PLUGIN_BASE (mm_plugin_base_supports_task_get_plugin (task));
    port = mm_plugin_base_supports_task_get_port (task);

    /* Try to get device IDs from udev. Note that it is not an error
     * if we can't get them in our case, as we also support serial
     * modems. */
    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);
    mm_plugin_base_get_device_ids (base, subsys, name, &vendor, NULL);

    /* Vendors: Cinterion (0x1e2d)
     *          Siemens   (0x0681)
     */
    if (vendor == 0x1e2d || vendor == 0x0681) {
        mm_dbg ("Cinterion/Siemens USB modem detected");
        return TRUE;
    }

    /* We may get Cinterion modems connected in RS232 port, try to get
     * probed Vendor ID string to check */
    if (mm_plugin_base_supports_task_propagate_cached (task)) {
        const gchar *probed_vendor;
        gchar *probed_vendor_strdown;
        gboolean probed_vendor_correct = FALSE;

        probed_vendor = mm_plugin_base_supports_task_get_probed_vendor (task);
        if (!probed_vendor)
            return FALSE;

        /* Lowercase the vendor string and compare */
        probed_vendor_strdown = g_utf8_strdown (probed_vendor, -1);
        if (strstr (probed_vendor_strdown, "cinterion") ||
            strstr (probed_vendor_strdown, "siemens")) {
            mm_dbg ("Cinterion/Siemens RS232 modem detected");
            probed_vendor_correct = TRUE;
        }

        g_free (probed_vendor_strdown);

        return probed_vendor_correct;
    }

    return FALSE;
}

static void
probe_result (MMPluginBase *base,
              MMPluginBaseSupportsTask *task,
              guint32 capabilities,
              gpointer user_data)
{
    /* Note: the signal contains only capabilities, but we can also query the
     * probed vendor and product strings here. */

    /* Check vendor */
    mm_plugin_base_supports_task_complete (task,
                                           (check_vendor_cinterion (task) ?
                                            get_level_for_capabilities (capabilities) : 0));
}

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    guint32 cached = 0;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    if (strcmp (g_udev_device_get_subsystem (port), "tty"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* First thing to check in this plugin is if we got capabilities already.
     * This is because we have a later check of the probed vendor, which is
     * taken also during port probing.
     * Note that we also relaunch a port probe if we got a cached value but no
     * capabilities set (used when trying to detect RS232 modems during
     * re-scans). */
    if (!mm_plugin_base_supports_task_propagate_cached (task) ||
        !mm_plugin_base_supports_task_get_probed_capabilities (task)) {
        /* Kick off a probe */
        if (mm_plugin_base_probe_port (base, task, 100000, NULL))
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;

        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    /* Check vendor */
    if (!check_vendor_cinterion (task))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* Completed! */
    cached = mm_plugin_base_supports_task_get_probed_capabilities (task);
    mm_plugin_base_supports_task_complete (task, get_level_for_capabilities (cached));
    return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
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
    guint16 vendor = 0x1e2d;
    guint16 product = 0;
    MMPortType ptype;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    /* Try to get Product IDs from udev. Note that it is not an error
     * if we can't get them in our case, as we also support serial
     * modems. */
    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);
    mm_plugin_base_get_device_ids (base, subsys, name, NULL, &product);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    ptype = mm_plugin_base_probed_capabilities_to_port_type (caps);
    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        if (caps & MM_PLUGIN_BASE_PORT_CAP_GSM) {
            modem = mm_modem_cinterion_gsm_new (sysfs_path,
                                                mm_plugin_base_supports_task_get_driver (task),
                                                mm_plugin_get_name (MM_PLUGIN (base)),
                                                vendor,
                                                product);
        }

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, ptype, MM_AT_PORT_FLAG_NONE, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else if (get_level_for_capabilities (caps)) {
        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, MM_AT_PORT_FLAG_NONE, NULL, error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

static void
mm_plugin_cinterion_init (MMPluginCinterion *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_cinterion_class_init (MMPluginCinterionClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
