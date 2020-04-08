/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your hso) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-private-boxed-types.h"
#include "mm-plugin-hso.h"
#include "mm-broadband-modem-hso.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPluginHso, mm_plugin_hso, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom init */

#define TAG_HSO_AT_CONTROL     "hso-at-control"
#define TAG_HSO_AT_APP         "hso-at-app"
#define TAG_HSO_AT_MODEM       "hso-at-modem"
#define TAG_HSO_AT_GPS_CONTROL "hso-at-gps-control"
#define TAG_HSO_GPS            "hso-gps"
#define TAG_HSO_DIAG           "hso-diag"

static gboolean
hso_custom_init_finish (MMPortProbe *probe,
                        GAsyncResult *result,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
hso_custom_init (MMPortProbe *probe,
                 MMPortSerialAt *port,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    MMKernelDevice *kernel_port;
    GTask *task;
    const gchar *subsys, *sysfs_path;

    subsys = mm_port_probe_get_port_subsys (probe);
    kernel_port = mm_port_probe_peek_port (probe);
    sysfs_path = mm_kernel_device_get_sysfs_path (kernel_port);

    if (g_str_equal (subsys, "tty")) {
        gchar *hsotype_path;
        gchar *contents = NULL;

        hsotype_path = g_build_filename (sysfs_path, "hsotype", NULL);
        if (g_file_get_contents (hsotype_path, &contents, NULL, NULL)) {
            mm_obj_dbg (probe, "HSO port type %s: %s", hsotype_path, contents);
            if (g_str_has_prefix (contents, "Control")) {
                g_object_set_data (G_OBJECT (probe), TAG_HSO_AT_CONTROL, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, TRUE);
            } else if (g_str_has_prefix (contents, "Application")) {
                g_object_set_data (G_OBJECT (probe), TAG_HSO_AT_APP, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, TRUE);
            } else if (g_str_has_prefix (contents, "Modem")) {
                g_object_set_data (G_OBJECT (probe), TAG_HSO_AT_MODEM, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, TRUE);
            } else if (g_str_has_prefix (contents, "GPS Control")) {
                g_object_set_data (G_OBJECT (probe), TAG_HSO_AT_GPS_CONTROL, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, TRUE);
            } else if (g_str_has_prefix (contents, "GPS")) {
                /* Not an AT port, but the port to grab GPS traces */
                g_object_set_data (G_OBJECT (probe), TAG_HSO_GPS, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, FALSE);
                mm_port_probe_set_result_qcdm (probe, FALSE);
            } else if (g_str_has_prefix (contents, "Diag")) {
                g_object_set_data (G_OBJECT (probe), TAG_HSO_DIAG, GUINT_TO_POINTER (TRUE));
                mm_port_probe_set_result_at (probe, FALSE);

                /* Don't automatically tag as QCDM, as the 'hso' driver reports
                 * a DIAG port for some Icera-based modems, which don't have
                 * QCDM ports since they aren't made by Qualcomm.
                 */
            }
            g_free (contents);
        }
        g_free (hsotype_path);
    }

    task = g_task_new (probe, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_hso_new (uid,
                                                      drivers,
                                                      mm_plugin_get_name (self),
                                                      vendor,
                                                      product));
}

static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    const gchar *subsys;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;
    MMPortType port_type;

    subsys = mm_port_probe_get_port_subsys (probe);
    port_type = mm_port_probe_get_port_type (probe);

    /* Detect AT port types */
    if (g_str_equal (subsys, "tty")) {
        if (g_object_get_data (G_OBJECT (probe), TAG_HSO_AT_CONTROL))
            pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
        else if (g_object_get_data (G_OBJECT (probe), TAG_HSO_AT_APP))
            pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
        else if (g_object_get_data (G_OBJECT (probe), TAG_HSO_AT_GPS_CONTROL))
            pflags = MM_PORT_SERIAL_AT_FLAG_GPS_CONTROL;
        else if (g_object_get_data (G_OBJECT (probe), TAG_HSO_AT_MODEM))
            pflags = MM_PORT_SERIAL_AT_FLAG_PPP;
        else if (g_object_get_data (G_OBJECT (probe), TAG_HSO_GPS)) {
            /* Not an AT port, but the port to grab GPS traces */
            g_assert (port_type == MM_PORT_TYPE_UNKNOWN);
            port_type = MM_PORT_TYPE_GPS;
        }
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_peek_port (probe),
                                    port_type,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const gchar *drivers[] = { "hso", NULL };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (hso_custom_init),
        .finish = G_CALLBACK (hso_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_HSO,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      MM_PLUGIN_SEND_DELAY,         (guint64) 0,
                      NULL));
}

static void
mm_plugin_hso_init (MMPluginHso *self)
{
}

static void
mm_plugin_hso_class_init (MMPluginHsoClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
