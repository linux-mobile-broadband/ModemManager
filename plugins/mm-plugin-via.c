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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-via.h"
#include "mm-modem-via.h"
#include "mm-generic-cdma.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPluginVia, mm_plugin_via, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_VIA,
                                    MM_PLUGIN_BASE_NAME, "Via CBP7",
                                    MM_PLUGIN_BASE_SORT_LAST, TRUE,
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
    if (capabilities & CAP_CDMA)
        return 10;
    if (capabilities & MM_PLUGIN_BASE_PORT_CAP_AT)
        return 10;
    return 0;
}

static gboolean
check_vendor_via_cbp7 (MMPluginBaseSupportsTask *task)
{
    const gchar *product;
    gchar *lower = NULL;
    gboolean supported = FALSE;

    if (!mm_plugin_base_supports_task_propagate_cached (task))
        return FALSE;

    product = mm_plugin_base_supports_task_get_probed_product (task);
    if (product) {
        /* Lowercase the product string and compare */
        lower = g_utf8_strdown (product, -1);
        if (strstr (lower, "cbp7")) {
            mm_dbg ("China TeleCom CBP7x modem detected");
            supported = TRUE;
        } else if (strstr (lower, "2770p")) {
            mm_dbg ("Fusion Wireless 2770p modem detected");
            supported = TRUE;
        }
        g_free (lower);
    }
    return supported;
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
                                           (check_vendor_via_cbp7 (task) ?
                                            get_level_for_capabilities (capabilities) : 0));
}

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    guint16 vendor = 0;
    const char *subsys, *name;
    guint32 cached;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, NULL))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (strcmp (subsys, "tty"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* First thing to check in this plugin is if we got capabilities already.
     * This is because we have a later check of the probed vendor, which is
     * taken also during port probing.
     */
    if (!mm_plugin_base_supports_task_propagate_cached (task) ||
        !mm_plugin_base_supports_task_get_probed_capabilities (task)) {
        /* Kick off a probe */
        if (mm_plugin_base_probe_port (base, task, 100000, NULL))
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;

        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    /* Check vendor */
    if (!check_vendor_via_cbp7 (task))
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
    MMPortType ptype;
    guint16 vendor = 0, product = 0;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    if (!get_level_for_capabilities (caps)) {
        g_set_error (error, 0, 0, "Only CDMA modems are currently supported by this plugin.");
        return NULL;
    }

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    ptype = mm_plugin_base_probed_capabilities_to_port_type (caps);
    if (!existing) {
        if (caps & CAP_CDMA) {
            modem = mm_modem_via_new (sysfs_path,
                                      mm_plugin_base_supports_task_get_driver (task),
                                      mm_plugin_get_name (MM_PLUGIN (base)),
                                      !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856),
                                      !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856_A),
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
mm_plugin_via_init (MMPluginVia *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_via_class_init (MMPluginViaClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
