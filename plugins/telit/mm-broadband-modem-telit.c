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

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-broadband-modem-telit.h"
#include "mm-modem-helpers-telit.h"
#include "mm-telit-enums-types.h"
#include "mm-shared-telit.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void shared_telit_init (MMSharedTelit *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;
static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemTelit, mm_broadband_modem_telit, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_TELIT, shared_telit_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init));

#define CSIM_UNLOCK_MAX_TIMEOUT 3

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED
} FeatureSupport;

struct _MMBroadbandModemTelitPrivate {
    FeatureSupport csim_lock_support;
    MMTelitQssStatus qss_status;
    MMTelitCsimLockState csim_lock_state;
    GTask *csim_lock_task;
    guint csim_lock_timeout_id;
    gboolean parse_qss;
    MMModemLocationSource enabled_sources;
};


typedef struct {
    MMModemLocationSource source;
    guint gps_enable_step;
} LocationGatheringContext;

/*
 * AT$GPSNMUN
 *    enable:  0 NMEA stream disabled (default)
 *             1 NMEA stream enabled in the form $GPSNMUN: <nmea sentence><CR>
 *             2 NMEA stream enabled in the form <nmea sentence><CR>
 *             3 dedicated NMEA stream
 *    GGA:     0 disable (default), 1 enable
 *    GLL:     0 disable (default), 1 enable
 *    GSA:     0 disable (default), 1 enable
 *    GSV:     0 disable (default), 1 enable
 *    RMC:     0 disable (default), 1 enable
 *    VTG:     0 disable (default), 1 enable
 */
static const gchar *gps_enable[] = {
    "$GPSP=1",
    "$GPSNMUN=2,1,1,1,1,1,1"
};

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

    mm_base_modem_at_command_finish (self, res, &error);
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
    MMBroadbandModemTelit *telit = MM_BROADBAND_MODEM_TELIT (self);
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
        telit->priv->enabled_sources &= ~source;

        if (!(telit->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                            MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                            MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            stop_gps = TRUE;
    }

    if (stop_gps) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "$GPSP=0",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)gps_disabled_ready,
                                  task);
        return;
    }
    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
gps_enabled_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    LocationGatheringContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        g_prefix_error (&error, "couldn't power up GNSS controller: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    /* After Receiver was powered up we still have to enable unsolicited NMEA events */
    if (ctx->gps_enable_step < G_N_ELEMENTS (gps_enable)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  gps_enable[ctx->gps_enable_step++],
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)gps_enabled_ready,
                                  task);
        return;
    }

    mm_obj_dbg (self, "GNSS controller is powered up");

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
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBroadbandModemTelit *self = MM_BROADBAND_MODEM_TELIT (_self);
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
        /* Only start GPS engine if not done already */
        if (!(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                             MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            start_gps = TRUE;
        self->priv->enabled_sources |= ctx->source;
    }

    if (start_gps && ctx->gps_enable_step < G_N_ELEMENTS (gps_enable)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  gps_enable[ctx->gps_enable_step++],
                                  3,
                                  FALSE,
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
    ctx->gps_enable_step = 0;
    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

static void
trace_received (MMPortSerialGps *port,
                const gchar *trace,
                MMIfaceModemLocation *self)
{
    mm_iface_modem_location_gps_update (self, trace);
}

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialGps *gps_data_port;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_telit_parent_class)->setup_ports (self);

    gps_data_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
    if (gps_data_port) {
        /* It may happen that the modem was started with GPS already enabled,
         * in this case GPSP AT command returns always error. Disable it for consistency
         */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "$GPSP=0", 3, FALSE, FALSE, NULL);

        /* Add handler for the NMEA traces */
        mm_port_serial_gps_add_trace_handler (gps_data_port,
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    }
}

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
gpsp_test_ready (MMIfaceModemLocation *self,
                 GAsyncResult *res,
                 GTask *task)
{
    GError *error = NULL;
    MMModemLocationSource sources;

    sources = GPOINTER_TO_UINT (g_task_get_task_data (task));
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        mm_obj_dbg (self, "GPS controller not supported: %s", error->message);
        g_clear_error (&error);
    } else if (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

    g_task_return_int (task, sources);
    g_object_unref (task);
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
    g_task_set_task_data (task, GUINT_TO_POINTER (sources), NULL);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$GPSP=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)gpsp_test_ready,
                              task);
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
/* After Sim Unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_ready (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* A short delay is necessary with some SIMs when
    they have just been unlocked. Using 1 second as secure margin. */
    g_timeout_add_seconds (1, (GSourceFunc) after_sim_unlock_ready, task);
}

