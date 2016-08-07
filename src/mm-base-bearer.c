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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2015 Azimut Electronics
 * Copyright (C) 2011 - 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-daemon-enums-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-bearer.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-bearer-stats.h"

/* We require up to 20s to get a proper IP when using PPP */
#define BEARER_IP_TIMEOUT_DEFAULT 20

#define BEARER_DEFERRED_UNREGISTRATION_TIMEOUT 15

#define BEARER_STATS_UPDATE_TIMEOUT 30

#define BEARER_CONNECTION_MONITOR_TIMEOUT 5

G_DEFINE_TYPE (MMBaseBearer, mm_base_bearer, MM_GDBUS_TYPE_BEARER_SKELETON)

typedef enum {
    CONNECTION_FORBIDDEN_REASON_NONE,
    CONNECTION_FORBIDDEN_REASON_UNREGISTERED,
    CONNECTION_FORBIDDEN_REASON_ROAMING,
    CONNECTION_FORBIDDEN_REASON_LAST
} ConnectionForbiddenReason;

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_STATUS,
    PROP_CONFIG,
    PROP_DEFAULT_IP_FAMILY,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseBearerPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this BEARER */
    MMBaseModem *modem;
    /* The path where the BEARER object is exported */
    gchar *path;
    /* Status of this bearer */
    MMBearerStatus status;
    /* Configuration of the bearer */
    MMBearerProperties *config;
    /* Default IP family of this bearer */
    MMBearerIpFamily default_ip_family;

    /* Cancellable for connect() */
    GCancellable *connect_cancellable;
    /* handler id for the disconnect + cancel connect request */
    gulong disconnect_signal_handler;

    /* Connection status monitoring */
    guint connection_monitor_id;

    /*-- 3GPP specific --*/
    guint deferred_3gpp_unregistration_id;
    /* Reason if 3GPP connection is forbidden */
    ConnectionForbiddenReason reason_3gpp;
    /* Handler ID for the registration state change signals */
    guint id_3gpp_registration_change;

    /*-- CDMA specific --*/
    guint deferred_cdma_unregistration_id;
    /* Reason if CDMA connection is forbidden */
    ConnectionForbiddenReason reason_cdma;
    /* Handler IDs for the registration state change signals */
    guint id_cdma1x_registration_change;
    guint id_evdo_registration_change;

    /* The stats object to expose */
    MMBearerStats *stats;
    /* Handler id for the stats update timeout */
    guint stats_update_id;
    /* Timer to measure the duration of the connection */
    GTimer *duration_timer;
};

/*****************************************************************************/

static const gchar *connection_forbidden_reason_str [CONNECTION_FORBIDDEN_REASON_LAST] = {
    "none",
    "Not registered in the network",
    "Registered in roaming network, and roaming not allowed"
};

/*****************************************************************************/

void
mm_base_bearer_export (MMBaseBearer *self)
{
    static guint id = 0;
    gchar *path;

    path = g_strdup_printf (MM_DBUS_BEARER_PREFIX "/%d", id++);
    g_object_set (self,
                  MM_BASE_BEARER_PATH, path,
                  NULL);
    g_free (path);
}

/*****************************************************************************/

static void
connection_monitor_stop (MMBaseBearer *self)
{
    if (self->priv->connection_monitor_id) {
        g_source_remove (self->priv->connection_monitor_id);
        self->priv->connection_monitor_id = 0;
    }
}

static void
load_connection_status_ready (MMBaseBearer *self,
                              GAsyncResult *res)
{
    GError                   *error = NULL;
    MMBearerConnectionStatus  status;

    status = MM_BASE_BEARER_GET_CLASS (self)->load_connection_status_finish (self, res, &error);
    if (status == MM_BEARER_CONNECTION_STATUS_UNKNOWN) {
        mm_warn ("checking if connected failed: %s", error->message);
        g_error_free (error);
        return;
    }

    /* Report connection or disconnection */
    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED || status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
    mm_dbg ("connection status loaded: %s", mm_bearer_connection_status_get_string (status));
    mm_base_bearer_report_connection_status (self, status);
}

static gboolean
connection_monitor_cb (MMBaseBearer *self)
{
    /* If the implementation knows how to update stat values, run it */
    MM_BASE_BEARER_GET_CLASS (self)->load_connection_status (
            self,
            (GAsyncReadyCallback)load_connection_status_ready,
            NULL);
    return G_SOURCE_CONTINUE;
}

