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

#define TAG_SUPPORTS_INFO "huawei-supports-info"

typedef struct {
    MMAtSerialPort *serial;
    guint id;
    MMPortType ptype;
    /* Whether or not there's already a detected modem that "owns" this port,
     * in which case we'll claim it, but if no capabilities are detected it'll
     * just be ignored.
     */
    gboolean parent_modem;
} HuaweiSupportsInfo;

static void
huawei_supports_info_destroy (gpointer user_data)
{
    HuaweiSupportsInfo *info = user_data;

    if (info->id)
        g_source_remove (info->id);
    if (info->serial)
        g_object_unref (info->serial);
    memset (info, 0, sizeof (HuaweiSupportsInfo));
    g_free (info);
}

static gboolean
probe_secondary_supported (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = user_data;
    HuaweiSupportsInfo *info;

    info = g_object_get_data (G_OBJECT (task), TAG_SUPPORTS_INFO);

    info->id = 0;
    g_object_unref (info->serial);
    info->serial = NULL;

    /* Yay, supported, we got an unsolicited message */
    info->ptype = MM_PORT_TYPE_SECONDARY;
    mm_plugin_base_supports_task_complete (task, 10);
    return FALSE;
}

static void
probe_secondary_handle_msg (MMAtSerialPort *port,
                            GMatchInfo *match_info,
                            gpointer user_data)
{
    MMPluginBaseSupportsTask *task = user_data;
    HuaweiSupportsInfo *info;

    info = g_object_get_data (G_OBJECT (task), TAG_SUPPORTS_INFO);
    g_source_remove (info->id);
    info->id = g_idle_add (probe_secondary_supported, task);
}

static gboolean
probe_secondary_timeout (gpointer user_data)
{
    MMPluginBaseSupportsTask *task = user_data;
    HuaweiSupportsInfo *info;
    guint level = 0;

    info = g_object_get_data (G_OBJECT (task), TAG_SUPPORTS_INFO);
    info->id = 0;
    g_object_unref (info->serial);
    info->serial = NULL;

    /* Supported, but ignored if this port's parent device is already a modem */
    if (info->parent_modem) {
        info->ptype = MM_PORT_TYPE_IGNORED;
        level = 10;
    }

    mm_plugin_base_supports_task_complete (task, level);
    return FALSE;
}

static void
add_regex (MMAtSerialPort *port, const char *match, gpointer user_data)
{
    GRegex *regex;

    regex = g_regex_new (match, G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, probe_secondary_handle_msg, user_data, NULL);
    g_regex_unref (regex);
}

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    guint32 cached = 0, level;
    const char *subsys, *name, *driver;
    int usbif;
    guint16 vendor = 0, product = 0;
    guint32 existing_type = MM_MODEM_TYPE_UNKNOWN;

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

    /* The secondary ports don't necessarily respond correctly to probing, so
     * we need to use the first port that does respond to probing to create the
     * right type of mode (GSM or CDMA), and then re-check the other interfaces.
     */
    if (!existing && usbif != 0)
        return MM_PLUGIN_SUPPORTS_PORT_DEFER;

    /* CDMA devices don't have problems with the secondary ports, so after
     * ensuring we have a device by probing the first port, probe the secondary
     * ports on CDMA devices too.
     */
    if (existing)
        g_object_get (G_OBJECT (existing), MM_MODEM_TYPE, &existing_type, NULL);

    if (usbif == 0 || (existing_type == MM_MODEM_TYPE_CDMA)) {
        if (mm_plugin_base_get_cached_port_capabilities (base, port, &cached)) {
            level = get_level_for_capabilities (cached);
            if (level) {
                mm_plugin_base_supports_task_complete (task, level);
                return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
            }
            return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
        }

        /* Otherwise kick off a probe */
        if (mm_plugin_base_probe_port (base, task, 100000, NULL))
            return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    } else {
        HuaweiSupportsInfo *info;
        GError *error = NULL;

        /* Listen for Huawei-specific unsolicited messages */
        info = g_malloc0 (sizeof (HuaweiSupportsInfo));
        info->parent_modem = !!existing;

        info->serial = mm_at_serial_port_new (name, MM_PORT_TYPE_PRIMARY);
        g_object_set (G_OBJECT (info->serial), MM_PORT_CARRIER_DETECT, FALSE, NULL);

        mm_at_serial_port_set_response_parser (info->serial,
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_new (),
                                               mm_serial_parser_v1_destroy);

        add_regex (info->serial, "\\r\\n\\^RSSI:(\\d+)\\r\\n", task);
        add_regex (info->serial, "\\r\\n\\^MODE:(\\d),(\\d)\\r\\n", task);
        add_regex (info->serial, "\\r\\n\\^DSFLOWRPT:(.+)\\r\\n", task);
        add_regex (info->serial, "\\r\\n\\^BOOT:.+\\r\\n", task);
        add_regex (info->serial, "\\r\\r\\^BOOT:.+\\r\\r", task);

        info->id = g_timeout_add_seconds (7, probe_secondary_timeout, task);

        if (!mm_serial_port_open (MM_SERIAL_PORT (info->serial), &error)) {
            mm_warn ("(Huawei) %s: couldn't open serial port: (%d) %s",
                     name,
                     error ? error->code : -1,
                     error && error->message ? error->message : "(unknown)");
            g_clear_error (&error);
            huawei_supports_info_destroy (info);
            return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
        }

        g_object_set_data_full (G_OBJECT (task), TAG_SUPPORTS_INFO,
                                info, huawei_supports_info_destroy);

        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

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

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
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
            if (!mm_modem_grab_port (modem, subsys, name, MM_PORT_TYPE_UNKNOWN, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else {
        HuaweiSupportsInfo *info;
        MMPortType ptype = MM_PORT_TYPE_UNKNOWN;

        info = g_object_get_data (G_OBJECT (task), TAG_SUPPORTS_INFO);
        if (info)
            ptype = info->ptype;
        else if (caps & MM_PLUGIN_BASE_PORT_CAP_QCDM)
            ptype = MM_PORT_TYPE_QCDM;

        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, NULL, error))
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
