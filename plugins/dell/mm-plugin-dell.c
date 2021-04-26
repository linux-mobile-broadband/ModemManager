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
 * Copyright (C) 2015-2019 Aleksander Morgado <aleksander@aleksander.es>
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
#include "mm-broadband-modem-xmm.h"
#include "mm-common-telit.h"
#include "mm-log-object.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#include "mm-broadband-modem-mbim-xmm.h"
#include "mm-broadband-modem-mbim-foxconn.h"
#endif

#define MAX_PORT_PROBE_TIMEOUTS 3

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
    MMPortSerialAt *port;
    guint           gmi_retries;
    guint           cgmi_retries;
    guint           ati_retries;
    guint           timeouts;
} CustomInitContext;

static void
custom_init_context_free (CustomInitContext *ctx)
{
    g_object_unref (ctx->port);
    g_slice_free (CustomInitContext, ctx);
}

static gboolean
dell_custom_init_finish (MMPortProbe   *probe,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
novatel_custom_init_ready (MMPortProbe  *probe,
                           GAsyncResult *res,
                           GTask        *task)
{
    GError *error = NULL;

    if (!mm_common_novatel_custom_init_finish (probe, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
sierra_custom_init_ready (MMPortProbe  *probe,
                          GAsyncResult *res,
                          GTask        *task)
{
    GError *error = NULL;

    if (!mm_common_sierra_custom_init_finish (probe, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
telit_custom_init_ready (MMPortProbe  *probe,
                         GAsyncResult *res,
                         GTask        *task)
{
    GError *error = NULL;

    if (!telit_custom_init_finish (probe, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void custom_init_step (GTask *task);

static void
custom_init_step_next_command (GTask *task)
{
    CustomInitContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->timeouts = 0;
    if (ctx->gmi_retries > 0)
        ctx->gmi_retries = 0;
    else if (ctx->cgmi_retries > 0)
        ctx->cgmi_retries = 0;
    else if (ctx->ati_retries > 0)
        ctx->ati_retries = 0;
    custom_init_step (task);
}

static void
response_ready (MMPortSerialAt *port,
                GAsyncResult   *res,
                GTask          *task)
{
    CustomInitContext *ctx;
    MMPortProbe       *probe;
    const gchar       *response;
    GError            *error = NULL;
    gchar             *lower;
    DellManufacturer   manufacturer;

    probe = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        /* Non-timeout error, jump to next command */
        if (!g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            mm_obj_dbg (probe, "error probing AT port: %s", error->message);
            g_error_free (error);
            custom_init_step_next_command (task);
            return;
        }
        /* Directly retry same command on timeout */
        ctx->timeouts++;
        custom_init_step (task);
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
        g_object_set_data (G_OBJECT (probe), TAG_DELL_MANUFACTURER, GUINT_TO_POINTER (manufacturer));

        /* Run additional custom init, if needed */

        if (manufacturer == DELL_MANUFACTURER_NOVATEL) {
            mm_common_novatel_custom_init (probe,
                                           ctx->port,
                                           g_task_get_cancellable (task),
                                           (GAsyncReadyCallback) novatel_custom_init_ready,
                                           task);
            return;
        }

        if (manufacturer == DELL_MANUFACTURER_SIERRA) {
            mm_common_sierra_custom_init (probe,
                                          ctx->port,
                                          g_task_get_cancellable (task),
                                          (GAsyncReadyCallback) sierra_custom_init_ready,
                                          task);
            return;
        }

        if (manufacturer == DELL_MANUFACTURER_TELIT) {
            telit_custom_init (probe,
                               ctx->port,
                               g_task_get_cancellable (task),
                               (GAsyncReadyCallback) telit_custom_init_ready,
                               task);
            return;
        }

        /* Finish custom_init */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* If we got a response, but we didn't get an expected string, try with next command */
    custom_init_step_next_command (task);
}

static void
custom_init_step (GTask *task)
{
    CustomInitContext *ctx;
    MMPortProbe       *probe;

    probe = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* If cancelled, end without error right away */
    if (g_cancellable_is_cancelled (g_task_get_cancellable (task))) {
        mm_obj_dbg (probe, "no need to keep on running custom init: cancelled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

#if defined WITH_QMI
    /* If device has a QMI port, don't run anything else, as we don't care */
    if (mm_port_probe_list_has_qmi_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (probe)))) {
        mm_obj_dbg (probe, "no need to run custom init: device has QMI port");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
#endif

#if defined WITH_MBIM
    /* If device has a MBIM port, don't run anything else, as we don't care */
    if (mm_port_probe_list_has_mbim_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (probe)))) {
        mm_obj_dbg (probe, "no need to run custom init: device has MBIM port");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
#endif

    if (ctx->timeouts >= MAX_PORT_PROBE_TIMEOUTS) {
        mm_obj_dbg (probe, "couldn't detect real manufacturer: too many timeouts");
        mm_port_probe_set_result_at (probe, FALSE);
        goto out;
    }

    if (ctx->gmi_retries > 0) {
        ctx->gmi_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "AT+GMI",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback)response_ready,
                                   task);
        return;
    }

    if (ctx->cgmi_retries > 0) {
        ctx->cgmi_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "AT+CGMI",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback)response_ready,
                                   task);
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
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback)response_ready,
                                   task);
        return;
    }

    mm_obj_dbg (probe, "couldn't detect real manufacturer: all retries consumed");
out:
    /* Finish custom_init */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
dell_custom_init (MMPortProbe         *probe,
                  MMPortSerialAt      *port,
                  GCancellable        *cancellable,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    GTask             *task;
    CustomInitContext *ctx;

    ctx = g_slice_new0 (CustomInitContext);
    ctx->port = g_object_ref (port);
    ctx->gmi_retries = 3;
    ctx->cgmi_retries = 1;
    ctx->ati_retries = 1;

    task = g_task_new (probe, cancellable, callback, user_data);
    g_task_set_check_cancellable (task, FALSE);
    g_task_set_task_data (task, ctx, (GDestroyNotify) custom_init_context_free);

    custom_init_step (task);
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
              const gchar *uid,
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
        mm_obj_dbg (self, "QMI-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        /* Specific implementation for the DW5821e */
        if (vendor == 0x413c && (product == 0x81d7 || product == 0x81e0)) {
            mm_obj_dbg (self, "MBIM-powered DW5821e (T77W968) modem found...");
            return MM_BASE_MODEM (mm_broadband_modem_mbim_foxconn_new (uid,
                                                                       drivers,
                                                                       mm_plugin_get_name (self),
                                                                       vendor,
                                                                       product));
        }

        if (mm_port_probe_list_is_xmm (probes)) {
            mm_obj_dbg (self, "MBIM-powered XMM-based modem found...");
            return MM_BASE_MODEM (mm_broadband_modem_mbim_xmm_new (uid,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product));
        }

        mm_obj_dbg (self, "MBIM-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (uid,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_NOVATEL)) {
        mm_obj_dbg (self, "Novatel-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_novatel_new (uid,
                                                              drivers,
                                                              mm_plugin_get_name (self),
                                                              vendor,
                                                              product));
    }

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_SIERRA)) {
        mm_obj_dbg (self, "Sierra-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_sierra_new (uid,
                                                             drivers,
                                                             mm_plugin_get_name (self),
                                                             vendor,
                                                             product));
    }

    if (port_probe_list_has_manufacturer_port (probes, DELL_MANUFACTURER_TELIT)) {
        mm_obj_dbg (self, "Telit-powered Dell-branded modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_telit_new (uid,
                                                            drivers,
                                                            mm_plugin_get_name (self),
                                                            vendor,
                                                            product));
    }

    if (mm_port_probe_list_is_xmm (probes)) {
        mm_obj_dbg (self, "XMM-based modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_xmm_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }

    mm_obj_dbg (self, "Dell-branded generic modem found...");
    return MM_BASE_MODEM (mm_broadband_modem_new (uid,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));
}

/*****************************************************************************/

static gboolean
grab_port (MMPlugin     *self,
           MMBaseModem  *modem,
           MMPortProbe  *probe,
           GError      **error)
{
    if (MM_IS_BROADBAND_MODEM_SIERRA (modem))
        return mm_common_sierra_grab_port (self, modem, probe, error);

    if (MM_IS_BROADBAND_MODEM_TELIT (modem))
        return telit_grab_port (self, modem, probe, error);

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_peek_port (probe),
                                    mm_port_probe_get_port_type (probe),
                                    MM_PORT_SERIAL_AT_FLAG_NONE,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usbmisc", NULL };
    static const guint16 vendors[] = { 0x413c, 0 };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (dell_custom_init),
        .finish = G_CALLBACK (dell_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_DELL,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendors,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      MM_PLUGIN_XMM_PROBE,          TRUE,
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