static void
connection_monitor_start (MMBaseBearer *self)
{
    /* If not implemented, don't schedule anything */
    if (!MM_BASE_BEARER_GET_CLASS (self)->load_connection_status ||
        !MM_BASE_BEARER_GET_CLASS (self)->load_connection_status_finish)
        return;

    /* Schedule */
    g_assert (!self->priv->connection_monitor_id);
    self->priv->connection_monitor_id = g_timeout_add_seconds (BEARER_CONNECTION_MONITOR_TIMEOUT,
                                                               (GSourceFunc) connection_monitor_cb,
                                                               self);
}

/*****************************************************************************/

static void
bearer_update_interface_stats (MMBaseBearer *self)
{
    mm_gdbus_bearer_set_stats (
        MM_GDBUS_BEARER (self),
        mm_bearer_stats_get_dictionary (self->priv->stats));
}

static void
bearer_reset_interface_stats (MMBaseBearer *self)
{
    g_clear_object (&self->priv->stats);
    mm_gdbus_bearer_set_stats (MM_GDBUS_BEARER (self), NULL);
}

static void
bearer_stats_stop (MMBaseBearer *self)
{
    if (self->priv->duration_timer) {
        if (self->priv->stats)
            mm_bearer_stats_set_duration (self->priv->stats, (guint64) g_timer_elapsed (self->priv->duration_timer, NULL));
        g_timer_destroy (self->priv->duration_timer);
        self->priv->duration_timer = NULL;
    }

    if (self->priv->stats_update_id) {
        g_source_remove (self->priv->stats_update_id);
        self->priv->stats_update_id = 0;
    }
}

static void
reload_stats_ready (MMBaseBearer *self,
                    GAsyncResult *res)
{
    GError *error = NULL;
    guint64 rx_bytes = 0;
    guint64 tx_bytes = 0;

    if (!MM_BASE_BEARER_GET_CLASS (self)->reload_stats_finish (self, &rx_bytes, &tx_bytes, res, &error)) {
        mm_warn ("Reloading stats failed: %s", error->message);
        g_error_free (error);
        return;
    }

    /* We only update stats if they were retrieved properly */
    mm_bearer_stats_set_duration (self->priv->stats, (guint32) g_timer_elapsed (self->priv->duration_timer, NULL));
    mm_bearer_stats_set_tx_bytes (self->priv->stats, rx_bytes);
    mm_bearer_stats_set_rx_bytes (self->priv->stats, tx_bytes);
    bearer_update_interface_stats (self);
}

static gboolean
stats_update_cb (MMBaseBearer *self)
{
    /* If the implementation knows how to update stat values, run it */
    if (MM_BASE_BEARER_GET_CLASS (self)->reload_stats &&
        MM_BASE_BEARER_GET_CLASS (self)->reload_stats_finish) {
        MM_BASE_BEARER_GET_CLASS (self)->reload_stats (
            self,
            (GAsyncReadyCallback)reload_stats_ready,
            NULL);
        return G_SOURCE_CONTINUE;
    }

    /* Otherwise, just update duration and we're done */
    mm_bearer_stats_set_duration (self->priv->stats, (guint32) g_timer_elapsed (self->priv->duration_timer, NULL));
    mm_bearer_stats_set_tx_bytes (self->priv->stats, 0);
    mm_bearer_stats_set_rx_bytes (self->priv->stats, 0);
    bearer_update_interface_stats (self);
    return G_SOURCE_CONTINUE;
}

static void
bearer_stats_start (MMBaseBearer *self)
{
    /* Allocate new stats object. If there was one already created from a
     * previous run, deallocate it */
    g_assert (!self->priv->stats);
    self->priv->stats = mm_bearer_stats_new ();

    /* Start duration timer */
    g_assert (!self->priv->duration_timer);
    self->priv->duration_timer = g_timer_new ();

    /* Schedule */
    g_assert (!self->priv->stats_update_id);
    self->priv->stats_update_id = g_timeout_add_seconds (BEARER_STATS_UPDATE_TIMEOUT,
                                                         (GSourceFunc) stats_update_cb,
                                                         self);
    /* Load initial values */
    stats_update_cb (self);
}

/*****************************************************************************/

static void
bearer_reset_interface_status (MMBaseBearer *self)
{
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip4_config (
        MM_GDBUS_BEARER (self),
        mm_bearer_ip_config_get_dictionary (NULL));
    mm_gdbus_bearer_set_ip6_config (
        MM_GDBUS_BEARER (self),
        mm_bearer_ip_config_get_dictionary (NULL));
}

