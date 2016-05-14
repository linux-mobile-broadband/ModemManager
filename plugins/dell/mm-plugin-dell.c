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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin-dell.h"
#include "mm-common-novatel.h"
#include "mm-private-boxed-types.h"
#include "mm-broadband-modem.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-common-novatel.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-common-sierra.h"
#include "mm-broadband-modem-telit.h"
#include "mm-common-telit.h"
#include "mm-log.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginDell, mm_plugin_dell, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

#define TAG_DELL_MANUFACTURER   "dell-manufacturer"
typedef enum {
    DELL_MANUFACTURER_UNKNOWN  = 0,
    DELL_MANUFACTURER_NOVATEL  = 1,
    DELL_MANUFACTURER_SIERRA   = 2,
    DELL_MANUFACTURER_ERICSSON = 3,
    DELL_MANUFACTURER_TELIT    = 4
} DellManufacturer;

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortProbe *probe;
    MMPortSerialAt *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint gmi_retries;
    guint cgmi_retries;
    guint ati_retries;
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
dell_custom_init_finish (MMPortProbe *probe,
                         GAsyncResult *result,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
novatel_custom_init_ready (MMPortProbe       *probe,
                           GAsyncResult      *res,
                           CustomInitContext *ctx)
{
    GError *error = NULL;

    if (!mm_common_novatel_custom_init_finish (probe, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

static void
sierra_custom_init_ready (MMPortProbe       *probe,
                          GAsyncResult      *res,
                          CustomInitContext *ctx)
{
    GError *error = NULL;

    if (!mm_common_sierra_custom_init_finish (probe, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

static void
telit_custom_init_ready (MMPortProbe       *probe,
                         GAsyncResult      *res,
                         CustomInitContext *ctx)
{
    GError *error = NULL;

    if (!telit_custom_init_finish (probe, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

static void custom_init_step (CustomInitContext *ctx);

static void
custom_init_step_next_command (CustomInitContext *ctx)
{
    if (ctx->gmi_retries > 0)
        ctx->gmi_retries = 0;
    else if (ctx->cgmi_retries > 0)
        ctx->cgmi_retries = 0;
    else if (ctx->ati_retries > 0)
        ctx->ati_retries = 0;
    custom_init_step (ctx);
}

static void
response_ready (MMPortSerialAt *port,
                GAsyncResult *res,
                CustomInitContext *ctx)
{
    const gchar *response;
    GError *error = NULL;
    gchar *lower;
    DellManufacturer manufacturer;

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        /* Non-timeout error, jump to next command */
        if (!g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            mm_dbg ("(Dell) Error probing AT port: %s", error->message);
            g_error_free (error);
            custom_init_step_next_command (ctx);
            return;
        }
        /* Directly retry same command on timeout */
        custom_init_step (ctx);
        g_error_free (error);
        return;
    }

    /* Guess manufacturer from response */
    lower = g_ascii_strdown (response, -1);
    if (strstr (lower, "novatel"))
        manufacturer = DELL_MANUFACTURER_NOVATEL;
    else if (strstr (lower, "sierra"))
        manufacturer = DELL_MANUFACTURER_SIERRA;
    else if (strstr (lower, "ericsson"))
        manufacturer = DELL_MANUFACTURER_ERICSSON;
    else if (strstr (lower, "telit"))
        manufacturer = DELL_MANUFACTURER_TELIT;
    else
        manufacturer = DELL_MANUFACTURER_UNKNOWN;
    g_free (lower);

    /* Tag based on manufacturer */
    if (manufacturer != DELL_MANUFACTURER_UNKNOWN) {
        g_object_set_data (G_OBJECT (ctx->probe), TAG_DELL_MANUFACTURER, GUINT_TO_POINTER (manufacturer));

        /* Run additional custom init, if needed */

        if (manufacturer == DELL_MANUFACTURER_NOVATEL) {
            mm_common_novatel_custom_init (ctx->probe,
                                           ctx->port,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback) novatel_custom_init_ready,
                                           ctx);
            return;
        }

        if (manufacturer == DELL_MANUFACTURER_SIERRA) {
            mm_common_sierra_custom_init (ctx->probe,
                                          ctx->port,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback) sierra_custom_init_ready,
                                          ctx);
            return;
        }

        if (manufacturer == DELL_MANUFACTURER_TELIT) {
            telit_custom_init (ctx->probe,
                               ctx->port,
                               ctx->cancellable,
                               (GAsyncReadyCallback) telit_custom_init_ready,
                               ctx);
            return;
        }

        /* Finish custom_init */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

    /* If we got a response, but we didn't get an expected string, try with next command */
    custom_init_step_next_command (ctx);
}

static void
custom_init_step (CustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Dell) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

#if defined WITH_QMI
    /* If device has a QMI port, don't run anything else, as we don't care */
    if (mm_port_probe_list_has_qmi_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (ctx->probe)))) {
        mm_dbg ("(Dell) no need to run custom init in (%s): device has QMI port",
                mm_port_get_device (MM_PORT (ctx->port)));
        mm_port_probe_set_result_at (ctx->probe, FALSE);
        mm_port_probe_set_result_qcdm (ctx->probe, FALSE);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }
#endif

#if defined WITH_MBIM
    /* If device has a MBIM port, don't run anything else, as we don't care */
    if (mm_port_probe_list_has_mbim_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (ctx->probe)))) {
        mm_dbg ("(Dell) no need to run custom init in (%s): device has MBIM port",
                mm_port_get_device (MM_PORT (ctx->port)));
        mm_port_probe_set_result_at (ctx->probe, FALSE);
        mm_port_probe_set_result_qcdm (ctx->probe, FALSE);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }
#endif

    if (ctx->gmi_retries > 0) {
        ctx->gmi_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "AT+GMI",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)response_ready,
                                   ctx);
        return;
    }

    if (ctx->cgmi_retries > 0) {
        ctx->cgmi_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "AT+CGMI",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)response_ready,
                                   ctx);
        return;
    }

    if (ctx->ati_retries > 0) {
        ctx->ati_retries--;
        /* Note: in Ericsson devices, ATI3 seems to reply the vendor string */
        mm_port_serial_at_command (ctx->port,
                                   "ATI1I2I3",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)response_ready,
                                   ctx);
        return;
    }

    /* Finish custom_init */
    mm_dbg ("(Dell) couldn't flip secondary port to AT in (%s): all retries consumed",
            mm_port_get_device (MM_PORT (ctx->port)));
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

