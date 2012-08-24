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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-sierra.h"
#include "mm-modem-sierra-gsm.h"
#include "mm-modem-sierra-cdma.h"

G_DEFINE_TYPE (MMPluginSierra, mm_plugin_sierra, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_SIERRA,
                                    MM_PLUGIN_BASE_NAME, "Sierra",
                                    NULL));
}

/*****************************************************************************/

#define TAG_SIERRA_APP1_PORT      "sierra-app1-port"
#define TAG_SIERRA_APP_PPP_OK     "sierra-app-ppp-ok"

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
    return 0;
}

static void
handle_probe_response (MMPluginBase *self,
                       MMPluginBaseSupportsTask *task,
                       const char *cmd,
                       const char *response,
                       const GError *error)
{
    if (error || !response || strcmp (cmd, "I")) {
        MM_PLUGIN_BASE_CLASS (mm_plugin_sierra_parent_class)->handle_probe_response (self, task, cmd, response, error);
        return;
    }

    if (strstr (response, "APP1")) {
        g_object_set_data (G_OBJECT (task), TAG_SIERRA_APP1_PORT, GUINT_TO_POINTER (TRUE));

        /* 885 can handle PPP on the APP ports, leaving the primary port open
         * for command and status while connected.  Older modems (ie 8775) say
         * they can but fail during PPP.
         */
        if (strstr (response, "C885"))
            g_object_set_data (G_OBJECT (task), TAG_SIERRA_APP_PPP_OK, GUINT_TO_POINTER (TRUE));

        mm_plugin_base_supports_task_complete (task, 10);
        return;
    }

    /* Not an app port, let the superclass handle the response */
    MM_PLUGIN_BASE_CLASS (mm_plugin_sierra_parent_class)->handle_probe_response (self, task, cmd, response, error);
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
    const char *driver, *subsys;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    if (!port)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    driver = mm_plugin_base_supports_task_get_driver (task);
    if (!driver || (strcmp (driver, "sierra") && strcmp (driver, "sierra_net")))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    subsys = g_udev_device_get_subsystem (port);
    g_assert (subsys);
    if (!strcmp (subsys, "net")) {
        /* Can't grab the net port until we know whether this is a CDMA or GSM device */
        if (!existing)
            return MM_PLUGIN_SUPPORTS_PORT_DEFER;

        mm_plugin_base_supports_task_complete (task, 10);
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

    /* Check if a previous probing was already launched in this port */
    if (mm_plugin_base_supports_task_propagate_cached (task)) {
        guint32 level;

        /* A previous probing was already done, use its results */
        level = get_level_for_capabilities (mm_plugin_base_supports_task_get_probed_capabilities (task));
        if (level) {
            mm_plugin_base_supports_task_complete (task, level);
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
        }
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    /* Otherwise kick off a probe */
    if (mm_plugin_base_probe_port_leave_echo (base, task, 100000, NULL))
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
    MMPortType ptype;
    guint16 vendor = 0, product = 0;
    MMAtPortFlags pflags = MM_AT_PORT_FLAG_NONE;
    gboolean sierra_app1_port;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    ptype = mm_plugin_base_probed_capabilities_to_port_type (caps);

    /* Is it a GSM secondary port? */
    sierra_app1_port = !!g_object_get_data (G_OBJECT (task), TAG_SIERRA_APP1_PORT);
    if (sierra_app1_port) {
        if (g_object_get_data (G_OBJECT (task), TAG_SIERRA_APP_PPP_OK))
            pflags = MM_AT_PORT_FLAG_PPP;
        else
            pflags = MM_AT_PORT_FLAG_SECONDARY;

        /* Secondary ports may not be tagged as AT since they only speak a
         * limited command set.  But we know they're AT if they are tagged
         * as secondary ports.
         */
        ptype = MM_PORT_TYPE_AT;
    } else if (ptype == MM_PORT_TYPE_AT)
        pflags = MM_AT_PORT_FLAG_PRIMARY;

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        if ((caps & MM_PLUGIN_BASE_PORT_CAP_GSM) || sierra_app1_port) {
            modem = mm_modem_sierra_gsm_new (sysfs_path,
                                             mm_plugin_base_supports_task_get_driver (task),
                                             mm_plugin_get_name (MM_PLUGIN (base)),
                                             vendor,
                                             product);
        } else if (caps & CAP_CDMA) {
            modem = mm_modem_sierra_cdma_new (sysfs_path,
                                              mm_plugin_base_supports_task_get_driver (task),
                                              mm_plugin_get_name (MM_PLUGIN (base)),
                                              !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856),
                                              !!(caps & MM_PLUGIN_BASE_PORT_CAP_IS856_A),
                                              vendor,
                                              product);
        }

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, ptype, pflags, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else if (   get_level_for_capabilities (caps)
               || (ptype != MM_PORT_TYPE_UNKNOWN)
               || (strcmp (subsys, "net") == 0)) {

        /* FIXME: we don't yet know how to activate IP on CDMA devices using
         * pseudo-ethernet ports.
         */
        if (strcmp (subsys, "net") == 0 && MM_IS_MODEM_SIERRA_CDMA (modem))
            return modem;

        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, pflags, NULL, error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

static void
mm_plugin_sierra_init (MMPluginSierra *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_sierra_class_init (MMPluginSierraClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
    pb_class->handle_probe_response = handle_probe_response;
}