static void
bearer_update_status (MMBaseBearer *self,
                      MMBearerStatus status)
{
    /* NOTE: we do allow status 'CONNECTED' here; it may happen if we go into
     * DISCONNECTING and we cannot disconnect */

    /* Update the property value */
    self->priv->status = status;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);

    /* Ensure that we don't expose any connection related data in the
     * interface when going into disconnected state. */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTED) {
        bearer_reset_interface_status (self);
        /* Stop statistics */
        bearer_stats_stop (self);
        /* Stop connection monitoring */
        connection_monitor_stop (self);
    }
}

static void
bearer_update_status_connected (MMBaseBearer *self,
                                const gchar *interface,
                                MMBearerIpConfig *ipv4_config,
                                MMBearerIpConfig *ipv6_config)
{
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), TRUE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), interface);
    mm_gdbus_bearer_set_ip4_config (
        MM_GDBUS_BEARER (self),
        mm_bearer_ip_config_get_dictionary (ipv4_config));
    mm_gdbus_bearer_set_ip6_config (
        MM_GDBUS_BEARER (self),
        mm_bearer_ip_config_get_dictionary (ipv6_config));

    /* Start statistics */
    bearer_stats_start (self);

    /* Start connection monitor, if supported */
    connection_monitor_start (self);

    /* Update the property value */
    self->priv->status = MM_BEARER_STATUS_CONNECTED;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
}

/*****************************************************************************/

static void
reset_deferred_unregistration (MMBaseBearer *self)
{
    if (self->priv->deferred_cdma_unregistration_id) {
        g_source_remove (self->priv->deferred_cdma_unregistration_id);
        self->priv->deferred_cdma_unregistration_id = 0;
    }

    if (self->priv->deferred_3gpp_unregistration_id) {
        g_source_remove (self->priv->deferred_3gpp_unregistration_id);
        self->priv->deferred_3gpp_unregistration_id = 0;
    }
}

static gboolean
deferred_3gpp_unregistration_cb (MMBaseBearer *self)
{
    g_warn_if_fail (self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_UNREGISTERED);
    self->priv->deferred_3gpp_unregistration_id = 0;

    mm_dbg ("Forcing bearer disconnection, not registered in 3GPP network");
    mm_base_bearer_disconnect_force (self);
    return G_SOURCE_REMOVE;
}

static void
modem_3gpp_registration_state_changed (MMIfaceModem3gpp *modem,
                                       GParamSpec *pspec,
                                       MMBaseBearer *self)
{
    MMModem3gppRegistrationState state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

    g_object_get (modem,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    switch (state) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE:
    case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED:
    case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
        self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_UNREGISTERED;
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
    case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING:
        self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
        if (mm_bearer_properties_get_allow_roaming (mm_base_bearer_peek_config (self)))
            self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
        else
            self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_ROAMING;
        break;
    }

    /* If no reason to disconnect, or if it's a mixed CDMA+LTE modem without a CDMA reason,
     * just don't do anything. */
    if (self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_NONE ||
        (mm_iface_modem_is_cdma (MM_IFACE_MODEM (modem)) &&
         self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_NONE)) {
        reset_deferred_unregistration (self);
        return;
    }

    /* Modem is roaming and roaming not allowed, report right away */
    if (self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_ROAMING) {
        mm_dbg ("Bearer not allowed to connect, registered in roaming 3GPP network");
        reset_deferred_unregistration (self);
        mm_base_bearer_disconnect_force (self);
        return;
    }

    /* Modem reports being unregistered */
    if (self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_UNREGISTERED) {
        /* If there is already a notification pending, just return */
        if (self->priv->deferred_3gpp_unregistration_id)
            return;

        /* If the bearer is not connected, report right away */
        if (self->priv->status != MM_BEARER_STATUS_CONNECTED) {
            mm_dbg ("Bearer not allowed to connect, not registered in 3GPP network");
            mm_base_bearer_disconnect_force (self);
            return;
        }

        /* Otherwise, setup the new timeout */
        mm_dbg ("Connected bearer not registered in 3GPP network");
        self->priv->deferred_3gpp_unregistration_id =
            g_timeout_add_seconds (BEARER_DEFERRED_UNREGISTRATION_TIMEOUT,
                                   (GSourceFunc) deferred_3gpp_unregistration_cb,
                                   self);
        return;
    }

    g_assert_not_reached ();
}