static void
dell_custom_init (MMPortProbe *probe,
                  MMPortSerialAt *port,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    CustomInitContext *ctx;
    GUdevDevice *udevDevice;

    udevDevice = mm_port_probe_peek_port (probe);

    ctx = g_slice_new0 (CustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             dell_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->gmi_retries = 3;
    ctx->cgmi_retries = 3;
    ctx->ati_retries = 3;

    /* Dell-branded Telit modems always answer to +GMI
     * Avoid +CGMI and ATI sending for minimizing port probing time */
    if (g_udev_device_get_property_as_boolean (udevDevice, "ID_MM_TELIT_PORTS_TAGGED")) {
        ctx->cgmi_retries = 0;
        ctx->ati_retries = 0;
    }

    custom_init_step (ctx);
}

/*****************************************************************************/

static gboolean
port_probe_list_has_manufacturer_port (GList *probes,
                                       DellManufacturer manufacturer)
{
    GList *l;

    for (l = probes; l; l = g_list_next (l)) {
        if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (l->data), TAG_DELL_MANUFACTURER)) == manufacturer)
            return TRUE;
    }
    return FALSE;
}

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    /* Note: at this point we don't make any difference between different
     * Dell-branded QMI or MBIM modems; they may come from Novatel, Ericsson or
     * Sierra. */

#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_dbg ("MBIM-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (sysfs_path,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_NOVATEL)) {
        mm_dbg ("Novatel-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_novatel_new (sysfs_path,
                                                              drivers,
                                                              mm_plugin_get_name (self),
                                                              vendor,
                                                              product));
    }

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_SIERRA)) {
        mm_dbg ("Sierra-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_sierra_new (sysfs_path,
                                                             drivers,
                                                             mm_plugin_get_name (self),
                                                             vendor,
                                                             product));
    }

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_TELIT)) {
        mm_dbg ("Telit-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_telit_new (sysfs_path,
                                                            drivers,
                                                            mm_plugin_get_name (self),
                                                            vendor,
                                                            product));
    }

    mm_dbg ("Dell-branded generic modem found...");
    return MM_BASE_MODEM (mm_broadband_modem_new (sysfs_path,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    if (MM_IS_BROADBAND_MODEM_SIERRA (modem))
        return mm_common_sierra_grab_port (self, modem, probe, error);

    if (MM_IS_BROADBAND_MODEM_TELIT (modem))
        return telit_grab_port (self, modem, probe, error);

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_parent_path (probe),
                                    mm_port_probe_get_port_type (probe),
                                    MM_PORT_SERIAL_AT_FLAG_NONE,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const guint16 vendors[] = { 0x413c, 0 };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (dell_custom_init),
        .finish = G_CALLBACK (dell_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_DELL,
                      MM_PLUGIN_NAME,                  "Dell",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS,    subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS,    vendors,
                      MM_PLUGIN_ALLOWED_AT,            TRUE,
                      MM_PLUGIN_CUSTOM_INIT,           &custom_init,
                      MM_PLUGIN_ALLOWED_QCDM,          TRUE,
                      MM_PLUGIN_ALLOWED_QMI,           TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,          TRUE,
                      NULL));
}

static void
mm_plugin_dell_init (MMPluginDell *self)
{
}

static void
mm_plugin_dell_class_init (MMPluginDellClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port    = grab_port;
}
