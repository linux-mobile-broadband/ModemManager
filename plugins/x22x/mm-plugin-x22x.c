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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-plugin-x22x.h"
#include "mm-broadband-modem-x22x.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

G_DEFINE_TYPE (MMPluginX22x, mm_plugin_x22x, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortSerialAt *port;
    guint retries;
} X22xCustomInitContext;

static void
x22x_custom_init_context_free (X22xCustomInitContext *ctx)
{
    g_object_unref (ctx->port);
    g_slice_free (X22xCustomInitContext, ctx);
}

static gboolean
x22x_custom_init_finish (MMPortProbe *probe,
                         GAsyncResult *result,
                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void x22x_custom_init_step (GTask *task);

static void
gmr_ready (MMPortSerialAt *port,
           GAsyncResult *res,
           GTask *task)
{
    const gchar *p;
    const gchar *response;
    GError *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        g_error_free (error);
        /* Just retry... */
        x22x_custom_init_step (task);
        return;
    }

    /* Note the lack of a ':' on the GMR; the X200 doesn't send one */
    p = mm_strip_tag (response, "AT+GMR");
    if (p && *p != 'L') {
        /* X200 modems have a GMR firmware revision that starts with 'L', and
         * as far as I can tell X060s devices have a revision starting with 'C'.
         * So use that to determine if the device is an X200, which this plugin
         * does supports.
         */
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Not supported with the X22X plugin");
    } else {
        mm_dbg ("(X22X) device is supported by this plugin");
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
x22x_custom_init_step (GTask *task)
{
    X22xCustomInitContext *ctx;
    GCancellable *cancellable;

    ctx = g_task_get_task_data (task);
    cancellable = g_task_get_cancellable (task);

    /* If cancelled, end */
    if (g_cancellable_is_cancelled (cancellable)) {
        mm_dbg ("(X22X) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (ctx->retries == 0) {
        /* In this case, we need the AT command result to decide whether we can
         * support this modem or not, so really fail if we didn't get it. */
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get device revision information");
        g_object_unref (task);
        return;
    }

    ctx->retries--;
    mm_port_serial_at_command (
        ctx->port,
        "AT+GMR",
        3,
        FALSE, /* raw */
        FALSE, /* allow_cached */
        cancellable,
        (GAsyncReadyCallback)gmr_ready,
        task);
}

static void
x22x_custom_init (MMPortProbe *probe,
                  MMPortSerialAt *port,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MMDevice *device;
    X22xCustomInitContext *ctx;
    GTask *task;

    ctx = g_slice_new (X22xCustomInitContext);
    ctx->port = g_object_ref (port);
    ctx->retries = 3;

    task = g_task_new (probe, cancellable, callback, user_data);
    g_task_set_check_cancellable (task, FALSE);
    g_task_set_task_data (task, ctx, (GDestroyNotify)x22x_custom_init_context_free);

    /* TCT/Alcatel in their infinite wisdom assigned the same USB VID/PID to
     * the x060s (Longcheer firmware) and the x200 (X22X, this plugin) and thus
     * we can't tell them apart via udev rules.  Worse, they both report the
     * same +GMM and +GMI, so we're left with just +GMR which is a sketchy way
     * to tell modems apart.  We can't really use X22X-specific commands
     * like AT+SSND because we're not sure if they work when the SIM PIN has not
     * been entered yet; many modems have a limited command parser before the
     * SIM is unlocked.
     */
    device = mm_port_probe_peek_device (probe);
    if (mm_device_get_vendor (device) != 0x1bbb ||
        mm_device_get_product (device) != 0x0000) {
        /* If not exactly this vendor/product, just skip */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    x22x_custom_init_step (task);
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
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered X22X modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_x22x_new (uid,
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
    MMKernelDevice *port;
    MMPortType ptype;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    port = mm_port_probe_peek_port (probe);
    ptype = mm_port_probe_get_port_type (probe);

    if (ptype == MM_PORT_TYPE_AT) {
        /* Look for port type hints; just probing can't distinguish which port should
         * be the data/primary port on these devices.  We have to tag them based on
         * what the Windows .INF files say the port layout should be.
         */
        if (mm_kernel_device_get_property_as_boolean (port, "ID_MM_X22X_PORT_TYPE_MODEM")) {
            mm_dbg ("x22x: AT port '%s/%s' flagged as primary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
        } else if (mm_kernel_device_get_property_as_boolean (port, "ID_MM_X22X_PORT_TYPE_AUX")) {
            mm_dbg ("x22x: AT port '%s/%s' flagged as secondary",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
        } else {
            /* If the port was tagged by the udev rules but isn't a primary or secondary,
             * then ignore it to guard against race conditions if a device just happens
             * to show up with more than two AT-capable ports.
             */
            ptype = MM_PORT_TYPE_IGNORED;
        }
    }

    return mm_base_modem_grab_port (modem,
                                    port,
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    /* Vendors: TAMobile and Olivetti */
    static const guint16 vendor_ids[] = { 0x1bbb, 0x0b3c, 0 };
    /* Only handle X22X tagged devices here. */
    static const gchar *udev_tags[] = {
        "ID_MM_X22X_TAGGED",
        NULL
    };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (x22x_custom_init),
        .finish = G_CALLBACK (x22x_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_X22X,
                      MM_PLUGIN_NAME,               "X22X",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_ALLOWED_UDEV_TAGS,  udev_tags,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      NULL));
}

static void
mm_plugin_x22x_init (MMPluginX22x *self)
{
}

static void
mm_plugin_x22x_class_init (MMPluginX22xClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