static gboolean
deferred_cdma_unregistration_cb (MMBaseBearer *self)
{
    g_warn_if_fail (self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_UNREGISTERED);
    self->priv->deferred_cdma_unregistration_id = 0;

    mm_dbg ("Forcing bearer disconnection, not registered in CDMA network");
    mm_base_bearer_disconnect_force (self);
    return G_SOURCE_REMOVE;
}

static void
modem_cdma_registration_state_changed (MMIfaceModemCdma *modem,
                                       GParamSpec *pspec,
                                       MMBaseBearer *self)
{
    MMModemCdmaRegistrationState cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    MMModemCdmaRegistrationState evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

    g_object_get (modem,
                  MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE, &cdma1x_state,
                  MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, &evdo_state,
                  NULL);

    if (cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING ||
        evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING) {
        if (mm_bearer_properties_get_allow_roaming (mm_base_bearer_peek_config (self)))
            self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
        else
            self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_ROAMING;
    } else if (cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN ||
               evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN) {
        self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
    } else {
        self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_UNREGISTERED;
    }

    /* If no reason to disconnect, or if it's a mixed CDMA+LTE modem without a 3GPP reason,
     * just don't do anything. */
    if (self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_NONE ||
        (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem)) &&
         self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_NONE)) {
        reset_deferred_unregistration (self);
        return;
    }

    /* Modem is roaming and roaming not allowed, report right away */
    if (self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_ROAMING) {
        mm_dbg ("Bearer not allowed to connect, registered in roaming CDMA network");
        reset_deferred_unregistration (self);
        mm_base_bearer_disconnect_force (self);
        return;
    }

    /* Modem reports being unregistered */
    if (self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_UNREGISTERED) {
        /* If there is already a notification pending, just return */
        if (self->priv->deferred_cdma_unregistration_id)
            return;

        /* If the bearer is not connected, report right away */
        if (self->priv->status != MM_BEARER_STATUS_CONNECTED) {
            mm_dbg ("Bearer not allowed to connect, not registered in CDMA network");
            mm_base_bearer_disconnect_force (self);
            return;
        }

        /* Otherwise, setup the new timeout */
        mm_dbg ("Connected bearer not registered in CDMA network");
        self->priv->deferred_cdma_unregistration_id =
            g_timeout_add_seconds (BEARER_DEFERRED_UNREGISTRATION_TIMEOUT,
                                   (GSourceFunc) deferred_cdma_unregistration_cb,
                                   self);
        return;
    }

    g_assert_not_reached ();
}

static void
set_signal_handlers (MMBaseBearer *self)
{
    g_assert (self->priv->modem != NULL);
    g_assert (self->priv->config != NULL);

    /* Don't set the 3GPP registration change signal handlers if they
     * are already set. */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self->priv->modem)) &&
        !self->priv->id_3gpp_registration_change) {
        self->priv->id_3gpp_registration_change =
            g_signal_connect (self->priv->modem,
                              "notify::" MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                              G_CALLBACK (modem_3gpp_registration_state_changed),
                              self);
        modem_3gpp_registration_state_changed (MM_IFACE_MODEM_3GPP (self->priv->modem), NULL, self);
    }

    /* Don't set the CDMA1x/EV-DO registration change signal handlers if they
     * are already set. */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self->priv->modem)) &&
        !self->priv->id_cdma1x_registration_change &&
        !self->priv->id_evdo_registration_change) {
        self->priv->id_cdma1x_registration_change =
            g_signal_connect (self->priv->modem,
                              "notify::" MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
                              G_CALLBACK (modem_cdma_registration_state_changed),
                              self);
        self->priv->id_evdo_registration_change =
            g_signal_connect (self->priv->modem,
                              "notify::" MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE,
                              G_CALLBACK (modem_cdma_registration_state_changed),
                              self);
        modem_cdma_registration_state_changed (MM_IFACE_MODEM_CDMA (self->priv->modem), NULL, self);
    }
}

static void
reset_signal_handlers (MMBaseBearer *self)
{
    if (!self->priv->modem)
        return;

    if (self->priv->id_3gpp_registration_change) {
        if (g_signal_handler_is_connected (self->priv->modem, self->priv->id_3gpp_registration_change))
            g_signal_handler_disconnect (self->priv->modem, self->priv->id_3gpp_registration_change);
        self->priv->id_3gpp_registration_change = 0;
    }
    if (self->priv->id_cdma1x_registration_change) {
        if (g_signal_handler_is_connected (self->priv->modem, self->priv->id_cdma1x_registration_change))
            g_signal_handler_disconnect (self->priv->modem, self->priv->id_cdma1x_registration_change);
        self->priv->id_cdma1x_registration_change = 0;
    }
    if (self->priv->id_evdo_registration_change) {
        if (g_signal_handler_is_connected (self->priv->modem, self->priv->id_evdo_registration_change))
            g_signal_handler_disconnect (self->priv->modem, self->priv->id_evdo_registration_change);
        self->priv->id_evdo_registration_change = 0;
    }
}

