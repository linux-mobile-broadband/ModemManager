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
 * Copyright (C) 2012 Google Inc.
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "mm-broadband-modem-qmi.h"

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-oma.h"
#include "mm-sim-qmi.h"
#include "mm-bearer-qmi.h"
#include "mm-sms-qmi.h"
#include "mm-sms-part-3gpp.h"
#include "mm-sms-part-cdma.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_oma_init (MMIfaceModemOma *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);
static void iface_modem_signal_init (MMIfaceModemSignal *iface);

static MMIfaceModemMessaging *iface_modem_messaging_parent;
static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_OMA, iface_modem_oma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init))

struct _MMBroadbandModemQmiPrivate {
    /* Cached device IDs, retrieved by the modem interface when loading device
     * IDs, and used afterwards in the 3GPP and CDMA interfaces. */
    gchar *imei;
    gchar *meid;
    gchar *esn;

    /* Cached supported radio interfaces; in order to load supported modes */
    GArray *supported_radio_interfaces;

    /* Cached supported frequency bands; in order to handle ANY */
    GArray *supported_bands;

    /* 3GPP and CDMA share unsolicited events setup/enable/disable/cleanup */
    gboolean unsolicited_events_enabled;
    gboolean unsolicited_events_setup;
    guint event_report_indication_id;
#if defined WITH_NEWEST_QMI_COMMANDS
    guint signal_info_indication_id;
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* New devices may not support the legacy DMS UIM commands */
    gboolean dms_uim_deprecated;

    /* 3GPP/CDMA registration helpers */
    gchar *current_operator_id;
    gchar *current_operator_description;
    gboolean unsolicited_registration_events_enabled;
    gboolean unsolicited_registration_events_setup;
    guint serving_system_indication_id;
#if defined WITH_NEWEST_QMI_COMMANDS
    guint system_info_indication_id;
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* CDMA activation helpers */
    MMModemCdmaActivationState activation_state;
    guint activation_event_report_indication_id;
    GTask *activation_task;

    /* Messaging helpers */
    gboolean messaging_fallback_at;
    gboolean messaging_unsolicited_events_enabled;
    gboolean messaging_unsolicited_events_setup;
    guint messaging_event_report_indication_id;

    /* Location helpers */
    MMModemLocationSource enabled_sources;
    guint location_event_report_indication_id;

    /* Oma helpers */
    gboolean oma_unsolicited_events_enabled;
    gboolean oma_unsolicited_events_setup;
    guint oma_event_report_indication_id;

    /* Firmware helpers */
    GList *firmware_list;
    MMFirmwareProperties *current_firmware;

    /* For notifying when the qmi-proxy connection is dead */
    guint qmi_device_removed_id;
};

/*****************************************************************************/

static QmiClient *
peek_qmi_client (MMBroadbandModemQmi *self,
                 QmiService service,
                 GError **error)
{
    MMPortQmi *port;
    QmiClient *client;

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    if (!port) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek QMI port");
        return NULL;
    }

    client = mm_port_qmi_peek_client (port,
                                      service,
                                      MM_PORT_QMI_FLAG_DEFAULT);
    if (!client)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek client for service '%s'",
                     qmi_service_get_string (service));

    return client;
}

static gboolean
ensure_qmi_client (MMBroadbandModemQmi *self,
                   QmiService service,
                   QmiClient **o_client,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GError *error = NULL;
    QmiClient *client;

    client = peek_qmi_client (self, service, &error);
    if (!client) {
        g_task_report_error (self, callback, user_data, ensure_qmi_client, error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

/*****************************************************************************/
/* Power cycle */

static gboolean
power_cycle_finish (MMBroadbandModemQmi *self,
                    GAsyncResult *res,
                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
power_cycle_set_operating_mode_reset_ready (QmiClientDms *client,
                                            GAsyncResult *res,
                                            GTask *task)
{
    QmiMessageDmsSetOperatingModeOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output ||
        !qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
    } else {
        mm_info ("Modem is being rebooted now");
        g_task_return_boolean (task, TRUE);
    }

    if (output)
        qmi_message_dms_set_operating_mode_output_unref (output);

    g_object_unref (task);
}

static void
power_cycle_set_operating_mode_offline_ready (QmiClientDms *client,
                                              GAsyncResult *res,
                                              GTask *task)
{
    QmiMessageDmsSetOperatingModeInput *input;
    QmiMessageDmsSetOperatingModeOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_set_operating_mode_output_unref (output);
        return;
    }

    qmi_message_dms_set_operating_mode_output_unref (output);

    /* Now, go into reset mode. This will fully reboot the modem, and the current
     * modem object should get disposed. */
    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, QMI_DMS_OPERATING_MODE_RESET, NULL);
    qmi_client_dms_set_operating_mode (client,
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)power_cycle_set_operating_mode_reset_ready,
                                       task);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

static void
power_cycle (MMBroadbandModemQmi *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    QmiMessageDmsSetOperatingModeInput *input;
    GTask *task;
    QmiClient *client;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Now, go into offline mode */
    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, QMI_DMS_OPERATING_MODE_OFFLINE, NULL);
    qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (client),
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)power_cycle_set_operating_mode_offline_ready,
                                       task);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

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
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBaseBearer *bearer;
    GTask *task;

    /* We just create a MMBearerQmi */
    bearer = mm_bearer_qmi_new (MM_BROADBAND_MODEM_QMI (self), properties);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Current Capabilities loading (Modem interface) */

typedef struct {
    QmiClientNas *nas_client;
    QmiClientDms *dms_client;
    gboolean run_get_system_selection_preference;
    gboolean run_get_technology_preference;
    MMQmiCapabilitiesContext capabilities_context;
} LoadCurrentCapabilitiesContext;

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_CAPABILITY_NONE;
    }
    return (MMModemCapability)value;
}

static void
load_current_capabilities_context_free (LoadCurrentCapabilitiesContext *ctx)
{
    g_object_unref (ctx->nas_client);
    g_object_unref (ctx->dms_client);
    g_slice_free (LoadCurrentCapabilitiesContext, ctx);
}

static void load_current_capabilities_context_step (GTask *task);

static void
load_current_capabilities_get_capabilities_ready (QmiClientDms *client,
                                                  GAsyncResult *res,
                                                  GTask *task)
{
    LoadCurrentCapabilitiesContext *ctx;
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Capabilities: ");
        g_task_return_error (task, error);
    } else {
        guint i;
        GArray *radio_interface_list;

        qmi_message_dms_get_capabilities_output_get_info (
            output,
            NULL, /* info_max_tx_channel_rate */
            NULL, /* info_max_rx_channel_rate */
            NULL, /* info_data_service_capability */
            NULL, /* info_sim_capability */
            &radio_interface_list,
            NULL);

        for (i = 0; i < radio_interface_list->len; i++) {
            ctx->capabilities_context.dms_capabilities |=
                mm_modem_capability_from_qmi_radio_interface (g_array_index (radio_interface_list,
                                                                             QmiDmsRadioInterface,
                                                                             i));
        }
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_task_return_int (task,
                       mm_modem_capability_from_qmi_capabilities_context (&ctx->capabilities_context));
    g_object_unref (task);
}

static void
load_current_capabilities_get_technology_preference_ready (QmiClientNas *client,
                                                           GAsyncResult *res,
                                                           GTask *task)
{
    LoadCurrentCapabilitiesContext *ctx;
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_get_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_technology_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't get technology preference: %s", error->message);
        g_error_free (error);
    } else {
        qmi_message_nas_get_technology_preference_output_get_active (
            output,
            &ctx->capabilities_context.nas_tp_mask,
            NULL, /* duration */
            NULL);
    }

    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);

    /* Mark as TP already run */
    ctx->run_get_technology_preference = FALSE;
    load_current_capabilities_context_step (task);
}

static void
load_current_capabilities_get_system_selection_preference_ready (QmiClientNas *client,
                                                                 GAsyncResult *res,
                                                                 GTask *task)
{
    LoadCurrentCapabilitiesContext *ctx;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't get system selection preference: %s", error->message);
        g_error_free (error);
    } else {
        qmi_message_nas_get_system_selection_preference_output_get_mode_preference (
            output,
            &ctx->capabilities_context.nas_ssp_mode_preference_mask,
            NULL);
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    /* Mark as SSP already run */
    ctx->run_get_system_selection_preference = FALSE;
    load_current_capabilities_context_step (task);
}

static void
load_current_capabilities_context_step (GTask *task)
{
    LoadCurrentCapabilitiesContext *ctx;

    ctx = g_task_get_task_data (task);
    if (ctx->run_get_system_selection_preference) {
        qmi_client_nas_get_system_selection_preference (
            ctx->nas_client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)load_current_capabilities_get_system_selection_preference_ready,
            task);
        return;
    }

    if (ctx->run_get_technology_preference) {
        qmi_client_nas_get_technology_preference (
            ctx->nas_client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)load_current_capabilities_get_technology_preference_ready,
            task);
        return;
    }

    qmi_client_dms_get_capabilities (
        ctx->dms_client,
        NULL, /* no input */
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_current_capabilities_get_capabilities_ready,
        task);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    LoadCurrentCapabilitiesContext *ctx;
    GTask *task;
    QmiClient *nas_client = NULL;
    QmiClient *dms_client = NULL;

    /* Best way to get current capabilities (ie, enabled radios) is
     * Get System Selection Preference's "mode preference" TLV, but that's
     * only supported by NAS >= 1.1, meaning older Gobi devices don't
     * implement it.
     *
     * On these devices, the DMS Get Capabilities call appears to report
     * currently enabled radios, but this does not take the user's
     * technology preference into account.
     *
     * So in the absence of System Selection Preference, we check the
     * Technology Preference first, and if that is "AUTO" we fall back to
     * Get Capabilities.
     */

    mm_dbg ("loading current capabilities...");

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &nas_client,
                            callback, user_data))
        return;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &dms_client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (LoadCurrentCapabilitiesContext);
    ctx->nas_client = g_object_ref (nas_client);
    ctx->dms_client = g_object_ref (dms_client);

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_get_system_selection_preference = qmi_client_check_version (nas_client, 1, 1);
    ctx->run_get_technology_preference = TRUE;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)load_current_capabilities_context_free);

    load_current_capabilities_context_step (task);
}

/*****************************************************************************/
/* Supported capabilities loading (Modem interface) */

static GArray *
modem_load_supported_capabilities_finish (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_capabilities_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GTask *task)
{
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get supported capabilities: ");
        g_task_return_error (task, error);
    } else {
        MMBroadbandModemQmi *self;
        guint i;
        MMModemCapability mask = MM_MODEM_CAPABILITY_NONE;
        MMModemCapability single;
        GArray *radio_interface_list;
        GArray *supported_combinations;
        GArray *filtered_combinations;

        self = g_task_get_source_object (task);

        qmi_message_dms_get_capabilities_output_get_info (
            output,
            NULL, /* info_max_tx_channel_rate */
            NULL, /* info_max_rx_channel_rate */
            NULL, /* info_data_service_capability */
            NULL, /* info_sim_capability */
            &radio_interface_list,
            NULL);

        for (i = 0; i < radio_interface_list->len; i++) {
            mask |= mm_modem_capability_from_qmi_radio_interface (g_array_index (radio_interface_list,
                                                                                 QmiDmsRadioInterface,
                                                                                 i));
        }

        /* Cache supported radio interfaces */
        if (self->priv->supported_radio_interfaces)
            g_array_unref (self->priv->supported_radio_interfaces);
        self->priv->supported_radio_interfaces = g_array_ref (radio_interface_list);

        supported_combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 7);

        /* Add all possible supported capability combinations, we will filter
         * them out afterwards */

        /* GSM/UMTS */
        single = MM_MODEM_CAPABILITY_GSM_UMTS;
        g_array_append_val (supported_combinations, single);
        /* CDMA/EVDO */
        single = MM_MODEM_CAPABILITY_CDMA_EVDO;
        g_array_append_val (supported_combinations, single);
        /* LTE only */
        single = MM_MODEM_CAPABILITY_LTE;
        g_array_append_val (supported_combinations, single);
        /* GSM/UMTS + CDMA/EVDO */
        single = (MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_GSM_UMTS);
        g_array_append_val (supported_combinations, single);
        /* GSM/UMTS + LTE */
        single = (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE);
        g_array_append_val (supported_combinations, single);
        /* CDMA/EVDO + LTE */
        single = (MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE);
        g_array_append_val (supported_combinations, single);
        /* GSM/UMTS + CDMA/EVDO + LTE */
        single = (MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE);
        g_array_append_val (supported_combinations, single);

        /* Now filter out based on the real capabilities of the modem */
        filtered_combinations = mm_filter_supported_capabilities (mask,
                                                                  supported_combinations);
        g_array_unref (supported_combinations);

        g_task_return_pointer (task,
                               filtered_combinations,
                               (GDestroyNotify) g_array_unref);
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_supported_capabilities (MMIfaceModem *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading supported capabilities...");
    qmi_client_dms_get_capabilities (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_capabilities_ready,
                                     g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Current capabilities setting (Modem interface) */

typedef struct {
    QmiClientNas *client;
    MMModemCapability capabilities;
    gboolean run_set_system_selection_preference;
    gboolean run_set_technology_preference;
} SetCurrentCapabilitiesContext;

static void
set_current_capabilities_context_free (SetCurrentCapabilitiesContext *ctx)
{
    g_object_unref (ctx->client);
    g_slice_free (SetCurrentCapabilitiesContext, ctx);
}

static gboolean
set_current_capabilities_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
capabilities_power_cycle_ready (MMBroadbandModemQmi *self,
                                GAsyncResult *res,
                                GTask *task)
{
    GError *error = NULL;

    if (!power_cycle_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
capabilities_power_cycle (GTask *task)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    /* Power cycle the modem */
    power_cycle (self,
                 (GAsyncReadyCallback)capabilities_power_cycle_ready,
                 task);
}

static void set_current_capabilities_context_step (GTask *task);

static void
capabilities_set_technology_preference_ready (QmiClientNas *client,
                                              GAsyncResult *res,
                                              GTask *task)
{
    SetCurrentCapabilitiesContext *ctx;
    QmiMessageNasSetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_set_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_set_technology_preference_output_get_result (output, &error) &&
               !g_error_matches (error,
                                 QMI_PROTOCOL_ERROR,
                                 QMI_PROTOCOL_ERROR_NO_EFFECT)) {
        mm_dbg ("Couldn't set technology preference: %s", error->message);
        g_error_free (error);
        qmi_message_nas_set_technology_preference_output_unref (output);
    } else {
        if (error)
            g_error_free (error);

        /* Good! now reboot the modem */
        capabilities_power_cycle (task);
        qmi_message_nas_set_technology_preference_output_unref (output);
        return;
    }

    ctx->run_set_technology_preference = FALSE;
    set_current_capabilities_context_step (task);
}

static void
capabilities_set_system_selection_preference_ready (QmiClientNas *client,
                                                    GAsyncResult *res,
                                                    GTask *task)
{
    SetCurrentCapabilitiesContext *ctx;
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't set system selection preference: %s", error->message);
        g_error_free (error);
        qmi_message_nas_set_system_selection_preference_output_unref (output);
    } else {
        /* Good! now reboot the modem */
        capabilities_power_cycle (task);
        qmi_message_nas_set_system_selection_preference_output_unref (output);
        return;
    }

    /* Try with the deprecated command */
    ctx->run_set_system_selection_preference = FALSE;
    set_current_capabilities_context_step (task);
}

static void
set_current_capabilities_context_step (GTask *task)
{
    SetCurrentCapabilitiesContext *ctx;

    ctx = g_task_get_task_data (task);
    if (ctx->run_set_system_selection_preference) {
        QmiMessageNasSetSystemSelectionPreferenceInput *input;
        QmiNasRatModePreference pref;

        pref = mm_modem_capability_to_qmi_rat_mode_preference (ctx->capabilities);
        if (!pref) {
            gchar *str;

            str = mm_modem_capability_build_string_from_mask (ctx->capabilities);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unhandled capabilities setting: '%s'",
                                     str);
            g_object_unref (task);
            g_free (str);
            return;
        }

        input = qmi_message_nas_set_system_selection_preference_input_new ();
        qmi_message_nas_set_system_selection_preference_input_set_mode_preference (input, pref, NULL);
        qmi_message_nas_set_system_selection_preference_input_set_change_duration (input, QMI_NAS_CHANGE_DURATION_PERMANENT, NULL);

        qmi_client_nas_set_system_selection_preference (
            ctx->client,
            input,
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)capabilities_set_system_selection_preference_ready,
            task);
        qmi_message_nas_set_system_selection_preference_input_unref (input);
        return;
    }

    if (ctx->run_set_technology_preference) {
        QmiMessageNasSetTechnologyPreferenceInput *input;
        QmiNasRadioTechnologyPreference pref;

        pref = mm_modem_capability_to_qmi_radio_technology_preference (ctx->capabilities);
        if (!pref) {
            gchar *str;

            str = mm_modem_capability_build_string_from_mask (ctx->capabilities);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unhandled capabilities setting: '%s'",
                                     str);
            g_object_unref (task);
            g_free (str);
            return;
        }

        input = qmi_message_nas_set_technology_preference_input_new ();
        qmi_message_nas_set_technology_preference_input_set_current (input, pref, QMI_NAS_PREFERENCE_DURATION_PERMANENT, NULL);

        qmi_client_nas_set_technology_preference (
            ctx->client,
            input,
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)capabilities_set_technology_preference_ready,
            task);
        qmi_message_nas_set_technology_preference_input_unref (input);
        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Setting capabilities is not supported by this device");
    g_object_unref (task);
}

static void
set_current_capabilities (MMIfaceModem *self,
                          MMModemCapability capabilities,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    SetCurrentCapabilitiesContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (SetCurrentCapabilitiesContext);
    ctx->client = g_object_ref (client);
    ctx->capabilities = capabilities;

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_set_system_selection_preference = qmi_client_check_version (client, 1, 1);

    /* Technology preference introduced in NAS 1.0, so always available */
    ctx->run_set_technology_preference = TRUE;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)set_current_capabilities_context_free);

    set_current_capabilities_context_step (task);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_manufacturer_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GTask *task)
{
    QmiMessageDmsGetManufacturerOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_manufacturer_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_manufacturer_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Manufacturer: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_manufacturer_output_get_manufacturer (output, &str, NULL);
        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_get_manufacturer_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading manufacturer...");
    qmi_client_dms_get_manufacturer (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_manufacturer_ready,
                                     g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_model_ready (QmiClientDms *client,
                     GAsyncResult *res,
                     GTask *task)
{
    QmiMessageDmsGetModelOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_model_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_model_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Model: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_model_output_get_model (output, &str, NULL);
        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_get_model_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading model...");
    qmi_client_dms_get_model (QMI_CLIENT_DMS (client),
                              NULL,
                              5,
                              NULL,
                              (GAsyncReadyCallback)dms_get_model_ready,
                              g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_revision_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GTask *task)
{
    QmiMessageDmsGetRevisionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_revision_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_revision_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Revision: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_revision_output_get_revision (output, &str, NULL);
        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_get_revision_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading revision...");
    qmi_client_dms_get_revision (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_get_revision_ready,
                                 g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Hardware Revision loading (Modem interface) */

static gchar *
modem_load_hardware_revision_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_hardware_revision_ready (QmiClientDms *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    QmiMessageDmsGetHardwareRevisionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_hardware_revision_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_hardware_revision_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Hardware Revision: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_hardware_revision_output_get_revision (output, &str, NULL);
        g_task_return_pointer (task, g_strdup (str), g_free);
    }

    if (output)
        qmi_message_dms_get_hardware_revision_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_hardware_revision (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading hardware revision...");
    qmi_client_dms_get_hardware_revision (QMI_CLIENT_DMS (client),
                                          NULL,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)dms_get_hardware_revision_ready,
                                          g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Equipment Identifier loading (Modem interface) */

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_ids_ready (QmiClientDms *client,
                   GAsyncResult *res,
                   GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageDmsGetIdsOutput *output = NULL;
    GError *error = NULL;
    const gchar *str;
    guint len;

    output = qmi_client_dms_get_ids_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_get_ids_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get IDs: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_get_ids_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);

    /* In order:
     * If we have a IMEI, use it...
     * Otherwise, if we have a ESN, use it...
     * Otherwise, if we have a MEID, use it...
     * Otherwise, 'unknown'
     */

    if (qmi_message_dms_get_ids_output_get_imei (output, &str, NULL) &&
        str[0] != '\0') {
        g_free (self->priv->imei);
        self->priv->imei = g_strdup (str);
    }

    if (qmi_message_dms_get_ids_output_get_esn (output, &str, NULL) &&
        str[0] != '\0') {
        g_clear_pointer (&self->priv->esn, g_free);
        len = strlen (str);
        if (len == 7)
            self->priv->esn = g_strdup_printf ("0%s", str);  /* zero-pad to 8 chars */
        else if (len == 8)
            self->priv->esn = g_strdup (str);
        else
            mm_dbg ("Invalid ESN reported: '%s' (unexpected length)", str);
    }

    if (qmi_message_dms_get_ids_output_get_meid (output, &str, NULL) &&
        str[0] != '\0') {
        g_clear_pointer (&self->priv->meid, g_free);
        len = strlen (str);
        if (len == 14)
            self->priv->meid = g_strdup (str);
        else
            mm_dbg ("Invalid MEID reported: '%s' (unexpected length)", str);
    }

    if (self->priv->imei)
        str = self->priv->imei;
    else if (self->priv->esn)
        str = self->priv->esn;
    else if (self->priv->meid)
        str = self->priv->meid;
    else
        str = "unknown";

    g_task_return_pointer (task, g_strdup (str), g_free);
    g_object_unref (task);

    qmi_message_dms_get_ids_output_unref (output);
}

static void
modem_load_equipment_identifier (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading equipment identifier...");
    qmi_client_dms_get_ids (QMI_CLIENT_DMS (client),
                            NULL,
                            5,
                            NULL,
                            (GAsyncReadyCallback)dms_get_ids_ready,
                            g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Device identifier loading (Modem interface) */

static gchar *
modem_load_device_identifier_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_device_identifier (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    gchar *device_identifier;
    GTask *task;

    mm_dbg ("loading device identifier...");

    /* Just use dummy ATI/ATI1 replies, all the other internal info should be
     * enough for uniqueness */
    device_identifier = mm_broadband_modem_create_device_identifier (MM_BROADBAND_MODEM (self), "", "");

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, device_identifier, g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Own Numbers loading (Modem interface) */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_msisdn_ready (QmiClientDms *client,
                      GAsyncResult *res,
                      GTask *task)
{
    QmiMessageDmsGetMsisdnOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_msisdn_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_msisdn_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get MSISDN: ");
        g_task_return_error (task, error);
    } else {
        const gchar *str = NULL;
        GStrv numbers;

        qmi_message_dms_get_msisdn_output_get_msisdn (output, &str, NULL);
        numbers =  g_new0 (gchar *, 2);
        numbers[0] = g_strdup (str);
        g_task_return_pointer (task, numbers, (GDestroyNotify)g_strfreev);
    }

    if (output)
        qmi_message_dms_get_msisdn_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading own numbers...");
    qmi_client_dms_get_msisdn (QMI_CLIENT_DMS (client),
                               NULL,
                               5,
                               NULL,
                               (GAsyncReadyCallback)dms_get_msisdn_ready,
                               g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Check if unlock required (Modem interface) */

typedef enum {
    LOAD_UNLOCK_REQUIRED_STEP_FIRST,
    LOAD_UNLOCK_REQUIRED_STEP_CDMA,
    LOAD_UNLOCK_REQUIRED_STEP_DMS,
    LOAD_UNLOCK_REQUIRED_STEP_UIM,
} LoadUnlockRequiredStep;

typedef struct {
    LoadUnlockRequiredStep step;
    QmiClient *dms;
    QmiClient *uim;
} LoadUnlockRequiredContext;

static MMModemLock
modem_load_unlock_required_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCK_UNKNOWN;
    }
    return (MMModemLock)value;
}

static void load_unlock_required_context_step (GTask *task);

/* Used also when loading unlock retries left */
static gboolean
uim_get_card_status_output_parse (QmiMessageUimGetCardStatusOutput  *output,
                                  MMModemLock                       *o_lock,
                                  guint                             *o_pin1_retries,
                                  guint                             *o_puk1_retries,
                                  guint                             *o_pin2_retries,
                                  guint                             *o_puk2_retries,
                                  GError                           **error)
{
    GArray *cards;
    QmiMessageUimGetCardStatusOutputCardStatusCardsElement *card;
    QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement *app;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
    guint i;
    gint card_i = -1;
    gint application_j = -1;
    guint n_absent = 0;
    guint n_error = 0;
    guint n_invalid = 0;

    /* This command supports MULTIPLE cards with MULTIPLE applications each. For our
     * purposes, we're going to consider as the SIM to use the first card present
     * with a SIM/USIM application. */

    if (!qmi_message_uim_get_card_status_output_get_result (output, error)) {
        g_prefix_error (error, "QMI operation failed: ");
        return FALSE;
    }

    qmi_message_uim_get_card_status_output_get_card_status (
        output,
        NULL, /* index_gw_primary */
        NULL, /* index_1x_primary */
        NULL, /* index_gw_secondary */
        NULL, /* index_1x_secondary */
        &cards,
        NULL);

    if (cards->len == 0) {
        g_set_error (error, QMI_CORE_ERROR, QMI_CORE_ERROR_FAILED,
                     "No cards reported");
        return FALSE;
    }

    if (cards->len > 1)
        mm_dbg ("Multiple cards reported: %u", cards->len);

    /* All KNOWN applications in all cards will need to be in READY state for us
     * to consider UNLOCKED */
    for (i = 0; i < cards->len; i++) {
        card = &g_array_index (cards, QmiMessageUimGetCardStatusOutputCardStatusCardsElement, i);

        switch (card->card_state) {
        case QMI_UIM_CARD_STATE_PRESENT: {
            guint j;
            gboolean sim_usim_found = FALSE;

            if (card->applications->len == 0) {
                mm_dbg ("No applications reported in card [%u]", i);
                n_invalid++;
                break;
            }

            if (card->applications->len > 1)
                mm_dbg ("Multiple applications reported in card [%u]: %u", i, card->applications->len);

            for (j = 0; j < card->applications->len; j++) {
                app = &g_array_index (card->applications, QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement, j);

                if (app->type == QMI_UIM_CARD_APPLICATION_TYPE_UNKNOWN) {
                    mm_dbg ("Unknown application [%u] found in card [%u]: %s. Ignored.",
                            j, i, qmi_uim_card_application_state_get_string (app->state));
                    continue;
                }

                mm_dbg ("Application '%s' [%u] in card [%u]: %s",
                        qmi_uim_card_application_type_get_string (app->type), j, i, qmi_uim_card_application_state_get_string (app->state));

                if (app->type == QMI_UIM_CARD_APPLICATION_TYPE_SIM || app->type == QMI_UIM_CARD_APPLICATION_TYPE_USIM) {
                    /* We found the card/app pair to use! Only keep the first found,
                     * but still, keep on looping to log about the remaining ones */
                    if (card_i < 0 && application_j < 0) {
                        card_i = i;
                        application_j = j;
                    }

                    sim_usim_found = TRUE;
                }
            }

            if (!sim_usim_found) {
                mm_dbg ("No SIM/USIM application found in card [%u]", i);
                n_invalid++;
            }

            break;
        }

        case QMI_UIM_CARD_STATE_ABSENT:
            mm_dbg ("Card '%u' is absent", i);
            n_absent++;
            break;

        case QMI_UIM_CARD_STATE_ERROR:
        default:
            n_error++;
            if (qmi_uim_card_error_get_string (card->error_code) != NULL)
                mm_warn ("Card '%u' is unusable: %s", i, qmi_uim_card_error_get_string (card->error_code));
            else
                mm_warn ("Card '%u' is unusable: unknown error", i);
            break;
        }

        /* go on to next card */
    }

    /* If we found no card/app to use, we need to report an error */
    if (card_i < 0 || application_j < 0) {
        /* If not a single card found, report SIM not inserted */
        if (n_absent > 0 && !n_error && !n_invalid)
            g_set_error (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,
                         "No card found");
        else if (n_error > 0)
            g_set_error (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                         "Card error");
        else
            g_set_error (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,
                         "Card failure: %u absent, %u errors, %u invalid",
                         n_absent, n_error, n_invalid);
        return FALSE;
    }

    /* Get card/app to use */
    card = &g_array_index (cards, QmiMessageUimGetCardStatusOutputCardStatusCardsElement, card_i);
    app = &g_array_index (card->applications, QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement, application_j);

    /* If card not ready yet, return RETRY error.
     * If the application state reports needing PIN/PUk, consider that ready as
     * well, and let the logic fall down to check PIN1/PIN2. */
    if (app->state != QMI_UIM_CARD_APPLICATION_STATE_READY &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PIN1_OR_UPIN_PIN_REQUIRED &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PUK1_OR_UPIN_PUK_REQUIRED &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PIN1_BLOCKED) {
        mm_dbg ("Neither SIM nor USIM are ready");
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY,
                     "SIM not ready yet (retry)");
        return FALSE;
    }

    /* Report retries if requested to do so */
    if (o_pin1_retries)
        *o_pin1_retries = app->pin1_retries;
    if (o_puk1_retries)
        *o_puk1_retries = app->puk1_retries;
    if (o_pin2_retries)
        *o_pin2_retries = app->pin2_retries;
    if (o_puk2_retries)
        *o_puk2_retries = app->puk2_retries;

    /* Early bail out if lock status isn't wanted at this point, so that we
     * don't fail with an error the unlock retries check */
    if (!o_lock)
        return TRUE;

    /* Card is ready, what's the lock status? */

    /* PIN1 */
    switch (app->pin1_state) {
    case QMI_UIM_PIN_STATE_PERMANENTLY_BLOCKED:
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "SIM PIN/PUK permanently blocked");
        return FALSE;

    case QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED:
        lock = MM_MODEM_LOCK_SIM_PIN;
        break;

    case QMI_UIM_PIN_STATE_BLOCKED:
        lock = MM_MODEM_LOCK_SIM_PUK;
        break;

    case QMI_UIM_PIN_STATE_DISABLED:
    case QMI_UIM_PIN_STATE_ENABLED_VERIFIED:
        lock = MM_MODEM_LOCK_NONE;
        break;

    default:
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "Unknown SIM PIN/PUK status");
        return FALSE;
    }

    /* PIN2 */
    if (lock == MM_MODEM_LOCK_NONE) {
        switch (app->pin2_state) {
        case QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED:
            lock = MM_MODEM_LOCK_SIM_PIN2;
            break;

        case QMI_UIM_PIN_STATE_PERMANENTLY_BLOCKED:
            mm_warn ("PUK2 permanently blocked");
        case QMI_UIM_PIN_STATE_BLOCKED:
            lock = MM_MODEM_LOCK_SIM_PUK2;
            break;

        case QMI_UIM_PIN_STATE_DISABLED:
        case QMI_UIM_PIN_STATE_ENABLED_VERIFIED:
            break;

        default:
            mm_warn ("Unknown SIM PIN2/PUK2 status");
            break;
        }
    }

    *o_lock = lock;
    return TRUE;
}

static void
unlock_required_uim_get_card_status_ready (QmiClientUim *client,
                                           GAsyncResult *res,
                                           GTask *task)
{
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!uim_get_card_status_output_parse (output,
                                           &lock,
                                           NULL, NULL, NULL, NULL,
                                           &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else
        g_task_return_int (task, lock);
    g_object_unref (task);

    qmi_message_uim_get_card_status_output_unref (output);
}

static void
dms_uim_get_pin_status_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadUnlockRequiredContext *ctx;
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
    QmiDmsUimPinStatus current_status;

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        /* We get InvalidQmiCommand on newer devices which don't like the legacy way */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            g_error_free (error);
            qmi_message_dms_uim_get_pin_status_output_unref (output);
            /* Flag that the command is unsupported, and try with the new way */
            self->priv->dms_uim_deprecated = TRUE;
            ctx->step++;
            load_unlock_required_context_step (task);
            return;
        }

        /* Internal and uim-uninitialized errors are retry-able before being fatal */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INTERNAL) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_UIM_UNINITIALIZED)) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_RETRY,
                                     "Couldn't get PIN status (retry): %s",
                                     error->message);
            g_object_unref (task);
            g_error_free (error);
            qmi_message_dms_uim_get_pin_status_output_unref (output);
            return;
        }

        /* Other errors, just propagate them */
        g_prefix_error (&error, "Couldn't get PIN status: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_uim_get_pin_status_output_unref (output);
        return;
    }

    /* Command succeeded, process results */

    if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
            output,
            &current_status,
            NULL, /* verify_retries_left */
            NULL, /* unblock_retries_left */
            NULL))
        lock = mm_modem_lock_from_qmi_uim_pin_status (current_status, TRUE);

    if (lock == MM_MODEM_LOCK_NONE &&
        qmi_message_dms_uim_get_pin_status_output_get_pin2_status (
            output,
            &current_status,
            NULL, /* verify_retries_left */
            NULL, /* unblock_retries_left */
            NULL)) {
        MMModemLock lock2;

        /* We only use the PIN2 status if it isn't unknown */
        lock2 = mm_modem_lock_from_qmi_uim_pin_status (current_status, FALSE);
        if (lock2 != MM_MODEM_LOCK_UNKNOWN)
            lock = lock2;
    }

    /* We're done! */
    g_task_return_int (task, lock);
    g_object_unref (task);
}

