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
 * Copyright (C) 2018-2020 Aleksander Morgado <aleksander@aleksander.es>
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
#include "mm-modem-helpers-quectel.h"

/*****************************************************************************/
/* Private context */

#define PRIVATE_TAG "shared-quectel-private-tag"
static GQuark private_quark;

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

typedef struct {
    MMBroadbandModemClass *broadband_modem_class_parent;
    MMIfaceModem          *iface_modem_parent;
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  provided_sources;
    MMModemLocationSource  enabled_sources;
    FeatureSupport         qgps_supported;
    GRegex                *qgpsurc_regex;
    GRegex                *qlwurc_regex;
} Private;

static void
private_free (Private *priv)
{
    g_regex_unref (priv->qgpsurc_regex);
    g_regex_unref (priv->qlwurc_regex);
    g_slice_free (Private, priv);
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
        priv->qgpsurc_regex     = g_regex_new ("\\r\\n\\+QGPSURC:.*", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        priv->qlwurc_regex      = g_regex_new ("\\r\\n\\+QLWURC:.*", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

        g_assert (MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_broadband_modem_class);
        priv->broadband_modem_class_parent = MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_broadband_modem_class (self);

        g_assert (MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_modem_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_modem_location_interface (self);

        g_assert (MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_modem_interface);
        priv->iface_modem_parent = MM_SHARED_QUECTEL_GET_INTERFACE (self)->peek_parent_modem_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }
    return priv;
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

void
mm_shared_quectel_setup_ports (MMBroadbandModem *self)
{
    Private        *priv;
    MMPortSerialAt *ports[2];
    guint           i;

    priv = get_private (MM_SHARED_QUECTEL (self));
    g_assert (priv->broadband_modem_class_parent);
    g_assert (priv->broadband_modem_class_parent->setup_ports);

    /* Parent setup always first */
    priv->broadband_modem_class_parent->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* Ignore +QGPSURC */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            priv->qgpsurc_regex,
            NULL, NULL, NULL);

        /* Ignore +QLWURC */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            priv->qlwurc_regex,
            NULL, NULL, NULL);
    }
}

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
    g_autoptr(GError) error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish (self, res, &error))
        mm_obj_warn (self, "couldn't check SIM swap: %s", error->message);
    else
        mm_obj_dbg (self, "check SIM swap completed");
}

static void
quectel_qusim_unsolicited_handler (MMPortSerialAt *port,
                                   GMatchInfo     *match_info,
                                   MMIfaceModem   *self)
{
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish)
        return;

    mm_obj_dbg (self, "checking SIM swap");
    MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap (
        self,
        NULL,
        (GAsyncReadyCallback)quectel_qusim_check_for_sim_swap_ready,
        NULL);
}

/*****************************************************************************/
/* Setup SIM hot swap context (Modem interface) */