/*****************************************************************************/
/* CONNECT */

gboolean
mm_base_bearer_connect_finish (MMBaseBearer *self,
                               GAsyncResult *res,
                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_after_cancel_ready (MMBaseBearer *self,
                               GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BASE_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_warn ("Error disconnecting bearer '%s': '%s'. "
                 "Will assume disconnected anyway.",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
    else
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);

    /* Report disconnection to the bearer object using class method
     * mm_bearer_report_connection_status. This gives subclass implementations a
     * chance to correctly update their own connection state, in case this base
     * class ignores a failed disconnection attempt.
     */
    mm_base_bearer_report_connection_status (self, MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
}

static void
connect_ready (MMBaseBearer *self,
               GAsyncResult *res,
               GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    gboolean launch_disconnect = FALSE;
    MMBearerConnectResult *result;

    /* NOTE: connect() implementations *MUST* handle cancellations themselves */
    result = MM_BASE_BEARER_GET_CLASS (self)->connect_finish (self, res, &error);
    if (!result) {
        mm_dbg ("Couldn't connect bearer '%s': '%s'",
                self->priv->path,
                error->message);
        if (g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_CANCELLED)) {
            /* Will launch disconnection */
            launch_disconnect = TRUE;
        } else
            bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTED);

        g_simple_async_result_take_error (simple, error);
    }
    /* Handle cancellations detected after successful connection */
    else if (g_cancellable_is_cancelled (self->priv->connect_cancellable)) {
        mm_dbg ("Connected bearer '%s', but need to disconnect", self->priv->path);
        mm_bearer_connect_result_unref (result);
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CANCELLED,
            "Bearer got connected, but had to disconnect after cancellation request");
            launch_disconnect = TRUE;
    }
    else {
        mm_dbg ("Connected bearer '%s'", self->priv->path);

        /* Update bearer and interface status */
        bearer_update_status_connected (
            self,
            mm_port_get_device (mm_bearer_connect_result_peek_data (result)),
            mm_bearer_connect_result_peek_ipv4_config (result),
            mm_bearer_connect_result_peek_ipv6_config (result));
        mm_bearer_connect_result_unref (result);
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (launch_disconnect) {
        bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTING);
        MM_BASE_BEARER_GET_CLASS (self)->disconnect (
            self,
            (GAsyncReadyCallback)disconnect_after_cancel_ready,
            NULL);
    }

    g_clear_object (&self->priv->connect_cancellable);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_base_bearer_connect (MMBaseBearer *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_assert (MM_BASE_BEARER_GET_CLASS (self)->connect != NULL);
    g_assert (MM_BASE_BEARER_GET_CLASS (self)->connect_finish != NULL);

    /* If already connecting, return error, don't allow a second request. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_IN_PROGRESS,
            "Bearer already being connected");
        return;
    }

    /* If currently disconnecting, return error, previous operation should
     * finish before allowing to connect again. */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Bearer currently being disconnected");
        return;
    }

    /* Check 3GPP roaming allowance, *only* roaming related here */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self->priv->modem)) &&
        self->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_ROAMING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNAUTHORIZED,
            "Not allowed to connect bearer in 3GPP network: '%s'",
            connection_forbidden_reason_str[self->priv->reason_3gpp]);
        return;
    }

    /* Check CDMA roaming allowance, *only* roaming related here */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self->priv->modem)) &&
        self->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_ROAMING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNAUTHORIZED,
            "Not allowed to connect bearer in CDMA network: '%s'",
            connection_forbidden_reason_str[self->priv->reason_cdma]);
        return;
    }

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_base_bearer_connect);

    /* If already connected, done */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTED) {
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Connecting! */
    mm_dbg ("Connecting bearer '%s'", self->priv->path);
    self->priv->connect_cancellable = g_cancellable_new ();
    bearer_update_status (self, MM_BEARER_STATUS_CONNECTING);
    bearer_reset_interface_stats (self);
    MM_BASE_BEARER_GET_CLASS (self)->connect (
        self,
        self->priv->connect_cancellable,
        (GAsyncReadyCallback)connect_ready,
        result);
}

