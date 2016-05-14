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
#include "mm-plugin-longcheer.h"
#include "mm-broadband-modem-longcheer.h"

G_DEFINE_TYPE (MMPluginLongcheer, mm_plugin_longcheer, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortProbe *probe;
    MMPortSerialAt *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint retries;
} LongcheerCustomInitContext;

static void
longcheer_custom_init_context_complete_and_free (LongcheerCustomInitContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (LongcheerCustomInitContext, ctx);
}

static gboolean
longcheer_custom_init_finish (MMPortProbe *probe,
                              GAsyncResult *result,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void longcheer_custom_init_step (LongcheerCustomInitContext *ctx);

static void
gmr_ready (MMPortSerialAt *port,
           GAsyncResult *res,
           LongcheerCustomInitContext *ctx)
{
    const gchar *p;
    const gchar *response;
    GError *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        /* Just retry... */
        longcheer_custom_init_step (ctx);
        goto out;
    }

    /* Note the lack of a ':' on the GMR; the X200 doesn't send one */
    p = mm_strip_tag (response, "AT+GMR");
    if (p && *p == 'L') {
        /* X200 modems have a GMR firmware revision that starts with 'L', and
         * as far as I can tell X060s devices have a revision starting with 'C'.
         * So use that to determine if the device is an X200, which this plugin
         * does not support since it uses a different chipset even though the
         * X060s and the X200 have the exact same USB VID and PID.
         */
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "X200 cannot be supported with the Longcheer plugin");
    } else {
        mm_dbg ("(Longcheer) device is not a X200");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    longcheer_custom_init_context_complete_and_free (ctx);

out:
    if (error)
        g_error_free (error);
}

static void
longcheer_custom_init_step (LongcheerCustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Longcheer) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        longcheer_custom_init_context_complete_and_free (ctx);
        return;
    }

    if (ctx->retries == 0) {
        /* In this case, we need the AT command result to decide whether we can
         * support this modem or not, so really fail if we didn't get it. */
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get device revision information");
        longcheer_custom_init_context_complete_and_free (ctx);
        return;
    }

    ctx->retries--;
    mm_port_serial_at_command (
        ctx->port,
        "AT+GMR",
        3,
        FALSE, /* raw */
        FALSE, /* allow_cached */
        ctx->cancellable,
        (GAsyncReadyCallback)gmr_ready,
        ctx);
}

static void
longcheer_custom_init (MMPortProbe *probe,
                       MMPortSerialAt *port,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    MMDevice *device;
    LongcheerCustomInitContext *ctx;

    ctx = g_slice_new (LongcheerCustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             longcheer_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->retries = 3;

    /* TCT/Alcatel in their infinite wisdom assigned the same USB VID/PID to
     * the x060s (Longcheer firmware) and the x200 (something else) and thus
     * we can't tell them apart via udev rules.  Worse, they both report the
     * same +GMM and +GMI, so we're left with just +GMR which is a sketchy way
     * to tell modems apart.  We can't really use Longcheer-specific commands
     * like AT+MODODR or AT+PSRAT because we're not sure if they work when the
     * SIM PIN has not been entered yet; many modems have a limited command
     * parser before the SIM is unlocked.
     */
    device = mm_port_probe_peek_device (probe);
    if (mm_device_get_vendor (device) != 0x1bbb ||
        mm_device_get_product (device) != 0x0000) {
        /* If not exactly this vendor/product, just skip */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        longcheer_custom_init_context_complete_and_free (ctx);
        return;
    }

    longcheer_custom_init_step (ctx);
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
    return MM_BASE_MODEM (mm_broadband_modem_longcheer_new (sysfs_path,
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
    GUdevDevice *port;
    MMPortType ptype;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

    port = mm_port_probe_peek_port (probe);
    ptype = mm_port_probe_get_port_type (probe);

    /* Look for port type hints; just probing can't distinguish which port should
     * be the data/primary port on these devices.  We have to tag them based on
     * what the Windows .INF files say the port layout should be.
     */
    if (g_udev_device_get_property_as_boolean (port, "ID_MM_LONGCHEER_PORT_TYPE_MODEM")) {
        mm_dbg ("longcheer: AT port '%s/%s' flagged as primary",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
    } else if (g_udev_device_get_property_as_boolean (port, "ID_MM_LONGCHEER_PORT_TYPE_AUX")) {
        mm_dbg ("longcheer: AT port '%s/%s' flagged as secondary",
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
    static const gchar *subsystems[] = { "tty", NULL };
    /* Vendors: Longcheer and TAMobile */
    static const guint16 vendor_ids[] = { 0x1c9e, 0x1bbb, 0 };
    /* Some TAMobile devices are different chipsets and should be handled
     * by other plugins, so only handle LONGCHEER tagged devices here.
     */
    static const gchar *udev_tags[] = {
        "ID_MM_LONGCHEER_TAGGED",
        NULL
    };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (longcheer_custom_init),
        .finish = G_CALLBACK (longcheer_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_LONGCHEER,
                      MM_PLUGIN_NAME,               "Longcheer",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_UDEV_TAGS,  udev_tags,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      NULL));
}

static void
mm_plugin_longcheer_init (MMPluginLongcheer *self)
{
}

static void
mm_plugin_longcheer_class_init (MMPluginLongcheerClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