static void
load_unlock_required_context_step (GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadUnlockRequiredContext *ctx;
    GError *error = NULL;
    QmiClient *client;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case LOAD_UNLOCK_REQUIRED_STEP_FIRST:
        ctx->step++;
        /* Go on to next step */

    case LOAD_UNLOCK_REQUIRED_STEP_CDMA:
        /* CDMA-only modems don't need this */
        if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self))) {
            mm_dbg ("Skipping unlock check in CDMA-only modem...");
            g_task_return_int (task, MM_MODEM_LOCK_NONE);
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* Go on to next step */

    case LOAD_UNLOCK_REQUIRED_STEP_DMS:
        if (!self->priv->dms_uim_deprecated) {
            /* Failure to get DMS client is hard really */
            client = peek_qmi_client (self, QMI_SERVICE_DMS, &error);
            if (!client) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
            }

            mm_dbg ("loading unlock required (DMS)...");
            qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                               NULL,
                                               5,
                                               NULL,
                                               (GAsyncReadyCallback) dms_uim_get_pin_status_ready,
                                               task);
            return;
        }
        ctx->step++;
        /* Go on to next step */

    case LOAD_UNLOCK_REQUIRED_STEP_UIM:
        /* Failure to get UIM client at this point is hard as well */
        client = peek_qmi_client (self, QMI_SERVICE_UIM, &error);
        if (!client) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_dbg ("loading unlock required (UIM)...");
        qmi_client_uim_get_card_status (QMI_CLIENT_UIM (client),
                                        NULL,
                                        5,
                                        NULL,
                                        (GAsyncReadyCallback) unlock_required_uim_get_card_status_ready,
                                        task);
        return;
    }
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadUnlockRequiredContext *ctx;
    GTask *task;

    ctx = g_new0 (LoadUnlockRequiredContext, 1);
    ctx->step = LOAD_UNLOCK_REQUIRED_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    load_unlock_required_context_step (task);
}

/*****************************************************************************/
/* Check if unlock retries (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem  *self,
                                  GAsyncResult  *res,
                                  GError       **error)
{
    return MM_UNLOCK_RETRIES (g_task_propagate_pointer (G_TASK (res), error));
}

static void
unlock_retries_uim_get_card_status_ready (QmiClientUim *client,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    guint pin1_retries = 0;
    guint puk1_retries = 0;
    guint pin2_retries = 0;
    guint puk2_retries = 0;
    MMUnlockRetries *retries;

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!uim_get_card_status_output_parse (output,
                                           NULL,
                                           &pin1_retries, &puk1_retries,
                                           &pin2_retries, &puk2_retries,
                                           &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    retries = mm_unlock_retries_new ();
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN,  pin1_retries);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK,  puk1_retries);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2_retries);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2_retries);

    qmi_message_uim_get_card_status_output_unref (output);

    g_task_return_pointer (task, retries, g_object_unref);
    g_object_unref (task);
}

static void
uim_load_unlock_retries (MMBroadbandModemQmi *self,
                         GTask               *task)
{
    QmiClient *client;
    GError *error = NULL;

    client = peek_qmi_client (self, QMI_SERVICE_UIM, &error);
    if (!client) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_client_uim_get_card_status (QMI_CLIENT_UIM (client),
                                    NULL,
                                    5,
                                    NULL,
                                    (GAsyncReadyCallback) unlock_retries_uim_get_card_status_ready,
                                    task);
}

static void
unlock_retries_dms_uim_get_pin_status_ready (QmiClientDms *client,
                                             GAsyncResult *res,
                                             GTask        *task)
{
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;
    MMBroadbandModemQmi *self;
    MMUnlockRetries *retries;
    guint8 verify_retries_left;
    guint8 unblock_retries_left;

    self = g_task_get_source_object (task);

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        qmi_message_dms_uim_get_pin_status_output_unref (output);
        /* We get InvalidQmiCommand on newer devices which don't like the legacy way */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND)) {
            g_error_free (error);
            /* Flag that the command is unsupported, and try with the new way */
            self->priv->dms_uim_deprecated = TRUE;
            uim_load_unlock_retries (self, task);
            return;
        }
        g_prefix_error (&error, "Couldn't get unlock retries: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    retries = mm_unlock_retries_new ();

    if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
            output,
            NULL, /* current_status */
            &verify_retries_left,
            &unblock_retries_left,
            NULL)) {
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, verify_retries_left);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, unblock_retries_left);
    }

    if (qmi_message_dms_uim_get_pin_status_output_get_pin2_status (
            output,
            NULL, /* current_status */
            &verify_retries_left,
            &unblock_retries_left,
            NULL)) {
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, verify_retries_left);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, unblock_retries_left);
    }

    qmi_message_dms_uim_get_pin_status_output_unref (output);

    g_task_return_pointer (task, retries, g_object_unref);
    g_object_unref (task);
}

static void
dms_uim_load_unlock_retries (MMBroadbandModemQmi *self,
                             GTask               *task)
{
    QmiClient *client;

    client = peek_qmi_client (self, QMI_SERVICE_DMS, NULL);
    if (!client) {
        /* Very unlikely that this will ever happen, but anyway, try with
         * UIM service instead */
        uim_load_unlock_retries (self, task);
        return;
    }

    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback) unlock_retries_dms_uim_get_pin_status_ready,
                                       task);
}

static void
modem_load_unlock_retries (MMIfaceModem *_self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MMBroadbandModemQmi *self;
    GTask *task;

    self = MM_BROADBAND_MODEM_QMI (_self);
    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("loading unlock retries...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_load_unlock_retries (MM_BROADBAND_MODEM_QMI (self), task);
    else
        uim_load_unlock_retries (MM_BROADBAND_MODEM_QMI (self), task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
modem_load_supported_bands_finish (MMIfaceModem *_self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GArray *supported_bands;
    
    supported_bands = g_task_propagate_pointer (G_TASK (res), error);
    if (supported_bands) {
        if (self->priv->supported_bands)
            g_array_unref (self->priv->supported_bands);

        /* Cache the supported bands value */
        self->priv->supported_bands = g_array_ref (supported_bands);
    }
    return supported_bands;
}

static void
dms_get_band_capabilities_ready (QmiClientDms *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    QmiMessageDmsGetBandCapabilitiesOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_get_band_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_band_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get band capabilities: ");
        g_task_return_error (task, error);
    } else {
        GArray *mm_bands;
        QmiDmsBandCapability qmi_bands = 0;
        QmiDmsLteBandCapability qmi_lte_bands = 0;

        qmi_message_dms_get_band_capabilities_output_get_band_capability (
            output,
            &qmi_bands,
            NULL);
        qmi_message_dms_get_band_capabilities_output_get_lte_band_capability (
            output,
            &qmi_lte_bands,
            NULL);

        mm_bands = mm_modem_bands_from_qmi_band_capabilities (qmi_bands, qmi_lte_bands);

        if (mm_bands->len == 0) {
            g_array_unref (mm_bands);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't parse the list of supported bands");
        } else {
            g_task_return_pointer (task, mm_bands, (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_dms_get_band_capabilities_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("loading band capabilities...");
    qmi_client_dms_get_band_capabilities (QMI_CLIENT_DMS (client),
                                          NULL,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)dms_get_band_capabilities_ready,
                                          g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
modem_load_current_bands_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
nas_get_rf_band_information_ready (QmiClientNas *client,
                                   GAsyncResult *res,
                                   GTask *task)
{
    QmiMessageNasGetRfBandInformationOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_rf_band_information_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_get_rf_band_information_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get current band information: ");
        g_task_return_error (task, error);
    } else {
        GArray *mm_bands;
        GArray *info_array = NULL;

        qmi_message_nas_get_rf_band_information_output_get_list (output, &info_array, NULL);

        mm_bands = mm_modem_bands_from_qmi_rf_band_information_array (info_array);

        if (mm_bands->len == 0) {
            g_array_unref (mm_bands);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't parse the list of current bands");
        } else {
            g_task_return_pointer (task, mm_bands, (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_nas_get_rf_band_information_output_unref (output);

    g_object_unref (task);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
load_bands_get_system_selection_preference_ready (QmiClientNas *client,
                                                  GAsyncResult *res,
                                                  GTask *task)
{
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get system selection preference: ");
        g_task_return_error (task, error);
    } else {
        GArray *mm_bands;
        QmiNasBandPreference band_preference_mask = 0;
        QmiNasLteBandPreference lte_band_preference_mask = 0;

        qmi_message_nas_get_system_selection_preference_output_get_band_preference (
            output,
            &band_preference_mask,
            NULL);

        qmi_message_nas_get_system_selection_preference_output_get_lte_band_preference (
            output,
            &lte_band_preference_mask,
            NULL);

        mm_bands = mm_modem_bands_from_qmi_band_preference (band_preference_mask,
                                                            lte_band_preference_mask);

        if (mm_bands->len == 0) {
            g_array_unref (mm_bands);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't parse the list of current bands");
        } else {
            gchar *str;

            str = qmi_nas_band_preference_build_string_from_mask (band_preference_mask);
            mm_dbg ("Bands reported in system selection preference: '%s'", str);
            g_free (str);

            g_task_return_pointer (task, mm_bands, (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    g_object_unref (task);
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("loading current bands...");

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Introduced in NAS 1.19 */
    if (qmi_client_check_version (client, 1, 19)) {
        qmi_client_nas_get_rf_band_information (QMI_CLIENT_NAS (client),
                                                NULL,
                                                5,
                                                NULL,
                                                (GAsyncReadyCallback)nas_get_rf_band_information_ready,
                                                task);
        return;
    }
#endif

    qmi_client_nas_get_system_selection_preference (
        QMI_CLIENT_NAS (client),
        NULL, /* no input */
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_bands_get_system_selection_preference_ready,
        task);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

static gboolean
set_current_bands_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
bands_set_system_selection_preference_ready (QmiClientNas *client,
                                             GAsyncResult *res,
                                             GTask *task)
{
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set system selection preference: ");
        g_task_return_error (task, error);

    } else
        /* Good! TODO: do we really need to wait for the indication? */
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);

    g_object_unref (task);
}

static void
set_current_bands (MMIfaceModem *_self,
                   GArray *bands_array,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    QmiMessageNasSetSystemSelectionPreferenceInput *input;
    GTask *task;
    QmiClient *client = NULL;
    QmiNasBandPreference qmi_bands = 0;
    QmiNasLteBandPreference qmi_lte_bands = 0;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Handle ANY separately */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        if (!self->priv->supported_bands) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Cannot handle 'ANY' if supported bands are unknown");
            g_object_unref (task);
            return;
        }

        mm_modem_bands_to_qmi_band_preference (self->priv->supported_bands,
                                               &qmi_bands,
                                               &qmi_lte_bands);
    } else
        mm_modem_bands_to_qmi_band_preference (bands_array,
                                               &qmi_bands,
                                               &qmi_lte_bands);

    input = qmi_message_nas_set_system_selection_preference_input_new ();
    qmi_message_nas_set_system_selection_preference_input_set_band_preference (input, qmi_bands, NULL);
    if (mm_iface_modem_is_3gpp_lte (_self))
        qmi_message_nas_set_system_selection_preference_input_set_lte_band_preference (input, qmi_lte_bands, NULL);

    qmi_message_nas_set_system_selection_preference_input_set_change_duration (input, QMI_NAS_CHANGE_DURATION_PERMANENT, NULL);

    qmi_client_nas_set_system_selection_preference (
        QMI_CLIENT_NAS (client),
        input,
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)bands_set_system_selection_preference_ready,
        task);
    qmi_message_nas_set_system_selection_preference_input_unref (input);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
modem_load_supported_modes_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_supported_modes (MMIfaceModem *_self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;
    GArray *combinations;
    MMModemModeCombination mode;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->supported_radio_interfaces) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot load supported modes, no radio interface list");
        g_object_unref (task);
        return;
    }

    /* Build combinations
     *
     *    (1) If current capabilities [GSM/UMTS]:
     *       [2G only]
     *       [3G only]
     *       [2G + 3G]
     *       [2G + 3G] 2G preferred
     *       [2G + 3G] 3G preferred
     *
     *     (2) If current capabilities [CDMA/EVDO]:
     *       [2G only]
     *       [3G only]
     *
     *     (3) If current capabilities [LTE]:
     *       [4G only]
     *
     *     (4) If current capabilities [GSM/UMTS + CDMA/EVDO]:
     *       [2G only]
     *       [3G only]
     *       [2G + 3G]
     *       [2G + 3G] 2G preferred
     *       [2G + 3G] 3G preferred
     *
     *     (5) If current capabilities [GSM/UMTS + LTE]:
     *       [2G + 3G + 4G]
     *
     *     (6) If current capabilities [CDMA/EVDO + LTE]:
     *       [2G + 3G + 4G]
     *
     *     (7) If current capabilities [GSM/UMTS + CDMA/EVDO + LTE]:
     *       [2G + 3G + 4G]
     */

    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 5);

    /* LTE only, don't allow further mode switching */
    if (mm_iface_modem_is_3gpp_lte_only (_self)) {
        /* 4G only */
        mode.allowed = MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }
    /* LTE and others, only allow to have all, no further preference */
    else if (mm_iface_modem_is_3gpp_lte (_self)) {
        /* 2G, 3G and 4G */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }
    /* Non-LTE modem, include allowed and preferred combinations */
    else {
        MMModemMode mask_all;
        guint i;
        GArray *all;
        GArray *filtered;


        /* Build all, based on the supported radio interfaces */
        mask_all = MM_MODEM_MODE_NONE;
        for (i = 0; i < self->priv->supported_radio_interfaces->len; i++)
            mask_all |= mm_modem_mode_from_qmi_radio_interface (g_array_index (self->priv->supported_radio_interfaces,
                                                                               QmiDmsRadioInterface,
                                                                               i));


        /* 2G only */
        mode.allowed = MM_MODEM_MODE_2G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 3G only */
        mode.allowed = MM_MODEM_MODE_3G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 2G and 3G */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 2G and 3G, 2G preferred */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_2G;
        g_array_append_val (combinations, mode);
        /* 2G and 3G, 3G preferred */
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_3G;
        g_array_append_val (combinations, mode);

        /* Filter out those unsupported modes */
        mode.allowed = mask_all;
        mode.preferred = MM_MODEM_MODE_NONE;
        all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
        g_array_append_val (all, mode);
        filtered = mm_filter_supported_modes (all, combinations);
        g_array_unref (all);
        g_array_unref (combinations);
        combinations = filtered;
    }

    g_task_return_pointer (task, combinations, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported IP families (Modem interface) */

static MMBearerIpFamily
modem_load_supported_ip_families_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_IP_FAMILY_NONE;
    }
    return (MMBearerIpFamily)value;
}

static void
modem_load_supported_ip_families (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self)))
        /* CDMA-only: IPv4 */
        g_task_return_int (task, MM_BEARER_IP_FAMILY_IPV4);
    else
        /* Assume IPv4 + IPv6 supported */
        g_task_return_int (task,
                           MM_BEARER_IP_FAMILY_IPV4 |
                           MM_BEARER_IP_FAMILY_IPV6 |
                           MM_BEARER_IP_FAMILY_IPV4V6);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load signal quality (Modem interface) */

/* Limit the value betweeen [-113,-51] and scale it to a percentage */
#define STRENGTH_TO_QUALITY(strength)                                   \
    (guint8)(100 - ((CLAMP (strength, -113, -51) + 51) * 100 / (-113 + 51)))

static gboolean
qmi_dbm_valid (gint8 dbm, QmiNasRadioInterface radio_interface)
{
    /* Different radio interfaces have different signal quality bounds */
    switch (radio_interface) {
    case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
    case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
        return (dbm > -125 && dbm < -30);
    case QMI_NAS_RADIO_INTERFACE_UMTS:
        return (dbm > -125 && dbm < -30);
    default:
        break;
    }
    return TRUE;
}

static guint
load_signal_quality_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return 0;
    }
    return value;
}

#if defined WITH_NEWEST_QMI_COMMANDS

static gboolean
common_signal_info_get_quality (gint8 cdma1x_rssi,
                                gint8 evdo_rssi,
                                gint8 gsm_rssi,
                                gint8 wcdma_rssi,
                                gint8 lte_rssi,
                                guint8 *out_quality,
                                MMModemAccessTechnology *out_act)
{
    gint8 rssi_max = -125;
    QmiNasRadioInterface signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_UNKNOWN;

    g_assert (out_quality != NULL);
    g_assert (out_act != NULL);

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. TODO: When several technologies are in use, if
     * the indication only contains the data of the one which passed a threshold
     * value, we'll need to have an internal cache of per-technology values, in
     * order to report always the one with the maximum value. */

    if (cdma1x_rssi < 0) {
        mm_dbg ("RSSI (CDMA): %d dBm", cdma1x_rssi);
        if (qmi_dbm_valid (cdma1x_rssi, QMI_NAS_RADIO_INTERFACE_CDMA_1X)) {
            rssi_max = MAX (cdma1x_rssi, rssi_max);
            signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_CDMA_1X;
        }
    }

    if (evdo_rssi < 0) {
        mm_dbg ("RSSI (HDR): %d dBm", evdo_rssi);
        if (qmi_dbm_valid (evdo_rssi, QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO)) {
            rssi_max = MAX (evdo_rssi, rssi_max);
            signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO;
        }
    }

    if (gsm_rssi < 0) {
        mm_dbg ("RSSI (GSM): %d dBm", gsm_rssi);
        if (qmi_dbm_valid (gsm_rssi, QMI_NAS_RADIO_INTERFACE_GSM)) {
            rssi_max = MAX (gsm_rssi, rssi_max);
            signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_GSM;
        }
    }

    if (wcdma_rssi < 0) {
        mm_dbg ("RSSI (WCDMA): %d dBm", wcdma_rssi);
        if (qmi_dbm_valid (wcdma_rssi, QMI_NAS_RADIO_INTERFACE_UMTS)) {
            rssi_max = MAX (wcdma_rssi, rssi_max);
            signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_UMTS;
        }
    }

    if (lte_rssi < 0) {
        mm_dbg ("RSSI (LTE): %d dBm", lte_rssi);
        if (qmi_dbm_valid (lte_rssi, QMI_NAS_RADIO_INTERFACE_LTE)) {
            rssi_max = MAX (lte_rssi, rssi_max);
            signal_info_radio_interface = QMI_NAS_RADIO_INTERFACE_LTE;
        }
    }

    if (rssi_max < 0 && rssi_max > -125) {
        /* This RSSI comes as negative dBms */
        *out_quality = STRENGTH_TO_QUALITY (rssi_max);
        *out_act = mm_modem_access_technology_from_qmi_radio_interface (signal_info_radio_interface);

        mm_dbg ("RSSI: %d dBm --> %u%%", rssi_max, *out_quality);
        return TRUE;
    }

    return FALSE;
}

static gboolean
signal_info_get_quality (MMBroadbandModemQmi *self,
                         QmiMessageNasGetSignalInfoOutput *output,
                         guint8 *out_quality,
                         MMModemAccessTechnology *out_act)
{
    gint8 cdma1x_rssi = 0;
    gint8 evdo_rssi = 0;
    gint8 gsm_rssi = 0;
    gint8 wcdma_rssi = 0;
    gint8 lte_rssi = 0;

    qmi_message_nas_get_signal_info_output_get_cdma_signal_strength (output, &cdma1x_rssi, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_hdr_signal_strength (output, &evdo_rssi, NULL, NULL, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_gsm_signal_strength (output, &gsm_rssi, NULL);
    qmi_message_nas_get_signal_info_output_get_wcdma_signal_strength (output, &wcdma_rssi, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_lte_signal_strength (output, &lte_rssi, NULL, NULL, NULL, NULL);

    return common_signal_info_get_quality (cdma1x_rssi, evdo_rssi, gsm_rssi, wcdma_rssi, lte_rssi, out_quality, out_act);
}

static void
get_signal_info_ready (QmiClientNas *client,
                       GAsyncResult *res,
                       GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetSignalInfoOutput *output;
    GError *error = NULL;
    guint8 quality = 0;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    output = qmi_client_nas_get_signal_info_finish (client, res, &error);
    if (!output) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_signal_info_output_get_result (output, &error)) {
        qmi_message_nas_get_signal_info_output_unref (output);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);

    if (!signal_info_get_quality (self, output, &quality, &act)) {
        qmi_message_nas_get_signal_info_output_unref (output);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Signal info reported invalid signal strength.");
        g_object_unref (task);
        return;
    }

    /* We update the access technologies directly here when loading signal
     * quality. It goes a bit out of context, but we can do it nicely */
    mm_iface_modem_update_access_technologies (
        MM_IFACE_MODEM (self),
        act,
        (MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK | MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK));

    g_task_return_int (task, quality);
    g_object_unref (task);

    qmi_message_nas_get_signal_info_output_unref (output);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static gboolean
signal_strength_get_quality_and_access_tech (MMBroadbandModemQmi *self,
                                             QmiMessageNasGetSignalStrengthOutput *output,
                                             guint8 *o_quality,
                                             MMModemAccessTechnology *o_act)
{
    GArray *array = NULL;
    gint8 signal_max = 0;
    QmiNasRadioInterface main_interface;
    MMModemAccessTechnology act;

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. */

    /* The mandatory one is always present */
    qmi_message_nas_get_signal_strength_output_get_signal_strength (output, &signal_max, &main_interface, NULL);
    mm_dbg ("Signal strength (%s): %d dBm",
            qmi_nas_radio_interface_get_string (main_interface),
            signal_max);

    /* Treat results as invalid if main signal strength is invalid */
    if (!qmi_dbm_valid (signal_max, main_interface))
        return FALSE;

    act = mm_modem_access_technology_from_qmi_radio_interface (main_interface);

    /* On multimode devices we may get more */
    if (qmi_message_nas_get_signal_strength_output_get_strength_list (output, &array, NULL)) {
        guint i;

        for (i = 0; i < array->len; i++) {
            QmiMessageNasGetSignalStrengthOutputStrengthListElement *element;

            element = &g_array_index (array, QmiMessageNasGetSignalStrengthOutputStrengthListElement, i);

            mm_dbg ("Signal strength (%s): %d dBm",
                    qmi_nas_radio_interface_get_string (element->radio_interface),
                    element->strength);

            if (qmi_dbm_valid (element->strength, element->radio_interface)) {
                signal_max = MAX (element->strength, signal_max);
                act |= mm_modem_access_technology_from_qmi_radio_interface (element->radio_interface);
            }
        }
    }

    if (signal_max < 0) {
        /* This signal strength comes as negative dBms */
        *o_quality = STRENGTH_TO_QUALITY (signal_max);
        *o_act = act;

        mm_dbg ("Signal strength: %d dBm --> %u%%", signal_max, *o_quality);
    }

    return (signal_max < 0);
}

static void
get_signal_strength_ready (QmiClientNas *client,
                           GAsyncResult *res,
                           GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetSignalStrengthOutput *output;
    GError *error = NULL;
    guint8 quality = 0;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    output = qmi_client_nas_get_signal_strength_finish (client, res, &error);
    if (!output) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_signal_strength_output_get_result (output, &error)) {
        qmi_message_nas_get_signal_strength_output_unref (output);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);

    if (!signal_strength_get_quality_and_access_tech (self, output, &quality, &act)) {
        qmi_message_nas_get_signal_strength_output_unref (output);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "GetSignalStrength signal strength invalid.");
        g_object_unref (task);
        return;
    }

    /* We update the access technologies directly here when loading signal
     * quality. It goes a bit out of context, but we can do it nicely */
    mm_iface_modem_update_access_technologies (
        MM_IFACE_MODEM (self),
        act,
        (MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK | MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK));

    g_task_return_int (task, quality);
    g_object_unref (task);

    qmi_message_nas_get_signal_strength_output_unref (output);
}

static void
load_signal_quality (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiClient *client = NULL;
    GTask *task;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("loading signal quality...");

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        qmi_client_nas_get_signal_info (QMI_CLIENT_NAS (client),
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_signal_info_ready,
                                        task);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    qmi_client_nas_get_signal_strength (QMI_CLIENT_NAS (client),
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_signal_strength_ready,
                                        task);
}

/*****************************************************************************/
/* Powering up the modem (Modem interface) */

typedef enum {
    SET_OPERATING_MODE_STEP_FIRST,
    SET_OPERATING_MODE_STEP_FCC_AUTH,
    SET_OPERATING_MODE_STEP_RETRY,
    SET_OPERATING_MODE_STEP_LAST
} SetOperatingModeStep;

typedef struct {
    QmiClientDms *client;
    QmiMessageDmsSetOperatingModeInput *input;
    SetOperatingModeStep step;
} SetOperatingModeContext;

static void
set_operating_mode_context_free (SetOperatingModeContext *ctx)
{
    g_object_unref (ctx->client);
    qmi_message_dms_set_operating_mode_input_unref (ctx->input);
    g_slice_free (SetOperatingModeContext, ctx);
}

static gboolean
modem_power_up_down_off_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_operating_mode_context_step (GTask *task);

static void
dms_set_fcc_authentication_ready (QmiClientDms *client,
                                  GAsyncResult *res,
                                  GTask *task)
{
    SetOperatingModeContext *ctx;
    QmiMessageDmsSetFccAuthenticationOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_dms_set_fcc_authentication_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_fcc_authentication_output_get_result (output, &error)) {
        /* No hard errors */
        mm_dbg ("Couldn't set FCC authentication: %s", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_dms_set_fcc_authentication_output_unref (output);

    /* Retry Set Operating Mode */
    ctx->step++;
    set_operating_mode_context_step (task);
}

static void
dms_set_operating_mode_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GTask *task)
{
    SetOperatingModeContext *ctx;
    QmiMessageDmsSetOperatingModeOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output) {
        /* If unsupported, just go out without errors */
        if (g_error_matches (error, QMI_CORE_ERROR, QMI_CORE_ERROR_UNSUPPORTED)) {
            mm_dbg ("Device doesn't support operating mode setting. Ignoring power update.");
            g_error_free (error);
            ctx->step = SET_OPERATING_MODE_STEP_LAST;
            set_operating_mode_context_step (task);
            return;
        }

        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        QmiDmsOperatingMode mode;

        /*
         * Some new devices, like the Dell DW5770, will return an internal error when
         * trying to bring the power mode to online.
         *
         * Other devices, like some rebranded EM7455 modules, will return an "invalid
         * transition" instead when trying to bring the power mode to online.
         *
         * We can avoid this by sending the magic "DMS Set FCC Auth" message before
         * retrying.
         */
        if (ctx->step == SET_OPERATING_MODE_STEP_FIRST &&
            qmi_message_dms_set_operating_mode_input_get_mode (ctx->input, &mode, NULL) &&
            mode == QMI_DMS_OPERATING_MODE_ONLINE &&
            (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INTERNAL) ||
             g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_TRANSITION))) {
            g_error_free (error);
            /* Go on to FCC auth */
            ctx->step++;
            set_operating_mode_context_step (task);
            qmi_message_dms_set_operating_mode_output_unref (output);
            return;
        }

        g_prefix_error (&error, "Couldn't set operating mode: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_set_operating_mode_output_unref (output);
        return;
    }

    qmi_message_dms_set_operating_mode_output_unref (output);

    /* Good! we're done, go to last step */
    ctx->step = SET_OPERATING_MODE_STEP_LAST;
    set_operating_mode_context_step (task);
}

static void
set_operating_mode_context_step (GTask *task)
{
    SetOperatingModeContext *ctx;

    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case SET_OPERATING_MODE_STEP_FIRST:
        mm_dbg ("Setting device operating mode...");
        qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (ctx->client),
                                           ctx->input,
                                           20,
                                           NULL,
                                           (GAsyncReadyCallback)dms_set_operating_mode_ready,
                                           task);
        return;
    case SET_OPERATING_MODE_STEP_FCC_AUTH:
        mm_dbg ("Setting FCC auth...");
        qmi_client_dms_set_fcc_authentication (QMI_CLIENT_DMS (ctx->client),
                                               NULL,
                                               5,
                                               NULL,
                                               (GAsyncReadyCallback)dms_set_fcc_authentication_ready,
                                               task);
        return;
    case SET_OPERATING_MODE_STEP_RETRY:
        mm_dbg ("Setting device operating mode (retry)...");
        qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (ctx->client),
                                           ctx->input,
                                           20,
                                           NULL,
                                           (GAsyncReadyCallback)dms_set_operating_mode_ready,
                                           task);
        return;
    case SET_OPERATING_MODE_STEP_LAST:
        /* Good! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    default:
        g_assert_not_reached ();
    }
}

static void
common_power_up_down_off (MMIfaceModem *self,
                          QmiDmsOperatingMode mode,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    SetOperatingModeContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    /* Setup context */
    ctx = g_slice_new0 (SetOperatingModeContext);
    ctx->client = g_object_ref (client);
    ctx->input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (ctx->input, mode, NULL);
    ctx->step = SET_OPERATING_MODE_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)set_operating_mode_context_free);

    set_operating_mode_context_step (task);
}

static void
modem_power_off (MMIfaceModem *self,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    common_power_up_down_off (self,
                              QMI_DMS_OPERATING_MODE_OFFLINE,
                              callback,
                              user_data);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    common_power_up_down_off (self,
                              QMI_DMS_OPERATING_MODE_LOW_POWER,
                              callback,
                              user_data);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    common_power_up_down_off (self,
                              QMI_DMS_OPERATING_MODE_ONLINE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

static MMModemPowerState
load_power_state_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_POWER_STATE_UNKNOWN;
    }
    return (MMModemPowerState)value;
}