typedef struct {
    MMBaseBearer *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleConnectContext;

static void
handle_connect_context_free (HandleConnectContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_connect_ready (MMBaseBearer *self,
                      GAsyncResult *res,
                      HandleConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_bearer_connect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_bearer_complete_connect (MM_GDBUS_BEARER (self), ctx->invocation);

    handle_connect_context_free (ctx);
}

static void
handle_connect_auth_ready (MMBaseModem *modem,
                           GAsyncResult *res,
                           HandleConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_connect_context_free (ctx);
        return;
    }

    mm_base_bearer_connect (ctx->self,
                            (GAsyncReadyCallback)handle_connect_ready,
                            ctx);
}

static gboolean
handle_connect (MMBaseBearer *self,
                GDBusMethodInvocation *invocation)
{
    HandleConnectContext *ctx;

    ctx = g_new0 (HandleConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_connect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* DISCONNECT */

gboolean
mm_base_bearer_disconnect_finish (MMBaseBearer *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_ready (MMBaseBearer *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_BASE_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_dbg ("Couldn't disconnect bearer '%s'", self->priv->path);
        bearer_update_status (self, MM_BEARER_STATUS_CONNECTED);
        g_simple_async_result_take_error (simple, error);
    }
    else {
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);
        bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTED);
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
status_changed_complete_disconnect (MMBaseBearer *self,
                                    GParamSpec *pspec,
                                    GSimpleAsyncResult *simple)
{
    /* We may get other states here before DISCONNECTED, like DISCONNECTING or
     * even CONNECTED. */
    if (self->priv->status != MM_BEARER_STATUS_DISCONNECTED)
        return;

    mm_dbg ("Disconnected bearer '%s' after cancelling previous connect request",
            self->priv->path);
    g_signal_handler_disconnect (self,
                                 self->priv->disconnect_signal_handler);
    self->priv->disconnect_signal_handler = 0;

    /* Note: interface state is updated when the DISCONNECTED state is set */

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_base_bearer_disconnect (MMBaseBearer *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *simple;

    g_assert (MM_BASE_BEARER_GET_CLASS (self)->disconnect != NULL);
    g_assert (MM_BASE_BEARER_GET_CLASS (self)->disconnect_finish != NULL);

    simple = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_base_bearer_disconnect);

    /* If already disconnected, done */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTED) {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* If already disconnecting, return error, don't allow a second request. */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING) {
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_IN_PROGRESS,
            "Bearer already being disconnected");
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    mm_dbg ("Disconnecting bearer '%s'", self->priv->path);

    /* If currently connecting, try to cancel that operation, and wait to get
     * disconnected. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        /* Set ourselves as disconnecting */
        bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTING);

        /* We MUST ensure that we get to DISCONNECTED */
        g_cancellable_cancel (self->priv->connect_cancellable);
        /* Note that we only allow to remove disconnected bearers, so should
         * be safe to assume that we'll get the signal handler called properly
         */
        self->priv->disconnect_signal_handler =
            g_signal_connect (self,
                              "notify::" MM_BASE_BEARER_STATUS,
                              (GCallback)status_changed_complete_disconnect,
                              simple); /* takes ownership */

        return;
    }

    /* Disconnecting! */
    bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTING);
    MM_BASE_BEARER_GET_CLASS (self)->disconnect (
        self,
        (GAsyncReadyCallback)disconnect_ready,
        simple); /* takes ownership */
}

typedef struct {
    MMBaseBearer *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleDisconnectContext;

static void
handle_disconnect_context_free (HandleDisconnectContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_disconnect_ready (MMBaseBearer *self,
                         GAsyncResult *res,
                         HandleDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_bearer_disconnect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_bearer_complete_disconnect (MM_GDBUS_BEARER (self), ctx->invocation);

    handle_disconnect_context_free (ctx);
}

static void
handle_disconnect_auth_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              HandleDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_disconnect_context_free (ctx);
        return;
    }

    mm_base_bearer_disconnect (ctx->self,
                               (GAsyncReadyCallback)handle_disconnect_ready,
                               ctx);
}

