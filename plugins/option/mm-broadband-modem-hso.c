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

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-hso.h"
#include "mm-broadband-bearer-hso.h"
#include "mm-bearer-list.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;
static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemHso, mm_broadband_modem_hso, MM_TYPE_BROADBAND_MODEM_OPTION, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init));

struct _MMBroadbandModemHsoPrivate {
    /* Regex for connected notifications */
    GRegex *_owancall_regex;

    MMModemLocationSource enabled_sources;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
broadband_bearer_hso_new_ready (GObject *source,
                                GAsyncResult *res,
                                GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_hso_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (mm_bearer_properties_get_ip_type (properties) &
        (MM_BEARER_IP_FAMILY_IPV6 | MM_BEARER_IP_FAMILY_IPV4V6)) {
        mm_obj_dbg (self, "creating generic bearer (IPv6 requested)...");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_new_ready,
                                 task);
        return;
    }

    mm_obj_dbg (self, "creating HSO bearer...");
    mm_broadband_bearer_hso_new (MM_BROADBAND_MODEM_HSO (self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_hso_new_ready,
                                 task);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    int pin1, puk1;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "_OERCN:");
    if (sscanf (response, " %d, %d", &pin1, &puk1) == 2) {
        MMUnlockRetries *retries;
        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
        g_task_return_pointer (task, retries, g_object_unref);
    } else {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid unlock retries response: '%s'",
                                 response);
    }
    g_object_unref (task);
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "_OERCN?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_unlock_retries_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

typedef struct {
    guint cid;
    MMBearerConnectionStatus status;
} BearerListReportStatusForeachContext;

static void
bearer_list_report_status_foreach (MMBaseBearer *bearer,
                                   BearerListReportStatusForeachContext *ctx)
{
    if (mm_broadband_bearer_get_3gpp_cid (MM_BROADBAND_BEARER (bearer)) != ctx->cid)
        return;

    mm_base_bearer_report_connection_status (MM_BASE_BEARER (bearer), ctx->status);
}

static void
hso_connection_status_changed (MMPortSerialAt *port,
                               GMatchInfo *match_info,
                               MMBroadbandModemHso *self)
{
    MMBearerList *list = NULL;
    BearerListReportStatusForeachContext ctx;
    guint cid;
    guint status;

    /* Ensure we got proper parsed values */
    if (!mm_get_uint_from_match_info (match_info, 1, &cid) ||
        !mm_get_uint_from_match_info (match_info, 2, &status))
        return;

    /* Setup context */
    ctx.cid = cid;
    ctx.status = MM_BEARER_CONNECTION_STATUS_UNKNOWN;

    switch (status) {
    case 1:
        ctx.status = MM_BEARER_CONNECTION_STATUS_CONNECTED;
        break;
    case 3:
        ctx.status = MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED;
        break;
    case 0:
        ctx.status = MM_BEARER_CONNECTION_STATUS_DISCONNECTED;
        break;
    default:
        break;
    }

    /* If unknown status, don't try to report anything */
    if (ctx.status == MM_BEARER_CONNECTION_STATUS_UNKNOWN)
        return;

    /* If empty bearer list, nothing else to do */
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list)
        return;

    /* Will report status only in the bearer with the specific CID */
    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_report_status_foreach,
                            &ctx);
    g_object_unref (list);
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        mm_port_serial_at_add_unsolicited_msg_handler (
            mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
            MM_BROADBAND_MODEM_HSO (self)->priv->_owancall_regex,
            (MMPortSerialAtUnsolicitedMsgFn)hso_connection_status_changed,
            self,
            NULL);

        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    /* Our own cleanup first */
    mm_port_serial_at_add_unsolicited_msg_handler (
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        MM_BROADBAND_MODEM_HSO (self)->priv->_owancall_regex,
        NULL, NULL, NULL);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