gboolean
mm_shared_quectel_setup_sim_hot_swap_finish (MMIfaceModem  *self,
                                             GAsyncResult  *res,
                                             GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_sim_hot_swap_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    Private           *priv;
    g_autoptr(GError)  error = NULL;

    priv = get_private (MM_SHARED_QUECTEL (self));

    if (!priv->iface_modem_parent->setup_sim_hot_swap_finish (self, res, &error))
        mm_obj_dbg (self, "additional SIM hot swap detection setup failed: %s", error->message);

    /* The +QUSIM based setup never fails, so we can safely return success here */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_quectel_setup_sim_hot_swap (MMIfaceModem        *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    Private        *priv;
    MMPortSerialAt *ports[2];
    GTask          *task;
    GRegex         *pattern;
    guint           i;

    priv = get_private (MM_SHARED_QUECTEL (self));

    task = g_task_new (self, NULL, callback, user_data);

    ports[0] = mm_base_modem_peek_port_primary   (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    pattern = g_regex_new ("\\+QUSIM:\\s*1\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (ports[i])
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                pattern,
                (MMPortSerialAtUnsolicitedMsgFn)quectel_qusim_unsolicited_handler,
                self,
                NULL);
    }

    g_regex_unref (pattern);
    mm_obj_dbg (self, "+QUSIM detection set up");

    /* Now, if available, setup parent logic */
    if (priv->iface_modem_parent->setup_sim_hot_swap &&
        priv->iface_modem_parent->setup_sim_hot_swap_finish) {
        priv->iface_modem_parent->setup_sim_hot_swap (self,
                                                      (GAsyncReadyCallback) parent_setup_sim_hot_swap_ready,
                                                      task);
        return;
    }

    /* Otherwise, we're done */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* GPS trace received */

static void
trace_received (MMPortSerialGps      *port,
                const gchar          *trace,
                MMIfaceModemLocation *self)
{
    mm_iface_modem_location_gps_update (self, trace);
}

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

MMModemLocationSource
mm_shared_quectel_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                     GAsyncResult          *res,
                                                     GError               **error)
{
    GError *inner_error = NULL;
    gssize  value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
probe_qgps_ready (MMBaseModem  *_self,
                  GAsyncResult *res,
                  GTask        *task)
{
    MMSharedQuectel       *self;
    Private               *priv;
    MMModemLocationSource  sources;

    self = MM_SHARED_QUECTEL (g_task_get_source_object (task));
    priv = get_private (self);

    priv->qgps_supported = (!!mm_base_modem_at_command_finish (_self, res, NULL) ?
                            FEATURE_SUPPORTED : FEATURE_NOT_SUPPORTED);

    mm_obj_dbg (self, "GPS management with +QGPS is %ssupported",
                priv->qgps_supported ? "" : "not ");

    /* Recover parent sources */
    sources = GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Only flag as provided those sources not already provided by the parent */
    if (priv->qgps_supported == FEATURE_SUPPORTED) {
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_NMEA))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_NMEA;
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_RAW))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_RAW;
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))
            priv->provided_sources |= MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED;

        sources |= priv->provided_sources;

        /* Add handler for the NMEA traces in the GPS data port */
        mm_port_serial_gps_add_trace_handler (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)),
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    }

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    Private               *priv;
    MMModemLocationSource  sources;
    GError                *error = NULL;

    priv = get_private (MM_SHARED_QUECTEL (self));
    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own check. If we don't have any GPS port, we're done */
    if (!mm_base_modem_peek_port_gps (MM_BASE_MODEM (self))) {
        mm_obj_dbg (self, "no GPS data port found: no GPS capabilities");
        g_task_return_int (task, sources);
        g_object_unref (task);
        return;
    }

    /* Store parent supported sources in task data */
    g_task_set_task_data (task, GUINT_TO_POINTER (sources), NULL);

    /* Probe QGPS support */
    g_assert (priv->qgps_supported == FEATURE_SUPPORT_UNKNOWN);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+QGPS=?",
                              3,
                              TRUE, /* cached */
                              (GAsyncReadyCallback)probe_qgps_ready,
                              task);
}