/*****************************************************************************/
/* Setup SIM hot swap (Modem interface) */

typedef enum {
    QSS_SETUP_STEP_FIRST,
    QSS_SETUP_STEP_QUERY,
    QSS_SETUP_STEP_ENABLE_PRIMARY_PORT,
    QSS_SETUP_STEP_ENABLE_SECONDARY_PORT,
    QSS_SETUP_STEP_LAST
} QssSetupStep;

typedef struct {
    QssSetupStep step;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    GError *primary_error;
    GError *secondary_error;
} QssSetupContext;

static void qss_setup_step (GTask *task);
static void pending_csim_unlock_complete (MMBroadbandModemTelit *self);

static void
telit_qss_unsolicited_handler (MMPortSerialAt *port,
                               GMatchInfo *match_info,
                               MMBroadbandModemTelit *self)
{
    MMTelitQssStatus cur_qss_status;
    MMTelitQssStatus prev_qss_status;

    if (!mm_get_int_from_match_info (match_info, 1, (gint*)&cur_qss_status))
        return;

    prev_qss_status = self->priv->qss_status;
    self->priv->qss_status = cur_qss_status;

    if (self->priv->csim_lock_state >= CSIM_LOCK_STATE_LOCK_REQUESTED) {

        if (prev_qss_status > QSS_STATUS_SIM_REMOVED && cur_qss_status == QSS_STATUS_SIM_REMOVED) {
            mm_obj_dbg (self, "QSS handler: #QSS=0 after +CSIM=1: CSIM locked!");
            self->priv->csim_lock_state = CSIM_LOCK_STATE_LOCKED;
        }

        if (prev_qss_status == QSS_STATUS_SIM_REMOVED && cur_qss_status != QSS_STATUS_SIM_REMOVED) {
            mm_obj_dbg (self, "QSS handler: #QSS>=1 after +CSIM=0: CSIM unlocked!");
            self->priv->csim_lock_state = CSIM_LOCK_STATE_UNLOCKED;

            if (self->priv->csim_lock_timeout_id) {
                g_source_remove (self->priv->csim_lock_timeout_id);
                self->priv->csim_lock_timeout_id = 0;
            }

            pending_csim_unlock_complete (self);
        }

        return;
    }

    if (cur_qss_status != prev_qss_status)
        mm_obj_dbg (self, "QSS handler: status changed %s -> %s",
                    mm_telit_qss_status_get_string (prev_qss_status),
                    mm_telit_qss_status_get_string (cur_qss_status));

    if (self->priv->parse_qss == FALSE) {
        mm_obj_dbg (self, "QSS handler: message ignored");
        return;
    }

    if ((prev_qss_status == QSS_STATUS_SIM_REMOVED && cur_qss_status != QSS_STATUS_SIM_REMOVED) ||
        (prev_qss_status > QSS_STATUS_SIM_REMOVED && cur_qss_status == QSS_STATUS_SIM_REMOVED)) {
        mm_obj_info (self, "QSS handler: SIM swap detected");
        mm_broadband_modem_sim_hot_swap_detected (MM_BROADBAND_MODEM (self));
    }
}

static void
qss_setup_context_free (QssSetupContext *ctx)
{
    g_clear_object (&(ctx->primary));
    g_clear_object (&(ctx->secondary));
    g_clear_error (&(ctx->primary_error));
    g_clear_error (&(ctx->secondary_error));
    g_slice_free (QssSetupContext, ctx);
}