static void
dms_get_operating_mode_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GTask *task)
{
    QmiMessageDmsGetOperatingModeOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_operating_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_get_operating_mode_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get operating mode: ");
        g_task_return_error (task, error);
    } else {
        QmiDmsOperatingMode mode = QMI_DMS_OPERATING_MODE_UNKNOWN;

        qmi_message_dms_get_operating_mode_output_get_mode (output, &mode, NULL);

        switch (mode) {
        case QMI_DMS_OPERATING_MODE_ONLINE:
            g_task_return_int (task, MM_MODEM_POWER_STATE_ON);
            break;
        case QMI_DMS_OPERATING_MODE_LOW_POWER:
        case QMI_DMS_OPERATING_MODE_PERSISTENT_LOW_POWER:
        case QMI_DMS_OPERATING_MODE_MODE_ONLY_LOW_POWER:
            g_task_return_int (task, MM_MODEM_POWER_STATE_LOW);
            break;
        case QMI_DMS_OPERATING_MODE_OFFLINE:
            g_task_return_int (task, MM_MODEM_POWER_STATE_OFF);
            break;
        default:
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unhandled power state: '%s' (%u)",
                                     qmi_dms_operating_mode_get_string (mode),
                                     mode);
            break;
        }
    }

    if (output)
        qmi_message_dms_get_operating_mode_output_unref (output);

    g_object_unref (task);
}

static void
load_power_state (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    mm_dbg ("Getting device operating mode...");
    qmi_client_dms_get_operating_mode (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)dms_get_operating_mode_ready,
                                       g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_qmi_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New QMI SIM */
    mm_sim_qmi_new (MM_BASE_MODEM (self),
                    MM_BROADBAND_MODEM_QMI (self)->priv->dms_uim_deprecated,
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
modem_reset_finish (MMIfaceModem *self,
                    GAsyncResult *res,
                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}


static void
modem_reset_power_cycle_ready (MMBroadbandModemQmi *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;

    if (!power_cycle_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}


static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    /* Power cycle the modem */
    power_cycle (MM_BROADBAND_MODEM_QMI (self),
                 (GAsyncReadyCallback)modem_reset_power_cycle_ready,
                 g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Factory reset (Modem interface) */

static gboolean
modem_factory_reset_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
dms_restore_factory_defaults_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    GTask *task)
{
    QmiMessageDmsRestoreFactoryDefaultsOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_restore_factory_defaults_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_dms_restore_factory_defaults_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't restore factory defaults: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_dms_restore_factory_defaults_output_unref (output);

    g_object_unref (task);
}

static void
modem_factory_reset (MMIfaceModem *self,
                     const gchar *code,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiMessageDmsRestoreFactoryDefaultsInput *input;
    GTask *task;
    QmiClient *client = NULL;
    GError *error = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_dms_restore_factory_defaults_input_new ();
    if (!qmi_message_dms_restore_factory_defaults_input_set_service_programming_code (
            input,
            code,
            &error)) {
        qmi_message_dms_restore_factory_defaults_input_unref (input);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_dbg ("performing a factory reset...");
    qmi_client_dms_restore_factory_defaults (QMI_CLIENT_DMS (client),
                                             input,
                                             10,
                                             NULL,
                                             (GAsyncReadyCallback)dms_restore_factory_defaults_ready,
                                             task);
}

/*****************************************************************************/
/* Load current modes (Modem interface) */

typedef struct {
    QmiClientNas *client;
    gboolean run_get_system_selection_preference;
    gboolean run_get_technology_preference;
} LoadCurrentModesContext;

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadCurrentModesResult;

static void
load_current_modes_context_free (LoadCurrentModesContext *ctx)
{
    g_object_unref (ctx->client);
    g_free (ctx);
}

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadCurrentModesResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *allowed = result->allowed;
    *preferred = result->preferred;
    g_free (result);
    return TRUE;
}

static void load_current_modes_context_step (GTask *task);

static void
get_technology_preference_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    LoadCurrentModesContext *ctx;
    LoadCurrentModesResult *result = NULL;
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_get_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_technology_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't get technology preference: %s", error->message);
        g_error_free (error);
    } else {
        MMModemMode allowed;
        QmiNasRadioTechnologyPreference preference_mask;

        qmi_message_nas_get_technology_preference_output_get_active (
            output,
            &preference_mask,
            NULL, /* duration */
            NULL);
        allowed = mm_modem_mode_from_qmi_radio_technology_preference (preference_mask);
        if (allowed == MM_MODEM_MODE_NONE) {
            gchar *str;

            str = qmi_nas_radio_technology_preference_build_string_from_mask (preference_mask);
            mm_dbg ("Unsupported modes reported: '%s'", str);
            g_free (str);
        } else {
            /* We got a valid value from here */
            result = g_new (LoadCurrentModesResult, 1);
            result->allowed = allowed;
            result->preferred = MM_MODEM_MODE_NONE;
        }
    }

    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);

    if (!result) {
        ctx->run_get_technology_preference = FALSE;
        load_current_modes_context_step (task);
        return;
    }

    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
current_modes_get_system_selection_preference_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     GTask *task)
{
    LoadCurrentModesContext *ctx;
    LoadCurrentModesResult *result = NULL;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;
    QmiNasRatModePreference mode_preference_mask = 0;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't get system selection preference: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_mode_preference (
                   output,
                   &mode_preference_mask,
                   NULL)) {
        mm_dbg ("Mode preference not reported in system selection preference");
    } else {
        MMModemMode allowed;

        allowed = mm_modem_mode_from_qmi_rat_mode_preference (mode_preference_mask);
        if (allowed == MM_MODEM_MODE_NONE) {
            gchar *str;

            str = qmi_nas_rat_mode_preference_build_string_from_mask (mode_preference_mask);
            mm_dbg ("Unsupported modes reported: '%s'", str);
            g_free (str);
        } else {
            QmiNasGsmWcdmaAcquisitionOrderPreference gsm_or_wcdma;

            /* We got a valid value from here */
            result = g_new (LoadCurrentModesResult, 1);
            result->allowed = allowed;
            result->preferred = MM_MODEM_MODE_NONE;

            if ((mode_preference_mask & QMI_NAS_RAT_MODE_PREFERENCE_GSM) &&
                (mode_preference_mask & QMI_NAS_RAT_MODE_PREFERENCE_UMTS) &&
                qmi_message_nas_get_system_selection_preference_output_get_gsm_wcdma_acquisition_order_preference (
                        output,
                        &gsm_or_wcdma,
                        NULL)) {
                result->preferred = mm_modem_mode_from_qmi_gsm_wcdma_acquisition_order_preference (gsm_or_wcdma);
            }
        }
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    if (!result) {
        /* Try with the deprecated command */
        ctx->run_get_system_selection_preference = FALSE;
        load_current_modes_context_step (task);
        return;
    }

    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
load_current_modes_context_step (GTask *task)
{
    LoadCurrentModesContext *ctx;

    ctx = g_task_get_task_data (task);

    if (ctx->run_get_system_selection_preference) {
        qmi_client_nas_get_system_selection_preference (
            ctx->client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)current_modes_get_system_selection_preference_ready,
            task);
        return;
    }

    if (ctx->run_get_technology_preference) {
        qmi_client_nas_get_technology_preference (
            ctx->client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)get_technology_preference_ready,
            task);
        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Loading current modes is not supported by this device");
    g_object_unref (task);
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    LoadCurrentModesContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_new0 (LoadCurrentModesContext, 1);
    ctx->client = g_object_ref (client);

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_get_system_selection_preference = qmi_client_check_version (client, 1, 1);

    /* Technology preference introduced in NAS 1.0, so always available */
    ctx->run_get_technology_preference = TRUE;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)load_current_modes_context_free);

    load_current_modes_context_step (task);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    QmiClientNas *client;
    MMModemMode allowed;
    MMModemMode preferred;
    gboolean run_set_system_selection_preference;
    gboolean run_set_technology_preference;
} SetCurrentModesContext;

static void
set_current_modes_context_free (SetCurrentModesContext *ctx)
{
    g_object_unref (ctx->client);
    g_slice_free (SetCurrentModesContext, ctx);
}

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_current_modes_context_step (GTask *task);

static void
set_technology_preference_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    SetCurrentModesContext *ctx;
    QmiMessageNasSetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_set_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_set_technology_preference_output_get_result (output, &error) &&
               !g_error_matches (error,
                                 QMI_PROTOCOL_ERROR,
                                 QMI_PROTOCOL_ERROR_NO_EFFECT)) {
        mm_dbg ("Couldn't set technology preference: %s", error->message);
        g_error_free (error);
        qmi_message_nas_set_technology_preference_output_unref (output);
    } else {
        if (error)
            g_error_free (error);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        qmi_message_nas_set_technology_preference_output_unref (output);
        return;
    }

    ctx->run_set_technology_preference = FALSE;
    set_current_modes_context_step (task);
}

static void
allowed_modes_set_system_selection_preference_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     GTask *task)
{
    SetCurrentModesContext *ctx;
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't set system selection preference: %s", error->message);
        g_error_free (error);
        qmi_message_nas_set_system_selection_preference_output_unref (output);
    } else {
        /* Good! TODO: do we really need to wait for the indication? */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        qmi_message_nas_set_system_selection_preference_output_unref (output);
        return;
    }

    /* Try with the deprecated command */
    ctx->run_set_system_selection_preference = FALSE;
    set_current_modes_context_step (task);
}

static void
set_current_modes_context_step (GTask *task)
{
    MMIfaceModem *self;
    SetCurrentModesContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (ctx->run_set_system_selection_preference) {
        QmiMessageNasSetSystemSelectionPreferenceInput *input;
        QmiNasRatModePreference pref;

        pref = mm_modem_mode_to_qmi_rat_mode_preference (ctx->allowed,
                                                         mm_iface_modem_is_cdma (self),
                                                         mm_iface_modem_is_3gpp (self));
        if (!pref) {
            gchar *str;

            str = mm_modem_mode_build_string_from_mask (ctx->allowed);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unhandled allowed mode setting: '%s'",
                                     str);
            g_object_unref (task);
            g_free (str);
            return;
        }

        input = qmi_message_nas_set_system_selection_preference_input_new ();
        qmi_message_nas_set_system_selection_preference_input_set_mode_preference (input, pref, NULL);

        /* Only set acquisition order preference if both 2G and 3G given as allowed */
        if (mm_iface_modem_is_3gpp (self) &&
            ctx->allowed & MM_MODEM_MODE_2G &&
            ctx->allowed & MM_MODEM_MODE_3G) {
            QmiNasGsmWcdmaAcquisitionOrderPreference order;

            order = mm_modem_mode_to_qmi_gsm_wcdma_acquisition_order_preference (ctx->preferred);
            qmi_message_nas_set_system_selection_preference_input_set_gsm_wcdma_acquisition_order_preference (input, order, NULL);
        }

        qmi_message_nas_set_system_selection_preference_input_set_change_duration (input, QMI_NAS_CHANGE_DURATION_PERMANENT, NULL);

        qmi_client_nas_set_system_selection_preference (
            ctx->client,
            input,
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)allowed_modes_set_system_selection_preference_ready,
            task);
        qmi_message_nas_set_system_selection_preference_input_unref (input);
        return;
    }

    if (ctx->run_set_technology_preference) {
        QmiMessageNasSetTechnologyPreferenceInput *input;
        QmiNasRadioTechnologyPreference pref;

        if (ctx->preferred != MM_MODEM_MODE_NONE) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Cannot set specific preferred mode");
            g_object_unref (task);
            return;
        }

        pref = mm_modem_mode_to_qmi_radio_technology_preference (ctx->allowed,
                                                                 mm_iface_modem_is_cdma (self));
        if (!pref) {
            gchar *str;

            str = mm_modem_mode_build_string_from_mask (ctx->allowed);
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unhandled allowed mode setting: '%s'",
                                     str);
            g_object_unref (task);
            g_free (str);
            return;
        }

        input = qmi_message_nas_set_technology_preference_input_new ();
        qmi_message_nas_set_technology_preference_input_set_current (input, pref, QMI_NAS_PREFERENCE_DURATION_PERMANENT, NULL);

        qmi_client_nas_set_technology_preference (
            ctx->client,
            input,
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)set_technology_preference_ready,
            task);
        qmi_message_nas_set_technology_preference_input_unref (input);
        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Setting allowed modes is not supported by this device");
    g_object_unref (task);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    SetCurrentModesContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (SetCurrentModesContext);
    ctx->client = g_object_ref (client);

    if (allowed == MM_MODEM_MODE_ANY && ctx->preferred == MM_MODEM_MODE_NONE) {
        ctx->allowed = MM_MODEM_MODE_NONE;
        if (mm_iface_modem_is_2g (self))
            ctx->allowed |= MM_MODEM_MODE_2G;
        if (mm_iface_modem_is_3g (self))
            ctx->allowed |= MM_MODEM_MODE_3G;
        if (mm_iface_modem_is_4g (self))
            ctx->allowed |= MM_MODEM_MODE_4G;
        ctx->preferred = MM_MODEM_MODE_NONE;
    } else {
        ctx->allowed = allowed;
        ctx->preferred = preferred;
    }

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_set_system_selection_preference = qmi_client_check_version (client, 1, 1);

    /* Technology preference introduced in NAS 1.0, so always available */
    ctx->run_set_technology_preference = TRUE;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_current_modes_context_free);

    set_current_modes_context_step (task);
}

/*****************************************************************************/
/* IMEI loading (3GPP interface) */

static gchar *
modem_3gpp_load_imei_finish (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_imei (MMIfaceModem3gpp *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->imei)
        g_task_return_pointer (task, g_strdup (self->priv->imei), g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device doesn't report a valid IMEI");
    g_object_unref (task);
}

/*****************************************************************************/
/* Facility locks status loading (3GPP interface) */

typedef struct {
    QmiClient *client;
    guint current;
    MMModem3gppFacility facilities;
    MMModem3gppFacility locks;
} LoadEnabledFacilityLocksContext;

static void get_next_facility_lock_status (GTask *task);

static void
load_enabled_facility_locks_context_free (LoadEnabledFacilityLocksContext *ctx)
{
    g_object_unref (ctx->client);
    g_free (ctx);
}

static MMModem3gppFacility
modem_3gpp_load_enabled_facility_locks_finish (MMIfaceModem3gpp *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_3GPP_FACILITY_NONE;
    }
    return (MMModem3gppFacility)value;
}

static void
get_sim_lock_status_via_pin_status_ready (QmiClientDms *client,
                                          GAsyncResult *res,
                                          GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;
    QmiMessageDmsUimGetPinStatusOutput *output;
    gboolean enabled;

    ctx = g_task_get_task_data (task);

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, NULL);
    if (!output ||
        !qmi_message_dms_uim_get_pin_status_output_get_result (output, NULL)) {
        mm_dbg ("Couldn't query PIN status, assuming SIM PIN is disabled");
        enabled = FALSE;
    } else {
        QmiDmsUimPinStatus current_status;

        if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
            output,
            &current_status,
            NULL, /* verify_retries_left */
            NULL, /* unblock_retries_left */
            NULL)) {
            enabled = mm_pin_enabled_from_qmi_uim_pin_status (current_status);
            mm_dbg ("PIN is reported %s", (enabled ? "enabled" : "disabled"));
        } else {
            mm_dbg ("Couldn't find PIN1 status in the result, assuming SIM PIN is disabled");
            enabled = FALSE;
        }
    }

    if (output)
        qmi_message_dms_uim_get_pin_status_output_unref (output);

    if (enabled) {
        ctx->locks |= (MM_MODEM_3GPP_FACILITY_SIM);
    } else {
        ctx->locks &= ~(MM_MODEM_3GPP_FACILITY_SIM);
    }

    /* No more facilities to query, all done */
    g_task_return_int (task, ctx->locks);
    g_object_unref (task);
}

/* the SIM lock cannot be queried with the qmi_get_ck_status function,
 * therefore using the PIN status */
static void
get_sim_lock_status_via_pin_status (GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;

    ctx = g_task_get_task_data (task);

    mm_dbg ("Retrieving PIN status to check for enabled PIN");
    /* if the SIM is locked or not can only be queried by locking at
     * the PIN status */
    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (ctx->client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)get_sim_lock_status_via_pin_status_ready,
                                       task);
}

static void
dms_uim_get_ck_status_ready (QmiClientDms *client,
                             GAsyncResult *res,
                             GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;
    gchar *facility_str;
    QmiMessageDmsUimGetCkStatusOutput *output;

    ctx = g_task_get_task_data (task);
    facility_str = mm_modem_3gpp_facility_build_string_from_mask (1 << ctx->current);
    output = qmi_client_dms_uim_get_ck_status_finish (client, res, NULL);
    if (!output ||
        !qmi_message_dms_uim_get_ck_status_output_get_result (output, NULL)) {
        /* On errors, we'll just assume disabled */
        mm_dbg ("Couldn't query facility '%s' status, assuming disabled", facility_str);
        ctx->locks &= ~(1 << ctx->current);
    } else {
        QmiDmsUimFacilityState state;
        guint8 verify_retries_left;
        guint8 unblock_retries_left;

        qmi_message_dms_uim_get_ck_status_output_get_ck_status (
            output,
            &state,
            &verify_retries_left,
            &unblock_retries_left,
            NULL);

        mm_dbg ("Facility '%s' is: '%s'",
                facility_str,
                qmi_dms_uim_facility_state_get_string (state));

        if (state == QMI_DMS_UIM_FACILITY_STATE_ACTIVATED ||
            state == QMI_DMS_UIM_FACILITY_STATE_BLOCKED) {
            ctx->locks |= (1 << ctx->current);
        }
    }

    if (output)
        qmi_message_dms_uim_get_ck_status_output_unref (output);
    g_free (facility_str);

    /* And go on with the next one */
    ctx->current++;
    get_next_facility_lock_status (task);
}

static void
get_next_facility_lock_status (GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;
    guint i;

    ctx = g_task_get_task_data (task);

    for (i = ctx->current; i < sizeof (MMModem3gppFacility) * 8; i++) {
        guint32 facility = 1 << i;

        /* Found the next one to query! */
        if (ctx->facilities & facility) {
            QmiMessageDmsUimGetCkStatusInput *input;

            /* Keep the current one */
            ctx->current = i;

            /* Query current */
            input = qmi_message_dms_uim_get_ck_status_input_new ();
            qmi_message_dms_uim_get_ck_status_input_set_facility (
                input,
                mm_3gpp_facility_to_qmi_uim_facility (facility),
                NULL);
            qmi_client_dms_uim_get_ck_status (QMI_CLIENT_DMS (ctx->client),
                                              input,
                                              5,
                                              NULL,
                                              (GAsyncReadyCallback)dms_uim_get_ck_status_ready,
                                              task);
            qmi_message_dms_uim_get_ck_status_input_unref (input);
            return;
        }
    }

    get_sim_lock_status_via_pin_status (task);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    LoadEnabledFacilityLocksContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_new (LoadEnabledFacilityLocksContext, 1);
    ctx->client = g_object_ref (client);

    /* Set initial list of facilities to query */
    ctx->facilities = (MM_MODEM_3GPP_FACILITY_PH_SIM |
                       MM_MODEM_3GPP_FACILITY_NET_PERS |
                       MM_MODEM_3GPP_FACILITY_NET_SUB_PERS |
                       MM_MODEM_3GPP_FACILITY_PROVIDER_PERS |
                       MM_MODEM_3GPP_FACILITY_CORP_PERS);
    ctx->locks = MM_MODEM_3GPP_FACILITY_NONE;
    ctx->current = 0;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_enabled_facility_locks_context_free);

    get_next_facility_lock_status (task);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
modem_3gpp_scan_networks_finish (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static MMModem3gppNetworkAvailability
network_availability_from_qmi_nas_network_status (QmiNasNetworkStatus qmi)
{
    if (qmi & QMI_NAS_NETWORK_STATUS_CURRENT_SERVING)
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT;

    if (qmi & QMI_NAS_NETWORK_STATUS_AVAILABLE) {
        if (qmi & QMI_NAS_NETWORK_STATUS_FORBIDDEN)
            return MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN;
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE;
    }

    return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
}

static MM3gppNetworkInfo *
get_3gpp_network_info (QmiMessageNasNetworkScanOutputNetworkInformationElement *element)
{
    GString *aux;
    MM3gppNetworkInfo *info;

    info = g_new (MM3gppNetworkInfo, 1);
    info->status = network_availability_from_qmi_nas_network_status (element->network_status);

    aux = g_string_new ("");
    /* MCC always 3 digits */
    g_string_append_printf (aux, "%.3"G_GUINT16_FORMAT, element->mcc);
    /* Guess about MNC, if < 100 assume it's 2 digits, no PCS info here */
    if (element->mnc >= 100)
        g_string_append_printf (aux, "%.3"G_GUINT16_FORMAT, element->mnc);
    else
        g_string_append_printf (aux, "%.2"G_GUINT16_FORMAT, element->mnc);

    info->operator_code = g_string_free (aux, FALSE);
    info->operator_short = NULL;
    info->operator_long = g_strdup (element->description);
    info->access_tech = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    return info;
}

static MMModemAccessTechnology
get_3gpp_access_technology (GArray *array,
                            gboolean *array_used_flags,
                            guint16 mcc,
                            guint16 mnc)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        QmiMessageNasNetworkScanOutputRadioAccessTechnologyElement *element;

        if (array_used_flags[i])
            continue;

        element = &g_array_index (array, QmiMessageNasNetworkScanOutputRadioAccessTechnologyElement, i);
        if (element->mcc == mcc &&
            element->mnc == mnc) {
            array_used_flags[i] = TRUE;
            return mm_modem_access_technology_from_qmi_radio_interface (element->radio_interface);
        }
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
nas_network_scan_ready (QmiClientNas *client,
                        GAsyncResult *res,
                        GTask *task)
{
    QmiMessageNasNetworkScanOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_network_scan_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_network_scan_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't scan networks: ");
        g_task_return_error (task, error);
    } else {
        GList *scan_result = NULL;
        GArray *info_array = NULL;

        if (qmi_message_nas_network_scan_output_get_network_information (output, &info_array, NULL)) {
            GArray *rat_array = NULL;
            gboolean *rat_array_used_flags = NULL;
            guint i;

            /* Get optional RAT array */
            qmi_message_nas_network_scan_output_get_radio_access_technology (output, &rat_array, NULL);
            if (rat_array)
                rat_array_used_flags = g_new0 (gboolean, rat_array->len);

            for (i = 0; i < info_array->len; i++) {
                QmiMessageNasNetworkScanOutputNetworkInformationElement *info_element;
                MM3gppNetworkInfo *info;

                info_element = &g_array_index (info_array, QmiMessageNasNetworkScanOutputNetworkInformationElement, i);

                info = get_3gpp_network_info (info_element);
                if (rat_array)
                    info->access_tech = get_3gpp_access_technology (rat_array,
                                                                    rat_array_used_flags,
                                                                    info_element->mcc,
                                                                    info_element->mnc);

                scan_result = g_list_append (scan_result, info);
            }

            g_free (rat_array_used_flags);
        }

        g_task_return_pointer (task, scan_result, (GDestroyNotify)mm_3gpp_network_info_list_free);
    }

    if (output)
        qmi_message_nas_network_scan_output_unref (output);

    g_object_unref (task);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    QmiClient *client = NULL;

    /* We will pass the GList in the GSimpleAsyncResult, so we must
     * ensure that there is a callback so that we get it properly
     * passed to the caller and deallocated afterwards */
    g_assert (callback != NULL);

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    mm_dbg ("Scanning networks...");
    qmi_client_nas_network_scan (QMI_CLIENT_NAS (client),
                                 NULL,
                                 300,
                                 NULL,
                                 (GAsyncReadyCallback)nas_network_scan_ready,
                                 g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load operator name (3GPP interface) */

static gchar *
modem_3gpp_load_operator_name_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->current_operator_description)
        g_task_return_pointer (task,
                               g_strdup (self->priv->current_operator_description),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Current operator description is still unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Load operator code (3GPP interface) */

static gchar *
modem_3gpp_load_operator_code_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_operator_code (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->current_operator_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->current_operator_id),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Current operator MCC/MNC is still unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
initiate_network_register_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    GError *error = NULL;
    QmiMessageNasInitiateNetworkRegisterOutput *output;

    output = qmi_client_nas_initiate_network_register_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_nas_initiate_network_register_output_get_result (output, &error)) {
        /* NOFX is not an error, they actually play pretty well */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            g_task_return_boolean (task, TRUE);
        } else {
            g_prefix_error (&error, "Couldn't initiate network register: ");
            g_task_return_error (task, error);
        }
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);

    if (output)
        qmi_message_nas_initiate_network_register_output_unref (output);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                const gchar *operator_id,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    guint16 mcc = 0;
    guint16 mnc = 0;
    QmiClient *client = NULL;
    QmiMessageNasInitiateNetworkRegisterInput *input;
    GError *error = NULL;

    /* Parse input MCC/MNC */
    if (operator_id && !mm_3gpp_parse_operator_id (operator_id, &mcc, &mnc, &error)) {
        g_assert (error != NULL);
        g_task_report_error (self,
                             callback,
                             user_data,
                             modem_3gpp_register_in_network,
                             error);
        return;
    }

    /* Get NAS client */
    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    input = qmi_message_nas_initiate_network_register_input_new ();

    if (mcc) {
        /* If the user sent a specific network to use, lock it in. */
        qmi_message_nas_initiate_network_register_input_set_action (
            input,
            QMI_NAS_NETWORK_REGISTER_TYPE_MANUAL,
            NULL);
        qmi_message_nas_initiate_network_register_input_set_manual_registration_info_3gpp (
            input,
            mcc,
            mnc,
            QMI_NAS_RADIO_INTERFACE_UNKNOWN,
            NULL);
    } else {
        /* Otherwise, automatic registration */
        qmi_message_nas_initiate_network_register_input_set_action (
            input,
            QMI_NAS_NETWORK_REGISTER_TYPE_AUTOMATIC,
            NULL);
    }

    qmi_client_nas_initiate_network_register (
        QMI_CLIENT_NAS (client),
        input,
        120,
        cancellable,
        (GAsyncReadyCallback)initiate_network_register_ready,
        g_task_new (self, NULL, callback, user_data));

    qmi_message_nas_initiate_network_register_input_unref (input);
}

/*****************************************************************************/
/* Registration checks (3GPP interface) */

static gboolean
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_process_serving_system_3gpp (MMBroadbandModemQmi *self,
                                    QmiMessageNasGetServingSystemOutput *response_output,
                                    QmiIndicationNasServingSystemOutput *indication_output)
{
    QmiNasRegistrationState registration_state;
    QmiNasAttachState cs_attach_state;
    QmiNasAttachState ps_attach_state;
    QmiNasNetworkType selected_network;
    GArray *radio_interfaces;
    GArray *data_service_capabilities;
    QmiNasRoamingIndicatorStatus roaming;
    guint16 mcc;
    guint16 mnc;
    const gchar *description;
    gboolean has_pcs_digit;
    guint16 lac;
    guint32 cid;
    MMModemAccessTechnology mm_access_technologies;
    MMModem3gppRegistrationState mm_cs_registration_state;
    MMModem3gppRegistrationState mm_ps_registration_state;

    if (response_output)
        qmi_message_nas_get_serving_system_output_get_serving_system (
            response_output,
            &registration_state,
            &cs_attach_state,
            &ps_attach_state,
            &selected_network,
            &radio_interfaces,
            NULL);
    else
        qmi_indication_nas_serving_system_output_get_serving_system (
            indication_output,
            &registration_state,
            &cs_attach_state,
            &ps_attach_state,
            &selected_network,
            &radio_interfaces,
            NULL);

    /* Build access technologies mask */
    data_service_capabilities = NULL;
    if (response_output)
        qmi_message_nas_get_serving_system_output_get_data_service_capability (response_output, &data_service_capabilities, NULL);
    else
        qmi_indication_nas_serving_system_output_get_data_service_capability (indication_output, &data_service_capabilities, NULL);

    if (data_service_capabilities)
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_data_capability_array (data_service_capabilities);
    else
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_radio_interface_array (radio_interfaces);

    /* Only process 3GPP info.
     * Seen the case already where 'selected_network' gives UNKNOWN but we still
     * have valid LTE info around. */
    if (selected_network == QMI_NAS_NETWORK_TYPE_3GPP ||
        (selected_network == QMI_NAS_NETWORK_TYPE_UNKNOWN &&
         (mm_access_technologies & MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK))) {
        mm_dbg ("Processing 3GPP info...");
    } else {
        MMModem3gppRegistrationState reg_state_3gpp;

        mm_dbg ("No 3GPP info given...");
        g_free (self->priv->current_operator_id);
        self->priv->current_operator_id = NULL;
        g_free (self->priv->current_operator_description);
        self->priv->current_operator_description = NULL;

        if (registration_state == QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING)
            reg_state_3gpp = MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;
        else
            reg_state_3gpp = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp);
        mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), 0, 0);
        return;
    }

    /* Get roaming status.
     * TODO: QMI may report per-access-technology roaming indicators, for when
     * the modem is connected to more than one network. How to handle those? */
    roaming = QMI_NAS_ROAMING_INDICATOR_STATUS_OFF;
    if (response_output)
        qmi_message_nas_get_serving_system_output_get_roaming_indicator (response_output, &roaming, NULL);
    else
        qmi_indication_nas_serving_system_output_get_roaming_indicator (indication_output, &roaming, NULL);

    /* Build MM registration states */
    mm_cs_registration_state =
        mm_modem_3gpp_registration_state_from_qmi_registration_state (
            cs_attach_state,
            registration_state,
            (roaming == QMI_NAS_ROAMING_INDICATOR_STATUS_ON));
    mm_ps_registration_state =
        mm_modem_3gpp_registration_state_from_qmi_registration_state (
            ps_attach_state,
            registration_state,
            (roaming == QMI_NAS_ROAMING_INDICATOR_STATUS_ON));

    /* Get and cache operator ID/name */
    if ((response_output &&
         qmi_message_nas_get_serving_system_output_get_current_plmn (
             response_output,
             &mcc,
             &mnc,
             &description,
             NULL)) ||
        (indication_output &&
         qmi_indication_nas_serving_system_output_get_current_plmn (
             indication_output,
             &mcc,
             &mnc,
             &description,
             NULL))) {
        /* When we don't have information about leading PCS digit, guess best */
        g_free (self->priv->current_operator_id);
        if (mnc >= 100)
            self->priv->current_operator_id =
                g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.3" G_GUINT16_FORMAT,
                                 mcc,
                                 mnc);
        else
            self->priv->current_operator_id =
                g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.2" G_GUINT16_FORMAT,
                                 mcc,
                                 mnc);

        g_clear_pointer (&self->priv->current_operator_description, g_free);
        /* Some Telit modems apparently sometimes report non-UTF8 characters */
        if (g_utf8_validate (description, -1, NULL))
            self->priv->current_operator_description = g_strdup (description);
    }

    /* If MNC comes with PCS digit, we must make sure the additional
     * leading '0' is added */
    if (((response_output &&
          qmi_message_nas_get_serving_system_output_get_mnc_pcs_digit_include_status (
              response_output,
              &mcc,
              &mnc,
              &has_pcs_digit,
              NULL)) ||
         (indication_output &&
          qmi_indication_nas_serving_system_output_get_mnc_pcs_digit_include_status (
              indication_output,
              &mcc,
              &mnc,
              &has_pcs_digit,
              NULL))) &&
        has_pcs_digit) {
        g_free (self->priv->current_operator_id);
        self->priv->current_operator_id =
            g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.3" G_GUINT16_FORMAT,
                             mcc,
                             mnc);
    }

    /* Report new registration states */
    mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), mm_cs_registration_state);
    mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), mm_ps_registration_state);
    if (mm_access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_LTE)
        mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self), mm_ps_registration_state);

    /* Get 3GPP location LAC and CI */
    lac = 0;
    cid = 0;
    if ((response_output &&
         qmi_message_nas_get_serving_system_output_get_lac_3gpp (response_output, &lac, NULL) &&
         qmi_message_nas_get_serving_system_output_get_cid_3gpp (response_output, &cid, NULL)) ||
        (indication_output &&
         qmi_indication_nas_serving_system_output_get_lac_3gpp (indication_output, &lac, NULL) &&
         qmi_indication_nas_serving_system_output_get_cid_3gpp (indication_output, &cid, NULL))) {
        /* Only update info in the interface if we get something */
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cid);
    }

    /* Note: don't update access technologies with the ones retrieved here; they
     * are not really the 'current' access technologies */
}