void
mm_shared_quectel_location_load_capabilities (MMIfaceModemLocation *_self,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (_self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_QUECTEL (_self));

    /* Chain up parent's setup */
    priv->iface_modem_location_parent->load_capabilities (_self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

/* NOTES:
 *  1) "+QGPSCFG=\"nmeasrc\",1" will be necessary for getting location data
 *     without the nmea port.
 *  2) may be necessary to set "+QGPSCFG=\"gpsnmeatype\".
 */
static const MMBaseModemAtCommand gps_startup[] = {
    { "+QGPSCFG=\"outport\",\"usbnmea\"", 3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { "+QGPS=1",                          3, FALSE, mm_base_modem_response_processor_no_result_continue },
    { NULL }
};

gboolean
mm_shared_quectel_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                    GAsyncResult          *res,
                                                    GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_startup_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMModemLocationSource  source;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_QUECTEL (self));

    mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Check if the nmea/raw gps port exists and is available */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
        if (!gps_port || !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
            if (error)
                g_task_return_error (task, error);
            else
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't open raw GPS serial port");
        } else {
            /* GPS port was successfully opened */
            priv->enabled_sources |= source;
            g_task_return_boolean (task, TRUE);
        }
    } else {
        /* No need to open GPS port */
        priv->enabled_sources |= source;
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_SHARED_QUECTEL (self));
    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_quectel_enable_location_gathering (MMIfaceModemLocation  *self,
                                             MMModemLocationSource  source,
                                             GAsyncReadyCallback    callback,
                                             gpointer               user_data)
{
    GTask    *task;
    Private  *priv;
    gboolean  start_gps = FALSE;

    priv = get_private (MM_SHARED_QUECTEL (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering_finish);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    /* Check if the source is provided by the parent */
    if (!(priv->provided_sources & source)) {
        priv->iface_modem_location_parent->enable_location_gathering (
            self,
            source,
            (GAsyncReadyCallback)parent_enable_location_gathering_ready,
            task);
        return;
    }

    /* Only start GPS engine if not done already */
    start_gps = ((source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                            MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                            MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) &&
                 !(priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                            MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                            MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)));

    if (start_gps) {
        mm_base_modem_at_sequence (
            MM_BASE_MODEM (self),
            gps_startup,
            NULL, /* response_processor_context */
            NULL, /* response_processor_context_free */
            (GAsyncReadyCallback)gps_startup_ready,
            task);
        return;
    }

    /* If the GPS is already running just return */
    priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

gboolean
mm_shared_quectel_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                     GAsyncResult          *res,
                                                     GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
qgps_end_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disable_location_gathering_parent_ready (MMIfaceModemLocation *self,
                                         GAsyncResult         *res,
                                         GTask                *task)
{
    GError  *error;
    Private *priv;

    priv = get_private (MM_SHARED_QUECTEL (self));
    if (!priv->iface_modem_location_parent->disable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_quectel_disable_location_gathering (MMIfaceModemLocation  *self,
                                              MMModemLocationSource  source,
                                              GAsyncReadyCallback    callback,
                                              gpointer               user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (MM_SHARED_QUECTEL (self));
    g_assert (priv->iface_modem_location_parent);

    task = g_task_new (self, NULL, callback, user_data);
    priv->enabled_sources &= ~source;

    /* Pass handling to parent if we don't handle it */
    if (!(source & priv->provided_sources)) {
        /* The step to disable location gathering may not exist */
        if (priv->iface_modem_location_parent->disable_location_gathering &&
            priv->iface_modem_location_parent->disable_location_gathering_finish) {
            priv->iface_modem_location_parent->disable_location_gathering (self,
                                                                           source,
                                                                           (GAsyncReadyCallback)disable_location_gathering_parent_ready,
                                                                           task);
            return;
        }

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Turn off gps on the modem if the source uses gps,
     * and there are no other gps sources enabled */
    if ((source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                   MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                   MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) &&
        !(priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                   MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                   MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))) {
        /* Close the data port if we don't need it anymore */
        if (source & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA)) {
            MMPortSerialGps *gps_port;

            gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
            if (gps_port)
                mm_port_serial_close (MM_PORT_SERIAL (gps_port));
        }

        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+QGPSEND",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)qgps_end_ready,
                                  task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Check support (Time interface) */

gboolean
mm_shared_quectel_time_check_support_finish (MMIfaceModemTime  *self,
                                             GAsyncResult      *res,
                                             GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
support_cclk_query_ready (MMBaseModem  *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    /* error never returned */
    g_task_return_boolean (task, !!mm_base_modem_at_command_finish (self, res, NULL));
    g_object_unref (task);
}

static void
support_cclk_query (GTask *task)
{
    MMBaseModem *self;

    self = g_task_get_source_object (task);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CCLK?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)support_cclk_query_ready,
                              task);
}

static void
ctzu_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    g_autoptr(GError) error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error))
        mm_obj_warn (self, "couldn't enable automatic time zone update: %s", error->message);

    support_cclk_query (task);
}

static void
ctzu_test_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    g_autoptr(GError)  error = NULL;
    const gchar       *response;
    gboolean           supports_disable;
    gboolean           supports_enable;
    gboolean           supports_enable_update_rtc;
    const gchar       *cmd = NULL;

    /* If CTZU isn't supported, run CCLK right away */
    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        support_cclk_query (task);
        return;
    }

    if (!mm_quectel_parse_ctzu_test_response (response,
                                              self,
                                              &supports_disable,
                                              &supports_enable,
                                              &supports_enable_update_rtc,
                                              &error)) {
        mm_obj_warn (self, "couldn't parse +CTZU test response: %s", error->message);
        support_cclk_query (task);
        return;
    }

    /* Custom time support check because some Quectel modems (e.g. EC25) require
     * +CTZU=3 in order to have the CCLK? time reported in localtime, instead of
     * UTC time. */
    if (supports_enable_update_rtc)
        cmd = "+CTZU=3";
    else if (supports_enable)
        cmd = "+CTZU=1";

    if (!cmd) {
        mm_obj_warn (self, "unknown +CTZU support");
        support_cclk_query (task);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)ctzu_set_ready,
                              task);
}

void
mm_shared_quectel_time_check_support (MMIfaceModemTime    *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CTZU=?",
                              3,
                              TRUE, /* cached! */
                              (GAsyncReadyCallback)ctzu_test_ready,
                              task);
}

/*****************************************************************************/

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