static gboolean
modem_setup_sim_hot_swap_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
telit_qss_enable_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GTask *task)
{
    QssSetupContext *ctx;
    MMPortSerialAt *port;
    GError **error;
    GRegex *pattern;

    ctx = g_task_get_task_data (task);

    if (ctx->step == QSS_SETUP_STEP_ENABLE_PRIMARY_PORT) {
        port = ctx->primary;
        error = &ctx->primary_error;
    } else if (ctx->step == QSS_SETUP_STEP_ENABLE_SECONDARY_PORT) {
        port = ctx->secondary;
        error = &ctx->secondary_error;
    } else
        g_assert_not_reached ();

    if (!mm_base_modem_at_command_full_finish (self, res, error)) {
        mm_obj_warn (self, "QSS: error enabling unsolicited on port %s: %s", mm_port_get_device (MM_PORT (port)), (*error)->message);
        goto next_step;
    }

    pattern = g_regex_new ("#QSS:\\s*([0-3])\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);
    mm_port_serial_at_add_unsolicited_msg_handler (
        port,
        pattern,
        (MMPortSerialAtUnsolicitedMsgFn)telit_qss_unsolicited_handler,
        self,
        NULL);
    g_regex_unref (pattern);

next_step:
    ctx->step++;
    qss_setup_step (task);
}

static void
telit_qss_query_ready (MMBaseModem *_self,
                       GAsyncResult *res,
                       GTask *task)
{
    MMBroadbandModemTelit *self;
    GError *error = NULL;
    const gchar *response;
    MMTelitQssStatus qss_status;
    QssSetupContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (error) {
        mm_obj_warn (self, "could not get \"#QSS?\" reply: %s", error->message);
        g_error_free (error);
        goto next_step;
    }

    qss_status = mm_telit_parse_qss_query (response, &error);
    if (error) {
        mm_obj_warn (self, "QSS query parse error: %s", error->message);
        g_error_free (error);
        goto next_step;
    }

    mm_obj_dbg (self, "QSS: current status is '%s'", mm_telit_qss_status_get_string (qss_status));
    self->priv->qss_status = qss_status;

next_step:
    ctx->step++;
    qss_setup_step (task);
}

static void
qss_setup_step (GTask *task)
{
    QssSetupContext *ctx;
    MMBroadbandModemTelit *self;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
        case QSS_SETUP_STEP_FIRST:
            ctx->step++;
            /* fall through */
        case QSS_SETUP_STEP_QUERY:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "#QSS?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback) telit_qss_query_ready,
                                      task);
            return;
        case QSS_SETUP_STEP_ENABLE_PRIMARY_PORT:
            mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                           ctx->primary,
                                           "#QSS=1",
                                           3,
                                           FALSE,
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback) telit_qss_enable_ready,
                                           task);
            return;
        case QSS_SETUP_STEP_ENABLE_SECONDARY_PORT:
            if (ctx->secondary) {
                mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                               ctx->secondary,
                                               "#QSS=1",
                                               3,
                                               FALSE,
                                               FALSE, /* raw */
                                               NULL, /* cancellable */
                                               (GAsyncReadyCallback) telit_qss_enable_ready,
                                               task);
                return;
            }
            ctx->step++;
            /* fall through */
        case QSS_SETUP_STEP_LAST:
            /* If all enabling actions failed (either both, or only primary if
             * there is no secondary), then we return an error */
            if (ctx->primary_error &&
                (ctx->secondary_error || !ctx->secondary))
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "QSS: couldn't enable unsolicited");
            else
                g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            break;

        default:
            g_assert_not_reached ();
    }
}

static void
modem_setup_sim_hot_swap (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    QssSetupContext *ctx;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (QssSetupContext);
    ctx->step = QSS_SETUP_STEP_FIRST;
    ctx->primary = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    ctx->secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));

    g_task_set_task_data (task, ctx, (GDestroyNotify) qss_setup_context_free);
    qss_setup_step (task);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface)
 *
 * NOTE: the logic must make sure that LOAD_UNLOCK_RETRIES_STEP_UNLOCK is always
 * run if LOAD_UNLOCK_RETRIES_STEP_LOCK has been run. Currently, the logic just
 * runs all intermediate steps ignoring errors (i.e. not completing the
 * operation if something fails), so the LOAD_UNLOCK_RETRIES_STEP_UNLOCK is
 * always run.
 */