static void
get_serving_system_3gpp_ready (QmiClientNas *client,
                               GAsyncResult *res,
                               GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetServingSystemOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_serving_system_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_serving_system_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get serving system: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_nas_get_serving_system_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);

    common_process_serving_system_3gpp (self, output, NULL);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    qmi_message_nas_get_serving_system_output_unref (output);
}

#if defined WITH_NEWEST_QMI_COMMANDS

static gboolean
process_common_info (QmiNasServiceStatus service_status,
                     gboolean domain_valid,
                     QmiNasNetworkServiceDomain domain,
                     gboolean roaming_status_valid,
                     QmiNasRoamingStatus roaming_status,
                     gboolean forbidden_valid,
                     gboolean forbidden,
                     gboolean lac_valid,
                     guint16 lac,
                     gboolean cid_valid,
                     guint32 cid,
                     gboolean network_id_valid,
                     const gchar *mcc,
                     const gchar *mnc,
                     MMModem3gppRegistrationState *mm_cs_registration_state,
                     MMModem3gppRegistrationState *mm_ps_registration_state,
                     guint16 *mm_lac,
                     guint32 *mm_cid,
                     gchar **mm_operator_id)
{
    MMModem3gppRegistrationState tmp_registration_state;
    gboolean apply_cs = TRUE;
    gboolean apply_ps = TRUE;

    if (service_status != QMI_NAS_SERVICE_STATUS_LIMITED &&
        service_status != QMI_NAS_SERVICE_STATUS_AVAILABLE &&
        service_status != QMI_NAS_SERVICE_STATUS_LIMITED_REGIONAL)
        return FALSE;

    /* If we don't have domain, unknown */
    if (!domain_valid)
        tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    else if (domain == QMI_NAS_NETWORK_SERVICE_DOMAIN_NONE)
        tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;
    else if (domain == QMI_NAS_NETWORK_SERVICE_DOMAIN_UNKNOWN)
        tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    else {
        /* If we have CS or PS service domain, assume registered for now */
        if (domain == QMI_NAS_NETWORK_SERVICE_DOMAIN_CS)
            apply_ps = FALSE;
        else if (domain == QMI_NAS_NETWORK_SERVICE_DOMAIN_PS)
            apply_cs = FALSE;
        else if (domain == QMI_NAS_NETWORK_SERVICE_DOMAIN_CS_PS)
            /* both apply */ ;

        /* Check if we really are roaming or forbidden */
        if (forbidden_valid && forbidden)
            tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_DENIED;
        else {
            if (roaming_status_valid && roaming_status == QMI_NAS_ROAMING_STATUS_ON)
                tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING;
            else
                tmp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_HOME;

            /* If we're registered either at home or roaming, try to get LAC/CID */
            if (lac_valid)
                *mm_lac = lac;
            if (cid_valid)
                *mm_cid = cid;
        }
    }

    if (apply_cs)
        *mm_cs_registration_state = tmp_registration_state;
    if (apply_ps)
        *mm_ps_registration_state = tmp_registration_state;

    if (network_id_valid) {
        *mm_operator_id = g_malloc (7);
        memcpy (*mm_operator_id, mcc, 3);
        if (mnc[2] == 0xFF) {
            memcpy (&((*mm_operator_id)[3]), mnc, 2);
            (*mm_operator_id)[5] = '\0';
        } else {
            memcpy (&((*mm_operator_id)[3]), mnc, 3);
            (*mm_operator_id)[6] = '\0';
        }
    }

    return TRUE;
}

static gboolean
process_gsm_info (QmiMessageNasGetSystemInfoOutput *response_output,
                  QmiIndicationNasSystemInfoOutput *indication_output,
                  MMModem3gppRegistrationState *mm_cs_registration_state,
                  MMModem3gppRegistrationState *mm_ps_registration_state,
                  guint16 *mm_lac,
                  guint32 *mm_cid,
                  gchar **mm_operator_id)
{
    QmiNasServiceStatus service_status;
    gboolean domain_valid;
    QmiNasNetworkServiceDomain domain;
    gboolean roaming_status_valid;
    QmiNasRoamingStatus roaming_status;
    gboolean forbidden_valid;
    gboolean forbidden;
    gboolean lac_valid;
    guint16 lac;
    gboolean cid_valid;
    guint32 cid;
    gboolean network_id_valid;
    const gchar *mcc;
    const gchar *mnc;

    g_assert ((response_output != NULL && indication_output == NULL) ||
              (response_output == NULL && indication_output != NULL));

    *mm_ps_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_cs_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_lac = 0;
    *mm_cid = 0;
    g_free (*mm_operator_id);
    *mm_operator_id = NULL;

    if (response_output) {
        if (!qmi_message_nas_get_system_info_output_get_gsm_service_status (
                response_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_message_nas_get_system_info_output_get_gsm_system_info (
                response_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* egprs support */
                NULL, NULL, /* dtm_support */
                NULL)) {
            mm_dbg ("No GSM service reported");
            /* No GSM service */
            return FALSE;
        }
    } else {
        if (!qmi_indication_nas_system_info_output_get_gsm_service_status (
                indication_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_indication_nas_system_info_output_get_gsm_system_info (
                indication_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* egprs support */
                NULL, NULL, /* dtm_support */
                NULL)) {
            mm_dbg ("No GSM service reported");
            /* No GSM service */
            return FALSE;
        }
    }

    if (!process_common_info (service_status,
                              domain_valid,         domain,
                              roaming_status_valid, roaming_status,
                              forbidden_valid,      forbidden,
                              lac_valid,            lac,
                              cid_valid,            cid,
                              network_id_valid,     mcc, mnc,
                              mm_cs_registration_state,
                              mm_ps_registration_state,
                              mm_lac,
                              mm_cid,
                              mm_operator_id)) {
        mm_dbg ("No GSM service registered");
        return FALSE;
    }

    return TRUE;
}

static gboolean
process_wcdma_info (QmiMessageNasGetSystemInfoOutput *response_output,
                    QmiIndicationNasSystemInfoOutput *indication_output,
                    MMModem3gppRegistrationState *mm_cs_registration_state,
                    MMModem3gppRegistrationState *mm_ps_registration_state,
                    guint16 *mm_lac,
                    guint32 *mm_cid,
                    gchar **mm_operator_id)
{
    QmiNasServiceStatus service_status;
    gboolean domain_valid;
    QmiNasNetworkServiceDomain domain;
    gboolean roaming_status_valid;
    QmiNasRoamingStatus roaming_status;
    gboolean forbidden_valid;
    gboolean forbidden;
    gboolean lac_valid;
    guint16 lac;
    gboolean cid_valid;
    guint32 cid;
    gboolean network_id_valid;
    const gchar *mcc;
    const gchar *mnc;
    gboolean hs_service_valid;
    QmiNasWcdmaHsService hs_service;

    g_assert ((response_output != NULL && indication_output == NULL) ||
              (response_output == NULL && indication_output != NULL));

    *mm_ps_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_cs_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_lac = 0;
    *mm_cid = 0;
    g_free (*mm_operator_id);
    *mm_operator_id = NULL;

    if (response_output) {
        if (!qmi_message_nas_get_system_info_output_get_wcdma_service_status (
                response_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_message_nas_get_system_info_output_get_wcdma_system_info (
                response_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* hs_call_status */
                &hs_service_valid,     &hs_service,
                NULL, NULL, /* primary_scrambling_code */
                NULL)) {
            mm_dbg ("No WCDMA service reported");
            /* No GSM service */
            return FALSE;
        }
    } else {
        if (!qmi_indication_nas_system_info_output_get_wcdma_service_status (
                indication_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_indication_nas_system_info_output_get_wcdma_system_info (
                indication_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* hs_call_status */
                &hs_service_valid,     &hs_service,
                NULL, NULL, /* primary_scrambling_code */
                NULL)) {
            mm_dbg ("No WCDMA service reported");
            /* No GSM service */
            return FALSE;
        }
    }

    if (!process_common_info (service_status,
                              domain_valid,         domain,
                              roaming_status_valid, roaming_status,
                              forbidden_valid,      forbidden,
                              lac_valid,            lac,
                              cid_valid,            cid,
                              network_id_valid,     mcc, mnc,
                              mm_cs_registration_state,
                              mm_ps_registration_state,
                              mm_lac,
                              mm_cid,
                              mm_operator_id)) {
        mm_dbg ("No WCDMA service registered");
        return FALSE;
    }

    return TRUE;
}

static gboolean
process_lte_info (QmiMessageNasGetSystemInfoOutput *response_output,
                  QmiIndicationNasSystemInfoOutput *indication_output,
                  MMModem3gppRegistrationState *mm_cs_registration_state,
                  MMModem3gppRegistrationState *mm_ps_registration_state,
                  guint16 *mm_lac,
                  guint32 *mm_cid,
                  gchar **mm_operator_id)
{
    QmiNasServiceStatus service_status;
    gboolean domain_valid;
    QmiNasNetworkServiceDomain domain;
    gboolean roaming_status_valid;
    QmiNasRoamingStatus roaming_status;
    gboolean forbidden_valid;
    gboolean forbidden;
    gboolean lac_valid;
    guint16 lac;
    gboolean cid_valid;
    guint32 cid;
    gboolean network_id_valid;
    const gchar *mcc;
    const gchar *mnc;

    g_assert ((response_output != NULL && indication_output == NULL) ||
              (response_output == NULL && indication_output != NULL));

    *mm_ps_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_cs_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    *mm_lac = 0;
    *mm_cid = 0;
    g_free (*mm_operator_id);
    *mm_operator_id = NULL;

    if (response_output) {
        if (!qmi_message_nas_get_system_info_output_get_lte_service_status (
                response_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_message_nas_get_system_info_output_get_lte_system_info (
                response_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* tac */
                NULL)) {
            mm_dbg ("No LTE service reported");
            /* No GSM service */
            return FALSE;
        }
    } else {
        if (!qmi_indication_nas_system_info_output_get_lte_service_status (
                indication_output,
                &service_status,
                NULL, /* true_service_status */
                NULL, /* preferred_data_path */
                NULL) ||
            !qmi_indication_nas_system_info_output_get_lte_system_info (
                indication_output,
                &domain_valid,         &domain,
                NULL, NULL, /* service_capability */
                &roaming_status_valid, &roaming_status,
                &forbidden_valid,      &forbidden,
                &lac_valid,            &lac,
                &cid_valid,            &cid,
                NULL, NULL, NULL, /* registration_reject_info */
                &network_id_valid,     &mcc, &mnc,
                NULL, NULL, /* tac */
                NULL)) {
            mm_dbg ("No LTE service reported");
            /* No GSM service */
            return FALSE;
        }
    }

    if (!process_common_info (service_status,
                              domain_valid,         domain,
                              roaming_status_valid, roaming_status,
                              forbidden_valid,      forbidden,
                              lac_valid,            lac,
                              cid_valid,            cid,
                              network_id_valid,     mcc, mnc,
                              mm_cs_registration_state,
                              mm_ps_registration_state,
                              mm_lac,
                              mm_cid,
                              mm_operator_id)) {
        mm_dbg ("No LTE service registered");
        return FALSE;
    }

    return TRUE;
}

static void
common_process_system_info_3gpp (MMBroadbandModemQmi *self,
                                 QmiMessageNasGetSystemInfoOutput *response_output,
                                 QmiIndicationNasSystemInfoOutput *indication_output)
{
    MMModem3gppRegistrationState cs_registration_state;
    MMModem3gppRegistrationState ps_registration_state;
    guint16 lac;
    guint32 cid;
    gchar *operator_id;
    gboolean has_lte_info;

    ps_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    cs_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    lac = 0;
    cid = 0;
    operator_id = NULL;

    /* Process infos, with the following priority:
     *   LTE > WCDMA > GSM
     * The first one giving results will be the one reported.
     */
    has_lte_info = process_lte_info (response_output, indication_output,
                                     &cs_registration_state,
                                     &ps_registration_state,
                                     &lac,
                                     &cid,
                                     &operator_id);
    if (!has_lte_info &&
        !process_wcdma_info (response_output, indication_output,
                             &cs_registration_state,
                             &ps_registration_state,
                             &lac,
                             &cid,
                             &operator_id) &&
        !process_gsm_info (response_output, indication_output,
                           &cs_registration_state,
                           &ps_registration_state,
                           &lac,
                           &cid,
                           &operator_id)) {
        mm_dbg ("No service (GSM, WCDMA or LTE) reported");
    }

    /* Cache current operator ID */
    if (operator_id) {
        g_free (self->priv->current_operator_id);
        self->priv->current_operator_id = operator_id;
    }

    /* Report new registration states */
    mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), cs_registration_state);
    mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), ps_registration_state);
    if (has_lte_info)
        mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self), ps_registration_state);
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cid);
}

static void
get_system_info_ready (QmiClientNas *client,
                       GAsyncResult *res,
                       GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetSystemInfoOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_system_info_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_system_info_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get system info: ");
        g_task_return_error (task, error);
        qmi_message_nas_get_system_info_output_unref (output);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);

    common_process_system_info_3gpp (self, output, NULL);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    qmi_message_nas_get_system_info_output_unref (output);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp *self,
                                    gboolean cs_supported,
                                    gboolean ps_supported,
                                    gboolean eps_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

#if defined WITH_NEWEST_QMI_COMMANDS
    /* System Info was added in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        qmi_client_nas_get_system_info (QMI_CLIENT_NAS (client),
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_system_info_ready,
                                        task);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    qmi_client_nas_get_serving_system (QMI_CLIENT_NAS (client),
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)get_serving_system_3gpp_ready,
                                       task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events (3GPP interface) */

typedef struct {
    QmiClientNas *client;
    gboolean enable; /* TRUE for enabling, FALSE for disabling */
} UnsolicitedRegistrationEventsContext;

static void
unsolicited_registration_events_context_free (UnsolicitedRegistrationEventsContext *ctx)
{
    g_object_unref (ctx->client);
    g_free (ctx);
}

static GTask *
unsolicited_registration_events_task_new (MMBroadbandModemQmi *self,
                                          QmiClient *client,
                                          gboolean enable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    UnsolicitedRegistrationEventsContext *ctx;
    GTask *task;

    ctx = g_new0 (UnsolicitedRegistrationEventsContext, 1);
    ctx->client = g_object_ref (client);
    ctx->enable = enable;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)unsolicited_registration_events_context_free);

    return task;
}

static gboolean
modem_3gpp_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                  GAsyncResult *res,
                                                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ri_serving_system_or_system_info_ready (QmiClientNas *client,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBroadbandModemQmi *self;
    UnsolicitedRegistrationEventsContext *ctx;
    QmiMessageNasRegisterIndicationsOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_register_indications_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_register_indications_output_get_result (output, &error)) {
        mm_dbg ("Couldn't register indications: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_nas_register_indications_output_unref (output);

    /* Just ignore errors for now */
    self->priv->unsolicited_registration_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_registration_events_serving_system (GTask *task)
{
    UnsolicitedRegistrationEventsContext *ctx;
    QmiMessageNasRegisterIndicationsInput *input;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_serving_system_events (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        task);
    qmi_message_nas_register_indications_input_unref (input);
}

#if defined WITH_NEWEST_QMI_COMMANDS
static void
common_enable_disable_unsolicited_registration_events_system_info (GTask *task)
{
    UnsolicitedRegistrationEventsContext *ctx;
    QmiMessageNasRegisterIndicationsInput *input;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_system_info (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        task);
    qmi_message_nas_register_indications_input_unref (input);
}
#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                    gboolean cs_supported,
                                                    gboolean ps_supported,
                                                    gboolean eps_supported,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = unsolicited_registration_events_task_new (self,
                                                     client,
                                                     FALSE,
                                                     callback,
                                                     user_data);

#if defined WITH_NEWEST_QMI_COMMANDS
    /* System Info was added in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        common_enable_disable_unsolicited_registration_events_system_info (task);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* Ability to explicitly enable/disable serving system indications was
     * added in NAS 1.2 */
    if (qmi_client_check_version (client, 1, 2)) {
        common_enable_disable_unsolicited_registration_events_serving_system (task);
        return;
    }

    /* Devices with NAS < 1.2 will just always issue serving system indications */
    self->priv->unsolicited_registration_events_enabled = FALSE;
    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Device doesn't allow disabling registration events");
    g_object_unref (task);
}

static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                   gboolean cs_supported,
                                                   gboolean ps_supported,
                                                   gboolean eps_supported,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (self,
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = unsolicited_registration_events_task_new (self,
                                                     client,
                                                     TRUE,
                                                     callback,
                                                     user_data);

    /* Ability to explicitly enable/disable serving system indications was
     * added in NAS 1.2 */
    if (qmi_client_check_version (client, 1, 2)) {
        common_enable_disable_unsolicited_registration_events_serving_system (task);
        return;
    }

    /* Devices with NAS < 1.2 will just always issue serving system indications */
    mm_dbg ("Assuming serving system indications are always enabled");
    self->priv->unsolicited_registration_events_enabled = TRUE;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Registration checks (CDMA interface) */

static gboolean
modem_cdma_run_registration_checks_finish (MMIfaceModemCdma *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_process_serving_system_cdma (MMBroadbandModemQmi *self,
                                    QmiMessageNasGetServingSystemOutput *response_output,
                                    QmiIndicationNasServingSystemOutput *indication_output)
{
    QmiNasRegistrationState registration_state;
    QmiNasNetworkType selected_network;
    GArray *radio_interfaces;
    GArray *data_service_capabilities;
    MMModemAccessTechnology mm_access_technologies;
    MMModemCdmaRegistrationState mm_cdma1x_registration_state;
    MMModemCdmaRegistrationState mm_evdo_registration_state;
    guint16 sid = 0;
    guint16 nid = 0;
    guint16 bs_id = 0;
    gint32 bs_longitude = G_MININT32;
    gint32 bs_latitude = G_MININT32;

    if (response_output)
        qmi_message_nas_get_serving_system_output_get_serving_system (
            response_output,
            &registration_state,
            NULL, /* cs_attach_state */
            NULL, /* ps_attach_state */
            &selected_network,
            &radio_interfaces,
            NULL);
    else
        qmi_indication_nas_serving_system_output_get_serving_system (
            indication_output,
            &registration_state,
            NULL, /* cs_attach_state */
            NULL, /* ps_attach_state */
            &selected_network,
            &radio_interfaces,
            NULL);

    /* Build access technologies mask */
    data_service_capabilities = NULL;
    if (response_output)
        qmi_message_nas_get_serving_system_output_get_data_service_capability (response_output,
                                                                               &data_service_capabilities,
                                                                               NULL);
    else
        qmi_indication_nas_serving_system_output_get_data_service_capability (indication_output,
                                                                              &data_service_capabilities,
                                                                              NULL);
    if (data_service_capabilities)
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_data_capability_array (data_service_capabilities);
    else
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_radio_interface_array (radio_interfaces);

    /* Only process 3GPP2 info */
    if (selected_network == QMI_NAS_NETWORK_TYPE_3GPP2 ||
        (selected_network == QMI_NAS_NETWORK_TYPE_UNKNOWN &&
         (mm_access_technologies & MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK))) {
        mm_dbg ("Processing CDMA info...");
    } else {
        mm_dbg ("No CDMA info given...");
        mm_iface_modem_cdma_update_cdma1x_registration_state (MM_IFACE_MODEM_CDMA (self),
                                                              MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                                                              0, 0);
        mm_iface_modem_cdma_update_evdo_registration_state (MM_IFACE_MODEM_CDMA (self),
                                                             MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_iface_modem_cdma_update_access_technologies (MM_IFACE_MODEM_CDMA (self),
                                                        MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_location_cdma_bs_clear (MM_IFACE_MODEM_LOCATION (self));
        return;
    }

    /* Get SID/NID */
    if (response_output)
        qmi_message_nas_get_serving_system_output_get_cdma_system_id (response_output, &sid, &nid, NULL);
    else
        qmi_indication_nas_serving_system_output_get_cdma_system_id (indication_output, &sid, &nid, NULL);

    /* Get BS location */
    if (response_output)
        qmi_message_nas_get_serving_system_output_get_cdma_base_station_info (response_output, &bs_id, &bs_latitude, &bs_longitude, NULL);
    else
        qmi_indication_nas_serving_system_output_get_cdma_base_station_info (indication_output, &bs_id, &bs_latitude, &bs_longitude, NULL);

    /* Build generic registration states */
    if (mm_access_technologies & MM_IFACE_MODEM_CDMA_ALL_CDMA1X_ACCESS_TECHNOLOGIES_MASK)
        mm_cdma1x_registration_state = mm_modem_cdma_registration_state_from_qmi_registration_state (registration_state);
    else
        mm_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

    if (mm_access_technologies & MM_IFACE_MODEM_CDMA_ALL_EVDO_ACCESS_TECHNOLOGIES_MASK)
        mm_evdo_registration_state = mm_modem_cdma_registration_state_from_qmi_registration_state (registration_state);
    else
        mm_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

    /* Process per-technology roaming flags */
    if (response_output) {
        GArray *array;

        if (qmi_message_nas_get_serving_system_output_get_roaming_indicator_list (response_output, &array, NULL)) {
            guint i;

            for (i = 0; i < array->len; i++) {
                QmiMessageNasGetServingSystemOutputRoamingIndicatorListElement *element;

                element = &g_array_index (array, QmiMessageNasGetServingSystemOutputRoamingIndicatorListElement, i);

                if (element->radio_interface == QMI_NAS_RADIO_INTERFACE_CDMA_1X &&
                    mm_cdma1x_registration_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED) {
                    if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_ON)
                        mm_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
                    else if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_OFF)
                        mm_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                } else if (element->radio_interface == QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO &&
                           mm_evdo_registration_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED) {
                    if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_ON)
                        mm_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
                    else if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_OFF)
                        mm_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                }
            }
        }
    } else {
        GArray *array;

        if (qmi_indication_nas_serving_system_output_get_roaming_indicator_list (indication_output, &array, NULL)) {
            guint i;

            for (i = 0; i < array->len; i++) {
                QmiIndicationNasServingSystemOutputRoamingIndicatorListElement *element;

                element = &g_array_index (array, QmiIndicationNasServingSystemOutputRoamingIndicatorListElement, i);

                if (element->radio_interface == QMI_NAS_RADIO_INTERFACE_CDMA_1X &&
                    mm_cdma1x_registration_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED) {
                    if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_ON)
                        mm_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
                    else if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_OFF)
                        mm_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                } else if (element->radio_interface == QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO &&
                           mm_evdo_registration_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED) {
                    if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_ON)
                        mm_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING;
                    else if (element->roaming_indicator == QMI_NAS_ROAMING_INDICATOR_STATUS_OFF)
                        mm_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_HOME;
                }
            }
        }
    }

    /* Note: don't rely on the 'Detailed Service Status', it's not always given. */

    /* Report new registration states */
    mm_iface_modem_cdma_update_cdma1x_registration_state (MM_IFACE_MODEM_CDMA (self),
                                                          mm_cdma1x_registration_state,
                                                          sid,
                                                          nid);
    mm_iface_modem_cdma_update_evdo_registration_state (MM_IFACE_MODEM_CDMA (self),
                                                        mm_evdo_registration_state);

    /* Note: don't update access technologies with the ones retrieved here; they
     * are not really the 'current' access technologies */

    /* Longitude and latitude given in units of 0.25 secs
     * Note that multiplying by 0.25 is like dividing by 4, so 60*60*4=14400 */
#define QMI_LONGITUDE_TO_DEGREES(longitude)       \
    (longitude != G_MININT32 ? \
     (((gdouble)longitude) / 14400.0) :           \
     MM_LOCATION_LONGITUDE_UNKNOWN)
#define QMI_LATITUDE_TO_DEGREES(latitude)         \
    (latitude != G_MININT32 ?   \
     (((gdouble)latitude) / 14400.0) :            \
     MM_LOCATION_LATITUDE_UNKNOWN)

    mm_iface_modem_location_cdma_bs_update (MM_IFACE_MODEM_LOCATION (self),
                                            QMI_LONGITUDE_TO_DEGREES (bs_longitude),
                                            QMI_LATITUDE_TO_DEGREES (bs_latitude));
}

static void
get_serving_system_cdma_ready (QmiClientNas *client,
                               GAsyncResult *res,
                               GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetServingSystemOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_serving_system_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_serving_system_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get serving system: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_nas_get_serving_system_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);

    common_process_serving_system_cdma (self, output, NULL);

    qmi_message_nas_get_serving_system_output_unref (output);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_cdma_run_registration_checks (MMIfaceModemCdma *self,
                                    gboolean cdma1x_supported,
                                    gboolean evdo_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    /* TODO: Run Get System Info in NAS >= 1.8 */

    qmi_client_nas_get_serving_system (QMI_CLIENT_NAS (client),
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)get_serving_system_cdma_ready,
                                       g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load initial activation state (CDMA interface) */

static MMModemCdmaActivationState
modem_cdma_load_activation_state_finish (MMIfaceModemCdma *_self,
                                         GAsyncResult *res,
                                         GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN;
    }

    /* Cache the value and also return it */
    self->priv->activation_state = (MMModemCdmaActivationState)value;

    return self->priv->activation_state;
}

static void
get_activation_state_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GTask *task)
{
    QmiDmsActivationState state = QMI_DMS_ACTIVATION_STATE_NOT_ACTIVATED;
    QmiMessageDmsGetActivationStateOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_get_activation_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_get_activation_state_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get activation state: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_get_activation_state_output_unref (output);
        return;
    }

    qmi_message_dms_get_activation_state_output_get_info (output, &state, NULL);
    qmi_message_dms_get_activation_state_output_unref (output);

    g_task_return_int (task,
                       mm_modem_cdma_activation_state_from_qmi_activation_state (state));
    g_object_unref (task);
}

