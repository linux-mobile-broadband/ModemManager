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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-novatel.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-log.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

G_DEFINE_TYPE (MMPluginNovatel, mm_plugin_novatel, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* Custom commands for AT probing */

/* We need to explicitly flip secondary ports to AT mode.
 * We also use this command also for checking AT support in the current port.
 */
static const MMPortProbeAtCommand custom_at_probe[] = {
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { "$NWDMAT=1", 3, mm_port_probe_response_processor_is_at },
    { NULL }
};

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortProbe *probe;
    MMPortSerialAt *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint nwdmat_retries;
    guint wait_time;
} CustomInitContext;

static void
custom_init_context_complete_and_free (CustomInitContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (CustomInitContext, ctx);
}

static gboolean
novatel_custom_init_finish (MMPortProbe *probe,
                            GAsyncResult *result,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void custom_init_step (CustomInitContext *ctx);

static void
nwdmat_ready (MMPortSerialAt *port,
              GAsyncResult *res,
              CustomInitContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            custom_init_step (ctx);
            goto out;
        }

        mm_dbg ("(Novatel) Error flipping secondary ports to AT mode: %s", error->message);
    }

    /* Finish custom_init */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);

out:
    if (error)
        g_error_free (error);
}

static gboolean
custom_init_wait_cb (CustomInitContext *ctx)
{
    custom_init_step (ctx);
    return FALSE;
}

static void
custom_init_step (CustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Novatel) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

    /* If device has a QMI port, don't run $NWDMAT */
    if (mm_port_probe_list_has_qmi_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (ctx->probe)))) {
        mm_dbg ("(Novatel) no need to run custom init in (%s): device has QMI port",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

    if (ctx->wait_time > 0) {
        ctx->wait_time--;
        g_timeout_add_seconds (1, (GSourceFunc)custom_init_wait_cb, ctx);
        return;
    }

    if (ctx->nwdmat_retries > 0) {
        ctx->nwdmat_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "$NWDMAT=1",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)nwdmat_ready,
                                   ctx);
        return;
    }

    /* Finish custom_init */
    mm_dbg ("(Novatel) couldn't flip secondary port to AT in (%s): all retries consumed",
            mm_port_get_device (MM_PORT (ctx->port)));
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

static void
novatel_custom_init (MMPortProbe *probe,
                     MMPortSerialAt *port,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    CustomInitContext *ctx;

    ctx = g_slice_new (CustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             novatel_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->nwdmat_retries = 3;
    ctx->wait_time = 2;

    custom_init_step (ctx);
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
        mm_dbg ("QMI-powered Novatel modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_novatel_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const guint16 vendors[] = { 0x1410, /* Novatel */
                                       0x413c, /* Dell */
                                       0 };
    static const mm_uint16_pair forbidden_products[] = { { 0x1410, 0x9010 }, /* Novatel E362 */
                                                         { 0, 0 } };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (novatel_custom_init),
        .finish = G_CALLBACK (novatel_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_NOVATEL,
                      MM_PLUGIN_NAME,                  "Novatel",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,    subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,    vendors,
                      MM_PLUGIN_FORBIDDEN_PRODUCT_IDS, forbidden_products,
                      MM_PLUGIN_ALLOWED_AT,            TRUE,
                      MM_PLUGIN_CUSTOM_INIT,           &custom_init,
                      MM_PLUGIN_ALLOWED_QCDM,          TRUE,
                      MM_PLUGIN_ALLOWED_QMI,           TRUE,
                      NULL));
}

static void
mm_plugin_novatel_init (MMPluginNovatel *self)
{
}

static void
mm_plugin_novatel_class_init (MMPluginNovatelClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
}