#define CSIM_LOCK_STR      "+CSIM=1"
#define CSIM_UNLOCK_STR    "+CSIM=0"
#define CSIM_QUERY_TIMEOUT 3

typedef enum {
    LOAD_UNLOCK_RETRIES_STEP_FIRST,
    LOAD_UNLOCK_RETRIES_STEP_LOCK,
    LOAD_UNLOCK_RETRIES_STEP_PARENT,
    LOAD_UNLOCK_RETRIES_STEP_UNLOCK,
    LOAD_UNLOCK_RETRIES_STEP_LAST
} LoadUnlockRetriesStep;

typedef struct {
    MMUnlockRetries *retries;
    LoadUnlockRetriesStep step;
} LoadUnlockRetriesContext;

static void load_unlock_retries_step (GTask *task);

static void
load_unlock_retries_context_free (LoadUnlockRetriesContext *ctx)
{
    g_object_unref (ctx->retries);
    g_slice_free (LoadUnlockRetriesContext, ctx);
}

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return (MMUnlockRetries *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
csim_unlock_ready (MMBaseModem  *_self,
                   GAsyncResult *res,
                   GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    /* Ignore errors */
    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response) {
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
            self->priv->csim_lock_support = FEATURE_NOT_SUPPORTED;
        }
        mm_obj_warn (self, "couldn't unlock SIM card: %s", error->message);
        g_error_free (error);
    }

    if (self->priv->csim_lock_support != FEATURE_NOT_SUPPORTED)
        self->priv->csim_lock_support = FEATURE_SUPPORTED;

    ctx->step++;
    load_unlock_retries_step (task);
}

static void
parent_load_unlock_retries_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    LoadUnlockRetriesContext *ctx;
    GError                   *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!(ctx->retries = iface_modem_parent->load_unlock_retries_finish (self, res, &error))) {
        mm_obj_warn (self, "couldn't load unlock retries with generic logic: %s", error->message);
        g_error_free (error);
    }

    ctx->step++;
    load_unlock_retries_step (task);
}

static void
csim_lock_ready (MMBaseModem  *_self,
                 GAsyncResult *res,
                 GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response) {
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
            self->priv->csim_lock_support = FEATURE_NOT_SUPPORTED;
            mm_obj_warn (self, "couldn't lock SIM card: %s; continuing without CSIM lock", error->message);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "Couldn't lock SIM card: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    } else {
        self->priv->csim_lock_state = CSIM_LOCK_STATE_LOCK_REQUESTED;
    }

    if (self->priv->csim_lock_support != FEATURE_NOT_SUPPORTED) {
        self->priv->csim_lock_support = FEATURE_SUPPORTED;
    }

    ctx->step++;
    load_unlock_retries_step (task);
}

static void
handle_csim_locking (GTask    *task,
                     gboolean  is_lock)
{
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);

    switch (self->priv->csim_lock_support) {
        case FEATURE_SUPPORT_UNKNOWN:
        case FEATURE_SUPPORTED:
            if (is_lock) {
                mm_base_modem_at_command (MM_BASE_MODEM (self),
                                          CSIM_LOCK_STR,
                                          CSIM_QUERY_TIMEOUT,
                                          FALSE,
                                          (GAsyncReadyCallback) csim_lock_ready,
                                          task);
            } else {
                mm_base_modem_at_command (MM_BASE_MODEM (self),
                                          CSIM_UNLOCK_STR,
                                          CSIM_QUERY_TIMEOUT,
                                          FALSE,
                                          (GAsyncReadyCallback) csim_unlock_ready,
                                          task);
            }
            break;
        case FEATURE_NOT_SUPPORTED:
            mm_obj_dbg (self, "CSIM lock not supported by this modem; skipping %s command",
                        is_lock ? "lock" : "unlock");
            ctx->step++;
            load_unlock_retries_step (task);
            break;
        default:
            g_assert_not_reached ();
            break;
    }
}

