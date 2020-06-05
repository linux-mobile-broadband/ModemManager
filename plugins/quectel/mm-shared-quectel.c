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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-quectel.h"

/*****************************************************************************/
/* Firmware update settings loading (Firmware interface) */

MMFirmwareUpdateSettings *
mm_shared_quectel_firmware_load_update_settings_finish (MMIfaceModemFirmware  *self,
                                                        GAsyncResult          *res,
                                                        GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
qfastboot_test_ready (MMBaseModem  *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMFirmwareUpdateSettings *update_settings;

    if (!mm_base_modem_at_command_finish (self, res, NULL))
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE);
    else {
        update_settings = mm_firmware_update_settings_new (MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT);
        mm_firmware_update_settings_set_fastboot_at (update_settings, "AT+QFASTBOOT");
    }

    g_task_return_pointer (task, update_settings, g_object_unref);
    g_object_unref (task);
}

void
mm_shared_quectel_firmware_load_update_settings (MMIfaceModemFirmware *self,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT+QFASTBOOT=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)qfastboot_test_ready,
                              task);
}

/*****************************************************************************/
/* "+QUSIM: 1" URC is emitted by Quectel modems after the USIM has been
 * (re)initialized. We register a handler for this URC and perform a check
 * for SIM swap when it is encountered. The motivation for this is to detect
 * M2M eUICC profile switches. According to SGP.02 chapter 3.2.1, the eUICC
 * shall trigger a REFRESH operation with eUICC reset when a new profile is
 * enabled. The +QUSIM URC appears after the eUICC has restarted and can act
 * as a trigger for profile switch check. This should basically be handled
 * the same as a physical SIM swap, so the existing SIM hot swap mechanism
 * is used.
 */

static void
quectel_qusim_check_for_sim_swap_ready (MMIfaceModem *self,
                                        GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't check SIM swap: %s", error->message);
        g_error_free (error);
    } else
        mm_obj_dbg (self, "check SIM swap completed");
}

static void
quectel_qusim_unsolicited_handler (MMPortSerialAt *port,
                                   GMatchInfo *match_info,
                                   MMIfaceModem* self)
{
    if (MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish) {
        mm_obj_dbg (self, "checking SIM swap");
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap (
            self,
            (GAsyncReadyCallback)quectel_qusim_check_for_sim_swap_ready,
            NULL);
    }
}

