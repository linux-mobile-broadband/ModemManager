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
#include <stdlib.h>
#include <gmodule.h>
#include <errno.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-huawei.h"
#include "mm-generic-gsm.h"
#include "mm-generic-cdma.h"
#include "mm-modem-huawei-gsm.h"
#include "mm-modem-huawei-cdma.h"
#include "mm-serial-parsers.h"
#include "mm-at-serial-port.h"
#include "mm-log.h"
#include "mm-errors.h"

G_DEFINE_TYPE (MMPluginHuawei, mm_plugin_huawei, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HUAWEI,
                                    MM_PLUGIN_BASE_NAME, "Huawei",
                                    NULL));
}

/*****************************************************************************/

#define TAG_HUAWEI_PCUI_PORT "huawei-pcui-port"
#define TAG_HUAWEI_MODEM_PORT "huawei-modem-port"
#define TAG_HUAWEI_NDIS_PORT "huawei-ndis-port"
#define TAG_HUAWEI_DIAG_PORT "huawei-diag-port"
#define TAG_GETPORTMODE_SUPPORTED "getportmode-supported"

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

static void
cache_port_mode (MMPlugin *plugin, const char *reply, const char *type, const char *tag)
{
    char *p;
    long i;

    /* Get the USB interface number of the PCUI port */
    p = strstr (reply, type);
    if (p) {
        errno = 0;
        /* shift by 1 so NULL return from g_object_get_data() means no tag */
        i = 1 + strtol (p + strlen (type), NULL, 10);
        if (i > 0 && i < 256 && errno == 0)
            g_object_set_data (G_OBJECT (plugin), tag, GINT_TO_POINTER ((int) i));
    }
}

static gboolean
getportmode_response_cb (MMPluginBaseSupportsTask *task,
                         GString *response,
                         GError *error,
                         guint32 tries,
                         gboolean *out_stop,
                         guint32 *out_level,
                         gpointer user_data)
{
    /* If any error occurred that was not ERROR or COMMAND NOT SUPPORT then
     * retry the command.
     */
    if (error) {
        if (g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_UNKNOWN) == FALSE)
            return tries <= 4 ? TRUE : FALSE;
    } else {
        MMPlugin *plugin = mm_plugin_base_supports_task_get_plugin (task);

        cache_port_mode (plugin, response->str, "PCUI:", TAG_HUAWEI_PCUI_PORT);
        cache_port_mode (plugin, response->str, "MDM:",  TAG_HUAWEI_MODEM_PORT);
        cache_port_mode (plugin, response->str, "NDIS:", TAG_HUAWEI_NDIS_PORT);
        cache_port_mode (plugin, response->str, "DIAG:", TAG_HUAWEI_DIAG_PORT);

        g_object_set_data (G_OBJECT (plugin), TAG_GETPORTMODE_SUPPORTED, GUINT_TO_POINTER (1));
    }

    /* No error or if ^GETPORTMODE is not supported, assume success */
    return FALSE;
}