static gboolean
handle_disconnect (MMBaseBearer *self,
                   GDBusMethodInvocation *invocation)
{
    HandleDisconnectContext *ctx;

    ctx = g_new0 (HandleDisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_disconnect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static void
base_bearer_dbus_export (MMBaseBearer *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-connect",
                      G_CALLBACK (handle_connect),
                      NULL);
    g_signal_connect (self,
                      "handle-disconnect",
                      G_CALLBACK (handle_disconnect),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export BEARER at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
base_bearer_dbus_unexport (MMBaseBearer *self)
{
    const gchar *path;

    path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self));
    /* Only unexport if currently exported */
    if (path) {
        mm_dbg ("Removing from DBus bearer at '%s'", path);
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
    }
}

/*****************************************************************************/

MMBearerStatus
mm_base_bearer_get_status (MMBaseBearer *self)
{
    return self->priv->status;
}

const gchar *
mm_base_bearer_get_path (MMBaseBearer *self)
{
    return self->priv->path;
}

MMBearerProperties *
mm_base_bearer_peek_config (MMBaseBearer *self)
{
    return self->priv->config;
}

MMBearerProperties *
mm_base_bearer_get_config (MMBaseBearer *self)
{
    return (self->priv->config ?
            g_object_ref (self->priv->config) :
            NULL);
}

MMBearerIpFamily
mm_base_bearer_get_default_ip_family (MMBaseBearer *self)
{
    return self->priv->default_ip_family;
}

/*****************************************************************************/

static void
disconnect_force_ready (MMBaseBearer *self,
                        GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BASE_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_warn ("Error disconnecting bearer '%s': '%s'. "
                 "Will assume disconnected anyway.",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
    else
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);

    /* Report disconnection to the bearer object using class method
     * mm_bearer_report_connection_status. This gives subclass implementations a
     * chance to correctly update their own connection state, in case this base
     * class ignores a failed disconnection attempt.
     */
    mm_base_bearer_report_connection_status (self, MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
}

void
mm_base_bearer_disconnect_force (MMBaseBearer *self)
{
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING ||
        self->priv->status == MM_BEARER_STATUS_DISCONNECTED)
        return;

    mm_dbg ("Forcing disconnection of bearer '%s'", self->priv->path);

    /* If currently connecting, try to cancel that operation. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        g_cancellable_cancel (self->priv->connect_cancellable);
        return;
    }

    /* Disconnecting! */
    bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTING);
    MM_BASE_BEARER_GET_CLASS (self)->disconnect (
        self,
        (GAsyncReadyCallback)disconnect_force_ready,
        NULL);
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer *self,
                          MMBearerConnectionStatus status)
{
    /* The only status expected at this point is DISCONNECTED or CONNECTED,
     * although here we just process the DISCONNECTED one.
     */
    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED || status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);

    /* In the generic bearer implementation we just need to reset the
     * interface status */
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED)
        bearer_update_status (self, MM_BEARER_STATUS_DISCONNECTED);
}

void
mm_base_bearer_report_connection_status (MMBaseBearer *self,
                                         MMBearerConnectionStatus status)
{
    return MM_BASE_BEARER_GET_CLASS (self)->report_connection_status (self, status);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseBearer *self = MM_BASE_BEARER (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (self->priv->path &&
            self->priv->connection)
            base_bearer_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            base_bearer_dbus_unexport (self);
        else if (self->priv->path)
            base_bearer_dbus_export (self);
        break;
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the BEARER's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_BASE_BEARER_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
            if (self->priv->config) {
                /* Listen to 3GPP/CDMA registration state changes. We need both
                 * 'config' and 'modem' set. */
                set_signal_handlers (self);
            }
        }
        break;
    case PROP_STATUS:
        /* We don't allow g_object_set()-ing the status property */
        g_assert_not_reached ();
        break;
    case PROP_CONFIG: {
        GVariant *dictionary;

        g_clear_object (&self->priv->config);
        self->priv->config = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Listen to 3GPP/CDMA registration state changes. We need both
             * 'config' and 'modem' set. */
            set_signal_handlers (self);
        }
        /* Also expose the properties */
        dictionary = mm_bearer_properties_get_dictionary (self->priv->config);
        mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (self), dictionary);
        if (dictionary)
            g_variant_unref (dictionary);
        break;
    }
    case PROP_DEFAULT_IP_FAMILY:
        self->priv->default_ip_family = g_value_get_flags (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBaseBearer *self = MM_BASE_BEARER (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    case PROP_STATUS:
        g_value_set_enum (value, self->priv->status);
        break;
    case PROP_CONFIG:
        g_value_set_object (value, self->priv->config);
        break;
    case PROP_DEFAULT_IP_FAMILY:
        g_value_set_flags (value, self->priv->default_ip_family);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_bearer_init (MMBaseBearer *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BASE_BEARER,
                                              MMBaseBearerPrivate);
    self->priv->status = MM_BEARER_STATUS_DISCONNECTED;
    self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
    self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
    self->priv->default_ip_family = MM_BEARER_IP_FAMILY_IPV4;

    /* Set defaults */
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip_timeout (MM_GDBUS_BEARER (self), BEARER_IP_TIMEOUT_DEFAULT);
    mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (self),
                                    mm_bearer_ip_config_get_dictionary (NULL));
    mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (self),
                                    mm_bearer_ip_config_get_dictionary (NULL));
}