gboolean
mm_shared_quectel_setup_sim_hot_swap_finish (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_shared_quectel_setup_sim_hot_swap (MMIfaceModem *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMPortSerialAt *port_primary;
    MMPortSerialAt *port_secondary;
    GTask *task;
    GRegex *pattern;

    task = g_task_new (self, NULL, callback, user_data);

    port_primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    port_secondary = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    pattern = g_regex_new ("\\+QUSIM:\\s*1\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);

    if (port_primary)
        mm_port_serial_at_add_unsolicited_msg_handler (
            port_primary,
            pattern,
            (MMPortSerialAtUnsolicitedMsgFn)quectel_qusim_unsolicited_handler,
            self,
            NULL);

    if (port_secondary)
        mm_port_serial_at_add_unsolicited_msg_handler (
            port_secondary,
            pattern,
            (MMPortSerialAtUnsolicitedMsgFn)quectel_qusim_unsolicited_handler,
            self,
            NULL);

    g_regex_unref (pattern);
    mm_obj_dbg (self, "+QUSIM detection set up");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Location State Variables */

#define PRIVATE_TAG "shared-quectel-private-tag"
static GQuark private_quark;

static const gchar *gps_startup[] = {
    "+QGPSCFG=\"outport\",\"usbnmea\"",
    // TODO: "+QGPSCFG=\"nmeasrc\",1" will be necessary for getting location data without the nmea port

    // perhaps these should be the highest value of everything that has nmea in it?
    // TODO: it may be necessary to set "+QGPSCFG=\"gpsnmeatype\", however 
    // the correct value will very based on the modem, it may be enough to

    // TODO: is it possible to report a gps interval,
    // or even better, allow users to set one?
    "+QGPS=1",
};

static const int qgps_nmea_port_sources = (
        MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA);

typedef struct {
    MMModemLocationSource source;
    unsigned long         idx;
    GError               *command_error;
} LocationGatheringContext;

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

typedef struct {
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  provided_sources;
    MMModemLocationSource  enabled_sources;
    FeatureSupport         qgps_supported;
} Private;

static LocationGatheringContext *
location_gathering_context_new (MMModemLocationSource source)
{
    LocationGatheringContext *ctx = g_new (LocationGatheringContext, 1);
    ctx->source                   = source;
    ctx->idx                      = 0;
    ctx->command_error            = NULL;

    return ctx;
}

static Private *
get_private (MMSharedQuectel *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        priv->provided_sources  = MM_MODEM_LOCATION_SOURCE_NONE;
        priv->enabled_sources   = MM_MODEM_LOCATION_SOURCE_NONE;
        priv->qgps_supported    = FEATURE_SUPPORT_UNKNOWN;

        g_assert (MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata (G_OBJECT (self), private_quark, priv);
    }
    return priv;
}

/*****************************************************************************/
/* Functions used to probe & report Location Support */

MMModemLocationSource
mm_shared_quectel_location_load_capabilities_finish (MMIfaceModemLocation *self,
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
build_provided_location_sources (GTask *task)
{
    MMModemLocationSource parent_sources;
    MMSharedQuectel *self;
    Private *priv;

    parent_sources = GPOINTER_TO_UINT (g_task_get_task_data (task));
    self = MM_SHARED_QUECTEL (g_task_get_source_object (task));
    priv = get_private (self);

    /* Only enable any location sources the modem supports gps
     * Then only report supporting a location source
     * if it's not already supported by the parent location source */
    if (priv->qgps_supported == FEATURE_SUPPORTED) {
        if (!(parent_sources & MM_MODEM_LOCATION_SOURCE_GPS_NMEA))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_NMEA;
        if (!(parent_sources & MM_MODEM_LOCATION_SOURCE_GPS_RAW))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_RAW;
        if (!(parent_sources & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED;
    }

    /* So we're done, complete */
    g_task_return_int (task, priv->provided_sources);
    g_object_unref (task);
}

static void
probe_qgps_ready (MMBaseModem *_self,
                  GAsyncResult *res,
                  GTask *task)
{
    MMSharedQuectel *self;
    Private *priv;
    GError *error = NULL;

    self = MM_SHARED_QUECTEL (g_task_get_source_object (task));
    priv = get_private (self);

    priv->qgps_supported = (!!mm_base_modem_at_command_full_finish (_self, res, &error) ?
                            FEATURE_SUPPORTED : FEATURE_NOT_SUPPORTED);

    build_provided_location_sources (task);
}

static void
probe_qgps (GTask *task)
{
    MMSharedQuectel *self;

    self = MM_SHARED_QUECTEL (g_task_get_source_object (task));

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+QGPS?",
                              3,
                              FALSE, /* not cached */
                              (GAsyncReadyCallback)probe_qgps_ready,
                              task);
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GTask *task)
{
    Private *priv;
    MMModemLocationSource sources;
    GError *error = NULL;

    priv = get_private (MM_SHARED_QUECTEL (self));
    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_set_task_data (task, GUINT_TO_POINTER (sources), NULL);

    if (priv->qgps_supported == FEATURE_SUPPORT_UNKNOWN)
        probe_qgps (task);
    else
        build_provided_location_sources (task);
}

void
mm_shared_quectel_location_load_capabilities (MMIfaceModemLocation *_self,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data)
{
    GTask *task;
    Private *priv;

    task = g_task_new (_self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_QUECTEL (_self));

    /* Chain up parent's setup */
    priv->iface_modem_location_parent->load_capabilities (_self,
                                                         (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                         task);
}

/*****************************************************************************/
/* Functions used to Enable Location */

static void qgps_enable_loop (MMBaseModem *self, GTask *task);

gboolean
mm_shared_quectel_enable_location_gathering_finish (MMIfaceModemLocation *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
qgps_check_enabled_ready (MMBaseModem *self, GAsyncResult *res, GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    LocationGatheringContext *ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);

    if (!response)
        g_task_return_error (task, error);
    else if (!g_str_equal (mm_strip_tag (response, "+QGPS:"), "1"))
        g_task_return_error (task, ctx->command_error);
    else {
        // TODO: Do I need to free ctx->command_error?
        ctx->command_error = NULL;
        qgps_enable_loop (self, task);
        return;
    }
    g_object_unref (task);
}

static void
qgps_enable_command_ready (MMBaseModem *self, GAsyncResult *res, GTask *task)
{
    LocationGatheringContext *ctx = g_task_get_task_data (task);
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error)) {
        ctx->command_error = error;
    }

    qgps_enable_loop (self, task);
}

static void
qgps_enable_loop (MMBaseModem *self, GTask *task)
{
    MMPortSerialGps *gps_port;
    LocationGatheringContext *ctx = g_task_get_task_data (task);
    GError *error = NULL;
    
    /* If there are more commands run them, then return */
    if (ctx->idx < G_N_ELEMENTS (gps_startup)) {
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                                       gps_startup[ctx->idx],
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL,  /* cancellable */
                                       (GAsyncReadyCallback)qgps_enable_command_ready,
                                       task);
        ctx->idx++;
        return;
    }

    if (ctx->command_error != NULL) {
        mm_base_modem_at_command (self,
                                  "+QGPS?",
                                  3, // timeout
                                  FALSE, // not cached
                                  (GAsyncReadyCallback)qgps_check_enabled_ready,
                                  task);
        return;
    }

    // TODO: The NMEA port isn't necessary to get NMEA data, use +QGPSNMEA to retrive data
    /* Last run Only: Check if the nmea/raw gps port
     * exists and is available Otherwise throw an error */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
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
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    GError  *error;
    Private *priv;

    priv = get_private (MM_SHARED_QUECTEL (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->
            enable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_quectel_enable_location_gathering (MMIfaceModemLocation *self,
                                             MMModemLocationSource source,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    LocationGatheringContext *ctx;
    GTask *task;
    Private *priv;
    gboolean start_gps = FALSE;

    priv = get_private (MM_SHARED_QUECTEL (self));
    task = g_task_new (self, NULL, callback, user_data);

    /* If the parent can enable the modem let it */
    if (!(priv->provided_sources& source)) {
        if (priv->iface_modem_location_parent->enable_location_gathering &&
            priv->iface_modem_location_parent->enable_location_gathering_finish) {
            g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);
            priv->iface_modem_location_parent->enable_location_gathering (
                self,
                source,
                (GAsyncReadyCallback)parent_enable_location_gathering_ready,
                task);
            return;
        }

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx = location_gathering_context_new (source);
    g_task_set_task_data (task, ctx, g_free);

    // NMEA and UNMANAGED are both enabled in the same way
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                       MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        if (priv->enabled_sources == MM_MODEM_LOCATION_SOURCE_NONE)
            start_gps = TRUE;

        priv->enabled_sources |= ctx->source;
    }

    if (start_gps) {
        qgps_enable_loop (MM_BASE_MODEM (self), task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if the GPS is already running just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Functions used to Disable Location */

gboolean
mm_shared_quectel_disable_location_gathering_finish (MMIfaceModemLocation *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
qgps_check_disabled_ready (MMBaseModem *self, GAsyncResult *res, GTask *task)
{
    const gchar *response;
    GError      *error = NULL;

    /* Something is very wrong if we can't query the
     * state of gps while we're disabling the gps */
    // TODO: could it make sense to report success?
    // if the modem won't tell us what the state of the gps is,
    // and modem manager should prbably treat it as disabled right?
    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_assert_not_reached ();
        g_task_return_error (task, error);
    } else if (!g_str_equal (mm_strip_tag (response, "+QGPS:"), "0"))
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Modem did not turn off");
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
qgps_disabled_ready (MMBaseModem *self, GAsyncResult *res, GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error)) {
        mm_base_modem_at_command (self,
                                  "+QGPS?",
                                  3, // timeout
                                  FALSE, // not cached
                                  (GAsyncReadyCallback)qgps_check_disabled_ready,
                                  task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disable_location_gathering_parent_ready (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    GError  *error;
    Private *priv = get_private (MM_SHARED_QUECTEL (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->
            disable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_quectel_disable_location_gathering (MMIfaceModemLocation *self,
                                              MMModemLocationSource source,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data)
{
    GTask           *task;
    MMPortSerialGps *gps_port;
    Private         *priv = get_private (MM_SHARED_QUECTEL (self));

    task = g_task_new (self, NULL, callback, user_data);
    priv->enabled_sources &= ~source;

    /* Pass handling to parent if we don't handle it */
    if (!(source & priv->provided_sources) &&
        priv->iface_modem_location_parent->disable_location_gathering &&
        priv->iface_modem_location_parent->disable_location_gathering_finish) {
        // TODO: Is there anything I can do about this long line?
        priv->iface_modem_location_parent->
            disable_location_gathering (self,
                                        source,
                                        (GAsyncReadyCallback)disable_location_gathering_parent_ready,
                                        user_data);
        return;
    }

    /* Close the nmea port if we don't need it anymore */
    if (source & qgps_nmea_port_sources &&
        !(priv->enabled_sources & qgps_nmea_port_sources)) {
        gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
        if (gps_port)
            mm_port_serial_close (MM_PORT_SERIAL (gps_port));
    }

    /* Turn off gps on the modem if the source uses gps,
     * and there are no other gps sources enabled */
    if ((source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                   MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                   MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) &&
        priv->enabled_sources == MM_MODEM_LOCATION_SOURCE_NONE) {
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                                       "+QGPSEND",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL,  /* cancellable */
                                       (GAsyncReadyCallback)qgps_disabled_ready,
                                       task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
shared_quectel_init (gpointer g_iface)
{
}

GType
mm_shared_quectel_get_type (void)
{
    static GType shared_quectel_type = 0;

    if (!G_UNLIKELY (shared_quectel_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQuectel),  /* class_size */
            shared_quectel_init,       /* base_init */
            NULL,                      /* base_finalize */
        };

        shared_quectel_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQuectel", &info, 0);
        g_type_interface_add_prerequisite (shared_quectel_type, MM_TYPE_IFACE_MODEM_FIRMWARE);
    }

    return shared_quectel_type;
}