static void
modem_cdma_load_activation_state (MMIfaceModemCdma *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    qmi_client_dms_get_activation_state (QMI_CLIENT_DMS (client),
                                         NULL,
                                         10,
                                         NULL,
                                         (GAsyncReadyCallback)get_activation_state_ready,
                                         g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Manual and OTA Activation (CDMA interface) */

#define MAX_MDN_CHECK_RETRIES 10

typedef enum {
    CDMA_ACTIVATION_STEP_FIRST,
    CDMA_ACTIVATION_STEP_ENABLE_INDICATIONS,
    CDMA_ACTIVATION_STEP_REQUEST_ACTIVATION,
    CDMA_ACTIVATION_STEP_WAIT_UNTIL_FINISHED,
    CDMA_ACTIVATION_STEP_POWER_CYCLE,
    CDMA_ACTIVATION_STEP_LAST
} CdmaActivationStep;

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientDms *client;
    CdmaActivationStep step;
    /* OTA activation... */
    QmiMessageDmsActivateAutomaticInput *input_automatic;
    /* Manual activation... */
    QmiMessageDmsActivateManualInput *input_manual;
    guint total_segments_size;
    guint segment_i;
    guint n_segments;
    GArray **segments;
    guint n_mdn_check_retries;
} CdmaActivationContext;

static void
cdma_activation_context_free (CdmaActivationContext *ctx)
{
    /* Cleanup the activation task from the private info */
    ctx->self->priv->activation_task = NULL;

    for (ctx->segment_i = 0; ctx->segment_i < ctx->n_segments; ctx->segment_i++)
        g_array_unref (ctx->segments[ctx->segment_i]);
    g_free (ctx->segments);

    if (ctx->input_automatic)
        qmi_message_dms_activate_automatic_input_unref (ctx->input_automatic);
    if (ctx->input_manual)
        qmi_message_dms_activate_manual_input_unref (ctx->input_manual);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (CdmaActivationContext, ctx);
}

static gboolean
modem_cdma_activate_finish (MMIfaceModemCdma *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
modem_cdma_activate_manual_finish (MMIfaceModemCdma *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void cdma_activation_context_step (GTask *task);

static void
cdma_activation_disable_indications (CdmaActivationContext *ctx)
{
    QmiMessageDmsSetEventReportInput *input;

    /* Remove the signal handler */
    g_assert (ctx->self->priv->activation_event_report_indication_id != 0);
    g_signal_handler_disconnect (ctx->client, ctx->self->priv->activation_event_report_indication_id);
    ctx->self->priv->activation_event_report_indication_id = 0;

    /* Disable the activation state change indications; don't worry about the result */
    input = qmi_message_dms_set_event_report_input_new ();
    qmi_message_dms_set_event_report_input_set_activation_state_reporting (input, FALSE, NULL);
    qmi_client_dms_set_event_report (ctx->client, input, 5, NULL, NULL, NULL);
    qmi_message_dms_set_event_report_input_unref (input);
}

static void
activation_power_cycle_ready (MMBroadbandModemQmi *self,
                              GAsyncResult *res,
                              GTask *task)
{
    CdmaActivationContext *ctx;
    GError *error = NULL;

    if (!power_cycle_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* And go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    cdma_activation_context_step (task);
}

static gboolean
retry_msisdn_check_cb (GTask *task)
{
    cdma_activation_context_step (task);
    return G_SOURCE_REMOVE;
}

static void
activate_manual_get_msisdn_ready (QmiClientDms *client,
                                  GAsyncResult *res,
                                  GTask *task)
{
    CdmaActivationContext *ctx;
    QmiMessageDmsGetMsisdnOutput *output = NULL;
    GError *error = NULL;
    const gchar *current_mdn = NULL;
    const gchar *expected_mdn = NULL;

    ctx = g_task_get_task_data (task);

    qmi_message_dms_activate_manual_input_get_info (ctx->input_manual,
                                                    NULL, /* spc */
                                                    NULL, /* sid */
                                                    &expected_mdn,
                                                    NULL, /* min */
                                                    NULL);

    output = qmi_client_dms_get_msisdn_finish (client, res, &error);
    if (output &&
        qmi_message_dms_get_msisdn_output_get_result (output, NULL) &&
        qmi_message_dms_get_msisdn_output_get_msisdn (output, &current_mdn, NULL) &&
        g_str_equal (current_mdn, expected_mdn)) {
        mm_dbg ("MDN successfully updated to '%s'", expected_mdn);
        qmi_message_dms_get_msisdn_output_unref (output);
        /* And go on to next step */
        ctx->step++;
        cdma_activation_context_step (task);
        return;
    }

    if (output)
        qmi_message_dms_get_msisdn_output_unref (output);

    if (ctx->n_mdn_check_retries < MAX_MDN_CHECK_RETRIES) {
        /* Retry after some time */
        mm_dbg ("MDN not yet updated, retrying...");
        g_timeout_add (1, (GSourceFunc) retry_msisdn_check_cb, task);
        return;
    }

    /* Well, all retries consumed already, return error */
    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "MDN was not correctly set during manual activation");
    g_object_unref (task);
}

static void
activation_event_report_indication_cb (QmiClientDms *client,
                                       QmiIndicationDmsEventReportOutput *output,
                                       MMBroadbandModemQmi *self)
{
    QmiDmsActivationState state;
    MMModemCdmaActivationState new;
    GError *error;

    /* If the indication doesn't have any activation state info, just return */
    if (!qmi_indication_dms_event_report_output_get_activation_state (output, &state, NULL))
        return;

    mm_dbg ("Activation state update: '%s'",
            qmi_dms_activation_state_get_string (state));

    new = mm_modem_cdma_activation_state_from_qmi_activation_state (state);

    if (self->priv->activation_state != new)
        mm_info ("Activation state changed: '%s'-->'%s'",
                 mm_modem_cdma_activation_state_get_string (self->priv->activation_state),
                 mm_modem_cdma_activation_state_get_string (new));

    /* Cache the new value */
    self->priv->activation_state = new;

    /* We consider a not-activated report in the indication as a failure */
    error = (new == MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED ?
             g_error_new (MM_CDMA_ACTIVATION_ERROR,
                          MM_CDMA_ACTIVATION_ERROR_UNKNOWN,
                          "Activation process failed") :
             NULL);

    /* Update activation state in the interface */
    mm_iface_modem_cdma_update_activation_state (MM_IFACE_MODEM_CDMA (self), new, error);

    /* Now, if we have a FINAL state, finish the ongoing activation state request */
    if (new != MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING) {
        GTask *task;
        CdmaActivationContext *ctx;

        g_assert (self->priv->activation_task != NULL);
        task = self->priv->activation_task;
        ctx = g_task_get_task_data (task);

        /* Disable further indications. */
        cdma_activation_disable_indications (ctx);

        /* If there is any error, finish the async method */
        if (error) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        /* Otherwise, go on to next step */
        ctx->step++;
        cdma_activation_context_step (task);
        return;
    }

    mm_dbg ("Activation process still ongoing...");
}

static void
activate_automatic_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask *task)
{
    CdmaActivationContext *ctx;
    QmiMessageDmsActivateAutomaticOutput *output;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_dms_activate_automatic_finish (client, res, &error);
    if (!output) {
        cdma_activation_disable_indications (ctx);
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_activate_automatic_output_get_result (output, &error)) {
        qmi_message_dms_activate_automatic_output_unref (output);
        cdma_activation_disable_indications (ctx);
        g_prefix_error (&error, "Couldn't request OTA activation: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_dms_activate_automatic_output_unref (output);

    /* Keep on */
    ctx->step++;
    cdma_activation_context_step (task);
}

static void
activate_manual_ready (QmiClientDms *client,
                       GAsyncResult *res,
                       GTask *task)
{
    CdmaActivationContext *ctx;
    QmiMessageDmsActivateManualOutput *output;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_dms_activate_manual_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_activate_manual_output_get_result (output, &error)) {
        qmi_message_dms_activate_manual_output_unref (output);
        g_prefix_error (&error, "Couldn't request manual activation: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_dms_activate_manual_output_unref (output);

    /* If pending segments to send, re-run same step */
    if (ctx->n_segments) {
        ctx->segment_i++;
        if (ctx->segment_i < ctx->n_segments) {
            /* There's a pending segment */
            cdma_activation_context_step (task);
            return;
        }
    }

    /* No more segments to send, go on */
    ctx->step++;
    cdma_activation_context_step (task);
}

static void
ser_activation_state_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GTask *task)
{
    CdmaActivationContext *ctx;
    QmiMessageDmsSetEventReportOutput *output;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    /* We cannot ignore errors, we NEED the indications to finish the
     * activation request properly */

    output = qmi_client_dms_set_event_report_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_set_event_report_output_get_result (output, &error)) {
        qmi_message_dms_set_event_report_output_unref (output);
        g_prefix_error (&error, "Couldn't set event report: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_dms_set_event_report_output_unref (output);

    /* Setup the indication handler */
    g_assert (ctx->self->priv->activation_event_report_indication_id == 0);
    ctx->self->priv->activation_event_report_indication_id =
        g_signal_connect (client,
                          "event-report",
                          G_CALLBACK (activation_event_report_indication_cb),
                          ctx->self);

    /* Keep on */
    ctx->step++;
    cdma_activation_context_step (task);
}

static void
cdma_activation_context_step (GTask *task)
{
    CdmaActivationContext *ctx;

    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case CDMA_ACTIVATION_STEP_FIRST:
        ctx->step++;
        /* Fall down to next step */

    case CDMA_ACTIVATION_STEP_ENABLE_INDICATIONS:
        /* Indications needed in automatic activation */
        if (ctx->input_automatic) {
            QmiMessageDmsSetEventReportInput *input;

            mm_info ("Activation step [1/5]: enabling indications");

            input = qmi_message_dms_set_event_report_input_new ();
            qmi_message_dms_set_event_report_input_set_activation_state_reporting (input, TRUE, NULL);
            qmi_client_dms_set_event_report (
                ctx->client,
                input,
                5,
                NULL,
                (GAsyncReadyCallback)ser_activation_state_ready,
                task);
            qmi_message_dms_set_event_report_input_unref (input);
            return;
        }

        /* Manual activation, no indications needed */
        g_assert (ctx->input_manual != NULL);
        mm_info ("Activation step [1/5]: indications not needed in manual activation");
        ctx->step++;
        /* Fall down to next step */

    case CDMA_ACTIVATION_STEP_REQUEST_ACTIVATION:
        /* Automatic activation */
        if (ctx->input_automatic) {
            mm_info ("Activation step [2/5]: requesting automatic (OTA) activation");

            qmi_client_dms_activate_automatic (ctx->client,
                                               ctx->input_automatic,
                                               10,
                                               NULL,
                                               (GAsyncReadyCallback)activate_automatic_ready,
                                               task);
            return;
        }

        /* Manual activation */
        g_assert (ctx->input_manual != NULL);
        if (!ctx->segments)
            mm_info ("Activation step [2/5]: requesting manual activation");
        else {
            mm_info ("Activation step [2/5]: requesting manual activation (PRL segment %u/%u)",
                     (ctx->segment_i + 1), ctx->n_segments);
            qmi_message_dms_activate_manual_input_set_prl (
                ctx->input_manual,
                (guint16)ctx->total_segments_size,
                (guint8)ctx->segment_i,
                ctx->segments[ctx->segment_i],
                NULL);
        }

        qmi_client_dms_activate_manual (ctx->client,
                                        ctx->input_manual,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)activate_manual_ready,
                                        task);
        return;

    case CDMA_ACTIVATION_STEP_WAIT_UNTIL_FINISHED:
        /* Automatic activation */
        if (ctx->input_automatic) {
            /* State updates via unsolicited messages */
            mm_info ("Activation step [3/5]: waiting for activation state updates");
            return;
        }

        /* Manual activation; needs MSISDN checks */
        g_assert (ctx->input_manual != NULL);
        ctx->n_mdn_check_retries++;
        mm_info ("Activation step [3/5]: checking MDN update (retry %u)", ctx->n_mdn_check_retries);
        qmi_client_dms_get_msisdn (ctx->client,
                                   NULL,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback)activate_manual_get_msisdn_ready,
                                   task);
        return;

    case CDMA_ACTIVATION_STEP_POWER_CYCLE:
        mm_info ("Activation step [4/5]: power-cycling...");
        power_cycle (ctx->self,
                     (GAsyncReadyCallback)activation_power_cycle_ready,
                     task);
        return;

    case CDMA_ACTIVATION_STEP_LAST:
        mm_info ("Activation step [5/5]: finished");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_cdma_activate (MMIfaceModemCdma *_self,
                     const gchar *carrier_code,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;
    CdmaActivationContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Fail if we have already an activation ongoing */
    if (self->priv->activation_task) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_IN_PROGRESS,
                                 "An activation operation is already in progress");
        g_object_unref (task);
        return;
    }

    /* Setup context */
    ctx = g_slice_new0 (CdmaActivationContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->step = CDMA_ACTIVATION_STEP_FIRST;

    /* Build base input bundle for the Automatic activation */
    ctx->input_automatic = qmi_message_dms_activate_automatic_input_new ();
    qmi_message_dms_activate_automatic_input_set_activation_code (ctx->input_automatic, carrier_code, NULL);

    g_task_set_task_data (task, ctx, (GDestroyNotify)cdma_activation_context_free);

    /* We keep the activation task in the private data, so that we don't
     * allow multiple activation requests at the same time. */
    self->priv->activation_task = task;
    cdma_activation_context_step (task);
}

static void
modem_cdma_activate_manual (MMIfaceModemCdma *_self,
                            MMCdmaManualActivationProperties *properties,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;
    CdmaActivationContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Fail if we have already an activation ongoing */
    if (self->priv->activation_task) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_IN_PROGRESS,
                                 "An activation operation is already in progress");
        g_object_unref (task);
        return;
    }

    /* Setup context */
    ctx = g_slice_new0 (CdmaActivationContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);

    g_task_set_task_data (task, ctx, (GDestroyNotify)cdma_activation_context_free);

    /* We keep the activation task in the private data, so that we don't
     * allow multiple activation requests at the same time. */
    self->priv->activation_task = task;

    /* Build base input bundle for the Manual activation */
    ctx->input_manual = qmi_message_dms_activate_manual_input_new ();
    qmi_message_dms_activate_manual_input_set_info (
        ctx->input_manual,
        mm_cdma_manual_activation_properties_get_spc (properties),
        mm_cdma_manual_activation_properties_get_sid (properties),
        mm_cdma_manual_activation_properties_get_mdn (properties),
        mm_cdma_manual_activation_properties_get_min (properties),
        NULL);

    if (mm_cdma_manual_activation_properties_get_mn_ha_key (properties))
        qmi_message_dms_activate_manual_input_set_mn_ha_key (
            ctx->input_manual,
            mm_cdma_manual_activation_properties_get_mn_ha_key (properties),
            NULL);

    if (mm_cdma_manual_activation_properties_get_mn_aaa_key (properties))
        qmi_message_dms_activate_manual_input_set_mn_aaa_key (
            ctx->input_manual,
            mm_cdma_manual_activation_properties_get_mn_aaa_key (properties),
            NULL);

    if (mm_cdma_manual_activation_properties_peek_prl_bytearray (properties)) {
        GByteArray *full_prl;
        guint i;
        guint adding;
        guint remaining;

        /* Just assume 512 is the max segment size...
         * TODO: probably need to read max segment size from the usb descriptor
         * WARN! Never ever use a MAX_PRL_SEGMENT_SIZE less than 64, or the sequence number
         * won't fit in a single byte!!! (16384/256=64) */
#define MAX_PRL_SEGMENT_SIZE 512

        full_prl = mm_cdma_manual_activation_properties_peek_prl_bytearray (properties);

        /* NOTE:  max PRL size should already be checked when reading from DBus,
         * so assert if longer */
        ctx->total_segments_size = full_prl->len;
        g_assert (ctx->total_segments_size <= 16384);

        ctx->n_segments = (guint) (full_prl->len / MAX_PRL_SEGMENT_SIZE);
        if (full_prl->len % MAX_PRL_SEGMENT_SIZE != 0)
            ctx->n_segments++;
        g_assert (ctx->n_segments <= 256);

        ctx->segments = g_new0 (GArray *, (ctx->n_segments + 1));

        adding = 0;
        remaining = full_prl->len;
        for (i = 0; i < ctx->n_segments; i++) {
            guint current_add;

            g_assert (remaining > 0);
            current_add = remaining > MAX_PRL_SEGMENT_SIZE ? MAX_PRL_SEGMENT_SIZE : remaining;
            ctx->segments[i] = g_array_sized_new (FALSE, FALSE, sizeof (guint8), current_add);
            g_array_append_vals (ctx->segments[i], &(full_prl->data[adding]), current_add);
            adding += current_add;
            g_assert (remaining >= current_add);
            remaining -= current_add;
        }

#undef MAX_PRL_SEGMENT_SIZE
    }

    cdma_activation_context_step (task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited registration event handlers
 * (3GPP and CDMA interface) */

static gboolean
common_setup_cleanup_unsolicited_registration_events_finish (MMBroadbandModemQmi *self,
                                                             GAsyncResult *res,
                                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#if defined WITH_NEWEST_QMI_COMMANDS
static void
system_info_indication_cb (QmiClientNas *client,
                           QmiIndicationNasSystemInfoOutput *output,
                           MMBroadbandModemQmi *self)
{
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        common_process_system_info_3gpp (self, NULL, output);
}
#endif

static void
serving_system_indication_cb (QmiClientNas *client,
                              QmiIndicationNasServingSystemOutput *output,
                              MMBroadbandModemQmi *self)
{
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        common_process_serving_system_3gpp (self, NULL, output);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        common_process_serving_system_cdma (self, NULL, output);
}

static void
common_setup_cleanup_unsolicited_registration_events (MMBroadbandModemQmi *self,
                                                      gboolean enable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_registration_events_setup) {
        mm_dbg ("Unsolicited registration events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Store new state */
    self->priv->unsolicited_registration_events_setup = enable;

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        /* Connect/Disconnect "System Info" indications */
        if (enable) {
            g_assert (self->priv->system_info_indication_id == 0);
            self->priv->system_info_indication_id =
                g_signal_connect (client,
                                  "system-info",
                                  G_CALLBACK (system_info_indication_cb),
                                  self);
        } else {
            g_assert (self->priv->system_info_indication_id != 0);
            g_signal_handler_disconnect (client, self->priv->system_info_indication_id);
            self->priv->system_info_indication_id = 0;
        }
    } else
#endif /* WITH_NEWEST_QMI_COMMANDS */
    {
        /* Connect/Disconnect "Serving System" indications */
        if (enable) {
            g_assert (self->priv->serving_system_indication_id == 0);
            self->priv->serving_system_indication_id =
                g_signal_connect (client,
                                  "serving-system",
                                  G_CALLBACK (serving_system_indication_cb),
                                  self);
        } else {
            g_assert (self->priv->serving_system_indication_id != 0);
            g_signal_handler_disconnect (client, self->priv->serving_system_indication_id);
            self->priv->serving_system_indication_id = 0;
        }
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited registration events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                 GAsyncResult *res,
                                                                 GError **error)

{    return common_setup_cleanup_unsolicited_registration_events_finish (MM_BROADBAND_MODEM_QMI (self), res, error);
}

static void
modem_3gpp_cleanup_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    common_setup_cleanup_unsolicited_registration_events (MM_BROADBAND_MODEM_QMI (self),
                                                          FALSE,
                                                          callback,
                                                          user_data);
}

static void
modem_3gpp_setup_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data)
{
    common_setup_cleanup_unsolicited_registration_events (MM_BROADBAND_MODEM_QMI (self),
                                                          TRUE,
                                                          callback,
                                                          user_data);
}

/*****************************************************************************/
/* MEID loading (CDMA interface) */

static gchar *
modem_cdma_load_meid_finish (MMIfaceModemCdma *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_cdma_load_meid (MMIfaceModemCdma *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->meid)
        g_task_return_pointer (task, g_strdup (self->priv->meid), g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device doesn't report a valid MEID");
    g_object_unref (task);
}

/*****************************************************************************/
/* ESN loading (CDMA interface) */

static gchar *
modem_cdma_load_esn_finish (MMIfaceModemCdma *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_cdma_load_esn (MMIfaceModemCdma *_self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->esn)
        g_task_return_pointer (task, g_strdup (self->priv->esn), g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device doesn't report a valid ESN");
    g_object_unref (task);
}

/*****************************************************************************/
/* Enabling/disabling unsolicited events (3GPP and CDMA interface)
 *
 * If NAS >= 1.8:
 *   - Config Signal Info (only when enabling)
 *   - Register Indications with Signal Info
 *
 * If NAS < 1.8:
 *   - Set Event Report with Signal Strength
 */

typedef struct {
    QmiClientNas *client;
    gboolean enable;
} EnableUnsolicitedEventsContext;

static void
enable_unsolicited_events_context_free (EnableUnsolicitedEventsContext *ctx)
{
    g_object_unref (ctx->client);
    g_free (ctx);
}

static gboolean
common_enable_disable_unsolicited_events_finish (MMBroadbandModemQmi *self,
                                                 GAsyncResult *res,
                                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ser_signal_strength_ready (QmiClientNas *client,
                           GAsyncResult *res,
                           GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableUnsolicitedEventsContext *ctx;
    QmiMessageNasSetEventReportOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_set_event_report_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_set_event_report_output_get_result (output, &error)) {
        mm_dbg ("Couldn't set event report: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_nas_set_event_report_output_unref (output);

    /* Just ignore errors for now */
    self->priv->unsolicited_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_events_signal_strength (GTask *task)
{
    EnableUnsolicitedEventsContext *ctx;

    /* The device doesn't really like to have many threshold values, so don't
     * grow this array without checking first */
    static const gint8 thresholds_data[] = { -80, -40, 0, 40, 80 };
    QmiMessageNasSetEventReportInput *input;
    GArray *thresholds;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_set_event_report_input_new ();

    /* Prepare thresholds, separated 20 each */
    thresholds = g_array_sized_new (FALSE, FALSE, sizeof (gint8), G_N_ELEMENTS (thresholds_data));

    /* Only set thresholds during enable */
    if (ctx->enable)
        g_array_append_vals (thresholds, thresholds_data, G_N_ELEMENTS (thresholds_data));

    qmi_message_nas_set_event_report_input_set_signal_strength_indicator (
        input,
        ctx->enable,
        thresholds,
        NULL);
    g_array_unref (thresholds);
    qmi_client_nas_set_event_report (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_signal_strength_ready,
        task);
    qmi_message_nas_set_event_report_input_unref (input);
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
ri_signal_info_ready (QmiClientNas *client,
                      GAsyncResult *res,
                      GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableUnsolicitedEventsContext *ctx;
    QmiMessageNasRegisterIndicationsOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_register_indications_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_register_indications_output_get_result (output, &error)) {
        mm_dbg ("Couldn't register indications: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_nas_register_indications_output_unref (output);

    /* Just ignore errors for now */
    ctx->self->priv->unsolicited_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_events_signal_info (GTask *task)
{
    EnableUnsolicitedEventsContext *ctx;
    QmiMessageNasRegisterIndicationsInput *input;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_signal_info (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_signal_info_ready,
        task);
    qmi_message_nas_register_indications_input_unref (input);
}

static void
config_signal_info_ready (QmiClientNas *client,
                          GAsyncResult *res,
                          GTask *task)
{
    QmiMessageNasConfigSignalInfoOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_config_signal_info_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_config_signal_info_output_get_result (output, &error)) {
        mm_dbg ("Couldn't config signal info: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_nas_config_signal_info_output_unref (output);

    /* Keep on */
    common_enable_disable_unsolicited_events_signal_info (task);
}

static void
common_enable_disable_unsolicited_events_signal_info_config (GTask *task)
{
    EnableUnsolicitedEventsContext *ctx;
    /* RSSI values go between -105 and -60 for 3GPP technologies,
     * and from -105 to -90 in 3GPP2 technologies (approx). */
    static const gint8 thresholds_data[] = { -100, -97, -95, -92, -90, -85, -80, -75, -70, -65 };
    QmiMessageNasConfigSignalInfoInput *input;
    GArray *thresholds;

    ctx = g_task_get_task_data (task);

    /* Signal info config only to be run when enabling */
    if (!ctx->enable) {
        common_enable_disable_unsolicited_events_signal_info (task);
        return;
    }

    input = qmi_message_nas_config_signal_info_input_new ();

    /* Prepare thresholds, separated 20 each */
    thresholds = g_array_sized_new (FALSE, FALSE, sizeof (gint8), G_N_ELEMENTS (thresholds_data));
    g_array_append_vals (thresholds, thresholds_data, G_N_ELEMENTS (thresholds_data));

    qmi_message_nas_config_signal_info_input_set_rssi_threshold (
        input,
        thresholds,
        NULL);
    g_array_unref (thresholds);
    qmi_client_nas_config_signal_info (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)config_signal_info_ready,
        task);
    qmi_message_nas_config_signal_info_input_unref (input);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
common_enable_disable_unsolicited_events (MMBroadbandModemQmi *self,
                                          gboolean enable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    EnableUnsolicitedEventsContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_events_enabled) {
        mm_dbg ("Unsolicited events already %s; skipping",
                enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx = g_new0 (EnableUnsolicitedEventsContext, 1);
    ctx->client = g_object_ref (client);

    g_task_set_task_data (task, NULL, (GDestroyNotify)enable_unsolicited_events_context_free);

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        common_enable_disable_unsolicited_events_signal_info_config (task);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    common_enable_disable_unsolicited_events_signal_strength (task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_QMI (self), res, error);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    common_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                              FALSE,
                                              callback,
                                              user_data);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    common_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                              TRUE,
                                              callback,
                                              user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (CDMA interface) */

static gboolean
modem_cdma_enable_disable_unsolicited_events_finish (MMIfaceModemCdma *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_QMI (self), res, error);
}

static void
modem_cdma_disable_unsolicited_events (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    common_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                              FALSE,
                                              callback,
                                              user_data);
}

static void
modem_cdma_enable_unsolicited_events (MMIfaceModemCdma *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    common_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                              TRUE,
                                              callback,
                                              user_data);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (3GPP and CDMA interface) */

static gboolean
common_setup_cleanup_unsolicited_events_finish (MMBroadbandModemQmi *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
event_report_indication_cb (QmiClientNas *client,
                            QmiIndicationNasEventReportOutput *output,
                            MMBroadbandModemQmi *self)
{
    gint8 signal_strength;
    QmiNasRadioInterface signal_strength_radio_interface;

    if (qmi_indication_nas_event_report_output_get_signal_strength (
            output,
            &signal_strength,
            &signal_strength_radio_interface,
            NULL)) {
        if (qmi_dbm_valid (signal_strength, signal_strength_radio_interface)) {
            guint8 quality;

            /* This signal strength comes as negative dBms */
            quality = STRENGTH_TO_QUALITY (signal_strength);

            mm_dbg ("Signal strength indication (%s): %d dBm --> %u%%",
                    qmi_nas_radio_interface_get_string (signal_strength_radio_interface),
                    signal_strength,
                    quality);

            mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
            mm_iface_modem_update_access_technologies (
                MM_IFACE_MODEM (self),
                mm_modem_access_technology_from_qmi_radio_interface (signal_strength_radio_interface),
                (MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK | MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK));
        } else {
            mm_dbg ("Ignoring invalid signal strength (%s): %d dBm",
                    qmi_nas_radio_interface_get_string (signal_strength_radio_interface),
                    signal_strength);
        }
    }
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
signal_info_indication_cb (QmiClientNas *client,
                           QmiIndicationNasSignalInfoOutput *output,
                           MMBroadbandModemQmi *self)
{
    gint8 cdma1x_rssi = 0;
    gint8 evdo_rssi = 0;
    gint8 gsm_rssi = 0;
    gint8 wcdma_rssi = 0;
    gint8 lte_rssi = 0;
    guint8 quality;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    qmi_indication_nas_signal_info_output_get_cdma_signal_strength (output, &cdma1x_rssi, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_hdr_signal_strength (output, &evdo_rssi, NULL, NULL, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_gsm_signal_strength (output, &gsm_rssi, NULL);
    qmi_indication_nas_signal_info_output_get_wcdma_signal_strength (output, &wcdma_rssi, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_lte_signal_strength (output, &lte_rssi, NULL, NULL, NULL, NULL);

    if (common_signal_info_get_quality (cdma1x_rssi,
                                        evdo_rssi,
                                        gsm_rssi,
                                        wcdma_rssi,
                                        lte_rssi,
                                        &quality,
                                        &act)) {
        mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
        mm_iface_modem_update_access_technologies (
            MM_IFACE_MODEM (self),
            act,
            (MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK | MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK));
    }
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
common_setup_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                         gboolean enable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_events_setup) {
        mm_dbg ("Unsolicited events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Store new state */
    self->priv->unsolicited_events_setup = enable;

    /* Connect/Disconnect "Event Report" indications */
    if (enable) {
        g_assert (self->priv->event_report_indication_id == 0);
        self->priv->event_report_indication_id =
            g_signal_connect (client,
                              "event-report",
                              G_CALLBACK (event_report_indication_cb),
                              self);
    } else {
        g_assert (self->priv->event_report_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->event_report_indication_id);
        self->priv->event_report_indication_id = 0;
    }

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Connect/Disconnect "Signal Info" indications.
     * Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        if (enable) {
            g_assert (self->priv->signal_info_indication_id == 0);
            self->priv->signal_info_indication_id =
                g_signal_connect (client,
                                  "signal-info",
                                  G_CALLBACK (signal_info_indication_cb),
                                  self);
        } else {
            g_assert (self->priv->signal_info_indication_id != 0);
            g_signal_handler_disconnect (client, self->priv->signal_info_indication_id);
            self->priv->signal_info_indication_id = 0;
        }
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_QMI (self), res, error);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    common_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                             FALSE,
                                             callback,
                                             user_data);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    common_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                             TRUE,
                                             callback,
                                             user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (CDMA interface) */

static gboolean
modem_cdma_setup_cleanup_unsolicited_events_finish (MMIfaceModemCdma *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_QMI (self), res, error);
}

static void
modem_cdma_cleanup_unsolicited_events (MMIfaceModemCdma *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    common_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                             FALSE,
                                             callback,
                                             user_data);
}

static void
modem_cdma_setup_unsolicited_events (MMIfaceModemCdma *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    common_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                             TRUE,
                                             callback,
                                             user_data);
}

/*****************************************************************************/
/* Check support (Messaging interface) */

static gboolean
messaging_check_support_finish (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_messaging_check_support_ready (MMIfaceModemMessaging *_self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    self->priv->messaging_fallback_at = iface_modem_messaging_parent->check_support_finish (_self, res, NULL);

    g_task_return_boolean (task, self->priv->messaging_fallback_at);
    g_object_unref (task);
}

static void
messaging_check_support (MMIfaceModemMessaging *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;
    MMPortQmi *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    /* If we have support for the WMS client, messaging is supported */
    if (!port || !mm_port_qmi_peek_client (port, QMI_SERVICE_WMS, MM_PORT_QMI_FLAG_DEFAULT)) {
        /* Try to fallback to AT support */
        iface_modem_messaging_parent->check_support (
            self,
            (GAsyncReadyCallback)parent_messaging_check_support_ready,
            task);
        return;
    }

    mm_dbg ("Messaging capabilities supported");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported storages (Messaging interface) */

static gboolean
messaging_load_supported_storages_finish (MMIfaceModemMessaging *_self,
                                          GAsyncResult *res,
                                          GArray **mem1,
                                          GArray **mem2,
                                          GArray **mem3,
                                          GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    MMSmsStorage supported;

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->load_supported_storages_finish (_self, res, mem1, mem2, mem3, error);
    }

    g_assert (g_task_propagate_boolean (G_TASK (res), NULL));

    *mem1 = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), 2);
    /* Add SM storage only if not CDMA-only */
    if (!mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self))) {
        supported = MM_SMS_STORAGE_SM;
        g_array_append_val (*mem1, supported);
    }
    supported = MM_SMS_STORAGE_ME;
    g_array_append_val (*mem1, supported);
    *mem2 = g_array_ref (*mem1);
    *mem3 = g_array_ref (*mem1);
    return TRUE;
}

static void
messaging_load_supported_storages (MMIfaceModemMessaging *_self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        iface_modem_messaging_parent->load_supported_storages (_self, callback, user_data);
        return;
    }

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup SMS format (Messaging interface) */

static gboolean
modem_messaging_setup_sms_format_finish (MMIfaceModemMessaging *_self,
                                         GAsyncResult *res,
                                         GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->setup_sms_format_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_messaging_setup_sms_format (MMIfaceModemMessaging *_self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->setup_sms_format (_self, callback, user_data);
    }

    /* noop */
    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Set default storage (Messaging interface) */

static gboolean
messaging_set_default_storage_finish (MMIfaceModemMessaging *_self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->set_default_storage_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);;
}

static void
wms_set_routes_ready (QmiClientWms *client,
                      GAsyncResult *res,
                      GTask *task)
{
    QmiMessageWmsSetRoutesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_wms_set_routes_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_wms_set_routes_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set routes: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);

    if (output)
        qmi_message_wms_set_routes_output_unref (output);
}

static void
messaging_set_default_storage (MMIfaceModemMessaging *_self,
                               MMSmsStorage storage,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    QmiClient *client = NULL;
    QmiMessageWmsSetRoutesInput *input;
    GArray *routes_array;
    QmiMessageWmsSetRoutesInputRouteListElement route;

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        iface_modem_messaging_parent->set_default_storage (_self, storage, callback, user_data);
        return;
    }

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    /* Build routes array and add it as input
     * Just worry about Class 0 and Class 1 messages for now */
    input = qmi_message_wms_set_routes_input_new ();
    routes_array = g_array_sized_new (FALSE, FALSE, sizeof (route), 2);
    route.message_type = QMI_WMS_MESSAGE_TYPE_POINT_TO_POINT;
    route.message_class = QMI_WMS_MESSAGE_CLASS_0;
    route.storage = mm_sms_storage_to_qmi_storage_type (storage);
    route.receipt_action = QMI_WMS_RECEIPT_ACTION_STORE_AND_NOTIFY;
    g_array_append_val (routes_array, route);
    route.message_class = QMI_WMS_MESSAGE_CLASS_1;
    g_array_append_val (routes_array, route);
    qmi_message_wms_set_routes_input_set_route_list (input, routes_array, NULL);

    mm_dbg ("setting default messaging routes...");
    qmi_client_wms_set_routes (QMI_CLIENT_WMS (client),
                               input,
                               5,
                               NULL,
                               (GAsyncReadyCallback)wms_set_routes_ready,
                               g_task_new (self, NULL, callback, user_data));

    qmi_message_wms_set_routes_input_unref (input);
    g_array_unref (routes_array);
}

/*****************************************************************************/
/* Load initial SMS parts */

typedef enum {
    LOAD_INITIAL_SMS_PARTS_STEP_FIRST,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_FIRST,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_ALL,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_READ,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_NOT_READ,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_SENT,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_NOT_SENT,
    LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_FIRST,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_ALL,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_READ,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_NOT_READ,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_SENT,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_NOT_SENT,
    LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST,
    LOAD_INITIAL_SMS_PARTS_STEP_LAST
} LoadInitialSmsPartsStep;

typedef struct {
    QmiClientWms *client;
    MMSmsStorage storage;
    LoadInitialSmsPartsStep step;

    /* For each step */
    GArray *message_array;
    guint i;
} LoadInitialSmsPartsContext;

static void
load_initial_sms_parts_context_free (LoadInitialSmsPartsContext *ctx)
{
    if (ctx->message_array)
        g_array_unref (ctx->message_array);

    g_object_unref (ctx->client);
    g_slice_free (LoadInitialSmsPartsContext, ctx);
}

static gboolean
load_initial_sms_parts_finish (MMIfaceModemMessaging *_self,
                               GAsyncResult *res,
                               GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->load_initial_sms_parts_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);;
}

static void read_next_sms_part (GTask *task);

static void
add_new_read_sms_part (MMIfaceModemMessaging *self,
                       QmiWmsStorageType storage,
                       guint32 index,
                       QmiWmsMessageTagType tag,
                       QmiWmsMessageFormat format,
                       GArray *data)
{
    MMSmsPart *part = NULL;
    GError *error = NULL;

    switch (format) {
    case QMI_WMS_MESSAGE_FORMAT_CDMA:
        part = mm_sms_part_cdma_new_from_binary_pdu (index,
                                                     (guint8 *)data->data,
                                                     data->len,
                                                     &error);

        break;
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT:
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_BROADCAST:
        part = mm_sms_part_3gpp_new_from_binary_pdu (index,
                                                     (guint8 *)data->data,
                                                     data->len,
                                                     &error);
        break;
    case QMI_WMS_MESSAGE_FORMAT_MWI:
        mm_dbg ("Don't know how to process 'message waiting indicator' messages");
        break;
    default:
        mm_dbg ("Unhandled message format '%u'", format);
        break;
    }

    if (part) {
        mm_dbg ("Correctly parsed PDU (%d)", index);
        mm_iface_modem_messaging_take_part (self,
                                            part,
                                            mm_sms_state_from_qmi_message_tag (tag),
                                            mm_sms_storage_from_qmi_storage_type (storage));
    } else if (error) {
        /* Don't treat the error as critical */
        mm_dbg ("Error parsing PDU (%d): %s", index, error->message);
        g_error_free (error);
    }
}

static void
wms_raw_read_ready (QmiClientWms *client,
                    GAsyncResult *res,
                    GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadInitialSmsPartsContext *ctx;
    QmiMessageWmsRawReadOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* Ignore errors, just keep on with the next messages */

    output = qmi_client_wms_raw_read_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_raw_read_output_get_result (output, &error)) {
        mm_dbg ("Couldn't read raw message: %s", error->message);
        g_error_free (error);
    } else {
        QmiWmsMessageTagType tag;
        QmiWmsMessageFormat format;
        GArray *data;
        QmiMessageWmsListMessagesOutputMessageListElement *message;

        message = &g_array_index (ctx->message_array,
                                  QmiMessageWmsListMessagesOutputMessageListElement,
                                  ctx->i);

        qmi_message_wms_raw_read_output_get_raw_message_data (
            output,
            &tag,
            &format,
            &data,
            NULL);
        add_new_read_sms_part (MM_IFACE_MODEM_MESSAGING (self),
                               mm_sms_storage_to_qmi_storage_type (ctx->storage),
                               message->memory_index,
                               tag,
                               format,
                               data);
    }

    if (output)
        qmi_message_wms_raw_read_output_unref (output);

    /* Keep on reading parts */
    ctx->i++;
    read_next_sms_part (task);
}

static void load_initial_sms_parts_step (GTask *task);

static void
read_next_sms_part (GTask *task)
{
    LoadInitialSmsPartsContext *ctx;
    QmiMessageWmsListMessagesOutputMessageListElement *message;
    QmiMessageWmsRawReadInput *input;

    ctx = g_task_get_task_data (task);

    if (ctx->i >= ctx->message_array->len ||
        !ctx->message_array) {
        /* If we just listed all SMS, we're done. Otherwise go to next tag. */
        if (ctx->step == LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_ALL)
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST;
        else if (ctx->step == LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_ALL)
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST;
        else
            ctx->step++;
        load_initial_sms_parts_step (task);
        return;
    }

    message = &g_array_index (ctx->message_array,
                              QmiMessageWmsListMessagesOutputMessageListElement,
                              ctx->i);

    input = qmi_message_wms_raw_read_input_new ();
    qmi_message_wms_raw_read_input_set_message_memory_storage_id (
        input,
        mm_sms_storage_to_qmi_storage_type (ctx->storage),
        message->memory_index,
        NULL);

    /* set message mode */
    if (ctx->step < LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST)
        qmi_message_wms_raw_read_input_set_message_mode (
            input,
            QMI_WMS_MESSAGE_MODE_GSM_WCDMA,
            NULL);
    else if (ctx->step < LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST)
        qmi_message_wms_raw_read_input_set_message_mode (
            input,
            QMI_WMS_MESSAGE_MODE_CDMA,
            NULL);
    else
        g_assert_not_reached ();

    qmi_client_wms_raw_read (QMI_CLIENT_WMS (ctx->client),
                             input,
                             3,
                             NULL,
                             (GAsyncReadyCallback)wms_raw_read_ready,
                             task);
    qmi_message_wms_raw_read_input_unref (input);
}

static void
wms_list_messages_ready (QmiClientWms *client,
                         GAsyncResult *res,
                         GTask *task)
{
    LoadInitialSmsPartsContext *ctx;
    QmiMessageWmsListMessagesOutput *output = NULL;
    GError *error = NULL;
    GArray *message_array;

    ctx = g_task_get_task_data (task);

    output = qmi_client_wms_list_messages_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_wms_list_messages_output_get_result (output, &error)) {
        /* Ignore error, keep on */
        mm_dbg ("Couldn't read SMS messages: %s", error->message);
        g_error_free (error);
        ctx->step++;
        load_initial_sms_parts_step (task);
        qmi_message_wms_list_messages_output_unref (output);
        return;
    }

    qmi_message_wms_list_messages_output_get_message_list (
        output,
        &message_array,
        NULL);

    /* Keep a reference to the array ourselves */
    if (ctx->message_array)
        g_array_unref (ctx->message_array);
    ctx->message_array = g_array_ref (message_array);

    qmi_message_wms_list_messages_output_unref (output);

    /* Start reading parts */
    ctx->i = 0;
    read_next_sms_part (task);
}

static void
load_initial_sms_parts_step (GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadInitialSmsPartsContext *ctx;
    QmiMessageWmsListMessagesInput *input;
    gint mode = -1;
    gint tag_type = -1;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case LOAD_INITIAL_SMS_PARTS_STEP_FIRST:
        ctx->step++;
        /* Fall down */
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_FIRST:
        /* If modem doesn't have 3GPP caps, skip 3GPP SMS */
        if (!mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST;
            load_initial_sms_parts_step (task);
            return;
        }
        ctx->step++;
        /* Fall down */
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_ALL:
        mm_dbg ("loading all 3GPP messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_READ:
        mm_dbg ("loading 3GPP MT-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_READ;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_NOT_READ:
        mm_dbg ("loading 3GPP MT-not-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_SENT:
        mm_dbg ("loading 3GPP MO-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_SENT;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_NOT_SENT:
        mm_dbg ("loading 3GPP MO-not-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_NOT_SENT;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST:
        ctx->step++;
        /* Fall down */
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_FIRST:
        /* If modem doesn't have CDMA caps, skip CDMA SMS */
        if (!mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST;
            load_initial_sms_parts_step (task);
            return;
        }
        ctx->step++;
        /* Fall down */
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_ALL:
        mm_dbg ("loading all CDMA messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_READ:
        mm_dbg ("loading CDMA MT-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_READ;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_NOT_READ:
        mm_dbg ("loading CDMA MT-not-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_SENT:
        mm_dbg ("loading CDMA MO-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_SENT;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_NOT_SENT:
        mm_dbg ("loading CDMA MO-not-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_NOT_SENT;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;
    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST:
        ctx->step++;
        /* Fall down */
    case LOAD_INITIAL_SMS_PARTS_STEP_LAST:
        /* All steps done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert (mode != -1);
    input = qmi_message_wms_list_messages_input_new ();
    qmi_message_wms_list_messages_input_set_storage_type (
        input,
        mm_sms_storage_to_qmi_storage_type (ctx->storage),
        NULL);
    qmi_message_wms_list_messages_input_set_message_mode (
        input,
        (QmiWmsMessageMode)mode,
        NULL);
    if (tag_type != -1)
        qmi_message_wms_list_messages_input_set_message_tag (
            input,
            (QmiWmsMessageTagType)tag_type,
            NULL);

    qmi_client_wms_list_messages (QMI_CLIENT_WMS (ctx->client),
                                  input,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback)wms_list_messages_ready,
                                  task);
    qmi_message_wms_list_messages_input_unref (input);
}

static void
load_initial_sms_parts (MMIfaceModemMessaging *_self,
                        MMSmsStorage storage,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    LoadInitialSmsPartsContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->load_initial_sms_parts (_self, storage, callback, user_data);
    }

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (LoadInitialSmsPartsContext);
    ctx->client = g_object_ref (client);
    ctx->storage = storage;
    ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_initial_sms_parts_context_free);

    load_initial_sms_parts_step (task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (Messaging interface) */

typedef struct {
    MMIfaceModemMessaging *self;
    QmiClientWms *client;
    QmiWmsStorageType storage;
    guint32 memory_index;
    QmiWmsMessageMode message_mode;
} IndicationRawReadContext;

static void
indication_raw_read_context_free (IndicationRawReadContext *ctx)
{
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (IndicationRawReadContext, ctx);
}

static void
wms_indication_raw_read_ready (QmiClientWms *client,
                               GAsyncResult *res,
                               IndicationRawReadContext *ctx)
{
    QmiMessageWmsRawReadOutput *output = NULL;
    GError *error = NULL;

    /* Ignore errors */

    output = qmi_client_wms_raw_read_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_raw_read_output_get_result (output, &error)) {
        mm_dbg ("Couldn't read raw message: %s", error->message);
        g_error_free (error);
    } else {
        QmiWmsMessageTagType tag;
        QmiWmsMessageFormat format;
        GArray *data;

        qmi_message_wms_raw_read_output_get_raw_message_data (
            output,
            &tag,
            &format,
            &data,
            NULL);
        add_new_read_sms_part (MM_IFACE_MODEM_MESSAGING (ctx->self),
                               ctx->storage,
                               ctx->memory_index,
                               tag,
                               format,
                               data);
    }

    if (output)
        qmi_message_wms_raw_read_output_unref (output);

    indication_raw_read_context_free (ctx);
}

static void
messaging_event_report_indication_cb (QmiClientNas *client,
                                      QmiIndicationWmsEventReportOutput *output,
                                      MMBroadbandModemQmi *self)
{
    QmiWmsStorageType storage;
    guint32 memory_index;

    /* Currently ignoring transfer-route MT messages */

    if (qmi_indication_wms_event_report_output_get_mt_message (
            output,
            &storage,
            &memory_index,
            NULL)) {
        IndicationRawReadContext *ctx;
        QmiMessageWmsRawReadInput *input;

        ctx = g_slice_new (IndicationRawReadContext);
        ctx->self = g_object_ref (self);
        ctx->client = g_object_ref (client);
        ctx->storage = storage;
        ctx->memory_index = memory_index;

        input = qmi_message_wms_raw_read_input_new ();
        qmi_message_wms_raw_read_input_set_message_memory_storage_id (
            input,
            storage,
            memory_index,
            NULL);

        /* Default to 3GPP message mode if none given */
        if (!qmi_indication_wms_event_report_output_get_message_mode (
                output,
                &ctx->message_mode,
                NULL))
            ctx->message_mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        qmi_message_wms_raw_read_input_set_message_mode (
            input,
            ctx->message_mode,
            NULL);

        qmi_client_wms_raw_read (QMI_CLIENT_WMS (client),
                                 input,
                                 3,
                                 NULL,
                                 (GAsyncReadyCallback)wms_indication_raw_read_ready,
                                 ctx);
        qmi_message_wms_raw_read_input_unref (input);
    }
}

static gboolean
messaging_cleanup_unsolicited_events_finish (MMIfaceModemMessaging *_self,
                                             GAsyncResult *res,
                                             GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->cleanup_unsolicited_events_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
messaging_setup_unsolicited_events_finish (MMIfaceModemMessaging *_self,
                                             GAsyncResult *res,
                                             GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->setup_unsolicited_events_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_cleanup_messaging_unsolicited_events (MMBroadbandModemQmi *self,
                                                   gboolean enable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->messaging_unsolicited_events_setup) {
        mm_dbg ("Messaging unsolicited events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Store new state */
    self->priv->messaging_unsolicited_events_setup = enable;

    /* Connect/Disconnect "Event Report" indications */
    if (enable) {
        g_assert (self->priv->messaging_event_report_indication_id == 0);
        self->priv->messaging_event_report_indication_id =
            g_signal_connect (client,
                              "event-report",
                              G_CALLBACK (messaging_event_report_indication_cb),
                              self);
    } else {
        g_assert (self->priv->messaging_event_report_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->messaging_event_report_indication_id);
        self->priv->messaging_event_report_indication_id = 0;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
messaging_cleanup_unsolicited_events (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->cleanup_unsolicited_events (_self, callback, user_data);
    }

    common_setup_cleanup_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                       FALSE,
                                                       callback,
                                                       user_data);
}

static void
messaging_setup_unsolicited_events (MMIfaceModemMessaging *_self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->setup_unsolicited_events (_self, callback, user_data);
    }

    common_setup_cleanup_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                       TRUE,
                                                       callback,
                                                       user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (Messaging interface) */

typedef struct {
    gboolean enable;
} EnableMessagingUnsolicitedEventsContext;

static gboolean
messaging_disable_unsolicited_events_finish (MMIfaceModemMessaging *_self,
                                             GAsyncResult *res,
                                             GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at && iface_modem_messaging_parent->disable_unsolicited_events_finish) {
        return iface_modem_messaging_parent->disable_unsolicited_events_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *_self,
                                            GAsyncResult *res,
                                            GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->enable_unsolicited_events_finish (_self, res, error);
    }

    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ser_messaging_indicator_ready (QmiClientWms *client,
                               GAsyncResult *res,
                               GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableMessagingUnsolicitedEventsContext *ctx;
    QmiMessageWmsSetEventReportOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_wms_set_event_report_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_set_event_report_output_get_result (output, &error)) {
        mm_dbg ("Couldn't set event report: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_wms_set_event_report_output_unref (output);

    /* Just ignore errors for now */
    self->priv->messaging_unsolicited_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_messaging_unsolicited_events (MMBroadbandModemQmi *self,
                                                    gboolean enable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    EnableMessagingUnsolicitedEventsContext *ctx;
    GTask *task;
    QmiClient *client = NULL;
    QmiMessageWmsSetEventReportInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->messaging_unsolicited_events_enabled) {
        mm_dbg ("Messaging unsolicited events already %s; skipping",
                enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx = g_new (EnableMessagingUnsolicitedEventsContext, 1);
    ctx->enable = enable;

    g_task_set_task_data (task, ctx, g_free);

    input = qmi_message_wms_set_event_report_input_new ();

    qmi_message_wms_set_event_report_input_set_new_mt_message_indicator (
        input,
        ctx->enable,
        NULL);
    qmi_client_wms_set_event_report (
        QMI_CLIENT_WMS (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_messaging_indicator_ready,
        task);
    qmi_message_wms_set_event_report_input_unref (input);
}

static void
messaging_disable_unsolicited_events (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        /* Generic implementation doesn't actually have a method to disable
         * unsolicited messaging events */
        if (!iface_modem_messaging_parent->disable_unsolicited_events) {
            GTask *task;

            task = g_task_new (self, NULL, callback, user_data);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        return iface_modem_messaging_parent->disable_unsolicited_events (_self, callback, user_data);
    }

    common_enable_disable_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                        FALSE,
                                                        callback,
                                                        user_data);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->enable_unsolicited_events (_self, callback, user_data);
    }

    common_enable_disable_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                        TRUE,
                                                        callback,
                                                        user_data);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMBaseSms *
messaging_create_sms (MMIfaceModemMessaging *_self)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle fallback */
    if (self->priv->messaging_fallback_at) {
        return iface_modem_messaging_parent->create_sms (_self);
    }

    return mm_sms_qmi_new (MM_BASE_MODEM (self));
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
    MMPortQmi *port;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));

    /* Now our own checks */

    /* If we have support for the PDS client, GPS and A-GPS location is supported */
    if (port && mm_port_qmi_peek_client (port,
                                         QMI_SERVICE_PDS,
                                         MM_PORT_QMI_FLAG_DEFAULT))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS);

    /* If the modem is CDMA, we have support for CDMA BS location */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        sources |= MM_MODEM_LOCATION_SOURCE_CDMA_BS;

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (
        self,
        (GAsyncReadyCallback)parent_load_capabilities_ready,
        task);
}

/*****************************************************************************/
/* Load SUPL server */

static gchar *
location_load_supl_server_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
get_agps_config_ready (QmiClientPds *client,
                       GAsyncResult *res,
                       GTask *task)
{
    QmiMessagePdsGetAgpsConfigOutput *output = NULL;
    GError *error = NULL;
    guint32 ip;
    guint32 port;
    GArray *url;
    gchar *str;

    output = qmi_client_pds_get_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_get_agps_config_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    str = NULL;

    /* Prefer IP/PORT to URL */
    if (qmi_message_pds_get_agps_config_output_get_location_server_address (
            output,
            &ip,
            &port,
            NULL) &&
        ip != 0 &&
        port != 0) {
        struct in_addr a = { .s_addr = ip };
        gchar buf[INET_ADDRSTRLEN + 1];

        memset (buf, 0, sizeof (buf));

        if (!inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1)) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Cannot convert numeric IP address to string");
            g_object_unref (task);
            return;
        }

        str = g_strdup_printf ("%s:%u", buf, port);
    }

    if (!str &&
        qmi_message_pds_get_agps_config_output_get_location_server_url (
            output,
            &url,
            NULL) &&
        url->len > 0) {
        str = g_convert (url->data, url->len, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    }

    if (!str)
        str = g_strdup ("");

    qmi_message_pds_get_agps_config_output_unref (output);

    g_task_return_pointer (task, str, g_free);
    g_object_unref (task);
}

static void
location_load_supl_server (MMIfaceModemLocation *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    QmiClient *client = NULL;
    QmiMessagePdsGetAgpsConfigInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_PDS, &client,
                            callback, user_data)) {
        return;
    }

    input = qmi_message_pds_get_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    qmi_client_pds_get_agps_config (
        QMI_CLIENT_PDS (client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)get_agps_config_ready,
        g_task_new (self, NULL, callback, user_data));
    qmi_message_pds_get_agps_config_input_unref (input);
}

/*****************************************************************************/
/* Set SUPL server */

static gboolean
location_set_supl_server_finish (MMIfaceModemLocation *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_agps_config_ready (QmiClientPds *client,
                       GAsyncResult *res,
                       GTask *task)
{
    QmiMessagePdsSetAgpsConfigOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_agps_config_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_pds_set_agps_config_output_unref (output);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
parse_as_ip_port (const gchar *supl,
                  guint32 *out_ip,
                  guint32 *out_port)
{
    gboolean valid = FALSE;
    gchar **split;
    guint port;
    guint32 ip;

    split = g_strsplit (supl, ":", -1);
    if (g_strv_length (split) != 2)
        goto out;

    if (!mm_get_uint_from_str (split[1], &port))
        goto out;
    if (port == 0 || port > G_MAXUINT16)
        goto out;
    if (inet_pton (AF_INET, split[0], &ip) <= 0)
        goto out;

    *out_ip = ip;
    *out_port = port;
    valid = TRUE;

out:
    g_strfreev (split);
    return valid;
}

static gboolean
parse_as_url (const gchar *supl,
              GArray **out_url)
{
    gchar *utf16;
    gsize utf16_len;

    utf16 = g_convert (supl, -1, "UTF-16BE", "UTF-8", NULL, &utf16_len, NULL);
    *out_url = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), utf16_len),
                                    utf16,
                                    utf16_len);
    g_free (utf16);
    return TRUE;
}

static void
location_set_supl_server (MMIfaceModemLocation *self,
                          const gchar *supl,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    QmiClient *client = NULL;
    QmiMessagePdsSetAgpsConfigInput *input;
    guint32 ip;
    guint32 port;
    GArray *url;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_PDS, &client,
                            callback, user_data)) {
        return;
    }

    input = qmi_message_pds_set_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    if (parse_as_ip_port (supl, &ip, &port))
        qmi_message_pds_set_agps_config_input_set_location_server_address (input, ip, port, NULL);
    else if (parse_as_url (supl, &url)) {
        qmi_message_pds_set_agps_config_input_set_location_server_url (input, url, NULL);
        g_array_unref (url);
    } else
        g_assert_not_reached ();

    qmi_client_pds_set_agps_config (
        QMI_CLIENT_PDS (client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)set_agps_config_ready,
        g_task_new (self, NULL, callback, user_data));
    qmi_message_pds_set_agps_config_input_unref (input);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

typedef struct {
    QmiClientPds *client;
    MMModemLocationSource source;
    /* Default tracking session (for A-GPS disabling) */
    QmiPdsOperatingMode session_operation;
    guint8 data_timeout;
    guint32 interval;
    guint32 accuracy_threshold;
} DisableLocationGatheringContext;

static void
disable_location_gathering_context_free (DisableLocationGatheringContext *ctx)
{
    if (ctx->client)
        g_object_unref (ctx->client);
    g_slice_free (DisableLocationGatheringContext, ctx);
}

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gps_service_state_stop_ready (QmiClientPds *client,
                              GAsyncResult *res,
                              GTask *task)
{
    MMBroadbandModemQmi *self;
    DisableLocationGatheringContext *ctx;
    QmiMessagePdsSetGpsServiceStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }

        g_error_free (error);
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    qmi_message_pds_set_gps_service_state_output_unref (output);

    g_assert (self->priv->location_event_report_indication_id != 0);
    g_signal_handler_disconnect (client, self->priv->location_event_report_indication_id);
    self->priv->location_event_report_indication_id = 0;

    mm_dbg ("GPS stopped");
    self->priv->enabled_sources &= ~ctx->source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_default_tracking_session_stop_ready (QmiClientPds *client,
                                         GAsyncResult *res,
                                         GTask *task)
{
    MMBroadbandModemQmi *self;
    DisableLocationGatheringContext *ctx;
    QmiMessagePdsSetDefaultTrackingSessionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_default_tracking_session_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    qmi_message_pds_set_default_tracking_session_output_unref (output);

    /* Done */
    mm_dbg ("A-GPS disabled");
    self->priv->enabled_sources &= ~ctx->source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
get_default_tracking_session_stop_ready (QmiClientPds *client,
                                         GAsyncResult *res,
                                         GTask *task)
{
    MMBroadbandModemQmi *self;
    DisableLocationGatheringContext *ctx;
    QmiMessagePdsSetDefaultTrackingSessionInput *input;
    QmiMessagePdsGetDefaultTrackingSessionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_get_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_get_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_get_default_tracking_session_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    qmi_message_pds_get_default_tracking_session_output_get_info (
        output,
        &ctx->session_operation,
        &ctx->data_timeout,
        &ctx->interval,
        &ctx->accuracy_threshold,
        NULL);

    qmi_message_pds_get_default_tracking_session_output_unref (output);

    if (ctx->session_operation == QMI_PDS_OPERATING_MODE_STANDALONE) {
        /* Done */
        mm_dbg ("A-GPS already disabled");
        self->priv->enabled_sources &= ~ctx->source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    input = qmi_message_pds_set_default_tracking_session_input_new ();
    qmi_message_pds_set_default_tracking_session_input_set_info (
        input,
        QMI_PDS_OPERATING_MODE_STANDALONE,
        ctx->data_timeout,
        ctx->interval,
        ctx->accuracy_threshold,
        NULL);
    qmi_client_pds_set_default_tracking_session (
        ctx->client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)set_default_tracking_session_stop_ready,
        task);
    qmi_message_pds_set_default_tracking_session_input_unref (input);
}

static void
disable_location_gathering (MMIfaceModemLocation *_self,
                            MMModemLocationSource source,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    DisableLocationGatheringContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* Nothing to be done to disable 3GPP or CDMA locations */
    if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI ||
        source == MM_MODEM_LOCATION_SOURCE_CDMA_BS) {
        /* Just mark it as disabled */
        self->priv->enabled_sources &= ~source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Setup context and client */
    if (!ensure_qmi_client (self,
                            QMI_SERVICE_PDS, &client,
                            callback, user_data)) {
        g_object_unref (task);
        return;
    }
    ctx = g_slice_new0 (DisableLocationGatheringContext);
    ctx->client = g_object_ref (client);
    ctx->source = source;

    g_task_set_task_data (task, ctx, (GDestroyNotify)disable_location_gathering_context_free);

    /* Disable A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        qmi_client_pds_get_default_tracking_session (
            ctx->client,
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)get_default_tracking_session_stop_ready,
            task);
        return;
    }

    /* Only stop GPS engine if no GPS-related sources enabled */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMModemLocationSource tmp;

        /* If no more GPS sources enabled, stop GPS */
        tmp = self->priv->enabled_sources;
        tmp &= ~source;
        if (!(tmp & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
            QmiMessagePdsSetGpsServiceStateInput *input;

            input = qmi_message_pds_set_gps_service_state_input_new ();
            qmi_message_pds_set_gps_service_state_input_set_state (input, FALSE, NULL);
            qmi_client_pds_set_gps_service_state (
                ctx->client,
                input,
                10,
                NULL, /* cancellable */
                (GAsyncReadyCallback)gps_service_state_stop_ready,
                task);
            qmi_message_pds_set_gps_service_state_input_unref (input);
            return;
        }

        /* Otherwise, we have more GPS sources enabled, we shouldn't stop GPS, just
         * return */
        self->priv->enabled_sources &= ~source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* The QMI implementation has a fixed set of capabilities supported. Arriving
     * here means we tried to disable one which wasn't set as supported, which should
     * not happen */
    g_assert_not_reached ();
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

static void
location_event_report_indication_cb (QmiClientPds *client,
                                     QmiIndicationPdsEventReportOutput *output,
                                     MMBroadbandModemQmi *self)
{
    QmiPdsPositionSessionStatus session_status;
    const gchar *nmea;

    if (qmi_indication_pds_event_report_output_get_position_session_status (
            output,
            &session_status,
            NULL)) {
        mm_dbg ("[GPS] session status changed: '%s'",
                qmi_pds_position_session_status_get_string (session_status));
    }

    if (qmi_indication_pds_event_report_output_get_nmea_position (
            output,
            &nmea,
            NULL)) {
        mm_dbg ("[NMEA] %s", nmea);
        mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
    }
}

typedef struct {
    QmiClientPds *client;
    MMModemLocationSource source;
    /* Default tracking session (for A-GPS enabling) */
    QmiPdsOperatingMode session_operation;
    guint8 data_timeout;
    guint32 interval;
    guint32 accuracy_threshold;
} EnableLocationGatheringContext;

static void
enable_location_gathering_context_free (EnableLocationGatheringContext *ctx)
{
    if (ctx->client)
        g_object_unref (ctx->client);
    g_slice_free (EnableLocationGatheringContext, ctx);
}

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ser_location_ready (QmiClientPds *client,
                    GAsyncResult *res,
                    GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableLocationGatheringContext *ctx;
    QmiMessagePdsSetEventReportOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_event_report_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_event_report_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set event report: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_event_report_output_unref (output);
        return;
    }

    qmi_message_pds_set_event_report_output_unref (output);

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_dbg ("Adding location event report indication handling");
    g_assert (self->priv->location_event_report_indication_id == 0);
    self->priv->location_event_report_indication_id =
        g_signal_connect (client,
                          "event-report",
                          G_CALLBACK (location_event_report_indication_cb),
                          self);

    /* Done */
    mm_dbg ("GPS started");
    self->priv->enabled_sources |= ctx->source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
auto_tracking_state_start_ready (QmiClientPds *client,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnableLocationGatheringContext *ctx;
    QmiMessagePdsSetEventReportInput *input;
    QmiMessagePdsSetAutoTrackingStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_auto_tracking_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_auto_tracking_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set auto-tracking state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_auto_tracking_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_auto_tracking_state_output_unref (output);

    ctx = g_task_get_task_data (task);

    /* Only gather standard NMEA traces */
    input = qmi_message_pds_set_event_report_input_new ();
    qmi_message_pds_set_event_report_input_set_nmea_position_reporting (input, TRUE, NULL);
    qmi_client_pds_set_event_report (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_location_ready,
        task);
    qmi_message_pds_set_event_report_input_unref (input);
}

static void
gps_service_state_start_ready (QmiClientPds *client,
                               GAsyncResult *res,
                               GTask *task)
{
    EnableLocationGatheringContext *ctx;
    QmiMessagePdsSetAutoTrackingStateInput *input;
    QmiMessagePdsSetGpsServiceStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    ctx = g_task_get_task_data (task);

    /* Enable auto-tracking for a continuous fix */
    input = qmi_message_pds_set_auto_tracking_state_input_new ();
    qmi_message_pds_set_auto_tracking_state_input_set_state (input, TRUE, NULL);
    qmi_client_pds_set_auto_tracking_state (
        ctx->client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)auto_tracking_state_start_ready,
        task);
    qmi_message_pds_set_auto_tracking_state_input_unref (input);
}

static void
set_default_tracking_session_start_ready (QmiClientPds *client,
                                          GAsyncResult *res,
                                          GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableLocationGatheringContext *ctx;
    QmiMessagePdsSetDefaultTrackingSessionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_default_tracking_session_output_unref (output);
        return;
    }

    qmi_message_pds_set_default_tracking_session_output_unref (output);

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* Done */
    mm_dbg ("A-GPS enabled");
    self->priv->enabled_sources |= ctx->source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
get_default_tracking_session_start_ready (QmiClientPds *client,
                                          GAsyncResult *res,
                                          GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableLocationGatheringContext *ctx;
    QmiMessagePdsSetDefaultTrackingSessionInput *input;
    QmiMessagePdsGetDefaultTrackingSessionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_get_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_get_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_get_default_tracking_session_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    qmi_message_pds_get_default_tracking_session_output_get_info (
        output,
        &ctx->session_operation,
        &ctx->data_timeout,
        &ctx->interval,
        &ctx->accuracy_threshold,
        NULL);

    qmi_message_pds_get_default_tracking_session_output_unref (output);

    if (ctx->session_operation == QMI_PDS_OPERATING_MODE_MS_ASSISTED) {
        /* Done */
        mm_dbg ("A-GPS already enabled");
        self->priv->enabled_sources |= ctx->source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    input = qmi_message_pds_set_default_tracking_session_input_new ();
    qmi_message_pds_set_default_tracking_session_input_set_info (
        input,
        QMI_PDS_OPERATING_MODE_MS_ASSISTED,
        ctx->data_timeout,
        ctx->interval,
        ctx->accuracy_threshold,
        NULL);
    qmi_client_pds_set_default_tracking_session (
        ctx->client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)set_default_tracking_session_start_ready,
        task);
    qmi_message_pds_set_default_tracking_session_input_unref (input);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    EnableLocationGatheringContext *ctx;
    GError *error = NULL;
    QmiClient *client;

    if (!iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Nothing else needed in the QMI side for LAC/CI */
    if (ctx->source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI) {
        self->priv->enabled_sources |= ctx->source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* CDMA modems need to re-run registration checks when enabling the CDMA BS
     * location source, so that we get up to date BS location information.
     * Note that we don't care for when the registration checks get finished.
     */
    if (ctx->source == MM_MODEM_LOCATION_SOURCE_CDMA_BS &&
        mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
        /* Reload registration to get LAC/CI */
        mm_iface_modem_cdma_run_registration_checks (MM_IFACE_MODEM_CDMA (self), NULL, NULL);
        /* Just mark it as enabled */
        self->priv->enabled_sources |= ctx->source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Setup context and client */
    client = peek_qmi_client (self, QMI_SERVICE_PDS, &error);
    if (!client) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    ctx->client = g_object_ref (client);

    /* Enabling A-GPS? */
    if (ctx->source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        qmi_client_pds_get_default_tracking_session (
            ctx->client,
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)get_default_tracking_session_start_ready,
            task);
        return;
    }

    /* NMEA and RAW are both enabled in the same way */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        /* Only start GPS engine if not done already */
        if (!(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                             MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
            QmiMessagePdsSetGpsServiceStateInput *input;

            input = qmi_message_pds_set_gps_service_state_input_new ();
            qmi_message_pds_set_gps_service_state_input_set_state (input, TRUE, NULL);
            qmi_client_pds_set_gps_service_state (
                ctx->client,
                input,
                10,
                NULL, /* cancellable */
                (GAsyncReadyCallback)gps_service_state_start_ready,
                task);
            qmi_message_pds_set_gps_service_state_input_unref (input);
            return;
        }

        /* GPS already started, we're done */
        self->priv->enabled_sources |= ctx->source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* The QMI implementation has a fixed set of capabilities supported. Arriving
     * here means we tried to enable one which wasn't set as supported, which should
     * not happen */
    g_assert_not_reached ();
}

static void
enable_location_gathering (MMIfaceModemLocation *self,
                           MMModemLocationSource source,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    EnableLocationGatheringContext *ctx;
    GTask *task;

    ctx = g_slice_new0 (EnableLocationGatheringContext);
    /* Store source to enable, there will be only one! */
    ctx->source = source;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enable_location_gathering_context_free);

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        ctx->source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Check support (OMA interface) */

static gboolean
oma_check_support_finish (MMIfaceModemOma *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
oma_check_support (MMIfaceModemOma *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GTask *task;
    MMPortQmi *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    /* If we have support for the OMA client, OMA is supported */
    if (!port || !mm_port_qmi_peek_client (port, QMI_SERVICE_OMA, MM_PORT_QMI_FLAG_DEFAULT)) {
        mm_dbg ("OMA capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        mm_dbg ("OMA capabilities supported");
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

/*****************************************************************************/
/* Load features (OMA interface) */

static MMOmaFeature
oma_load_features_finish (MMIfaceModemOma *self,
                          GAsyncResult *res,
                          GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_OMA_FEATURE_NONE;
    }
    return (MMOmaFeature)value;
}

static void
oma_get_feature_setting_ready (QmiClientOma *client,
                               GAsyncResult *res,
                               GTask *task)
{
    QmiMessageOmaGetFeatureSettingOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_oma_get_feature_setting_finish (client, res, &error);
    if (!output || !qmi_message_oma_get_feature_setting_output_get_result (output, &error))
        g_task_return_error (task, error);
    else {
        MMOmaFeature features = MM_OMA_FEATURE_NONE;
        gboolean enabled;

        if (qmi_message_oma_get_feature_setting_output_get_device_provisioning_service_update_config (
                output,
                &enabled,
                NULL) &&
            enabled)
            features |= MM_OMA_FEATURE_DEVICE_PROVISIONING;

        if (qmi_message_oma_get_feature_setting_output_get_prl_update_service_config (
                output,
                &enabled,
                NULL) &&
            enabled)
            features |= MM_OMA_FEATURE_PRL_UPDATE;

        if (qmi_message_oma_get_feature_setting_output_get_hfa_feature_config (
                output,
                &enabled,
                NULL) &&
            enabled)
            features |= MM_OMA_FEATURE_HANDS_FREE_ACTIVATION;

        g_task_return_int (task, features);
    }

    if (output)
        qmi_message_oma_get_feature_setting_output_unref (output);

    g_object_unref (task);
}

static void
oma_load_features (MMIfaceModemOma *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    qmi_client_oma_get_feature_setting (
        QMI_CLIENT_OMA (client),
        NULL,
        5,
        NULL,
        (GAsyncReadyCallback)oma_get_feature_setting_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Setup (OMA interface) */

static gboolean
oma_setup_finish (MMIfaceModemOma *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
oma_set_feature_setting_ready (QmiClientOma *client,
                               GAsyncResult *res,
                               GTask *task)
{
    QmiMessageOmaSetFeatureSettingOutput *output;
    GError *error = NULL;

    output = qmi_client_oma_set_feature_setting_finish (client, res, &error);
    if (!output || !qmi_message_oma_set_feature_setting_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);

    if (output)
        qmi_message_oma_set_feature_setting_output_unref (output);
}

static void
oma_setup (MMIfaceModemOma *self,
           MMOmaFeature features,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    QmiClient *client = NULL;
    QmiMessageOmaSetFeatureSettingInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    input = qmi_message_oma_set_feature_setting_input_new ();
    qmi_message_oma_set_feature_setting_input_set_device_provisioning_service_update_config (
        input,
        !!(features & MM_OMA_FEATURE_DEVICE_PROVISIONING),
        NULL);
    qmi_message_oma_set_feature_setting_input_set_prl_update_service_config (
        input,
        !!(features & MM_OMA_FEATURE_PRL_UPDATE),
        NULL);
    qmi_message_oma_set_feature_setting_input_set_hfa_feature_config (
        input,
        !!(features & MM_OMA_FEATURE_HANDS_FREE_ACTIVATION),
        NULL);

    qmi_client_oma_set_feature_setting (
        QMI_CLIENT_OMA (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)oma_set_feature_setting_ready,
        g_task_new (self, NULL, callback, user_data));

    qmi_message_oma_set_feature_setting_input_unref (input);
}

/*****************************************************************************/
/* Start client initiated session (OMA interface) */

static gboolean
oma_start_client_initiated_session_finish (MMIfaceModemOma *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
oma_start_session_ready (QmiClientOma *client,
                         GAsyncResult *res,
                         GTask *task)
{
    QmiMessageOmaStartSessionOutput *output;
    GError *error = NULL;

    output = qmi_client_oma_start_session_finish (client, res, &error);
    if (!output || !qmi_message_oma_start_session_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);

    if (output)
        qmi_message_oma_start_session_output_unref (output);
}

static void
oma_start_client_initiated_session (MMIfaceModemOma *self,
                                    MMOmaSessionType session_type,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    QmiClient *client = NULL;
    QmiMessageOmaStartSessionInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    /* It's already checked in mm-iface-modem-oma; so just assert if this is not ok */
    g_assert (session_type == MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE ||
              session_type == MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE ||
              session_type == MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION);

    input = qmi_message_oma_start_session_input_new ();
    qmi_message_oma_start_session_input_set_session_type (
        input,
        mm_oma_session_type_to_qmi_oma_session_type (session_type),
        NULL);

    qmi_client_oma_start_session (
        QMI_CLIENT_OMA (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)oma_start_session_ready,
        g_task_new (self, NULL, callback, user_data));

    qmi_message_oma_start_session_input_unref (input);
}

/*****************************************************************************/
/* Accept network initiated session (OMA interface) */

static gboolean
oma_accept_network_initiated_session_finish (MMIfaceModemOma *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
oma_send_selection_ready (QmiClientOma *client,
                          GAsyncResult *res,
                          GTask *task)
{
    QmiMessageOmaSendSelectionOutput *output;
    GError *error = NULL;

    output = qmi_client_oma_send_selection_finish (client, res, &error);
    if (!output || !qmi_message_oma_send_selection_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    if (output)
        qmi_message_oma_send_selection_output_unref (output);
}

static void
oma_accept_network_initiated_session (MMIfaceModemOma *self,
                                      guint session_id,
                                      gboolean accept,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    QmiClient *client = NULL;
    QmiMessageOmaSendSelectionInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    input = qmi_message_oma_send_selection_input_new ();
    qmi_message_oma_send_selection_input_set_network_initiated_alert_selection (
        input,
        accept,
        (guint16)session_id,
        NULL);

    qmi_client_oma_send_selection (
        QMI_CLIENT_OMA (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)oma_send_selection_ready,
        g_task_new (self, NULL, callback, user_data));

    qmi_message_oma_send_selection_input_unref (input);
}

/*****************************************************************************/
/* Cancel session (OMA interface) */

static gboolean
oma_cancel_session_finish (MMIfaceModemOma *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
oma_cancel_session_ready (QmiClientOma *client,
                          GAsyncResult *res,
                          GTask *task)
{
    QmiMessageOmaCancelSessionOutput *output;
    GError *error = NULL;

    output = qmi_client_oma_cancel_session_finish (client, res, &error);
    if (!output || !qmi_message_oma_cancel_session_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);

    if (output)
        qmi_message_oma_cancel_session_output_unref (output);
}

static void
oma_cancel_session (MMIfaceModemOma *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    qmi_client_oma_cancel_session (
        QMI_CLIENT_OMA (client),
        NULL,
        5,
        NULL,
        (GAsyncReadyCallback)oma_cancel_session_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (OMA interface) */

static void
oma_event_report_indication_cb (QmiClientNas *client,
                                QmiIndicationOmaEventReportOutput *output,
                                MMBroadbandModemQmi *self)
{
    QmiOmaSessionState qmi_session_state;
    QmiOmaSessionType network_initiated_alert_session_type;
    guint16 network_initiated_alert_session_id;

    /* Update session state? */
    if (qmi_indication_oma_event_report_output_get_session_state (
            output,
            &qmi_session_state,
            NULL)) {
        QmiOmaSessionFailedReason qmi_oma_session_failed_reason = QMI_OMA_SESSION_FAILED_REASON_UNKNOWN;

        if (qmi_session_state == QMI_OMA_SESSION_STATE_FAILED)
            qmi_indication_oma_event_report_output_get_session_fail_reason (
                output,
                &qmi_oma_session_failed_reason,
                NULL);

        mm_iface_modem_oma_update_session_state (
            MM_IFACE_MODEM_OMA (self),
            mm_oma_session_state_from_qmi_oma_session_state (qmi_session_state),
            mm_oma_session_state_failed_reason_from_qmi_oma_session_failed_reason (qmi_oma_session_failed_reason));
    }

    /* New network initiated session? */
    if (qmi_indication_oma_event_report_output_get_network_initiated_alert (
            output,
            &network_initiated_alert_session_type,
            &network_initiated_alert_session_id,
            NULL)) {
        MMOmaSessionType session_type;

        session_type = mm_oma_session_type_from_qmi_oma_session_type (network_initiated_alert_session_type);
        if (session_type == MM_OMA_SESSION_TYPE_UNKNOWN)
            mm_warn ("Unknown QMI OMA session type '%u'", network_initiated_alert_session_type);
        else
            mm_iface_modem_oma_add_pending_network_initiated_session (
                MM_IFACE_MODEM_OMA (self),
                session_type,
                (guint)network_initiated_alert_session_id);
    }
}

static gboolean
common_oma_setup_cleanup_unsolicited_events_finish (MMIfaceModemOma *_self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_cleanup_oma_unsolicited_events (MMBroadbandModemQmi *self,
                                             gboolean enable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->oma_unsolicited_events_setup) {
        mm_dbg ("OMA unsolicited events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Store new state */
    self->priv->oma_unsolicited_events_setup = enable;

    /* Connect/Disconnect "Event Report" indications */
    if (enable) {
        g_assert (self->priv->oma_event_report_indication_id == 0);
        self->priv->oma_event_report_indication_id =
            g_signal_connect (client,
                              "event-report",
                              G_CALLBACK (oma_event_report_indication_cb),
                              self);
    } else {
        g_assert (self->priv->oma_event_report_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->oma_event_report_indication_id);
        self->priv->oma_event_report_indication_id = 0;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
oma_cleanup_unsolicited_events (MMIfaceModemOma *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    common_setup_cleanup_oma_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                 FALSE,
                                                 callback,
                                                 user_data);
}

static void
oma_setup_unsolicited_events (MMIfaceModemOma *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    common_setup_cleanup_oma_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                 TRUE,
                                                 callback,
                                                 user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (OMA interface) */

typedef struct {
    gboolean enable;
} EnableOmaUnsolicitedEventsContext;

static gboolean
common_oma_enable_disable_unsolicited_events_finish (MMIfaceModemOma *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ser_oma_indicator_ready (QmiClientOma *client,
                         GAsyncResult *res,
                         GTask *task)
{
    MMBroadbandModemQmi *self;
    EnableOmaUnsolicitedEventsContext *ctx;
    QmiMessageOmaSetEventReportOutput *output = NULL;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_oma_set_event_report_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_oma_set_event_report_output_get_result (output, &error)) {
        mm_dbg ("Couldn't set event report: '%s'", error->message);
        g_error_free (error);
    }

    if (output)
        qmi_message_oma_set_event_report_output_unref (output);

    /* Just ignore errors for now */
    self->priv->oma_unsolicited_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_oma_unsolicited_events (MMBroadbandModemQmi *self,
                                              gboolean enable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data)
{
    EnableOmaUnsolicitedEventsContext *ctx;
    GTask *task;
    QmiClient *client = NULL;
    QmiMessageOmaSetEventReportInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_OMA, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->oma_unsolicited_events_enabled) {
        mm_dbg ("OMA unsolicited events already %s; skipping",
                enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx = g_new (EnableOmaUnsolicitedEventsContext, 1);
    ctx->enable = enable;

    g_task_set_task_data (task, ctx, g_free);

    input = qmi_message_oma_set_event_report_input_new ();
    qmi_message_oma_set_event_report_input_set_session_state_reporting (
        input,
        ctx->enable,
        NULL);
    qmi_message_oma_set_event_report_input_set_network_initiated_alert_reporting (
        input,
        ctx->enable,
        NULL);
    qmi_client_oma_set_event_report (
        QMI_CLIENT_OMA (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_oma_indicator_ready,
        task);
    qmi_message_oma_set_event_report_input_unref (input);
}

static void
oma_disable_unsolicited_events (MMIfaceModemOma *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    common_enable_disable_oma_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                  FALSE,
                                                  callback,
                                                  user_data);
}

static void
oma_enable_unsolicited_events (MMIfaceModemOma *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    common_enable_disable_oma_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                  TRUE,
                                                  callback,
                                                  user_data);
}

/*****************************************************************************/
/* Check firmware support (Firmware interface) */

typedef struct {
    gchar *build_id;
    GArray *modem_unique_id;
    GArray *pri_unique_id;
    gboolean current;
} FirmwarePair;

static void
firmware_pair_free (FirmwarePair *pair)
{
    g_free (pair->build_id);
    g_array_unref (pair->modem_unique_id);
    g_array_unref (pair->pri_unique_id);
    g_slice_free (FirmwarePair, pair);
}

typedef struct {
    QmiClientDms *client;
    GList *pairs;
    GList *l;
} FirmwareCheckSupportContext;

static void
firmware_check_support_context_free (FirmwareCheckSupportContext *ctx)
{
    g_list_free_full (ctx->pairs, (GDestroyNotify)firmware_pair_free);
    g_object_unref (ctx->client);
    g_slice_free (FirmwareCheckSupportContext, ctx);
}

static gboolean
firmware_check_support_finish (MMIfaceModemFirmware *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void get_next_image_info (GTask *task);

static void
get_pri_image_info_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask *task)
{
    MMBroadbandModemQmi *self;
    FirmwareCheckSupportContext *ctx;
    QmiMessageDmsGetStoredImageInfoOutput *output;
    GError *error = NULL;
    FirmwarePair *current;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);
    current = (FirmwarePair *)ctx->l->data;

    output = qmi_client_dms_get_stored_image_info_finish (client, res, &error);
    if (!output ||
        !qmi_message_dms_get_stored_image_info_output_get_result (output, &error)) {
        mm_warn ("Couldn't get detailed info for PRI image with build ID '%s': %s",
                 current->build_id,
                 error->message);
        g_error_free (error);
    } else {
        gchar *unique_id_str;
        MMFirmwareProperties *firmware;

        firmware = mm_firmware_properties_new (MM_FIRMWARE_IMAGE_TYPE_GOBI,
                                               current->build_id);

        unique_id_str = mm_utils_bin2hexstr ((const guint8 *)current->pri_unique_id->data,
                                             current->pri_unique_id->len);
        mm_firmware_properties_set_gobi_pri_unique_id (firmware, unique_id_str);
        g_free (unique_id_str);

        unique_id_str = mm_utils_bin2hexstr ((const guint8 *)current->modem_unique_id->data,
                                             current->modem_unique_id->len);
        mm_firmware_properties_set_gobi_modem_unique_id (firmware, unique_id_str);
        g_free (unique_id_str);

        /* Boot version (optional) */
        {
            guint16 boot_major_version;
            guint16 boot_minor_version;

            if (qmi_message_dms_get_stored_image_info_output_get_boot_version (
                    output,
                    &boot_major_version,
                    &boot_minor_version,
                    NULL)) {
                gchar *aux;

                aux = g_strdup_printf ("%u.%u", boot_major_version, boot_minor_version);
                mm_firmware_properties_set_gobi_boot_version (firmware, aux);
                g_free (aux);
            }
        }

        /* PRI version (optional) */
        {
            guint32 pri_version;
            const gchar *pri_info;

            if (qmi_message_dms_get_stored_image_info_output_get_pri_version (
                    output,
                    &pri_version,
                    &pri_info,
                    NULL)) {
                gchar *aux;

                aux = g_strdup_printf ("%u", pri_version);
                mm_firmware_properties_set_gobi_pri_version (firmware, aux);
                g_free (aux);

                mm_firmware_properties_set_gobi_pri_info (firmware, pri_info);
            }
        }

        /* Add firmware image to our internal list */
        self->priv->firmware_list = g_list_append (self->priv->firmware_list,
                                                   firmware);

        /* If this is is also the current image running, keep it */
        if (current->current) {
            if (self->priv->current_firmware)
                mm_warn ("A current firmware is already set (%s), not setting '%s' as current",
                         mm_firmware_properties_get_unique_id (self->priv->current_firmware),
                         current->build_id);
            else
                self->priv->current_firmware = g_object_ref (firmware);

        }
    }

    if (output)
        qmi_message_dms_get_stored_image_info_output_unref (output);

    /* Go on to the next one */
    ctx->l = g_list_next (ctx->l);
    get_next_image_info (task);
}

static void
get_next_image_info (GTask *task)
{
    MMBroadbandModemQmi *self;
    FirmwareCheckSupportContext *ctx;
    QmiMessageDmsGetStoredImageInfoInputImage image_id;
    QmiMessageDmsGetStoredImageInfoInput *input;
    FirmwarePair *current;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->l) {
        /* We're done */

        if (!self->priv->firmware_list) {
            mm_warn ("No valid firmware images listed. "
                     "Assuming firmware unsupported.");
            g_task_return_boolean (task, FALSE);
        } else
            g_task_return_boolean (task, TRUE);

        g_object_unref (task);
        return;
    }

    current = (FirmwarePair *)ctx->l->data;

    /* Query PRI image info */
    image_id.type = QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI;
    image_id.unique_id = current->pri_unique_id;
    image_id.build_id = current->build_id;
    input = qmi_message_dms_get_stored_image_info_input_new ();
    qmi_message_dms_get_stored_image_info_input_set_image (input, &image_id, NULL);
    qmi_client_dms_get_stored_image_info (ctx->client,
                                          input,
                                          10,
                                          NULL,
                                          (GAsyncReadyCallback)get_pri_image_info_ready,
                                          task);
    qmi_message_dms_get_stored_image_info_input_unref (input);
}

static gboolean
match_images (const gchar *pri_id, const gchar *modem_id)
{
    gsize modem_id_len;

    if (!pri_id || !modem_id)
        return FALSE;

    if (g_str_equal (pri_id, modem_id))
        return TRUE;

    /* If the Modem image build_id ends in '?' just use a prefix match.  eg,
     * assume that modem="02.08.02.00_?" matches pri="02.08.02.00_ATT" or
     * pri="02.08.02.00_GENERIC".
     */
    modem_id_len = strlen (modem_id);
    if (modem_id[modem_id_len - 1] != '?')
        return FALSE;

    return strncmp (pri_id, modem_id, modem_id_len - 1) == 0;
}

static void
list_stored_images_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask *task)
{
    FirmwareCheckSupportContext *ctx;
    GArray *array;
    gint pri_id;
    gint modem_id;
    guint i;
    guint j;
    QmiMessageDmsListStoredImagesOutputListImage *image_pri;
    QmiMessageDmsListStoredImagesOutputListImage *image_modem;
    QmiMessageDmsListStoredImagesOutput *output;

    output = qmi_client_dms_list_stored_images_finish (client, res, NULL);
    if (!output ||
        !qmi_message_dms_list_stored_images_output_get_result (output, NULL)) {
        /* Assume firmware unsupported */
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        if (output)
            qmi_message_dms_list_stored_images_output_unref (output);
        return;
    }

    qmi_message_dms_list_stored_images_output_get_list (
        output,
        &array,
        NULL);

    /* Find which index corresponds to each image type */
    pri_id = -1;
    modem_id = -1;
    for (i = 0; i < array->len; i++) {
        QmiMessageDmsListStoredImagesOutputListImage *image;

        image = &g_array_index (array,
                                QmiMessageDmsListStoredImagesOutputListImage,
                                i);

        switch (image->type) {
        case QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI:
            if (pri_id != -1)
                mm_warn ("Multiple array elements found with PRI type");
            else
                pri_id = (gint)i;
            break;
        case QMI_DMS_FIRMWARE_IMAGE_TYPE_MODEM:
            if (modem_id != -1)
                mm_warn ("Multiple array elements found with MODEM type");
            else
                modem_id = (gint)i;
            break;
        default:
            break;
        }
    }

    if (pri_id < 0 || modem_id < 0) {
        mm_warn ("We need both PRI (%s) and MODEM (%s) images. "
                 "Assuming firmware unsupported.",
                 pri_id < 0 ? "not found" : "found",
                 modem_id < 0 ? "not found" : "found");
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        qmi_message_dms_list_stored_images_output_unref (output);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Loop PRI images and try to find a pairing MODEM image with same boot ID */
    image_pri = &g_array_index (array,
                                QmiMessageDmsListStoredImagesOutputListImage,
                                pri_id);
    image_modem = &g_array_index (array,
                                  QmiMessageDmsListStoredImagesOutputListImage,
                                  modem_id);

    for (i = 0; i < image_pri->sublist->len; i++) {
        QmiMessageDmsListStoredImagesOutputListImageSublistSublistElement *subimage_pri;

        subimage_pri = &g_array_index (image_pri->sublist,
                                       QmiMessageDmsListStoredImagesOutputListImageSublistSublistElement,
                                       i);
        for (j = 0; j < image_modem->sublist->len; j++) {
            QmiMessageDmsListStoredImagesOutputListImageSublistSublistElement *subimage_modem;

            subimage_modem = &g_array_index (image_modem->sublist,
                                             QmiMessageDmsListStoredImagesOutputListImageSublistSublistElement,
                                             j);

            if (match_images (subimage_pri->build_id, subimage_modem->build_id)) {
                FirmwarePair *pair;

                mm_dbg ("Found pairing PRI+MODEM images with build ID '%s'", subimage_pri->build_id);
                pair = g_slice_new (FirmwarePair);
                pair->build_id = g_strdup (subimage_pri->build_id);
                pair->modem_unique_id = g_array_ref (subimage_modem->unique_id);
                pair->pri_unique_id = g_array_ref (subimage_pri->unique_id);
                pair->current = (image_pri->index_of_running_image == i ? TRUE : FALSE);
                ctx->pairs = g_list_append (ctx->pairs, pair);
                break;
            }
        }

        if (j == image_modem->sublist->len)
            mm_dbg ("Pairing for PRI image with build ID '%s' not found", subimage_pri->build_id);
    }

    if (!ctx->pairs) {
        mm_warn ("No valid PRI+MODEM pairs found. "
                 "Assuming firmware unsupported.");
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        qmi_message_dms_list_stored_images_output_unref (output);
        return;
    }

    /* Firmware is supported; now keep on loading info for each image and cache it */
    qmi_message_dms_list_stored_images_output_unref (output);

    ctx->l = ctx->pairs;
    get_next_image_info (task);
}

static void
firmware_check_support (MMIfaceModemFirmware *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    FirmwareCheckSupportContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (FirmwareCheckSupportContext);
    ctx->client = g_object_ref (client);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)firmware_check_support_context_free);

    mm_dbg ("loading firmware images...");
    qmi_client_dms_list_stored_images (QMI_CLIENT_DMS (client),
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)list_stored_images_ready,
                                       task);
}

/*****************************************************************************/
/* Load firmware list (Firmware interface) */

static void
firmware_list_free (GList *firmware_list)
{
    g_list_free_full (firmware_list, g_object_unref);
}

static GList *
firmware_load_list_finish (MMIfaceModemFirmware *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
firmware_load_list (MMIfaceModemFirmware *_self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GList *dup;
    GTask *task;

    /* We'll return the new list of new references we create here */
    dup = g_list_copy_deep (self->priv->firmware_list, (GCopyFunc)g_object_ref, NULL);

    task = g_task_new (self, NULL, callback, user_data);
    if (dup)
        g_task_return_pointer (task, dup, (GDestroyNotify)firmware_list_free);
    else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "firmware list unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Load current firmware (Firmware interface) */

static MMFirmwareProperties *
firmware_load_current_finish (MMIfaceModemFirmware *self,
                              GAsyncResult *res,
                              GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
firmware_load_current (MMIfaceModemFirmware *_self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->current_firmware)
        g_task_return_pointer (task,
                               g_object_ref (self->priv->current_firmware),
                               g_object_unref);
    else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "current firmware unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Change current firmware (Firmware interface) */

typedef struct {
    MMFirmwareProperties *firmware;
} FirmwareChangeCurrentContext;

static void
firmware_change_current_context_free (FirmwareChangeCurrentContext *ctx)
{
    if (ctx->firmware)
        g_object_unref (ctx->firmware);
    g_slice_free (FirmwareChangeCurrentContext, ctx);
}

static gboolean
firmware_change_current_finish (MMIfaceModemFirmware *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
firmware_power_cycle_ready (MMBroadbandModemQmi *self,
                            GAsyncResult *res,
                            GTask *task)
{
    GError *error = NULL;

    if (!power_cycle_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
firmware_select_stored_image_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageDmsSetFirmwarePreferenceOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_firmware_preference_finish (client, res, &error);
    if (!output) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_dms_set_firmware_preference_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_dms_set_firmware_preference_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);

    qmi_message_dms_set_firmware_preference_output_unref (output);

    /* Now, go into offline mode */
    power_cycle (self,
                 (GAsyncReadyCallback)firmware_power_cycle_ready,
                 task);
}

static MMFirmwareProperties *
find_firmware_properties_by_unique_id (MMBroadbandModemQmi *self,
                                       const gchar *unique_id)
{
    GList *l;

    for (l = self->priv->firmware_list; l; l = g_list_next (l)) {
        if (g_str_equal (mm_firmware_properties_get_unique_id (MM_FIRMWARE_PROPERTIES (l->data)),
                         unique_id))
            return g_object_ref (l->data);
    }

    return NULL;
}

static MMFirmwareProperties *
find_firmware_properties_by_gobi_pri_info_substring (MMBroadbandModemQmi *self,
                                                     const gchar *str,
                                                     guint *n_found)
{
    MMFirmwareProperties *first = NULL;
    GList *l;

    *n_found = 0;

    for (l = self->priv->firmware_list; l; l = g_list_next (l)) {
        const gchar *pri_info;

        pri_info = mm_firmware_properties_get_gobi_pri_info (MM_FIRMWARE_PROPERTIES (l->data));
        if (pri_info && strstr (pri_info, str)) {
            if (!first && *n_found == 0)
                first = g_object_ref (l->data);
            else
                g_clear_object (&first);
            (*n_found)++;
        }
    }

    return first;
}

static void
firmware_change_current (MMIfaceModemFirmware *_self,
                         const gchar *unique_id,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    QmiMessageDmsSetFirmwarePreferenceInput *input;
    FirmwareChangeCurrentContext *ctx;
    GTask *task;
    QmiClient *client = NULL;
    GArray *array;
    QmiMessageDmsSetFirmwarePreferenceInputListImage modem_image_id;
    QmiMessageDmsSetFirmwarePreferenceInputListImage pri_image_id;
    guint8 *tmp;
    gsize tmp_len;

    if (!ensure_qmi_client (self,
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (FirmwareChangeCurrentContext);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)firmware_change_current_context_free);

    /* Look for the firmware image with the requested unique ID */
    ctx->firmware = find_firmware_properties_by_unique_id (self, unique_id);
    if (!ctx->firmware) {
        guint n = 0;

        /* Ok, let's look at the PRI info */
        ctx->firmware = find_firmware_properties_by_gobi_pri_info_substring (self, unique_id, &n);
        if (n > 1) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_NOT_FOUND,
                                     "Multiple firmware images (%u) found matching '%s' as PRI info substring",
                                     n, unique_id);
            g_object_unref (task);
            return;
        }

        if (n == 0) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_NOT_FOUND,
                                     "Firmware with unique ID '%s' wasn't found",
                                     unique_id);
            g_object_unref (task);
            return;
        }

        g_assert (n == 1 && MM_IS_FIRMWARE_PROPERTIES (ctx->firmware));
    }

    /* If we're already in the requested firmware, we're done */
    if (self->priv->current_firmware &&
        g_str_equal (mm_firmware_properties_get_unique_id (self->priv->current_firmware),
                     mm_firmware_properties_get_unique_id (ctx->firmware))) {
        mm_dbg ("Modem is already running firmware image '%s'",
                mm_firmware_properties_get_unique_id (self->priv->current_firmware));
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Modem image ID */
    tmp_len = 0;
    tmp = (guint8 *)mm_utils_hexstr2bin (mm_firmware_properties_get_gobi_modem_unique_id (ctx->firmware), &tmp_len);
    modem_image_id.type = QMI_DMS_FIRMWARE_IMAGE_TYPE_MODEM;
    modem_image_id.build_id = (gchar *)mm_firmware_properties_get_unique_id (ctx->firmware);
    modem_image_id.unique_id = g_array_sized_new (FALSE, FALSE, sizeof (guint8), tmp_len);
    g_array_insert_vals (modem_image_id.unique_id, 0, tmp, tmp_len);
    g_free (tmp);

    /* PRI image ID */
    tmp_len = 0;
    tmp = (guint8 *)mm_utils_hexstr2bin (mm_firmware_properties_get_gobi_pri_unique_id (ctx->firmware), &tmp_len);
    pri_image_id.type = QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI;
    pri_image_id.build_id = (gchar *)mm_firmware_properties_get_unique_id (ctx->firmware);
    pri_image_id.unique_id = g_array_sized_new (FALSE, FALSE, sizeof (guint8), tmp_len);
    g_array_insert_vals (pri_image_id.unique_id, 0, tmp, tmp_len);
    g_free (tmp);

    mm_dbg ("Changing Gobi firmware to MODEM '%s' and PRI '%s' with Build ID '%s'...",
            mm_firmware_properties_get_gobi_modem_unique_id (ctx->firmware),
            mm_firmware_properties_get_gobi_pri_unique_id (ctx->firmware),
            unique_id);

    /* Build array of image IDs */
    array = g_array_sized_new (FALSE, FALSE, sizeof (QmiMessageDmsSetFirmwarePreferenceInputListImage), 2);
    g_array_append_val (array, modem_image_id);
    g_array_append_val (array, pri_image_id);

    input = qmi_message_dms_set_firmware_preference_input_new ();
    qmi_message_dms_set_firmware_preference_input_set_list (input, array, NULL);
    qmi_client_dms_set_firmware_preference (
        QMI_CLIENT_DMS (client),
        input,
        10,
        NULL,
        (GAsyncReadyCallback)firmware_select_stored_image_ready,
        task);
    g_array_unref (modem_image_id.unique_id);
    g_array_unref (pri_image_id.unique_id);
    qmi_message_dms_set_firmware_preference_input_unref (input);
}

/*****************************************************************************/
/* Check support (Signal interface) */

static gboolean
signal_check_support_finish (MMIfaceModemSignal *self,
                             GAsyncResult *res,
                             GError **error)
{

    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
signal_check_support (MMIfaceModemSignal *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMPortQmi *port;
    gboolean supported = FALSE;
    GTask *task;

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));

    /* If NAS service is available, assume either signal info or signal strength are supported */
    if (port)
        supported = !!mm_port_qmi_peek_client (port, QMI_SERVICE_NAS, MM_PORT_QMI_FLAG_DEFAULT);

    mm_dbg ("Extended signal capabilities %ssupported", supported ? "" : "not ");

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, supported);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load extended signal information */

typedef enum {
    SIGNAL_LOAD_VALUES_STEP_SIGNAL_FIRST,
    SIGNAL_LOAD_VALUES_STEP_SIGNAL_INFO,
    SIGNAL_LOAD_VALUES_STEP_SIGNAL_STRENGTH,
    SIGNAL_LOAD_VALUES_STEP_SIGNAL_LAST
} SignalLoadValuesStep;

typedef struct {
    MMSignal *cdma;
    MMSignal *evdo;
    MMSignal *gsm;
    MMSignal *umts;
    MMSignal *lte;
} SignalLoadValuesResult;

typedef struct {
    QmiClientNas *client;
    SignalLoadValuesStep step;
    SignalLoadValuesResult *values_result;
} SignalLoadValuesContext;

static void
signal_load_values_result_free (SignalLoadValuesResult *result)
{
    if (result->cdma)
        g_object_unref (result->cdma);
    if (result->evdo)
        g_object_unref (result->evdo);
    if (result->gsm)
        g_object_unref (result->gsm);
    if (result->umts)
        g_object_unref (result->umts);
    if (result->lte)
        g_object_unref (result->lte);
    g_slice_free (SignalLoadValuesResult, result);
}

static void
signal_load_values_context_free (SignalLoadValuesContext *ctx)
{
    if (ctx->values_result)
        signal_load_values_result_free (ctx->values_result);
    g_slice_free (SignalLoadValuesContext, ctx);
}

static gdouble
get_db_from_sinr_level (QmiNasEvdoSinrLevel level)
{
    switch (level) {
    case QMI_NAS_EVDO_SINR_LEVEL_0: return -9.0;
    case QMI_NAS_EVDO_SINR_LEVEL_1: return -6;
    case QMI_NAS_EVDO_SINR_LEVEL_2: return -4.5;
    case QMI_NAS_EVDO_SINR_LEVEL_3: return -3;
    case QMI_NAS_EVDO_SINR_LEVEL_4: return -2;
    case QMI_NAS_EVDO_SINR_LEVEL_5: return 1;
    case QMI_NAS_EVDO_SINR_LEVEL_6: return 3;
    case QMI_NAS_EVDO_SINR_LEVEL_7: return 6;
    case QMI_NAS_EVDO_SINR_LEVEL_8: return +9;
    default:
        mm_warn ("Invalid SINR level '%u'", level);
        return -G_MAXDOUBLE;
    }
}

static gboolean
signal_load_values_finish (MMIfaceModemSignal *self,
                           GAsyncResult *res,
                           MMSignal **cdma,
                           MMSignal **evdo,
                           MMSignal **gsm,
                           MMSignal **umts,
                           MMSignal **lte,
                           GError **error)
{
    SignalLoadValuesResult *values_result;

    values_result = g_task_propagate_pointer (G_TASK (res), error);
    if (!values_result)
        return FALSE;

    *cdma = values_result->cdma ? g_object_ref (values_result->cdma) : NULL;
    *evdo = values_result->evdo ? g_object_ref (values_result->evdo) : NULL;
    *gsm  = values_result->gsm  ? g_object_ref (values_result->gsm)  : NULL;
    *umts = values_result->umts ? g_object_ref (values_result->umts) : NULL;
    *lte  = values_result->lte  ? g_object_ref (values_result->lte)  : NULL;
    signal_load_values_result_free (values_result);
    return TRUE;
}

static void signal_load_values_context_step (GTask *task);

static void
signal_load_values_get_signal_strength_ready (QmiClientNas *client,
                                              GAsyncResult *res,
                                              GTask *task)
{
    SignalLoadValuesContext *ctx;
    QmiMessageNasGetSignalStrengthOutput *output;
    GArray *array;
    gint32 aux_int32;
    gint16 aux_int16;
    gint8 aux_int8;
    QmiNasRadioInterface radio_interface;
    QmiNasEvdoSinrLevel sinr;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_get_signal_strength_finish (client, res, NULL);
    if (!output || !qmi_message_nas_get_signal_strength_output_get_result (output, NULL)) {
        /* No hard errors, go on to next step */
        ctx->step++;
        signal_load_values_context_step (task);
        if (output)
            qmi_message_nas_get_signal_strength_output_unref (output);
        return;
    }

    /* Good, we have results */
    ctx->values_result = g_slice_new0 (SignalLoadValuesResult);

    /* RSSI
     *
     * We will assume that valid access technologies reported in this output
     * are the ones which are listed in the RSSI output. If a given access tech
     * is not given in this list, it will not be considered afterwards (e.g. if
     * no EV-DO is given in the RSSI list, the SINR level won't be processed,
     * even if the TLV is available.
     */
    if (qmi_message_nas_get_signal_strength_output_get_rssi_list (output, &array, NULL)) {
        guint i;

        for (i = 0; i < array->len; i++) {
            QmiMessageNasGetSignalStrengthOutputRssiListElement *element;

            element = &g_array_index (array, QmiMessageNasGetSignalStrengthOutputRssiListElement, i);

            switch (element->radio_interface) {
            case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
                if (!ctx->values_result->cdma)
                    ctx->values_result->cdma = mm_signal_new ();
                mm_signal_set_rssi (ctx->values_result->cdma, (gdouble)element->rssi);
                break;
            case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
                if (!ctx->values_result->evdo)
                    ctx->values_result->evdo = mm_signal_new ();
                mm_signal_set_rssi (ctx->values_result->evdo, (gdouble)element->rssi);
                break;
            case QMI_NAS_RADIO_INTERFACE_GSM:
                if (!ctx->values_result->gsm)
                    ctx->values_result->gsm = mm_signal_new ();
                mm_signal_set_rssi (ctx->values_result->gsm, (gdouble)element->rssi);
                break;
            case QMI_NAS_RADIO_INTERFACE_UMTS:
                if (!ctx->values_result->umts)
                    ctx->values_result->umts = mm_signal_new ();
                mm_signal_set_rssi (ctx->values_result->umts, (gdouble)element->rssi);
                break;
            case QMI_NAS_RADIO_INTERFACE_LTE:
                if (!ctx->values_result->lte)
                    ctx->values_result->lte = mm_signal_new ();
                mm_signal_set_rssi (ctx->values_result->lte, (gdouble)element->rssi);
                break;
            default:
                break;
            }
        }
    }

    /* ECIO (CDMA, EV-DO and UMTS) */
    if (qmi_message_nas_get_signal_strength_output_get_ecio_list (output, &array, NULL)) {
        guint i;

        for (i = 0; i < array->len; i++) {
            QmiMessageNasGetSignalStrengthOutputEcioListElement *element;

            element = &g_array_index (array, QmiMessageNasGetSignalStrengthOutputEcioListElement, i);

            switch (element->radio_interface) {
            case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
                if (ctx->values_result->cdma)
                    mm_signal_set_ecio (ctx->values_result->cdma, ((gdouble)element->ecio) * (-0.5));
                break;
            case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
                if (ctx->values_result->evdo)
                    mm_signal_set_ecio (ctx->values_result->evdo, ((gdouble)element->ecio) * (-0.5));
                break;
            case QMI_NAS_RADIO_INTERFACE_UMTS:
                if (ctx->values_result->umts)
                    mm_signal_set_ecio (ctx->values_result->umts, ((gdouble)element->ecio) * (-0.5));
                break;
            default:
                break;
            }
        }
    }

    /* IO (EV-DO) */
    if (qmi_message_nas_get_signal_strength_output_get_io (output, &aux_int32, NULL)) {
        if (ctx->values_result->evdo)
            mm_signal_set_io (ctx->values_result->evdo, (gdouble)aux_int32);
    }

    /* RSRP (LTE) */
    if (qmi_message_nas_get_signal_strength_output_get_lte_rsrp (output, &aux_int16, NULL)) {
        if (ctx->values_result->lte)
            mm_signal_set_rsrp (ctx->values_result->lte, (gdouble)aux_int16);
    }

    /* RSRQ (LTE) */
    if (qmi_message_nas_get_signal_strength_output_get_rsrq (output, &aux_int8, &radio_interface, NULL) &&
        radio_interface == QMI_NAS_RADIO_INTERFACE_LTE) {
        if (ctx->values_result->lte)
            mm_signal_set_rsrq (ctx->values_result->lte, (gdouble)aux_int8);
    }

    /* SNR (LTE) */
    if (qmi_message_nas_get_signal_strength_output_get_lte_snr (output, &aux_int16, NULL)) {
        if (ctx->values_result->lte)
            mm_signal_set_snr (ctx->values_result->lte, (0.1) * ((gdouble)aux_int16));
    }

    /* SINR (EV-DO) */
    if (qmi_message_nas_get_signal_strength_output_get_sinr (output, &sinr, NULL)) {
        if (ctx->values_result->evdo)
            mm_signal_set_sinr (ctx->values_result->evdo, get_db_from_sinr_level (sinr));
    }

    qmi_message_nas_get_signal_strength_output_unref (output);

    /* Go on */
    ctx->step++;
    signal_load_values_context_step (task);
}

static void
signal_load_values_get_signal_info_ready (QmiClientNas *client,
                                          GAsyncResult *res,
                                          GTask *task)
{
    SignalLoadValuesContext *ctx;
    QmiMessageNasGetSignalInfoOutput *output;
    gint8 rssi;
    gint16 ecio;
    QmiNasEvdoSinrLevel sinr_level;
    gint32 io;
    gint8 rsrq;
    gint16 rsrp;
    gint16 snr;

    ctx = g_task_get_task_data (task);
    output = qmi_client_nas_get_signal_info_finish (client, res, NULL);
    if (!output || !qmi_message_nas_get_signal_info_output_get_result (output, NULL)) {
        /* No hard errors, go on to next step */
        ctx->step++;
        signal_load_values_context_step (task);
        if (output)
            qmi_message_nas_get_signal_info_output_unref (output);
        return;
    }

    /* Good, we have results */
    ctx->values_result = g_slice_new0 (SignalLoadValuesResult);

    /* CDMA */
    if (qmi_message_nas_get_signal_info_output_get_cdma_signal_strength (output,
                                                                         &rssi,
                                                                         &ecio,
                                                                         NULL)) {
        ctx->values_result->cdma = mm_signal_new ();
        mm_signal_set_rssi (ctx->values_result->cdma, (gdouble)rssi);
        mm_signal_set_ecio (ctx->values_result->cdma, ((gdouble)ecio) * (-0.5));
    }

    /* HDR... */
    if (qmi_message_nas_get_signal_info_output_get_hdr_signal_strength (output,
                                                                        &rssi,
                                                                        &ecio,
                                                                        &sinr_level,
                                                                        &io,
                                                                        NULL)) {
        ctx->values_result->evdo = mm_signal_new ();
        mm_signal_set_rssi (ctx->values_result->evdo, (gdouble)rssi);
        mm_signal_set_ecio (ctx->values_result->evdo, ((gdouble)ecio) * (-0.5));
        mm_signal_set_sinr (ctx->values_result->evdo, get_db_from_sinr_level (sinr_level));
        mm_signal_set_io (ctx->values_result->evdo, (gdouble)io);
    }

    /* GSM */
    if (qmi_message_nas_get_signal_info_output_get_gsm_signal_strength (output,
                                                                        &rssi,
                                                                        NULL)) {
        ctx->values_result->gsm = mm_signal_new ();
        mm_signal_set_rssi (ctx->values_result->gsm, (gdouble)rssi);
    }

    /* WCDMA... */
    if (qmi_message_nas_get_signal_info_output_get_wcdma_signal_strength (output,
                                                                          &rssi,
                                                                          &ecio,
                                                                          NULL)) {
        ctx->values_result->umts = mm_signal_new ();
        mm_signal_set_rssi (ctx->values_result->umts, (gdouble)rssi);
        mm_signal_set_ecio (ctx->values_result->umts, ((gdouble)ecio) * (-0.5));
    }

    /* LTE... */
    if (qmi_message_nas_get_signal_info_output_get_lte_signal_strength (output,
                                                                        &rssi,
                                                                        &rsrq,
                                                                        &rsrp,
                                                                        &snr,
                                                                        NULL)) {
        ctx->values_result->lte = mm_signal_new ();
        mm_signal_set_rssi (ctx->values_result->lte, (gdouble)rssi);
        mm_signal_set_rsrq (ctx->values_result->lte, (gdouble)rsrq);
        mm_signal_set_rsrp (ctx->values_result->lte, (gdouble)rsrp);
        mm_signal_set_snr (ctx->values_result->lte, (0.1) * ((gdouble)snr));
    }

    qmi_message_nas_get_signal_info_output_unref (output);

    /* Keep on */
    ctx->step++;
    signal_load_values_context_step (task);
}

static void
signal_load_values_context_step (GTask *task)
{
    SignalLoadValuesContext *ctx;

#define VALUES_RESULT_LOADED(ctx)    \
    (ctx->values_result &&           \
     (ctx->values_result->cdma ||    \
      ctx->values_result->evdo ||    \
      ctx->values_result->gsm  ||    \
      ctx->values_result->umts ||    \
      ctx->values_result->lte))

    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_FIRST:
        ctx->step++;
        /* Fall down */

    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_INFO:
        if (qmi_client_check_version (QMI_CLIENT (ctx->client), 1, 8)) {
            qmi_client_nas_get_signal_info (ctx->client,
                                            NULL,
                                            5,
                                            NULL,
                                            (GAsyncReadyCallback)signal_load_values_get_signal_info_ready,
                                            task);
            return;
        }
        ctx->step++;
        /* Fall down */

   case SIGNAL_LOAD_VALUES_STEP_SIGNAL_STRENGTH:
       /* If already loaded with signal info, don't try signal strength */
       if (!VALUES_RESULT_LOADED (ctx)) {
           QmiMessageNasGetSignalStrengthInput *input;

           input = qmi_message_nas_get_signal_strength_input_new ();
           qmi_message_nas_get_signal_strength_input_set_request_mask (
               input,
               (QMI_NAS_SIGNAL_STRENGTH_REQUEST_RSSI |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_ECIO |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_IO |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_SINR |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_RSRQ |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_LTE_SNR |
                QMI_NAS_SIGNAL_STRENGTH_REQUEST_LTE_RSRP),
               NULL);
           qmi_client_nas_get_signal_strength (ctx->client,
                                               input,
                                               5,
                                               NULL,
                                               (GAsyncReadyCallback)signal_load_values_get_signal_strength_ready,
                                               task);
           qmi_message_nas_get_signal_strength_input_unref (input);
           return;
       }
       ctx->step++;
       /* Fall down */

    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_LAST:
        /* If any result is set, succeed */
        if (VALUES_RESULT_LOADED (ctx)) {
            g_task_return_pointer (task,
                                   g_memdup (&ctx->values_result, sizeof (ctx->values_result)),
                                   (GDestroyNotify)signal_load_values_result_free);
            ctx->values_result = NULL;
        } else {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "No way to load extended signal information");
        }
        return;
    }

    g_assert_not_reached ();

#undef VALUES_RESULT_LOADED
}

static void
signal_load_values (MMIfaceModemSignal *self,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    SignalLoadValuesContext *ctx;
    GTask *task;
    QmiClient *client = NULL;

    mm_dbg ("loading extended signal information...");

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (SignalLoadValuesContext);
    ctx->client = g_object_ref (client);
    ctx->step = SIGNAL_LOAD_VALUES_STEP_SIGNAL_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)signal_load_values_context_free);

    signal_load_values_context_step (task);
}

/*****************************************************************************/
/* First enabling step */

static gboolean
enabling_started_finish (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_enabling_started_ready (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;

    if (!MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->enabling_started_finish (
            self,
            res,
            &error)) {
        /* Don't treat this as fatal. Parent enabling may fail if it cannot grab a primary
         * AT port, which isn't really an issue in QMI-based modems */
        mm_dbg ("Couldn't start parent enabling: %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
enabling_started (MMBroadbandModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->enabling_started (
        self,
        (GAsyncReadyCallback)parent_enabling_started_ready,
        task);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMPortQmi *qmi;
    QmiService services[32];
    guint service_index;
} InitializationStartedContext;

static void
initialization_started_context_free (InitializationStartedContext *ctx)
{
    if (ctx->qmi)
        g_object_unref (ctx->qmi);
    g_free (ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        /* Don't treat this as fatal. Parent initialization may fail if it cannot grab a primary
         * AT port, which isn't really an issue in QMI-based modems */
        mm_dbg ("Couldn't start parent initialization: %s", error->message);
        g_error_free (error);
    }

    /* Just parent's pointer passed here */
    g_task_return_pointer (task, parent_ctx, NULL);
    g_object_unref (task);
}

static void
parent_initialization_started (GTask *task)
{
    MMBroadbandModem *self;

    self = g_task_get_source_object (task);
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started (
        self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        task);
}

static void
qmi_device_removed_cb (QmiDevice *device,
                       MMBroadbandModemQmi *self)
{
    /* Reprobe the modem here so we can get notifications back. */
    mm_info ("Connection to qmi-proxy for %s lost, reprobing",
             qmi_device_get_path_display (device));

    g_signal_handler_disconnect (device, self->priv->qmi_device_removed_id);
    self->priv->qmi_device_removed_id = 0;

    mm_base_modem_set_reprobe (MM_BASE_MODEM (self), TRUE);
    mm_base_modem_set_valid (MM_BASE_MODEM (self), FALSE);
}

static void
track_qmi_device_removed (MMBroadbandModemQmi *self,
                          MMPortQmi* qmi)
{
    QmiDevice *device;

    device = mm_port_qmi_peek_device (qmi);
    g_assert (device);

    self->priv->qmi_device_removed_id = g_signal_connect (
        device,
        QMI_DEVICE_SIGNAL_REMOVED,
        G_CALLBACK (qmi_device_removed_cb),
        self);
}

static void
untrack_qmi_device_removed (MMBroadbandModemQmi *self,
                            MMPortQmi* qmi)
{
    QmiDevice *device;

    if (self->priv->qmi_device_removed_id == 0)
        return;

    device = mm_port_qmi_peek_device (qmi);
    if (!device)
        return;

    g_signal_handler_disconnect (device, self->priv->qmi_device_removed_id);
    self->priv->qmi_device_removed_id = 0;
}

static void allocate_next_client (GTask *task);

static void
qmi_port_allocate_client_ready (MMPortQmi *qmi,
                                GAsyncResult *res,
                                GTask *task)
{
    InitializationStartedContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_port_qmi_allocate_client_finish (qmi, res, &error)) {
        mm_dbg ("Couldn't allocate client for service '%s': %s",
                qmi_service_get_string (ctx->services[ctx->service_index]),
                error->message);
        g_error_free (error);
    }

    ctx->service_index++;
    allocate_next_client (task);
}

static void
allocate_next_client (GTask *task)
{
    InitializationStartedContext *ctx;
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (ctx->services[ctx->service_index] == QMI_SERVICE_UNKNOWN) {
        /* Done we are, track device removal and launch parent's callback */
        track_qmi_device_removed (self, ctx->qmi);
        parent_initialization_started (task);
        return;
    }

    /* Otherwise, allocate next client */
    mm_port_qmi_allocate_client (ctx->qmi,
                                 ctx->services[ctx->service_index],
                                 MM_PORT_QMI_FLAG_DEFAULT,
                                 NULL,
                                 (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                 task);
}


static void
qmi_port_open_ready_no_data_format (MMPortQmi *qmi,
                                    GAsyncResult *res,
                                    GTask *task)
{
    GError *error = NULL;

    if (!mm_port_qmi_open_finish (qmi, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    allocate_next_client (task);
}

static void
qmi_port_open_ready (MMPortQmi *qmi,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationStartedContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_port_qmi_open_finish (qmi, res, &error)) {
        /* Really, really old devices (Gobi 1K, 2008-era firmware) may not
         * support SetDataFormat, so if we get an error opening the port
         * try without it.  The qmi_wwan driver will fix up any issues that
         * the device might have between raw-ip and 802.3 mode anyway.
         */
        mm_port_qmi_open (ctx->qmi,
                          FALSE,
                          NULL,
                          (GAsyncReadyCallback)qmi_port_open_ready_no_data_format,
                          task);
        return;
    }

    allocate_next_client (task);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;
    GTask *task;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->qmi = mm_base_modem_get_port_qmi (MM_BASE_MODEM (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_started_context_free);

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->qmi) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot initialize: QMI port went missing");
        g_object_unref (task);
        return;
    }

    if (mm_port_qmi_is_open (ctx->qmi)) {
        /* Nothing to be done, just track device removal and launch parent's
         * callback */
        track_qmi_device_removed (MM_BROADBAND_MODEM_QMI (self), ctx->qmi);
        parent_initialization_started (task);
        return;
    }

    /* Setup services to open */
    ctx->services[0] = QMI_SERVICE_DMS;
    ctx->services[1] = QMI_SERVICE_NAS;
    ctx->services[2] = QMI_SERVICE_WMS;
    ctx->services[3] = QMI_SERVICE_PDS;
    ctx->services[4] = QMI_SERVICE_OMA;
    ctx->services[5] = QMI_SERVICE_UIM;
    ctx->services[6] = QMI_SERVICE_UNKNOWN;

    /* Now open our QMI port */
    mm_port_qmi_open (ctx->qmi,
                      TRUE,
                      NULL,
                      (GAsyncReadyCallback)qmi_port_open_ready,
                      task);
}

/*****************************************************************************/

MMBroadbandModemQmi *
mm_broadband_modem_qmi_new (const gchar *device,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_qmi_init (MMBroadbandModemQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_QMI,
                                              MMBroadbandModemQmiPrivate);
}

static void
finalize (GObject *object)
{
    MMPortQmi *qmi;
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    qmi = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    if (qmi) {
        /* Disconnect signal handler for qmi-proxy disappearing, if it exists */
        untrack_qmi_device_removed (self, qmi);
        /* If we did open the QMI port during initialization, close it now */
        if (mm_port_qmi_is_open (qmi))
            mm_port_qmi_close (qmi);
    }

    g_free (self->priv->imei);
    g_free (self->priv->meid);
    g_free (self->priv->esn);
    g_free (self->priv->current_operator_id);
    g_free (self->priv->current_operator_description);
    if (self->priv->supported_bands)
        g_array_unref (self->priv->supported_bands);
    if (self->priv->supported_radio_interfaces)
        g_array_unref (self->priv->supported_radio_interfaces);

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    g_list_free_full (self->priv->firmware_list, g_object_unref);
    self->priv->firmware_list = NULL;

    g_clear_object (&self->priv->current_firmware);

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->load_supported_capabilities = modem_load_supported_capabilities;
    iface->load_supported_capabilities_finish = modem_load_supported_capabilities_finish;
    iface->set_current_capabilities = set_current_capabilities;
    iface->set_current_capabilities_finish = set_current_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
    iface->load_hardware_revision = modem_load_hardware_revision;
    iface->load_hardware_revision_finish = modem_load_hardware_revision_finish;
    iface->load_equipment_identifier = modem_load_equipment_identifier;
    iface->load_equipment_identifier_finish = modem_load_equipment_identifier_finish;
    iface->load_device_identifier = modem_load_device_identifier;
    iface->load_device_identifier_finish = modem_load_device_identifier_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->load_unlock_required = modem_load_unlock_required;
    iface->load_unlock_required_finish = modem_load_unlock_required_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_supported_bands = modem_load_supported_bands;
    iface->load_supported_bands_finish = modem_load_supported_bands_finish;
    iface->load_supported_modes = modem_load_supported_modes;
    iface->load_supported_modes_finish = modem_load_supported_modes_finish;
    iface->load_power_state = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->load_supported_ip_families = modem_load_supported_ip_families;
    iface->load_supported_ip_families_finish = modem_load_supported_ip_families_finish;

    /* Enabling/disabling */
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_down_off_finish;
    iface->modem_after_power_up = NULL;
    iface->modem_after_power_up_finish = NULL;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_up_down_off_finish;
    iface->modem_power_off = modem_power_off;
    iface->modem_power_off_finish = modem_power_up_down_off_finish;
    iface->setup_flow_control = NULL;
    iface->setup_flow_control_finish = NULL;
    iface->load_supported_charsets = NULL;
    iface->load_supported_charsets_finish = NULL;
    iface->setup_charset = NULL;
    iface->setup_charset_finish = NULL;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
    iface->load_current_bands = modem_load_current_bands;
    iface->load_current_bands_finish = modem_load_current_bands_finish;
    iface->set_current_bands = set_current_bands;
    iface->set_current_bands_finish = set_current_bands_finish;

    /* Don't try to load access technologies, as we would be using parent's
     * generic method (QCDM based). Access technologies are already reported via
     * QMI when we load signal quality. */
    iface->load_access_technologies = NULL;
    iface->load_access_technologies_finish = NULL;

    /* Create QMI-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Create QMI-specific bearer */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;

    /* Other actions */
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->factory_reset = modem_factory_reset;
    iface->factory_reset_finish = modem_factory_reset_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;
    iface->load_enabled_facility_locks = modem_3gpp_load_enabled_facility_locks;
    iface->load_enabled_facility_locks_finish = modem_3gpp_load_enabled_facility_locks_finish;

    /* Enabling/Disabling steps */
    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->setup_unsolicited_registration_events = modem_3gpp_setup_unsolicited_registration_events;
    iface->setup_unsolicited_registration_events_finish = modem_3gpp_setup_cleanup_unsolicited_registration_events_finish;
    iface->cleanup_unsolicited_registration_events = modem_3gpp_cleanup_unsolicited_registration_events;
    iface->cleanup_unsolicited_registration_events_finish = modem_3gpp_setup_cleanup_unsolicited_registration_events_finish;
    iface->enable_unsolicited_registration_events = modem_3gpp_enable_unsolicited_registration_events;
    iface->enable_unsolicited_registration_events_finish = modem_3gpp_enable_disable_unsolicited_registration_events_finish;
    iface->disable_unsolicited_registration_events = modem_3gpp_disable_unsolicited_registration_events;
    iface->disable_unsolicited_registration_events_finish = modem_3gpp_enable_disable_unsolicited_registration_events_finish;

    /* Other actions */
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface)
{
    /* Assume we don't have USSD support */
    iface->check_support = NULL;
    iface->check_support_finish = NULL;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface->load_meid = modem_cdma_load_meid;
    iface->load_meid_finish = modem_cdma_load_meid_finish;
    iface->load_esn = modem_cdma_load_esn;
    iface->load_esn_finish = modem_cdma_load_esn_finish;

    /* Enabling/Disabling steps */
    iface->setup_unsolicited_events = modem_cdma_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_cdma_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_cdma_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_cdma_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_cdma_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_cdma_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_cdma_enable_disable_unsolicited_events_finish;

    /* Other actions */
    iface->run_registration_checks = modem_cdma_run_registration_checks;
    iface->run_registration_checks_finish = modem_cdma_run_registration_checks_finish;
    iface->load_activation_state = modem_cdma_load_activation_state;
    iface->load_activation_state_finish = modem_cdma_load_activation_state_finish;
    iface->activate = modem_cdma_activate;
    iface->activate_finish = modem_cdma_activate_finish;
    iface->activate_manual = modem_cdma_activate_manual;
    iface->activate_manual_finish = modem_cdma_activate_manual_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface_modem_messaging_parent = g_type_interface_peek_parent (iface);

    iface->check_support = messaging_check_support;
    iface->check_support_finish = messaging_check_support_finish;
    iface->load_supported_storages = messaging_load_supported_storages;
    iface->load_supported_storages_finish = messaging_load_supported_storages_finish;
    iface->setup_sms_format = modem_messaging_setup_sms_format;
    iface->setup_sms_format_finish = modem_messaging_setup_sms_format_finish;
    iface->set_default_storage = messaging_set_default_storage;
    iface->set_default_storage_finish = messaging_set_default_storage_finish;
    iface->load_initial_sms_parts = load_initial_sms_parts;
    iface->load_initial_sms_parts_finish = load_initial_sms_parts_finish;
    iface->setup_unsolicited_events = messaging_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = messaging_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = messaging_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = messaging_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = messaging_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = messaging_disable_unsolicited_events_finish;
    iface->create_sms = messaging_create_sms;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities = location_load_capabilities;
    iface->load_capabilities_finish = location_load_capabilities_finish;
    iface->load_supl_server = location_load_supl_server;
    iface->load_supl_server_finish = location_load_supl_server_finish;
    iface->set_supl_server = location_set_supl_server;
    iface->set_supl_server_finish = location_set_supl_server_finish;
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
    iface->disable_location_gathering = disable_location_gathering;
    iface->disable_location_gathering_finish = disable_location_gathering_finish;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface->check_support = signal_check_support;
    iface->check_support_finish = signal_check_support_finish;
    iface->load_values = signal_load_values;
    iface->load_values_finish = signal_load_values_finish;
}

static void
iface_modem_oma_init (MMIfaceModemOma *iface)
{
    iface->check_support = oma_check_support;
    iface->check_support_finish = oma_check_support_finish;
    iface->load_features = oma_load_features;
    iface->load_features_finish = oma_load_features_finish;
    iface->setup = oma_setup;
    iface->setup_finish = oma_setup_finish;
    iface->start_client_initiated_session = oma_start_client_initiated_session;
    iface->start_client_initiated_session_finish = oma_start_client_initiated_session_finish;
    iface->accept_network_initiated_session = oma_accept_network_initiated_session;
    iface->accept_network_initiated_session_finish = oma_accept_network_initiated_session_finish;
    iface->cancel_session = oma_cancel_session;
    iface->cancel_session_finish = oma_cancel_session_finish;
    iface->setup_unsolicited_events = oma_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = common_oma_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = oma_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = common_oma_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = oma_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = common_oma_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = oma_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = common_oma_enable_disable_unsolicited_events_finish;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmware *iface)
{
    iface->check_support = firmware_check_support;
    iface->check_support_finish = firmware_check_support_finish;
    iface->load_list = firmware_load_list;
    iface->load_list_finish = firmware_load_list_finish;
    iface->load_current = firmware_load_current;
    iface->load_current_finish = firmware_load_current_finish;
    iface->change_current = firmware_change_current;
    iface->change_current_finish = firmware_change_current_finish;
}

static void
mm_broadband_modem_qmi_class_init (MMBroadbandModemQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemQmiPrivate));

    object_class->finalize = finalize;
    object_class->dispose = dispose;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
    broadband_modem_class->enabling_started = enabling_started;
    broadband_modem_class->enabling_started_finish = enabling_started_finish;
    /* Do not initialize the QMI modem through AT commands */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;
}