static void
pending_csim_unlock_complete (MMBroadbandModemTelit *self)
{
    LoadUnlockRetriesContext *ctx;

    ctx = g_task_get_task_data (self->priv->csim_lock_task);

    if (!ctx->retries) {
        g_task_return_new_error (self->priv->csim_lock_task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Could not get any of the SIM unlock retries values");
    } else {
        g_task_return_pointer (self->priv->csim_lock_task, g_object_ref (ctx->retries), g_object_unref);
    }

    g_clear_object (&self->priv->csim_lock_task);
}

static gboolean
csim_unlock_periodic_check (MMBroadbandModemTelit *self)
{
    if (self->priv->csim_lock_state != CSIM_LOCK_STATE_UNLOCKED)
        mm_obj_warn (self, "CSIM is still locked after %d seconds; trying to continue anyway", CSIM_UNLOCK_MAX_TIMEOUT);

    self->priv->csim_lock_timeout_id = 0;
    pending_csim_unlock_complete (self);
    g_object_unref (self);

    return G_SOURCE_REMOVE;
}

static void
load_unlock_retries_step (GTask *task)
{
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);
    switch (ctx->step) {
        case LOAD_UNLOCK_RETRIES_STEP_FIRST:
            ctx->step++;
            /* fall through */
        case LOAD_UNLOCK_RETRIES_STEP_LOCK:
            handle_csim_locking (task, TRUE);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PARENT:
            iface_modem_parent->load_unlock_retries (
                MM_IFACE_MODEM (self),
                (GAsyncReadyCallback)parent_load_unlock_retries_ready,
                task);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_UNLOCK:
            handle_csim_locking (task, FALSE);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_LAST:
            self->priv->csim_lock_task = task;
            if (self->priv->csim_lock_state == CSIM_LOCK_STATE_LOCKED) {
                mm_obj_dbg (self, "CSIM is locked, waiting for #QSS=1");
                self->priv->csim_lock_timeout_id = g_timeout_add_seconds (CSIM_UNLOCK_MAX_TIMEOUT,
                                                                          (GSourceFunc) csim_unlock_periodic_check,
                                                                          g_object_ref(self));
            } else {
                self->priv->csim_lock_state = CSIM_LOCK_STATE_UNLOCKED;
                pending_csim_unlock_complete (self);
            }
            break;
        default:
            break;
    }
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GTask *task;
    LoadUnlockRetriesContext *ctx;

    g_assert (iface_modem_parent->load_unlock_retries);
    g_assert (iface_modem_parent->load_unlock_retries_finish);

    ctx = g_slice_new0 (LoadUnlockRetriesContext);
    ctx->step = LOAD_UNLOCK_RETRIES_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_unlock_retries_context_free);

    load_unlock_retries_step (task);
}

/*****************************************************************************/
/* Modem after power up (Modem interface) */

static gboolean
modem_after_power_up_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_after_power_up (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GTask *task;
    MMBroadbandModemTelit *modem = MM_BROADBAND_MODEM_TELIT (self);

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "stop ignoring #QSS");
    modem->priv->parse_qss = TRUE;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
telit_modem_power_down_ready (MMBaseModem *self,
                              GAsyncResult *res,
                              GTask *task)
{
    GError *error = NULL;

    if (mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_dbg (self, "sgnore #QSS unsolicited during power down/low");
        MM_BROADBAND_MODEM_TELIT (self)->priv->parse_qss = FALSE;
    }

    if (error) {
        mm_obj_warn (self, "failed modem power down: %s", error->message);
        g_clear_error (&error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              20,
                              FALSE,
                              (GAsyncReadyCallback) telit_modem_power_down_ready,
                              task);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
modem_reset_finish (MMIfaceModem *self,
                    GAsyncResult *res,
                    GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT#REBOOT",
                              8,
                              FALSE,
                              callback,
                              user_data);
}
/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    GVariant *result;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result) {
        if (error)
            g_assert (*error);
        return FALSE;
    }

    *access_technologies = (MMModemAccessTechnology) g_variant_get_uint32 (result);
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static MMBaseModemAtResponseProcessorResult
response_processor_psnt_ignore_at_errors (MMBaseModem   *self,
                                          gpointer       none,
                                          const gchar   *command,
                                          const gchar   *response,
                                          gboolean       last_command,
                                          const GError  *error,
                                          GVariant     **result,
                                          GError       **result_error)
{
    const gchar *psnt;
    const gchar *mode;

    *result = NULL;
    *result_error = NULL;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {
            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    psnt = mm_strip_tag (response, "#PSNT:");
    mode = strchr (psnt, ',');
    if (mode) {
        switch (atoi (++mode)) {
        case 0:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 1:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EDGE);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 2:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UMTS);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 3:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_HSDPA);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 4:
            if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_LTE);
            else
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 5:
            if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self))) {
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
                return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
            }
            /* Fall-through since #PSNT: 5 is not supported in other than lte modems */
        default:
            break;
        }
    }

    g_set_error (result_error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse #PSNT response: '%s'",
                 response);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
}

