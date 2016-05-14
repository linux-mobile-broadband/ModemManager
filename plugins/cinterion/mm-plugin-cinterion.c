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
 * Copyright (C) 2011 - 2012 Google Inc.
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-cinterion.h"
#include "mm-broadband-modem-cinterion.h"
#include "mm-log.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi-cinterion.h"
#endif

G_DEFINE_TYPE (MMPluginCinterion, mm_plugin_cinterion, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom init */

#define TAG_CINTERION_APP_PORT   "cinterion-app-port"
#define TAG_CINTERION_MODEM_PORT "cinterion-modem-port"

typedef struct {
    MMPortProbe *probe;
    MMPortSerialAt *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
} CinterionCustomInitContext;

static void
cinterion_custom_init_context_complete_and_free (CinterionCustomInitContext *ctx)
{
    g_simple_async_result_complete (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (CinterionCustomInitContext, ctx);
}

static gboolean
cinterion_custom_init_finish (MMPortProbe *probe,
                           GAsyncResult *result,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
sqport_ready (MMPortSerialAt *port,
              GAsyncResult *res,
              CinterionCustomInitContext *ctx)
{
    const gchar *response;

    /* Ignore errors, just avoid tagging */
    response = mm_port_serial_at_command_finish (port, res, NULL);
    if (response) {
        /* A valid reply to AT^SQPORT tells us this is an AT port already */
        mm_port_probe_set_result_at (ctx->probe, TRUE);

        if (strstr (response, "Application"))
            g_object_set_data (G_OBJECT (ctx->probe), TAG_CINTERION_APP_PORT, GUINT_TO_POINTER (TRUE));
        else if (strstr (response, "Modem"))
            g_object_set_data (G_OBJECT (ctx->probe), TAG_CINTERION_MODEM_PORT, GUINT_TO_POINTER (TRUE));
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    cinterion_custom_init_context_complete_and_free (ctx);
}

static void
cinterion_custom_init (MMPortProbe *probe,
                       MMPortSerialAt *port,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    CinterionCustomInitContext *ctx;

    ctx = g_slice_new (CinterionCustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             cinterion_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    mm_port_serial_at_command (
        ctx->port,
        "AT^SQPORT?",
        3,
        FALSE, /* raw */
        FALSE, /* allow cached */
        ctx->cancellable,
        (GAsyncReadyCallback)sqport_ready,
        ctx);
}

/*****************************************************************************/

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered Cinterion modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_cinterion_new (sysfs_path,
                                                                    drivers,
                                                                    mm_plugin_get_name (self),
                                                                    vendor,
                                                                    product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_cinterion_new (sysfs_path,
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
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;
    MMPortType ptype;

    ptype = mm_port_probe_get_port_type (probe);

    if (g_object_get_data (G_OBJECT (probe), TAG_CINTERION_APP_PORT)) {
        mm_dbg ("(%s/%s)' Port flagged as primary",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
    } else if (g_object_get_data (G_OBJECT (probe), TAG_CINTERION_MODEM_PORT)) {
        mm_dbg ("(%s/%s)' Port flagged as PPP",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = MM_PORT_SERIAL_AT_FLAG_PPP;
    } else if (g_udev_device_get_property_as_boolean (mm_port_probe_peek_port (probe),
                                                      "ID_MM_CINTERION_PORT_TYPE_GPS")) {
        mm_dbg ("(%s/%s)' Port flagged as GPS",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        /* Not an AT port, but the port to grab GPS traces */
        g_warn_if_fail (ptype == MM_PORT_TYPE_UNKNOWN);
        ptype = MM_PORT_TYPE_GPS;
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_parent_path (probe),
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const gchar *vendor_strings[] = { "cinterion", "siemens", NULL };
    static const guint16 vendor_ids[] = { 0x1e2d, 0x0681, 0 };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (cinterion_custom_init),
        .finish = G_CALLBACK (cinterion_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_CINTERION,
                      MM_PLUGIN_NAME,                   "Cinterion",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,     subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_STRINGS, vendor_strings,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,     vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,             TRUE,
                      MM_PLUGIN_ALLOWED_QMI,            TRUE,
                      MM_PLUGIN_CUSTOM_INIT,            &custom_init,
                      NULL));
}

static void
mm_plugin_cinterion_init (MMPluginCinterion *self)
{
}

static void
mm_plugin_cinterion_class_init (MMPluginCinterionClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