static gboolean
curc_response_cb (MMPluginBaseSupportsTask *task,
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

#define TAG_DEFER_COUNTS_PREFIX "huawei-defer-counts"
#define MAX_DEFERS 5

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    const char *subsys, *name, *driver;
    int usbif;
    guint16 vendor = 0, product = 0;

    /* Can't do anything with non-serial ports */
    port = mm_plugin_base_supports_task_get_port (task);
    if (strcmp (g_udev_device_get_subsystem (port), "tty"))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    if (vendor != 0x12d1)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* The Gobi driver should claim Huawei Gobi modems */
    driver = mm_plugin_base_supports_task_get_driver (task);
    if (g_strcmp0 (driver, "qcserial") == 0)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    usbif = g_udev_device_get_property_as_int (port, "ID_USB_INTERFACE_NUM");
    if (usbif < 0)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* The primary port (called the "modem" port in the Windows drivers) is
     * always USB interface 0, and we need to detect that interface first for
     * two reasons: (1) to disable unsolicited messages on other ports that
     * may fill up the buffer and crash the device, and (2) to attempt to get
     * the port layout for hints about what the secondary port is (called the
     * "pcui" port in Windows).  Thus we probe USB interface 0 first and defer
     * probing other interfaces until we've got if0, at which point we allow
     * the other ports to be probed too.
     */
    if (!existing && usbif != 0) {
        gchar *tag;
        guint n_deferred;

        /* We need to defer the probing as usbif 0 wasn't probed yet. We need to
         * protect against the case of not having usbif 0 as an AT port, and we
         * do that by limiting the number of times we defer the probing. */

        tag = g_strdup_printf ("%s-%s/%s", TAG_DEFER_COUNTS_PREFIX, subsys, name);
        /* First time requested will be 0 */
        n_deferred = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (base), tag));
        if (n_deferred < MAX_DEFERS) {
            /* Update defer count */
            g_object_set_data (G_OBJECT (base), tag, GUINT_TO_POINTER (n_deferred + 1));
            g_free (tag);
            return MM_PLUGIN_SUPPORTS_PORT_DEFER;
        }

        mm_dbg ("(%s): no longer waiting for usbif 0, will launch probing in interface (%s/%s)",
                mm_plugin_get_name (MM_PLUGIN (base)), subsys, name);
        g_free (tag);

        /* Clear tag */
        g_object_set_data (G_OBJECT (base), tag, NULL);
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

    /* Turn off unsolicited messages on secondary ports until needed,
     * and try to get a port map from the modem.  The response will
     * get handled in custom_init_response().
     */
    if (usbif == 0) {
        mm_plugin_base_supports_task_add_custom_init_command (task,
                                                              "AT^CURC=0",
                                                              3,   /* delay */
                                                              curc_response_cb,
                                                              NULL);

        mm_plugin_base_supports_task_add_custom_init_command (task,
                                                              "AT^GETPORTMODE",
                                                              3,   /* delay */
                                                              getportmode_response_cb,
                                                              NULL);
    }

    /* Kick off a probe */
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
    const char *name, *subsys, *devfile, *sysfs_path;
    guint32 caps;
    guint16 vendor = 0, product = 0;
    MMPortType ptype;
    int usbif;
    MMAtPortFlags pflags = MM_AT_PORT_FLAG_NONE;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    devfile = g_udev_device_get_device_file (port);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's sysfs file.");
        return NULL;
    }

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        return NULL;
    }

    usbif = g_udev_device_get_property_as_int (port, "ID_USB_INTERFACE_NUM");
    if (usbif < 0) {
        g_set_error (error, 0, 0, "Could not get USB device interface number.");
        return NULL;
    }

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    ptype = mm_plugin_base_probed_capabilities_to_port_type (caps);

    if (usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (base), TAG_HUAWEI_PCUI_PORT)))
        pflags = MM_AT_PORT_FLAG_PRIMARY;
    else if (usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (base), TAG_HUAWEI_MODEM_PORT)))
        pflags = MM_AT_PORT_FLAG_PPP;
    else if (!g_object_get_data (G_OBJECT (base), TAG_HUAWEI_MODEM_PORT) &&
             usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (base), TAG_HUAWEI_NDIS_PORT)))
        /* If NDIS reported only instead of MDM, use it */
        pflags = MM_AT_PORT_FLAG_PPP;
    else if (!g_object_get_data (G_OBJECT (base), TAG_GETPORTMODE_SUPPORTED)) {
        /* If GETPORTMODE is not supported, we assume usbif 0 is the modem port */
        if ((usbif == 0) && (ptype == MM_PORT_TYPE_AT)) {
            pflags = MM_AT_PORT_FLAG_PPP;

            /* For CDMA modems we assume usbif0 is both primary and PPP, since
             * they don't have problems with talking on secondary ports.
             */
            if (caps & CAP_CDMA)
                pflags |= MM_AT_PORT_FLAG_PRIMARY;
        }
    }

    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        if (caps & MM_PLUGIN_BASE_PORT_CAP_GSM) {
            modem = mm_modem_huawei_gsm_new (sysfs_path,
                                             mm_plugin_base_supports_task_get_driver (task),
                                             mm_plugin_get_name (MM_PLUGIN (base)),
                                             vendor,
                                             product);
        } else if (caps & CAP_CDMA) {
            modem = mm_modem_huawei_cdma_new (sysfs_path,
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
    } else {
        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, pflags, NULL, error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

static void
mm_plugin_huawei_init (MMPluginHuawei *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_huawei_class_init (MMPluginHuaweiClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