static MMBaseModemAtResponseProcessorResult
response_processor_service_ignore_at_errors (MMBaseModem   *self,
                                             gpointer       none,
                                             const gchar   *command,
                                             const gchar   *response,
                                             gboolean       last_command,
                                             const GError  *error,
                                             GVariant     **result,
                                             GError       **result_error)
{
    const gchar *service;

    *result = NULL;
    *result_error = NULL;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command) {
            *result_error = g_error_copy (error);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
        }

        return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
    }

    service = mm_strip_tag (response, "+SERVICE:");
    if (service) {
        switch (atoi (service)) {
        case 1:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_1XRTT);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 2:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        case 3:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EVDOA);
            return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_SUCCESS;
        default:
            break;
        }
    }

    g_set_error (result_error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse +SERVICE response: '%s'",
                 response);
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_FAILURE;
}

static const MMBaseModemAtCommand access_tech_commands[] = {
    { "#PSNT?",    3, FALSE, response_processor_psnt_ignore_at_errors },
    { "+SERVICE?", 3, FALSE, response_processor_service_ignore_at_errors },
    { NULL }
};

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        access_tech_commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* CDMA-only modems don't support changing modes, default to parent's */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_task_return_pointer (task, all, (GDestroyNotify) g_array_unref);
        g_object_unref (task);
        return;
    }

    /* Filter out those unsupported modes */
    combinations = mm_telit_build_modes_list();
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp  *self,
                                             GAsyncResult      *res,
                                             GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cind_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't enable custom +CIND settings: %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult     *res,
                                        GTask            *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't enable parent 3GPP unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Our own enable now */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self)),
        /* Enable +CIEV only for: signal, service, roam */
        "AT+CIND=0,1,1,0,0,0,1,0,0",
        5,
        FALSE,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cind_set_ready,
        task);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp    *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/

MMBroadbandModemTelit *
mm_broadband_modem_telit_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_TELIT,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, FALSE,
                         NULL);
}

static void
mm_broadband_modem_telit_init (MMBroadbandModemTelit *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_TELIT,
                                              MMBroadbandModemTelitPrivate);

    self->priv->csim_lock_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->csim_lock_state = CSIM_LOCK_STATE_UNKNOWN;
    self->priv->qss_status = QSS_STATUS_UNKNOWN;
    self->priv->parse_qss = TRUE;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->set_current_bands = mm_shared_telit_modem_set_current_bands;
    iface->set_current_bands_finish = mm_shared_telit_modem_set_current_bands_finish;
    iface->load_current_bands = mm_shared_telit_modem_load_current_bands;
    iface->load_current_bands_finish = mm_shared_telit_modem_load_current_bands_finish;
    iface->load_supported_bands = mm_shared_telit_modem_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_telit_modem_load_supported_bands_finish;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->modem_after_power_up = modem_after_power_up;
    iface->modem_after_power_up_finish = modem_after_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = mm_shared_telit_load_current_modes;
    iface->load_current_modes_finish = mm_shared_telit_load_current_modes_finish;
    iface->set_current_modes = mm_shared_telit_set_current_modes;
    iface->set_current_modes_finish = mm_shared_telit_set_current_modes_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->setup_sim_hot_swap = modem_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = modem_setup_sim_hot_swap_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
}

static void
shared_telit_init (MMSharedTelit *iface)
{
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
mm_broadband_modem_telit_class_init (MMBroadbandModemTelitClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemTelitPrivate));
    broadband_modem_class->setup_ports = setup_ports;
}