static void
finalize (GObject *object)
{
    MMBaseBearer *self = MM_BASE_BEARER (object);

    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_bearer_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseBearer *self = MM_BASE_BEARER (object);

    connection_monitor_stop (self);
    bearer_stats_stop (self);
    g_clear_object (&self->priv->stats);

    if (self->priv->connection) {
        base_bearer_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    reset_signal_handlers (self);
    reset_deferred_unregistration (self);

    g_clear_object (&self->priv->modem);
    g_clear_object (&self->priv->config);

    G_OBJECT_CLASS (mm_base_bearer_parent_class)->dispose (object);
}

static void
mm_base_bearer_class_init (MMBaseBearerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseBearerPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    klass->report_connection_status = report_connection_status;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_BEARER_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BASE_BEARER_PATH,
                             "Path",
                             "DBus path of the Bearer",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_BASE_BEARER_MODEM,
                             "Modem",
                             "The Modem which owns this Bearer",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    properties[PROP_STATUS] =
        g_param_spec_enum (MM_BASE_BEARER_STATUS,
                           "Bearer status",
                           "Status of the bearer",
                           MM_TYPE_BEARER_STATUS,
                           MM_BEARER_STATUS_DISCONNECTED,
                           G_PARAM_READABLE);
    g_object_class_install_property (object_class, PROP_STATUS, properties[PROP_STATUS]);

    properties[PROP_CONFIG] =
        g_param_spec_object (MM_BASE_BEARER_CONFIG,
                             "Bearer configuration",
                             "List of user provided properties",
                             MM_TYPE_BEARER_PROPERTIES,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONFIG, properties[PROP_CONFIG]);

    properties[PROP_DEFAULT_IP_FAMILY] =
        g_param_spec_flags (MM_BASE_BEARER_DEFAULT_IP_FAMILY,
                            "Bearer default IP family",
                            "IP family to use for this bearer when no IP family is specified",
                            MM_TYPE_BEARER_IP_FAMILY,
                            MM_BEARER_IP_FAMILY_IPV4,
                            G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_DEFAULT_IP_FAMILY, properties[PROP_DEFAULT_IP_FAMILY]);
}

/*****************************************************************************/
/* Helpers to implement connect() */

struct _MMBearerConnectResult {
    volatile gint ref_count;
    MMPort *data;
    MMBearerIpConfig *ipv4_config;
    MMBearerIpConfig *ipv6_config;
};

MMBearerConnectResult *
mm_bearer_connect_result_ref (MMBearerConnectResult *result)
{
    g_atomic_int_inc (&result->ref_count);
    return result;
}

void
mm_bearer_connect_result_unref (MMBearerConnectResult *result)
{
    if (g_atomic_int_dec_and_test (&result->ref_count)) {
        if (result->ipv4_config)
            g_object_unref (result->ipv4_config);
        if (result->ipv6_config)
            g_object_unref (result->ipv6_config);
        if (result->data)
            g_object_unref (result->data);
        g_slice_free (MMBearerConnectResult, result);
    }
}

MMPort *
mm_bearer_connect_result_peek_data (MMBearerConnectResult *result)
{
    return result->data;
}

MMBearerIpConfig *
mm_bearer_connect_result_peek_ipv4_config (MMBearerConnectResult *result)
{
    return result->ipv4_config;
}

MMBearerIpConfig *
mm_bearer_connect_result_peek_ipv6_config (MMBearerConnectResult *result)
{
    return result->ipv6_config;
}

MMBearerConnectResult *
mm_bearer_connect_result_new (MMPort *data,
                              MMBearerIpConfig *ipv4_config,
                              MMBearerIpConfig *ipv6_config)
{
    MMBearerConnectResult *result;

    /* 'data' must always be given */
    g_assert (MM_IS_PORT (data));

    result = g_slice_new0 (MMBearerConnectResult);
    result->ref_count = 1;
    result->data = g_object_ref (data);
    if (ipv4_config)
        result->ipv4_config = g_object_ref (ipv4_config);
    if (ipv6_config)
        result->ipv6_config = g_object_ref (ipv6_config);
    return result;
}