static MMModemLocationSource
location_load_capabilities_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GTask *task)
{
    MMModemLocationSource sources;
    GError *error = NULL;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own check.
     *
     * We could issue AT_OIFACE? to list the interfaces currently enabled in the
     * module, to see if there is a 'GPS' interface enabled. But we'll just go
     * and see if there is already a 'GPS control' AT port and a raw serial 'GPS'
     * port grabbed.
     *
     * NOTE: A deeper implementation could handle the situation where the GPS
     * interface is found disabled in AT_OIFACE?. In this case, we could issue
     * AT_OIFACE="GPS",1 to enable it (and AT_OIFACE="GPS",0 to disable it), but
     * enabling/disabling GPS involves a complete reboot of the modem, which is
     * probably not the desired thing here.
     */
    if (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)) &&
        mm_base_modem_peek_port_gps_control (MM_BASE_MODEM (self)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (
        self,
        (GAsyncReadyCallback)parent_load_capabilities_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Enable/Disable location gathering (Location interface) */

typedef struct {
    MMModemLocationSource source;
} LocationGatheringContext;

/******************************/
/* Disable location gathering */

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_disabled_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    GTask *task)
{
    LocationGatheringContext *ctx;
    MMPortSerialGps *gps_port;
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (self, res, &error);

    ctx = g_task_get_task_data (task);

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        /* Even if we get an error here, we try to close the GPS port */
        gps_port = mm_base_modem_peek_port_gps (self);
        if (gps_port)
            mm_port_serial_close (MM_PORT_SERIAL (gps_port));
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
disable_location_gathering (MMIfaceModemLocation *self,
                            MMModemLocationSource source,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemHso *hso = MM_BROADBAND_MODEM_HSO (self);
    gboolean stop_gps = FALSE;
    LocationGatheringContext *ctx;
    GTask *task;

    ctx = g_new (LocationGatheringContext, 1);
    ctx->source = source;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    /* Only stop GPS engine if no GPS-related sources enabled */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                  MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                  MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        hso->priv->enabled_sources &= ~source;

        if (!(hso->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                            MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                            MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            stop_gps = TRUE;
    }

    if (stop_gps) {
        /* We enable continuous GPS fixes with AT_OGPS=0 */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_gps_control (MM_BASE_MODEM (self)),
                                       "_OGPS=0",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_disabled_ready,
                                       task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************/
/* Enable location gathering */

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_enabled_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    LocationGatheringContext *ctx;
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        gps_port = mm_base_modem_peek_port_gps (self);
        if (!gps_port ||
            !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
            if (error)
                g_task_return_error (task, error);
            else
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't open raw GPS serial port");
        } else
            g_task_return_boolean (task, TRUE);
    }
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBroadbandModemHso *self = MM_BROADBAND_MODEM_HSO (_self);
    LocationGatheringContext *ctx;
    gboolean start_gps = FALSE;
    GError *error = NULL;

    if (!iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own enabling */

    ctx = g_task_get_task_data (task);

    /* NMEA, RAW and UNMANAGED are all enabled in the same way */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                       MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        /* Only start GPS engine if not done already.
         * NOTE: interface already takes care of making sure that raw/nmea and
         * unmanaged are not enabled at the same time */
        if (!(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                             MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            start_gps = TRUE;
        self->priv->enabled_sources |= ctx->source;
    }

    if (start_gps) {
        /* We enable continuous GPS fixes with AT_OGPS=2 */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_gps_control (MM_BASE_MODEM (self)),
                                       "_OGPS=2",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_enabled_ready,
                                       task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
enable_location_gathering (MMIfaceModemLocation *self,
                           MMModemLocationSource source,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    LocationGatheringContext *ctx;
    GTask *task;

    ctx = g_new (LocationGatheringContext, 1);
    ctx->source = source;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
trace_received (MMPortSerialGps      *port,
                const gchar          *trace,
                MMIfaceModemLocation *self)
{
    mm_iface_modem_location_gps_update (self, trace);
}

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt *gps_control_port;
    MMPortSerialGps *gps_data_port;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_hso_parent_class)->setup_ports (self);

    /* _OWANCALL unsolicited messages are only expected in the primary port. */
    mm_port_serial_at_add_unsolicited_msg_handler (
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        MM_BROADBAND_MODEM_HSO (self)->priv->_owancall_regex,
        NULL, NULL, NULL);

    g_object_set (mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                  MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                  /* built-in echo removal conflicts with unsolicited _OWANCALL
                   * messages, which are not <CR><LF> prefixed. */
                  MM_PORT_SERIAL_AT_REMOVE_ECHO, FALSE,
                  NULL);

    gps_control_port = mm_base_modem_peek_port_gps_control (MM_BASE_MODEM (self));
    gps_data_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
    if (gps_control_port && gps_data_port) {
        /* It may happen that the modem was started with GPS already enabled, or
         * maybe ModemManager got rebooted and it was left enabled before. We'll make
         * sure that it is disabled when we initialize the modem */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       gps_control_port,
                                       "_OGPS=0",
                                       3, FALSE, FALSE, NULL, NULL, NULL);

        /* Add handler for the NMEA traces */
        mm_port_serial_gps_add_trace_handler (gps_data_port,
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemHso *
mm_broadband_modem_hso_new (const gchar *device,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_HSO,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemHso *self = MM_BROADBAND_MODEM_HSO (object);

    g_regex_unref (self->priv->_owancall_regex);

    G_OBJECT_CLASS (mm_broadband_modem_hso_parent_class)->finalize (object);
}

static void
mm_broadband_modem_hso_init (MMBroadbandModemHso *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_HSO,
                                              MMBroadbandModemHsoPrivate);

    self->priv->_owancall_regex = g_regex_new ("_OWANCALL: (\\d),\\s*(\\d)\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->enabled_sources = MM_MODEM_LOCATION_SOURCE_NONE;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;

    /* HSO modems don't need the extra 10s wait after powering up */
    iface->modem_after_power_up = NULL;
    iface->modem_after_power_up_finish = NULL;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities = location_load_capabilities;
    iface->load_capabilities_finish = location_load_capabilities_finish;
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
    iface->disable_location_gathering = disable_location_gathering;
    iface->disable_location_gathering_finish = disable_location_gathering_finish;
}

static void
mm_broadband_modem_hso_class_init (MMBroadbandModemHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemHsoPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
