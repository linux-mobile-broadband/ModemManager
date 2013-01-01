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
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

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
#include "mm-sim-qmi.h"
#include "mm-bearer-qmi.h"
#include "mm-sms-qmi.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);

static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init))

struct _MMBroadbandModemQmiPrivate {
    /* Cached device IDs, retrieved by the modem interface when loading device
     * IDs, and used afterwards in the 3GPP and CDMA interfaces. */
    gchar *imei;
    gchar *meid;
    gchar *esn;

    /* Cached supported frequency bands; in order to handle ANY */
    GArray *supported_bands;

    /* 3GPP and CDMA share unsolicited events setup/enable/disable/cleanup */
    gboolean unsolicited_events_enabled;
    gboolean unsolicited_events_setup;
    guint event_report_indication_id;
#if defined WITH_NEWEST_QMI_COMMANDS
    guint signal_info_indication_id;
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* 3GPP/CDMA registration helpers */
    gchar *current_operator_id;
    gchar *current_operator_description;
    gboolean unsolicited_registration_events_enabled;
    gboolean unsolicited_registration_events_setup;
    guint serving_system_indication_id;
#if defined WITH_NEWEST_QMI_COMMANDS
    guint system_info_indication_id;
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* Messaging helpers */
    gboolean messaging_unsolicited_events_enabled;
    gboolean messaging_unsolicited_events_setup;
    guint messaging_event_report_indication_id;

    /* Location helpers */
    MMModemLocationSource enabled_sources;
    guint location_event_report_indication_id;

    /* Firmware helpers */
    GList *firmware_list;
    MMFirmwareProperties *current_firmware;
};

/*****************************************************************************/

static QmiClient *
peek_qmi_client (MMBroadbandModemQmi *self,
                 QmiService service,
                 GError **error)
{
    MMQmiPort *port;
    QmiClient *client;

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    if (!port) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek QMI port");
        return NULL;
    }

    client = mm_qmi_port_peek_client (port,
                                      service,
                                      MM_QMI_PORT_FLAG_DEFAULT);
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
        g_simple_async_report_take_gerror_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBearer *bearer;
    GSimpleAsyncResult *result;

    /* Set a new ref to the bearer object as result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    /* We just create a MMBearerQmi */
    mm_dbg ("Creating QMI bearer in QMI modem");
    bearer = mm_bearer_qmi_new (MM_BROADBAND_MODEM_QMI (self),
                                properties);

    g_simple_async_result_set_op_res_gpointer (result, bearer, g_object_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Current Capabilities loading (Modem interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *nas_client;
    QmiClientDms *dms_client;
    GSimpleAsyncResult *result;
    gboolean run_get_system_selection_preference;
    gboolean run_get_technology_preference;
    gboolean run_get_capabilities;
    MMModemCapability capabilities;
} LoadCurrentCapabilitiesContext;

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CAPABILITY_NONE;

    caps = ((MMModemCapability) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static void
load_current_capabilities_context_complete_and_free (LoadCurrentCapabilitiesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->nas_client);
    g_object_unref (ctx->dms_client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void load_current_capabilities_context_step (LoadCurrentCapabilitiesContext *ctx);

static void
load_current_capabilities_get_capabilities_ready (QmiClientDms *client,
                                                  GAsyncResult *res,
                                                  LoadCurrentCapabilitiesContext *ctx)
{
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Capabilities: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        guint i;
        guint mask = MM_MODEM_CAPABILITY_NONE;
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
            mask |= mm_modem_capability_from_qmi_radio_interface (g_array_index (radio_interface_list,
                                                                                 QmiDmsRadioInterface,
                                                                                 i));
        }

        /* Final capabilities are the intersection between the Technology
         * Preference (ie, allowed modes) or SSP and the device's capabilities.
         * If the Technology Preference was "auto" or unknown we just fall back
         * to the Get Capabilities response.
         */
        if (ctx->capabilities == MM_MODEM_CAPABILITY_NONE)
            ctx->capabilities = mask;
        else
            ctx->capabilities &= mask;
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_simple_async_result_set_op_res_gpointer (ctx->result, GUINT_TO_POINTER (ctx->capabilities), NULL);
    load_current_capabilities_context_complete_and_free (ctx);
}

static void
load_current_capabilities_get_technology_preference_ready (QmiClientNas *client,
                                                           GAsyncResult *res,
                                                           LoadCurrentCapabilitiesContext *ctx)
{
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_get_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_dbg ("QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_technology_preference_output_get_result (output, &error)) {
        mm_dbg ("Couldn't get technology preference: %s", error->message);
        g_error_free (error);
    } else {
        QmiNasRadioTechnologyPreference preference_mask;

        qmi_message_nas_get_technology_preference_output_get_active (
            output,
            &preference_mask,
            NULL, /* duration */
            NULL);
        if (preference_mask != QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO) {
            gchar *str;

            str = qmi_nas_radio_technology_preference_build_string_from_mask (preference_mask);
            ctx->capabilities = mm_modem_capability_from_qmi_radio_technology_preference (preference_mask);
            mm_dbg ("%s modes reported in technology preference: '%s'",
                    ctx->capabilities == MM_MODEM_CAPABILITY_NONE ? "Unsupported" : "Valid",
                    str);
            g_free (str);
        }
    }

    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);

    /* Mark as TP already run */
    ctx->run_get_technology_preference = FALSE;

    /* Get DMS Capabilities too */
    load_current_capabilities_context_step (ctx);
}

static void
load_current_capabilities_get_system_selection_preference_ready (QmiClientNas *client,
                                                                 GAsyncResult *res,
                                                                 LoadCurrentCapabilitiesContext *ctx)
{
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;
    QmiNasRatModePreference mode_preference_mask = 0;

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
        QmiNasBandPreference band_preference_mask;

        mm_dbg ("Mode preference not reported in system selection preference");

        if (qmi_message_nas_get_system_selection_preference_output_get_band_preference (
                output,
                &band_preference_mask,
                NULL)) {
            gchar *str;

            str = qmi_nas_band_preference_build_string_from_mask (band_preference_mask);
            ctx->capabilities = mm_modem_capability_from_qmi_band_preference (band_preference_mask);
            mm_dbg ("%s bands reported in system selection preference: '%s'",
                    ctx->capabilities == MM_MODEM_CAPABILITY_NONE ? "Unsupported" : "Valid",
                    str);
            g_free (str);

            /* Just the presence of the LTE band preference tells us it's LTE */
            if (qmi_message_nas_get_system_selection_preference_output_get_lte_band_preference (
                    output,
                    NULL,
                    NULL)) {
                mm_dbg ("LTE band preference found");
                ctx->capabilities |= MM_MODEM_CAPABILITY_LTE;
            }
        } else
            mm_dbg ("Band preference not reported in system selection preference");
    } else {
        gchar *str;

        str = qmi_nas_rat_mode_preference_build_string_from_mask (mode_preference_mask);
        ctx->capabilities = mm_modem_capability_from_qmi_rat_mode_preference (mode_preference_mask);
        mm_dbg ("%s capabilities reported in system selection preference: '%s'",
                ctx->capabilities == MM_MODEM_CAPABILITY_NONE ? "Unsupported" : "Valid",
                str);
        g_free (str);
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    /* Mark as SSP already run */
    ctx->run_get_system_selection_preference = FALSE;

    /* If we got some value, cache it and go on to DMS Get Capabilities */
    if (ctx->capabilities != MM_MODEM_CAPABILITY_NONE)
        ctx->run_get_technology_preference = FALSE;

    load_current_capabilities_context_step (ctx);
}

static void
load_current_capabilities_context_step (LoadCurrentCapabilitiesContext *ctx)
{
    if (ctx->run_get_system_selection_preference) {
        qmi_client_nas_get_system_selection_preference (
            ctx->nas_client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)load_current_capabilities_get_system_selection_preference_ready,
            ctx);
        return;
    }

    if (ctx->run_get_technology_preference) {
        qmi_client_nas_get_technology_preference (
            ctx->nas_client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)load_current_capabilities_get_technology_preference_ready,
            ctx);
        return;
    }

    if (ctx->run_get_capabilities) {
        qmi_client_dms_get_capabilities (
            ctx->dms_client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)load_current_capabilities_get_capabilities_ready,
            ctx);
        return;
    }

    g_simple_async_result_set_error (
        ctx->result,
        MM_CORE_ERROR,
        MM_CORE_ERROR_UNSUPPORTED,
        "Loading current capabilities is not supported by this device");
    load_current_capabilities_context_complete_and_free (ctx);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    LoadCurrentCapabilitiesContext *ctx;
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

    ctx = g_new0 (LoadCurrentCapabilitiesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->nas_client = g_object_ref (nas_client);
    ctx->dms_client = g_object_ref (dms_client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_current_capabilities);
    ctx->capabilities = MM_MODEM_CAPABILITY_NONE;

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_get_system_selection_preference = qmi_client_check_version (nas_client, 1, 1);

    ctx->run_get_technology_preference = TRUE;
    ctx->run_get_capabilities = TRUE;

    load_current_capabilities_context_step (ctx);
}

/*****************************************************************************/
/* Modem Capabilities loading (Modem interface) */

static MMModemCapability
modem_load_modem_capabilities_finish (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CAPABILITY_NONE;

    caps = ((MMModemCapability) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded modem capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static void
dms_get_capabilities_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get modem capabilities: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        guint i;
        guint mask = MM_MODEM_CAPABILITY_NONE;
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
            mask |= mm_modem_capability_from_qmi_radio_interface (g_array_index (radio_interface_list,
                                                                                 QmiDmsRadioInterface,
                                                                                 i));
        }

        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (mask),
                                                   NULL);
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_modem_capabilities (MMIfaceModem *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_modem_capabilities);

    mm_dbg ("loading modem capabilities...");
    qmi_client_dms_get_capabilities (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_capabilities_ready,
                                     result);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    gchar *manufacturer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    manufacturer = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded manufacturer: %s", manufacturer);
    return manufacturer;
}

static void
dms_get_manufacturer_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetManufacturerOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_manufacturer_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_manufacturer_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Manufacturer: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_manufacturer_output_get_manufacturer (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_manufacturer_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_manufacturer);

    mm_dbg ("loading manufacturer...");
    qmi_client_dms_get_manufacturer (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_manufacturer_ready,
                                     result);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    gchar *model;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    model = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded model: %s", model);
    return model;
}

static void
dms_get_model_ready (QmiClientDms *client,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetModelOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_model_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_model_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Model: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_model_output_get_model (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_model_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_model);

    mm_dbg ("loading model...");
    qmi_client_dms_get_model (QMI_CLIENT_DMS (client),
                              NULL,
                              5,
                              NULL,
                              (GAsyncReadyCallback)dms_get_model_ready,
                              result);
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *revision;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    revision = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded revision: %s", revision);
    return revision;
}

static void
dms_get_revision_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetRevisionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_revision_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_revision_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Revision: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_revision_output_get_revision (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_revision_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_revision);

    mm_dbg ("loading revision...");
    qmi_client_dms_get_revision (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_get_revision_ready,
                                 result);
}

/*****************************************************************************/
/* Equipment Identifier loading (Modem interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClient *client;
    GSimpleAsyncResult *result;
} LoadEquipmentIdentifierContext;

static void
load_equipment_identifier_context_complete_and_free (LoadEquipmentIdentifierContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    gchar *equipment_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    equipment_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded equipment identifier: %s", equipment_identifier);
    return equipment_identifier;
}

static void
dms_get_ids_ready (QmiClientDms *client,
                   GAsyncResult *res,
                   LoadEquipmentIdentifierContext *ctx)
{
    QmiMessageDmsGetIdsOutput *output = NULL;
    GError *error = NULL;
    const gchar *str;

    output = qmi_client_dms_get_ids_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        load_equipment_identifier_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_dms_get_ids_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get IDs: ");
        g_simple_async_result_take_error (ctx->result, error);
        qmi_message_dms_get_ids_output_unref (output);
        load_equipment_identifier_context_complete_and_free (ctx);
        return;
    }

    /* In order:
     * If we have a IMEI, use it...
     * Otherwise, if we have a ESN, use it...
     * Otherwise, if we have a MEID, use it...
     * Otherwise, 'unknown'
     */

    if (qmi_message_dms_get_ids_output_get_imei (output, &str, NULL) &&
        str[0] != '\0' && str[0] != '0') {
        g_free (ctx->self->priv->imei);
        ctx->self->priv->imei = g_strdup (str);
    }

    if (qmi_message_dms_get_ids_output_get_esn (output, &str, NULL) &&
        str[0] != '\0' && str[0] != '0') {
        g_free (ctx->self->priv->esn);
        ctx->self->priv->esn = g_strdup (str);
    }

    if (qmi_message_dms_get_ids_output_get_meid (output, &str, NULL) &&
        str[0] != '\0' && str[0] != '0') {
        g_free (ctx->self->priv->meid);
        ctx->self->priv->meid = g_strdup (str);
    }

    if (ctx->self->priv->imei)
        str = ctx->self->priv->imei;
    else if (ctx->self->priv->esn)
        str = ctx->self->priv->esn;
    else if (ctx->self->priv->meid)
        str = ctx->self->priv->meid;
    else
        str = "unknown";

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               g_strdup (str),
                                               (GDestroyNotify)g_free);

    qmi_message_dms_get_ids_output_unref (output);
    load_equipment_identifier_context_complete_and_free (ctx);
}

static void
modem_load_equipment_identifier (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    LoadEquipmentIdentifierContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_new (LoadEquipmentIdentifierContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_equipment_identifier);

    mm_dbg ("loading equipment identifier...");
    qmi_client_dms_get_ids (QMI_CLIENT_DMS (client),
                            NULL,
                            5,
                            NULL,
                            (GAsyncReadyCallback)dms_get_ids_ready,
                            ctx);
}

/*****************************************************************************/
/* Device identifier loading (Modem interface) */

static gchar *
modem_load_device_identifier_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    gchar *device_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    device_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded device identifier: %s", device_identifier);
    return device_identifier;
}

static void
modem_load_device_identifier (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *device_identifier;

    mm_dbg ("loading device identifier...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_device_identifier);

    /* Just use dummy ATI/ATI1 replies, all the other internal info should be
     * enough for uniqueness */
    device_identifier = mm_broadband_modem_create_device_identifier (MM_BROADBAND_MODEM (self), "", "");
    g_simple_async_result_set_op_res_gpointer (result,
                                               device_identifier,
                                               (GDestroyNotify)g_free);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Own Numbers loading (Modem interface) */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    gchar **own_numbers;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    own_numbers = g_new0 (gchar *, 2);
    own_numbers[0] = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded own numbers: %s", own_numbers[0]);
    return own_numbers;
}

static void
dms_get_msisdn_ready (QmiClientDms *client,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetMsisdnOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_msisdn_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_msisdn_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get MSISDN: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_get_msisdn_output_get_msisdn (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_msisdn_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_own_numbers);

    mm_dbg ("loading own numbers...");
    qmi_client_dms_get_msisdn (QMI_CLIENT_DMS (client),
                               NULL,
                               5,
                               NULL,
                               (GAsyncReadyCallback)dms_get_msisdn_ready,
                               result);
}

/*****************************************************************************/
/* Check if unlock required (Modem interface) */

static MMModemLock
modem_load_unlock_required_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCK_UNKNOWN;

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                               G_SIMPLE_ASYNC_RESULT (res)));
}

static void
dms_uim_get_pin_status_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        /* When no SIM inserted, an internal error when checking PIN status
         * needs to be fatal so that we mark the modem unusable. */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INTERNAL)) {
            g_simple_async_result_set_error (simple,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,
                                             "Couldn't get PIN status: %s",
                                             error->message);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "Couldn't get PIN status: ");
            g_simple_async_result_take_error (simple, error);
        }
    } else {
        MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
        QmiDmsUimPinStatus current_status;

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
                NULL))
            lock = mm_modem_lock_from_qmi_uim_pin_status (current_status, FALSE);

        g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (lock), NULL);
    }

    if (output)
        qmi_message_dms_uim_get_pin_status_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_unlock_required);

    /* CDMA-only modems don't need this */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_dbg ("Skipping unlock check in CDMA-only modem...");
        g_simple_async_result_set_op_res_gpointer (result,
                                                   GUINT_TO_POINTER (MM_MODEM_LOCK_NONE),
                                                   NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_dbg ("loading unlock required...");
    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)dms_uim_get_pin_status_ready,
                                       result);
}

/*****************************************************************************/
/* Check if unlock retries (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_UNLOCK_RETRIES (g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
retry_count_dms_uim_get_pin_status_ready (QmiClientDms *client,
                                          GAsyncResult *res,
                                          GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get unlock retries: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        MMUnlockRetries *retries;
        guint8 verify_retries_left;
        guint8 unblock_retries_left;

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

        g_simple_async_result_set_op_res_gpointer (simple, retries, g_object_unref);
    }

    if (output)
        qmi_message_dms_uim_get_pin_status_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_unlock_retries);

    mm_dbg ("loading unlock retries...");
    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)retry_count_dms_uim_get_pin_status_ready,
                                       result);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
modem_load_supported_bands_finish (MMIfaceModem *_self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    if (self->priv->supported_bands)
        g_array_unref (self->priv->supported_bands);

    /* Cache the supported bands value */
    self->priv->supported_bands = g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                                   G_SIMPLE_ASYNC_RESULT (res)));

    return g_array_ref (self->priv->supported_bands);
}

static void
dms_get_band_capabilities_ready (QmiClientDms *client,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetBandCapabilitiesOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_get_band_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_band_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get band capabilities: ");
        g_simple_async_result_take_error (simple, error);
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
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't parse the list of supported bands");
        } else {
            g_simple_async_result_set_op_res_gpointer (simple,
                                                       mm_bands,
                                                       (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_dms_get_band_capabilities_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_bands);

    mm_dbg ("loading band capabilities...");
    qmi_client_dms_get_band_capabilities (QMI_CLIENT_DMS (client),
                                          NULL,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)dms_get_band_capabilities_ready,
                                          result);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
modem_load_current_bands_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
nas_get_rf_band_information_ready (QmiClientNas *client,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    QmiMessageNasGetRfBandInformationOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_rf_band_information_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_get_rf_band_information_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get current band information: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        GArray *mm_bands;
        GArray *info_array = NULL;

        qmi_message_nas_get_rf_band_information_output_get_list (output, &info_array, NULL);

        mm_bands = mm_modem_bands_from_qmi_rf_band_information_array (info_array);

        if (mm_bands->len == 0) {
            g_array_unref (mm_bands);
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't parse the list of current bands");
        } else {
            g_simple_async_result_set_op_res_gpointer (simple,
                                                       mm_bands,
                                                       (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_nas_get_rf_band_information_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
load_bands_get_system_selection_preference_ready (QmiClientNas *client,
                                                  GAsyncResult *res,
                                                  GSimpleAsyncResult *simple)
{
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get system selection preference: ");
        g_simple_async_result_take_error (simple, error);
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
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't parse the list of current bands");
        } else {
            gchar *str;

            str = qmi_nas_band_preference_build_string_from_mask (band_preference_mask);
            mm_dbg ("Bands reported in system selection preference: '%s'", str);
            g_free (str);

            g_simple_async_result_set_op_res_gpointer (simple,
                                                       mm_bands,
                                                       (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_current_bands);

    mm_dbg ("loading current bands...");

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Introduced in NAS 1.19 */
    if (qmi_client_check_version (ctx->client, 1, 19)) {
        qmi_client_nas_get_rf_band_information (QMI_CLIENT_NAS (client),
                                                NULL,
                                                5,
                                                NULL,
                                                (GAsyncReadyCallback)nas_get_rf_band_information_ready,
                                                result);
        return;
    }
#endif

    qmi_client_nas_get_system_selection_preference (
        QMI_CLIENT_NAS (client),
        NULL, /* no input */
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_bands_get_system_selection_preference_ready,
        result);
}

/*****************************************************************************/
/* Set bands (Modem interface) */

static gboolean
set_bands_finish (MMIfaceModem *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
bands_set_system_selection_preference_ready (QmiClientNas *client,
                                             GAsyncResult *res,
                                             GSimpleAsyncResult *simple)
{
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set system selection preference: ");
        g_simple_async_result_take_error (simple, error);

    } else
        /* Good! TODO: do we really need to wait for the indication? */
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
set_bands (MMIfaceModem *_self,
           GArray *bands_array,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    QmiMessageNasSetSystemSelectionPreferenceInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    QmiNasBandPreference qmi_bands = 0;
    QmiNasLteBandPreference qmi_lte_bands = 0;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_bands);

    /* Handle ANY separately */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        if (!self->priv->supported_bands) {
            g_simple_async_result_set_error (result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Cannot handle 'ANY' if supported bands are unknown");
            g_simple_async_result_complete_in_idle (result);
            g_object_unref (result);
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
        result);
    qmi_message_nas_set_system_selection_preference_input_unref (input);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static MMModemMode
modem_load_supported_modes_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return (MMModemMode)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                              G_SIMPLE_ASYNC_RESULT (res)));
}

static void
modem_load_supported_modes (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMModemMode mode;

    /* For QMI-powered modems, it is safe to assume they do 2G and 3G */
    mode = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

    /* Then, if the modem has LTE caps, it does 4G */
    if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
            mode |= MM_MODEM_MODE_4G;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_modes);
    g_simple_async_result_set_op_res_gpointer (result,
                                               GUINT_TO_POINTER (mode),
                                               NULL);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load signal quality (Modem interface) */

/* Limit the value betweeen [-113,-51] and scale it to a percentage */
#define STRENGTH_TO_QUALITY(strength)                                   \
    (guint8)(100 - ((CLAMP (strength, -113, -51) + 51) * 100 / (-113 + 51)))

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClient *client;
    GSimpleAsyncResult *result;
} LoadSignalQualityContext;

static void
load_signal_quality_context_complete_and_free (LoadSignalQualityContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static guint
load_signal_quality_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

#if defined WITH_NEWEST_QMI_COMMANDS

static gint8
signal_info_get_quality (MMBroadbandModemQmi *self,
                         QmiMessageNasGetSignalInfoOutput *output)
{
    gint8 rssi_max = 0;
    gint8 rssi;
    guint8 quality;

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. */

    if (qmi_message_nas_get_signal_info_output_get_cdma_signal_strength (output, &rssi, NULL, NULL)) {
        mm_dbg ("RSSI (CDMA): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_message_nas_get_signal_info_output_get_hdr_signal_strength (output, &rssi, NULL, NULL, NULL, NULL)) {
        mm_dbg ("RSSI (HDR): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_message_nas_get_signal_info_output_get_gsm_signal_strength (output, &rssi, NULL)) {
        mm_dbg ("RSSI (GSM): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_message_nas_get_signal_info_output_get_wcdma_signal_strength (output, &rssi, NULL, NULL)) {
        mm_dbg ("RSSI (WCDMA): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_message_nas_get_signal_info_output_get_lte_signal_strength (output, &rssi, NULL, NULL, NULL, NULL)) {
        mm_dbg ("RSSI (LTE): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    /* This RSSI comes as negative dBms */
    quality = STRENGTH_TO_QUALITY (rssi_max);

    mm_dbg ("RSSI: %d dBm --> %u%%", rssi_max, quality);
    return quality;
}

static void
get_signal_info_ready (QmiClientNas *client,
                       GAsyncResult *res,
                       LoadSignalQualityContext *ctx)
{
    QmiMessageNasGetSignalInfoOutput *output;
    GError *error = NULL;
    guint quality;

    output = qmi_client_nas_get_signal_info_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        load_signal_quality_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_nas_get_signal_info_output_get_result (output, &error)) {
        qmi_message_nas_get_signal_info_output_unref (output);
        g_simple_async_result_take_error (ctx->result, error);
        load_signal_quality_context_complete_and_free (ctx);
        return;
    }

    quality = signal_info_get_quality (ctx->self, output);

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        GUINT_TO_POINTER (quality),
        NULL);

    qmi_message_nas_get_signal_info_output_unref (output);
    load_signal_quality_context_complete_and_free (ctx);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
signal_strength_get_quality_and_access_tech (MMBroadbandModemQmi *self,
                                             QmiMessageNasGetSignalStrengthOutput *output,
                                             guint8 *o_quality,
                                             MMModemAccessTechnology *o_act)
{
    GArray *array = NULL;
    gint8 signal_max;
    QmiNasRadioInterface main_interface;
    MMModemAccessTechnology act;
    guint8 quality;

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. */

    /* The mandatory one is always present */
    qmi_message_nas_get_signal_strength_output_get_signal_strength (output, &signal_max, &main_interface, NULL);
    mm_dbg ("Signal strength (%s): %d dBm",
            qmi_nas_radio_interface_get_string (main_interface),
            signal_max);

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

            signal_max = MAX (element->strength, signal_max);

            act |= mm_modem_access_technology_from_qmi_radio_interface (element->radio_interface);
        }
    }

    /* This signal strength comes as negative dBms */
    quality = STRENGTH_TO_QUALITY (signal_max);

    mm_dbg ("Signal strength: %d dBm --> %u%%", signal_max, quality);

    *o_quality = quality;
    *o_act = act;
}

static void
get_signal_strength_ready (QmiClientNas *client,
                           GAsyncResult *res,
                           LoadSignalQualityContext *ctx)
{
    QmiMessageNasGetSignalStrengthOutput *output;
    GError *error = NULL;
    guint8 quality = 0;
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    output = qmi_client_nas_get_signal_strength_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        load_signal_quality_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_nas_get_signal_strength_output_get_result (output, &error)) {
        qmi_message_nas_get_signal_strength_output_unref (output);
        g_simple_async_result_take_error (ctx->result, error);
        load_signal_quality_context_complete_and_free (ctx);
        return;
    }

    signal_strength_get_quality_and_access_tech (ctx->self, output, &quality, &act);

    /* We update the access technologies directly here when loading signal
     * quality. It goes a bit out of context, but we can do it nicely */
    mm_iface_modem_update_access_technologies (
        MM_IFACE_MODEM (ctx->self),
        act,
        (MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK | MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK));

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        GUINT_TO_POINTER (quality),
        NULL);

    qmi_message_nas_get_signal_strength_output_unref (output);
    load_signal_quality_context_complete_and_free (ctx);
}

static void
load_signal_quality (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    LoadSignalQualityContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_new0 (LoadSignalQualityContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_signal_quality);

    mm_dbg ("loading signal quality...");

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (ctx->client, 1, 8)) {
        qmi_client_nas_get_signal_info (QMI_CLIENT_NAS (ctx->client),
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_signal_info_ready,
                                        ctx);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    qmi_client_nas_get_signal_strength (QMI_CLIENT_NAS (ctx->client),
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_signal_strength_ready,
                                        ctx);
}

/*****************************************************************************/
/* Powering up the modem (Modem interface) */

static gboolean
modem_power_up_down_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_set_operating_mode_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GSimpleAsyncResult *simple)
{
    QmiMessageDmsSetOperatingModeOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output) {
        if (g_error_matches (error,
                             QMI_CORE_ERROR,
                             QMI_CORE_ERROR_UNSUPPORTED)) {
            mm_dbg ("Device doesn't support operating mode setting. Ignoring power up/down");
            g_simple_async_result_set_op_res_gboolean (simple, TRUE);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "QMI operation failed: ");
            g_simple_async_result_take_error (simple, error);
        }
    } else if (!qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set operating mode: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_set_operating_mode_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
common_power_up_down (MMIfaceModem *self,
                      QmiDmsOperatingMode mode,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    QmiMessageDmsSetOperatingModeInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    GError *error = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_power_up_down);

    input = qmi_message_dms_set_operating_mode_input_new ();
    if (!qmi_message_dms_set_operating_mode_input_set_mode (
            input,
            mode,
            &error)) {
        qmi_message_dms_set_operating_mode_input_unref (input);
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_dbg ("Setting device operating mode...");
    qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (client),
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)dms_set_operating_mode_ready,
                                       result);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    common_power_up_down (self,
                          QMI_DMS_OPERATING_MODE_LOW_POWER,
                          callback,
                          user_data);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    common_power_up_down (self,
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
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_POWER_STATE_UNKNOWN;

    return (MMModemPowerState)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
dms_get_operating_mode_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetOperatingModeOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_operating_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_operating_mode_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get operating mode: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        QmiDmsOperatingMode mode = QMI_DMS_OPERATING_MODE_UNKNOWN;

        qmi_message_dms_get_operating_mode_output_get_mode (output, &mode, NULL);

        switch (mode) {
        case QMI_DMS_OPERATING_MODE_ONLINE:
            g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (MM_MODEM_POWER_STATE_ON), NULL);
            break;
        case QMI_DMS_OPERATING_MODE_LOW_POWER:
        case QMI_DMS_OPERATING_MODE_PERSISTENT_LOW_POWER:
        case QMI_DMS_OPERATING_MODE_MODE_ONLY_LOW_POWER:
            g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (MM_MODEM_POWER_STATE_LOW), NULL);
            break;
        case QMI_DMS_OPERATING_MODE_OFFLINE:
            g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (MM_MODEM_POWER_STATE_OFF), NULL);
            break;
        default:
            g_simple_async_result_set_error (simple,
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

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_power_state (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_power_state);

    mm_dbg ("Getting device operating mode...");
    qmi_client_dms_set_operating_mode (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)dms_get_operating_mode_ready,
                                       result);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMSim *
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
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* Factory reset (Modem interface) */

static gboolean
modem_factory_reset_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_restore_factory_defaults_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    GSimpleAsyncResult *simple)
{
    QmiMessageDmsRestoreFactoryDefaultsOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_restore_factory_defaults_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_restore_factory_defaults_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't restore factory defaults: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_restore_factory_defaults_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_factory_reset (MMIfaceModem *self,
                     const gchar *code,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiMessageDmsRestoreFactoryDefaultsInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    GError *error = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_factory_reset);

    input = qmi_message_dms_restore_factory_defaults_input_new ();
    if (!qmi_message_dms_restore_factory_defaults_input_set_service_programming_code (
            input,
            code,
            &error)) {
        qmi_message_dms_restore_factory_defaults_input_unref (input);
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_dbg ("performing a factory reset...");
    qmi_client_dms_restore_factory_defaults (QMI_CLIENT_DMS (client),
                                             input,
                                             10,
                                             NULL,
                                             (GAsyncReadyCallback)dms_restore_factory_defaults_ready,
                                             result);
}

/*****************************************************************************/
/* Load allowed modes (Modem interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *client;
    GSimpleAsyncResult *result;
    gboolean run_get_system_selection_preference;
    gboolean run_get_technology_preference;
} LoadAllowedModesContext;

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadAllowedModesResult;

static void
load_allowed_modes_context_complete_and_free (LoadAllowedModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadAllowedModesResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void load_allowed_modes_context_step (LoadAllowedModesContext *ctx);

static void
get_technology_preference_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 LoadAllowedModesContext *ctx)
{
    LoadAllowedModesResult *result = NULL;
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

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
            result = g_new (LoadAllowedModesResult, 1);
            result->allowed = allowed;
            result->preferred = MM_MODEM_MODE_NONE;
        }
    }

    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);

    if (!result) {
        ctx->run_get_technology_preference = FALSE;
        load_allowed_modes_context_step (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        result,
        (GDestroyNotify)g_free);
    load_allowed_modes_context_complete_and_free (ctx);
}

static void
allowed_modes_get_system_selection_preference_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     LoadAllowedModesContext *ctx)
{
    LoadAllowedModesResult *result = NULL;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;
    QmiNasRatModePreference mode_preference_mask = 0;

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
            result = g_new (LoadAllowedModesResult, 1);
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
        load_allowed_modes_context_step (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        result,
        (GDestroyNotify)g_free);
    load_allowed_modes_context_complete_and_free (ctx);
}

static void
load_allowed_modes_context_step (LoadAllowedModesContext *ctx)
{
    if (ctx->run_get_system_selection_preference) {
        qmi_client_nas_get_system_selection_preference (
            ctx->client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)allowed_modes_get_system_selection_preference_ready,
            ctx);
        return;
    }

    if (ctx->run_get_technology_preference) {
        qmi_client_nas_get_technology_preference (
            ctx->client,
            NULL, /* no input */
            5,
            NULL, /* cancellable */
            (GAsyncReadyCallback)get_technology_preference_ready,
            ctx);
        return;
    }

    g_simple_async_result_set_error (
        ctx->result,
        MM_CORE_ERROR,
        MM_CORE_ERROR_UNSUPPORTED,
        "Loading allowed modes is not supported by this device");
    load_allowed_modes_context_complete_and_free (ctx);
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    LoadAllowedModesContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_new0 (LoadAllowedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_allowed_modes);

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_get_system_selection_preference = qmi_client_check_version (client, 1, 1);

    /* Technology preference introduced in NAS 1.0, so always available */
    ctx->run_get_technology_preference = TRUE;

    load_allowed_modes_context_step (ctx);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *client;
    GSimpleAsyncResult *result;
    MMModemMode allowed;
    MMModemMode preferred;
    gboolean run_set_system_selection_preference;
    gboolean run_set_technology_preference;
} SetAllowedModesContext;

static void
set_allowed_modes_context_complete_and_free (SetAllowedModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void set_allowed_modes_context_step (SetAllowedModesContext *ctx);

static void
set_technology_preference_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 SetAllowedModesContext *ctx)
{
    QmiMessageNasSetTechnologyPreferenceOutput *output = NULL;
    GError *error = NULL;

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
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_allowed_modes_context_complete_and_free (ctx);
        qmi_message_nas_set_technology_preference_output_unref (output);
        return;
    }

    ctx->run_set_technology_preference = FALSE;
    set_allowed_modes_context_step (ctx);
}

static void
allowed_modes_set_system_selection_preference_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     SetAllowedModesContext *ctx)
{
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError *error = NULL;

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
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_allowed_modes_context_complete_and_free (ctx);
        qmi_message_nas_set_system_selection_preference_output_unref (output);
        return;
    }

    /* Try with the deprecated command */
    ctx->run_set_system_selection_preference = FALSE;
    set_allowed_modes_context_step (ctx);
}

static void
set_allowed_modes_context_step (SetAllowedModesContext *ctx)
{
    if (ctx->run_set_system_selection_preference) {
        QmiMessageNasSetSystemSelectionPreferenceInput *input;
        QmiNasRatModePreference pref;

        pref = mm_modem_mode_to_qmi_rat_mode_preference (ctx->allowed,
                                                         mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self)),
                                                         mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)));
        if (!pref) {
            gchar *str;

            str = mm_modem_mode_build_string_from_mask (ctx->allowed);
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Unhandled allowed mode setting: '%s'",
                                             str);
            g_free (str);
            set_allowed_modes_context_complete_and_free (ctx);
            return;
        }

        input = qmi_message_nas_set_system_selection_preference_input_new ();
        qmi_message_nas_set_system_selection_preference_input_set_mode_preference (input, pref, NULL);

        /* Only set acquisition order preference if both 2G and 3G given as allowed */
        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->self)) &&
            (ctx->allowed & (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G))) {
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
            ctx);
        qmi_message_nas_set_system_selection_preference_input_unref (input);
        return;
    }

    if (ctx->run_set_technology_preference) {
        QmiMessageNasSetTechnologyPreferenceInput *input;
        QmiNasRadioTechnologyPreference pref;

        if (ctx->preferred != MM_MODEM_MODE_NONE) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Cannot set specific preferred mode");
            set_allowed_modes_context_complete_and_free (ctx);
            return;
        }

        pref = mm_modem_mode_to_qmi_radio_technology_preference (ctx->allowed,
                                                                 mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->self)));
        if (!pref) {
            gchar *str;

            str = mm_modem_mode_build_string_from_mask (ctx->allowed);
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Unhandled allowed mode setting: '%s'",
                                             str);
            g_free (str);
            set_allowed_modes_context_complete_and_free (ctx);
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
            ctx);
        qmi_message_nas_set_technology_preference_input_unref (input);
        return;
    }

    g_simple_async_result_set_error (
        ctx->result,
        MM_CORE_ERROR,
        MM_CORE_ERROR_UNSUPPORTED,
        "Setting allowed modes is not supported by this device");
    set_allowed_modes_context_complete_and_free (ctx);
}

static void
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    SetAllowedModesContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_new0 (SetAllowedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_allowed_modes);
    ctx->allowed = allowed;
    ctx->preferred = preferred;

    /* System selection preference introduced in NAS 1.1 */
    ctx->run_set_system_selection_preference = qmi_client_check_version (client, 1, 1);

    /* Technology preference introduced in NAS 1.0, so always available */
    ctx->run_set_technology_preference = TRUE;

    set_allowed_modes_context_step (ctx);
}

/*****************************************************************************/
/* IMEI loading (3GPP interface) */

static gchar *
modem_3gpp_load_imei_finish (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             GError **error)
{
    gchar *imei;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    imei = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded IMEI: %s", imei);
    return imei;
}

static void
modem_3gpp_load_imei (MMIfaceModem3gpp *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_load_imei);

    if (self->priv->imei)
        g_simple_async_result_set_op_res_gpointer (result,
                                                   self->priv->imei,
                                                   NULL);
    else
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Device doesn't report a valid IMEI");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Facility locks status loading (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    QmiClient *client;
    guint current;
    MMModem3gppFacility facilities;
    MMModem3gppFacility locks;
} LoadEnabledFacilityLocksContext;

static void get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx);

static void
load_enabled_facility_locks_context_complete_and_free (LoadEnabledFacilityLocksContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMModem3gppFacility
modem_3gpp_load_enabled_facility_locks_finish (MMIfaceModem3gpp *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_3GPP_FACILITY_NONE;

    return ((MMModem3gppFacility) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
dms_uim_get_ck_status_ready (QmiClientDms *client,
                             GAsyncResult *res,
                             LoadEnabledFacilityLocksContext *ctx)
{
    gchar *facility_str;
    QmiMessageDmsUimGetCkStatusOutput *output;

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
    get_next_facility_lock_status (ctx);
}

static void
get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx)
{
    guint i;

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
                                              ctx);
            qmi_message_dms_uim_get_ck_status_input_unref (input);
            return;
        }
    }

    /* No more facilities to query, all done */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (ctx->locks),
                                               NULL);
    load_enabled_facility_locks_context_complete_and_free (ctx);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    LoadEnabledFacilityLocksContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_new (LoadEnabledFacilityLocksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_load_enabled_facility_locks);
    /* Set initial list of facilities to query */
    ctx->facilities = (MM_MODEM_3GPP_FACILITY_PH_SIM |
                       MM_MODEM_3GPP_FACILITY_NET_PERS |
                       MM_MODEM_3GPP_FACILITY_NET_SUB_PERS |
                       MM_MODEM_3GPP_FACILITY_PROVIDER_PERS |
                       MM_MODEM_3GPP_FACILITY_CORP_PERS);
    ctx->locks = MM_MODEM_3GPP_FACILITY_NONE;
    ctx->current = 0;

    get_next_facility_lock_status (ctx);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
modem_3gpp_scan_networks_finish (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* We return the GList as it is */
    return (GList *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
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
                        GSimpleAsyncResult *simple)
{
    QmiMessageNasNetworkScanOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_network_scan_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_network_scan_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't scan networks: ");
        g_simple_async_result_take_error (simple, error);
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

        /* We *require* a callback in the async method, as we're not setting a
         * GDestroyNotify callback */
        g_simple_async_result_set_op_res_gpointer (simple, scan_result, NULL);
    }

    if (output)
        qmi_message_nas_network_scan_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    /* We will pass the GList in the GSimpleAsyncResult, so we must
     * ensure that there is a callback so that we get it properly
     * passed to the caller and deallocated afterwards */
    g_assert (callback != NULL);

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_scan_networks);

    mm_dbg ("Scanning networks...");
    qmi_client_nas_network_scan (QMI_CLIENT_NAS (client),
                                 NULL,
                                 100,
                                 NULL,
                                 (GAsyncReadyCallback)nas_network_scan_ready,
                                 result);
}

/*****************************************************************************/
/* Load operator name (3GPP interface) */

static gchar *
modem_3gpp_load_operator_name_finish (MMIfaceModem3gpp *_self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    if (self->priv->current_operator_description)
        return g_strdup (self->priv->current_operator_description);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Current operator description is still unknown");
    return NULL;
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Just finish the async operation */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_load_operator_name);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load operator code (3GPP interface) */

static gchar *
modem_3gpp_load_operator_code_finish (MMIfaceModem3gpp *_self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    if (self->priv->current_operator_id)
        return g_strdup (self->priv->current_operator_id);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Current operator MCC/MNC is still unknown");
    return NULL;
}

static void
modem_3gpp_load_operator_code (MMIfaceModem3gpp *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* Just finish the async operation */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_load_operator_code);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
initiate_network_register_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    QmiMessageNasInitiateNetworkRegisterOutput *output;

    output = qmi_client_nas_initiate_network_register_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_initiate_network_register_output_get_result (output, &error)) {
        /* NOFX is not an error, they actually play pretty well */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        } else {
            g_prefix_error (&error, "Couldn't initiate network register: ");
            g_simple_async_result_take_error (simple, error);
        }
    } else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    if (output)
        qmi_message_nas_initiate_network_register_output_unref (output);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
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
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
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
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   modem_3gpp_register_in_network));

    qmi_message_nas_initiate_network_register_input_unref (input);
}

/*****************************************************************************/
/* Registration checks (3GPP interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *client;
    GSimpleAsyncResult *result;
} Run3gppRegistrationChecksContext;

static void
run_3gpp_registration_checks_context_complete_and_free (Run3gppRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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

        g_free (self->priv->current_operator_description);
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

    /* Get 3GPP location LAC and CI */
    lac = 0;
    cid = 0;
    if (response_output) {
        qmi_message_nas_get_serving_system_output_get_lac_3gpp (response_output, &lac, NULL);
        qmi_message_nas_get_serving_system_output_get_cid_3gpp (response_output, &cid, NULL);
    } else {
        qmi_indication_nas_serving_system_output_get_lac_3gpp (indication_output, &lac, NULL);
        qmi_indication_nas_serving_system_output_get_cid_3gpp (indication_output, &cid, NULL);
    }

    /* Report new registration states */
    mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), mm_cs_registration_state);
    mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), mm_ps_registration_state);
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cid);

    /* Note: don't update access technologies with the ones retrieved here; they
     * are not really the 'current' access technologies */
}

static void
get_serving_system_3gpp_ready (QmiClientNas *client,
                               GAsyncResult *res,
                               Run3gppRegistrationChecksContext *ctx)
{
    QmiMessageNasGetServingSystemOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_serving_system_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        run_3gpp_registration_checks_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_nas_get_serving_system_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get serving system: ");
        g_simple_async_result_take_error (ctx->result, error);
        qmi_message_nas_get_serving_system_output_unref (output);
        run_3gpp_registration_checks_context_complete_and_free (ctx);
        return;
    }

    common_process_serving_system_3gpp (ctx->self, output, NULL);

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    qmi_message_nas_get_serving_system_output_unref (output);
    run_3gpp_registration_checks_context_complete_and_free (ctx);
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
    gboolean apply_cs;
    gboolean apply_ps;

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
        *mm_cs_registration_state = tmp_registration_state;

    if (network_id_valid) {
        *mm_operator_id = g_malloc (7);
        memcpy (*mm_operator_id, mcc, 3);
        if (mnc[2] == 0xFF) {
            memcpy (*mm_operator_id, mnc, 2);
            (*mm_operator_id)[5] = '\0';
        } else {
            memcpy (*mm_operator_id, mnc, 3);
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
                &egprs_support_valid,  &egprs_support,
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
                &egprs_support_valid,  &egprs_support,
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

    ps_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    cs_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    lac = 0;
    cid = 0;
    operator_id = NULL;

    /* Process infos, with the following priority:
     *   LTE > WCDMA > GSM
     * The first one giving results will be the one reported.
     */
    if (!process_lte_info (response_output, indication_output,
                           &cs_registration_state,
                           &ps_registration_state,
                           &lac,
                           &cid,
                           &operator_id) &&
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
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, cid);
}

static void
get_system_info_ready (QmiClientNas *client,
                       GAsyncResult *res,
                       Run3gppRegistrationChecksContext *ctx)
{
    QmiMessageNasGetSystemInfoOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_system_info_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        run_3gpp_registration_checks_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_nas_get_system_info_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get system info: ");
        g_simple_async_result_take_error (ctx->result, error);
        qmi_message_nas_get_system_info_output_unref (output);
        run_3gpp_registration_checks_context_complete_and_free (ctx);
        return;
    }

    common_process_system_info_3gpp (ctx->self, output, NULL);

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    qmi_message_nas_get_system_info_output_unref (output);
    run_3gpp_registration_checks_context_complete_and_free (ctx);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp *self,
                                    gboolean cs_supported,
                                    gboolean ps_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    Run3gppRegistrationChecksContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = g_new0 (Run3gppRegistrationChecksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_run_registration_checks);

#if defined WITH_NEWEST_QMI_COMMANDS
    /* System Info was added in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        qmi_client_nas_get_system_info (ctx->client,
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback)get_system_info_ready,
                                        ctx);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    qmi_client_nas_get_serving_system (ctx->client,
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)get_serving_system_3gpp_ready,
                                       ctx);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events (3GPP interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *client;
    GSimpleAsyncResult *result;
    gboolean enable; /* TRUE for enabling, FALSE for disabling */
} UnsolicitedRegistrationEventsContext;

static void
unsolicited_registration_events_context_complete_and_free (UnsolicitedRegistrationEventsContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static UnsolicitedRegistrationEventsContext *
unsolicited_registration_events_context_new (MMBroadbandModemQmi *self,
                                             QmiClient *client,
                                             gboolean enable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    UnsolicitedRegistrationEventsContext *ctx;

    ctx = g_new0 (UnsolicitedRegistrationEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             unsolicited_registration_events_context_new);
    ctx->enable = FALSE;
    return ctx;
}

static gboolean
modem_3gpp_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                  GAsyncResult *res,
                                                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ri_serving_system_or_system_info_ready (QmiClientNas *client,
                                        GAsyncResult *res,
                                        UnsolicitedRegistrationEventsContext *ctx)
{
    QmiMessageNasRegisterIndicationsOutput *output = NULL;
    GError *error = NULL;

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
    ctx->self->priv->unsolicited_registration_events_enabled = ctx->enable;
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    unsolicited_registration_events_context_complete_and_free (ctx);
}

static void
common_enable_disable_unsolicited_registration_events_serving_system (UnsolicitedRegistrationEventsContext *ctx)
{
    QmiMessageNasRegisterIndicationsInput *input;

    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_serving_system_events (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        ctx);
    qmi_message_nas_register_indications_input_unref (input);
}

#if defined WITH_NEWEST_QMI_COMMANDS
static void
common_enable_disable_unsolicited_registration_events_system_info (UnsolicitedRegistrationEventsContext *ctx)
{
    QmiMessageNasRegisterIndicationsInput *input;

    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_system_info (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        ctx);
    qmi_message_nas_register_indications_input_unref (input);
}
#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                    gboolean cs_supported,
                                                    gboolean ps_supported,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    UnsolicitedRegistrationEventsContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = unsolicited_registration_events_context_new (MM_BROADBAND_MODEM_QMI (self),
                                                       client,
                                                       FALSE,
                                                       callback,
                                                       user_data);

#if defined WITH_NEWEST_QMI_COMMANDS
    /* System Info was added in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        common_enable_disable_unsolicited_registration_events_system_info (ctx);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    /* Ability to explicitly enable/disable serving system indications was
     * added in NAS 1.2 */
    if (qmi_client_check_version (client, 1, 2)) {
        common_enable_disable_unsolicited_registration_events_serving_system (ctx);
        return;
    }

    /* Devices with NAS < 1.2 will just always issue serving system indications */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Device doesn't allow disabling registration events");
    ctx->self->priv->unsolicited_registration_events_enabled = FALSE;
    unsolicited_registration_events_context_complete_and_free (ctx);
}

static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp *self,
                                                   gboolean cs_supported,
                                                   gboolean ps_supported,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    UnsolicitedRegistrationEventsContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    ctx = unsolicited_registration_events_context_new (MM_BROADBAND_MODEM_QMI (self),
                                                       client,
                                                       TRUE,
                                                       callback,
                                                       user_data);

    /* Ability to explicitly enable/disable serving system indications was
     * added in NAS 1.2 */
    if (qmi_client_check_version (client, 1, 2)) {
        common_enable_disable_unsolicited_registration_events_serving_system (ctx);
        return;
    }

    /* Devices with NAS < 1.2 will just always issue serving system indications */
    mm_dbg ("Assuming serving system indications are always enabled");
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    ctx->self->priv->unsolicited_registration_events_enabled = TRUE;
    unsolicited_registration_events_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* Registration checks (CDMA interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientNas *client;
    GSimpleAsyncResult *result;
} RunCdmaRegistrationChecksContext;

static void
run_cdma_registration_checks_context_complete_and_free (RunCdmaRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (RunCdmaRegistrationChecksContext, ctx);
}

static gboolean
modem_cdma_run_registration_checks_finish (MMIfaceModemCdma *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
    gint32 bs_longitude = MM_LOCATION_LONGITUDE_UNKNOWN;
    gint32 bs_latitude = MM_LOCATION_LATITUDE_UNKNOWN;

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
    (longitude != MM_LOCATION_LONGITUDE_UNKNOWN ? \
     (((gdouble)longitude) / 14400.0) :           \
     MM_LOCATION_LONGITUDE_UNKNOWN)
#define QMI_LATITUDE_TO_DEGREES(latitude)         \
    (latitude != MM_LOCATION_LATITUDE_UNKNOWN ?   \
     (((gdouble)latitude) / 14400.0) :            \
     MM_LOCATION_LATITUDE_UNKNOWN)

    mm_iface_modem_location_cdma_bs_update (MM_IFACE_MODEM_LOCATION (self),
                                            QMI_LONGITUDE_TO_DEGREES (bs_longitude),
                                            QMI_LATITUDE_TO_DEGREES (bs_latitude));
}

static void
get_serving_system_cdma_ready (QmiClientNas *client,
                               GAsyncResult *res,
                               RunCdmaRegistrationChecksContext *ctx)
{
    QmiMessageNasGetServingSystemOutput *output;
    GError *error = NULL;

    output = qmi_client_nas_get_serving_system_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        run_cdma_registration_checks_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_nas_get_serving_system_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get serving system: ");
        g_simple_async_result_take_error (ctx->result, error);
        qmi_message_nas_get_serving_system_output_unref (output);
        run_cdma_registration_checks_context_complete_and_free (ctx);
        return;
    }

    common_process_serving_system_cdma (ctx->self, output, NULL);

    qmi_message_nas_get_serving_system_output_unref (output);
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_cdma_registration_checks_context_complete_and_free (ctx);
}

static void
modem_cdma_run_registration_checks (MMIfaceModemCdma *self,
                                    gboolean cdma1x_supported,
                                    gboolean evdo_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    RunCdmaRegistrationChecksContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    /* Setup context */
    ctx = g_slice_new0 (RunCdmaRegistrationChecksContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_cdma_run_registration_checks);

    /* TODO: Run Get System Info in NAS >= 1.8 */

    qmi_client_nas_get_serving_system (ctx->client,
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)get_serving_system_cdma_ready,
                                       ctx);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited registration event handlers
 * (3GPP and CDMA interface) */

static gboolean
common_setup_cleanup_unsolicited_registration_events_finish (MMBroadbandModemQmi *self,
                                                             GAsyncResult *res,
                                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_setup_cleanup_unsolicited_registration_events);

    if (enable == self->priv->unsolicited_registration_events_setup) {
        mm_dbg ("Unsolicited registration events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
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

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
    gchar *meid;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    meid = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded MEID: %s", meid);
    return meid;
}

static void
modem_cdma_load_meid (MMIfaceModemCdma *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_cdma_load_meid);

    if (self->priv->meid)
        g_simple_async_result_set_op_res_gpointer (result,
                                                   self->priv->meid,
                                                   NULL);
    else
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Device doesn't report a valid MEID");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* ESN loading (CDMA interface) */

static gchar *
modem_cdma_load_esn_finish (MMIfaceModemCdma *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *esn;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    esn = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded ESN: %s", esn);
    return esn;
}

static void
modem_cdma_load_esn (MMIfaceModemCdma *_self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_cdma_load_esn);

    if (self->priv->esn)
        g_simple_async_result_set_op_res_gpointer (result,
                                                   self->priv->esn,
                                                   NULL);
    else
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Device doesn't report a valid ESN");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
    MMBroadbandModemQmi *self;
    GSimpleAsyncResult *result;
    QmiClientNas *client;
    gboolean enable;
} EnableUnsolicitedEventsContext;

static void
enable_unsolicited_events_context_complete_and_free (EnableUnsolicitedEventsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
common_enable_disable_unsolicited_events_finish (MMBroadbandModemQmi *self,
                                                 GAsyncResult *res,
                                                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ser_signal_strength_ready (QmiClientNas *client,
                           GAsyncResult *res,
                           EnableUnsolicitedEventsContext *ctx)
{
    QmiMessageNasSetEventReportOutput *output = NULL;
    GError *error = NULL;

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
    ctx->self->priv->unsolicited_events_enabled = ctx->enable;
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enable_unsolicited_events_context_complete_and_free (ctx);
}

static void
common_enable_disable_unsolicited_events_signal_strength (EnableUnsolicitedEventsContext *ctx)
{
    /* The device doesn't really like to have many threshold values, so don't
     * grow this array without checking first */
    static const gint8 thresholds_data[] = { -80, -40, 0, 40, 80 };
    QmiMessageNasSetEventReportInput *input;
    GArray *thresholds;

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
        ctx);
    qmi_message_nas_set_event_report_input_unref (input);
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
ri_signal_info_ready (QmiClientNas *client,
                      GAsyncResult *res,
                      EnableUnsolicitedEventsContext *ctx)
{
    QmiMessageNasRegisterIndicationsOutput *output = NULL;
    GError *error = NULL;

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
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enable_unsolicited_events_context_complete_and_free (ctx);
}

static void
common_enable_disable_unsolicited_events_signal_info (EnableUnsolicitedEventsContext *ctx)
{
    QmiMessageNasRegisterIndicationsInput *input;

    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_signal_info (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_signal_info_ready,
        ctx);
    qmi_message_nas_register_indications_input_unref (input);
}

static void
config_signal_info_ready (QmiClientNas *client,
                          GAsyncResult *res,
                          EnableUnsolicitedEventsContext *ctx)
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
    common_enable_disable_unsolicited_events_signal_info (ctx);
}

static void
common_enable_disable_unsolicited_events_signal_info_config (EnableUnsolicitedEventsContext *ctx)
{
    /* RSSI values go between -105 and -60 for 3GPP technologies,
     * and from -105 to -90 in 3GPP2 technologies (approx). */
    static const gint8 thresholds_data[] = { -100, -97, -95, -92, -90, -85, -80, -75, -70, -65 };
    QmiMessageNasConfigSignalInfoInput *input;
    GArray *thresholds;

    /* Signal info config only to be run when enabling */
    if (!ctx->enable) {
        common_enable_disable_unsolicited_events_signal_info (ctx);
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
        ctx);
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
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_enable_disable_unsolicited_events);

    if (enable == self->priv->unsolicited_events_enabled) {
        mm_dbg ("Unsolicited events already %s; skipping",
                enable ? "enabled" : "disabled");
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    ctx = g_new0 (EnableUnsolicitedEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->enable = enable;
    ctx->result = result;

#if defined WITH_NEWEST_QMI_COMMANDS
    /* Signal info introduced in NAS 1.8 */
    if (qmi_client_check_version (client, 1, 8)) {
        common_enable_disable_unsolicited_events_signal_info_config (ctx);
        return;
    }
#endif /* WITH_NEWEST_QMI_COMMANDS */

    common_enable_disable_unsolicited_events_signal_strength (ctx);
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
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
    }
}

#if defined WITH_NEWEST_QMI_COMMANDS

static void
signal_info_indication_cb (QmiClientNas *client,
                           QmiIndicationNasSignalInfoOutput *output,
                           MMBroadbandModemQmi *self)
{
    gint8 rssi_max = 0;
    gint8 rssi;
    guint8 quality;

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. TODO: When several technologies are in use, if
     * the indication only contains the data of the one which passed a threshold
     * value, we'll need to have an internal cache of per-technology values, in
     * order to report always the one with the maximum value. */

    if (qmi_indication_nas_signal_info_output_get_cdma_signal_strength (output, &rssi, NULL, NULL)) {
        mm_dbg ("RSSI (CDMA): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_indication_nas_signal_info_output_get_hdr_signal_strength (output, &rssi, NULL, NULL, NULL, NULL)) {
        mm_dbg ("RSSI (HDR): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_indication_nas_signal_info_output_get_gsm_signal_strength (output, &rssi, NULL)) {
        mm_dbg ("RSSI (GSM): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_indication_nas_signal_info_output_get_wcdma_signal_strength (output, &rssi, NULL, NULL)) {
        mm_dbg ("RSSI (WCDMA): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    if (qmi_indication_nas_signal_info_output_get_lte_signal_strength (output, &rssi, NULL, NULL, NULL, NULL)) {
        mm_dbg ("RSSI (LTE): %d dBm", rssi);
        rssi = MAX (rssi, rssi_max);
    }

    /* This RSSI comes as negative dBms */
    quality = STRENGTH_TO_QUALITY (rssi_max);

    mm_dbg ("RSSI: %d dBm --> %u%%", rssi_max, quality);

    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

#endif /* WITH_NEWEST_QMI_COMMANDS */

static void
common_setup_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                         gboolean enable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_enable_disable_unsolicited_events);

    if (enable == self->priv->unsolicited_events_setup) {
        mm_dbg ("Unsolicited events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
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

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
    /* no error expected here */
    return g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res));
}

static void
messaging_check_support (MMIfaceModemMessaging *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    gboolean supported;
    MMQmiPort *port;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        messaging_check_support);

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    if (!port)
        supported = FALSE;
    else
        /* If we have support for the WMS client, messaging is supported */
        supported = !!mm_qmi_port_peek_client (port,
                                               QMI_SERVICE_WMS,
                                               MM_QMI_PORT_FLAG_DEFAULT);

    /* We only handle 3GPP messaging (PDU based) currently, so just ignore
     * CDMA-only QMI modems */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self)) && supported) {
        mm_info ("Messaging capabilities supported by this modem, "
                 "but 3GPP2 messaging not supported yet by ModemManager");
        supported = FALSE;
    } else
        mm_dbg ("Messaging capabilities %s by this modem",
                supported ? "supported" : "not supported");

    g_simple_async_result_set_op_res_gboolean (result, supported);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load supported storages (Messaging interface) */

static gboolean
messaging_load_supported_storages_finish (MMIfaceModemMessaging *self,
                                          GAsyncResult *res,
                                          GArray **mem1,
                                          GArray **mem2,
                                          GArray **mem3,
                                          GError **error)
{
    MMSmsStorage supported [2] = { MM_SMS_STORAGE_SM, MM_SMS_STORAGE_ME };

    *mem1 = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), 2),
                                 supported, 2);
    *mem2 = g_array_ref (*mem1);
    *mem3 = g_array_ref (*mem1);

    return TRUE;
}

static void
messaging_load_supported_storages (MMIfaceModemMessaging *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        messaging_load_supported_storages);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Set default storage (Messaging interface) */

static gboolean
messaging_set_default_storage_finish (MMIfaceModemMessaging *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
wms_set_routes_ready (QmiClientWms *client,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    QmiMessageWmsSetRoutesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_wms_set_routes_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_wms_set_routes_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set routes: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_wms_set_routes_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
messaging_set_default_storage (MMIfaceModemMessaging *self,
                               MMSmsStorage storage,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    QmiMessageWmsSetRoutesInput *input;
    GArray *routes_array;
    QmiMessageWmsSetRoutesInputRouteListElement route;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        messaging_set_default_storage);

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
                               result);

    qmi_message_wms_set_routes_input_unref (input);
    g_array_unref (routes_array);
}

/*****************************************************************************/
/* Load initial SMS parts */

typedef struct {
    MMBroadbandModemQmi *self;
    GSimpleAsyncResult *result;
    QmiClientWms *client;
    MMSmsStorage storage;
    GArray *message_array;
    guint i;
} LoadInitialSmsPartsContext;

static void
load_initial_sms_parts_context_complete_and_free (LoadInitialSmsPartsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);

    if (ctx->message_array)
        g_array_unref (ctx->message_array);

    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (LoadInitialSmsPartsContext, ctx);
}

static gboolean
load_initial_sms_parts_finish (MMIfaceModemMessaging *self,
                               GAsyncResult *res,
                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void read_next_sms_part (LoadInitialSmsPartsContext *ctx);

static void
add_new_read_sms_part (MMIfaceModemMessaging *self,
                       QmiWmsStorageType storage,
                       guint32 index,
                       QmiWmsMessageTagType tag,
                       QmiWmsMessageFormat format,
                       GArray *data)
{
    switch (format) {
    case QMI_WMS_MESSAGE_FORMAT_CDMA:
        mm_dbg ("Skipping CDMA messages for now...");
        break;
    case QMI_WMS_MESSAGE_FORMAT_MWI:
        mm_dbg ("Don't know how to process 'message waiting indicator' messages");
        break;
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT:
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_BROADCAST: {
        MMSmsPart *part;
        GError *error = NULL;

        part = mm_sms_part_new_from_binary_pdu (index,
                                                (guint8 *)data->data,
                                                data->len,
                                                &error);
        if (part) {
            mm_dbg ("Correctly parsed PDU (%d)",
                    index);
            mm_iface_modem_messaging_take_part (self,
                                                part,
                                                mm_sms_state_from_qmi_message_tag (tag),
                                                mm_sms_storage_from_qmi_storage_type (storage));
        } else {
            /* Don't treat the error as critical */
            mm_dbg ("Error parsing PDU (%d): %s",
                    index,
                    error->message);
            g_error_free (error);
        }

        break;
    }
    default:
        mm_dbg ("Unhandled message format '%u'", format);
        break;
    }
}

static void
wms_raw_read_ready (QmiClientWms *client,
                    GAsyncResult *res,
                    LoadInitialSmsPartsContext *ctx)
{
    QmiMessageWmsRawReadOutput *output = NULL;
    GError *error = NULL;

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
        add_new_read_sms_part (MM_IFACE_MODEM_MESSAGING (ctx->self),
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
    read_next_sms_part (ctx);
}

static void
read_next_sms_part (LoadInitialSmsPartsContext *ctx)
{
    QmiMessageWmsListMessagesOutputMessageListElement *message;
    QmiMessageWmsRawReadInput *input;

    if (ctx->i >= ctx->message_array->len ||
        !ctx->message_array) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        load_initial_sms_parts_context_complete_and_free (ctx);
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
    qmi_client_wms_raw_read (QMI_CLIENT_WMS (ctx->client),
                             input,
                             3,
                             NULL,
                             (GAsyncReadyCallback)wms_raw_read_ready,
                             ctx);
    qmi_message_wms_raw_read_input_unref (input);
}

static void
wms_list_messages_ready (QmiClientWms *client,
                         GAsyncResult *res,
                         LoadInitialSmsPartsContext *ctx)
{
    QmiMessageWmsListMessagesOutput *output = NULL;
    GError *error = NULL;
    GArray *message_array;

    output = qmi_client_wms_list_messages_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        load_initial_sms_parts_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_wms_list_messages_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't list messages: ");
        g_simple_async_result_take_error (ctx->result, error);
        load_initial_sms_parts_context_complete_and_free (ctx);
        qmi_message_wms_list_messages_output_unref (output);
        return;
    }

    qmi_message_wms_list_messages_output_get_message_list (
        output,
        &message_array,
        NULL);

    /* Keep a reference to the array ourselves */
    ctx->message_array = g_array_ref (message_array);

    qmi_message_wms_list_messages_output_unref (output);

    /* Start reading parts */
    ctx->i = 0;
    read_next_sms_part (ctx);
}

static void
load_initial_sms_parts (MMIfaceModemMessaging *self,
                        MMSmsStorage storage,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    LoadInitialSmsPartsContext *ctx;
    QmiClient *client = NULL;
    QmiMessageWmsListMessagesInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (LoadInitialSmsPartsContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->storage = storage;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_initial_sms_parts);

    mm_dbg ("loading messages from storage '%s'...",
            mm_sms_storage_get_string (storage));

    /* Request to list messages in a given storage */
    input = qmi_message_wms_list_messages_input_new ();
    qmi_message_wms_list_messages_input_set_storage_type (
        input,
        mm_sms_storage_to_qmi_storage_type (storage),
        NULL);
    qmi_message_wms_list_messages_input_set_message_mode (
        input,
        QMI_WMS_MESSAGE_MODE_GSM_WCDMA,
        NULL);
    qmi_client_wms_list_messages (QMI_CLIENT_WMS (ctx->client),
                                  input,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback)wms_list_messages_ready,
                                  ctx);
    qmi_message_wms_list_messages_input_unref (input);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (Messaging interface) */

typedef struct {
    MMIfaceModemMessaging *self;
    QmiClientWms *client;
    QmiWmsStorageType storage;
    guint32 memory_index;
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
messaging_setup_cleanup_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                                   GAsyncResult *res,
                                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
common_setup_cleanup_messaging_unsolicited_events (MMBroadbandModemQmi *self,
                                                   gboolean enable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_setup_cleanup_messaging_unsolicited_events);

    if (enable == self->priv->messaging_unsolicited_events_setup) {
        mm_dbg ("Messaging unsolicited events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
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

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
messaging_cleanup_unsolicited_events (MMIfaceModemMessaging *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    common_setup_cleanup_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                       FALSE,
                                                       callback,
                                                       user_data);
}

static void
messaging_setup_unsolicited_events (MMIfaceModemMessaging *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    common_setup_cleanup_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                       TRUE,
                                                       callback,
                                                       user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (Messaging interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    GSimpleAsyncResult *result;
    QmiClientWms *client;
    gboolean enable;
} EnableMessagingUnsolicitedEventsContext;

static void
enable_messaging_unsolicited_events_context_complete_and_free (EnableMessagingUnsolicitedEventsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
messaging_enable_disable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ser_messaging_indicator_ready (QmiClientWms *client,
                               GAsyncResult *res,
                               EnableMessagingUnsolicitedEventsContext *ctx)
{
    QmiMessageWmsSetEventReportOutput *output = NULL;
    GError *error = NULL;

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
    ctx->self->priv->messaging_unsolicited_events_enabled = ctx->enable;
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enable_messaging_unsolicited_events_context_complete_and_free (ctx);
}

static void
common_enable_disable_messaging_unsolicited_events (MMBroadbandModemQmi *self,
                                                    gboolean enable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    EnableMessagingUnsolicitedEventsContext *ctx;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    QmiMessageWmsSetEventReportInput *input;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        common_enable_disable_messaging_unsolicited_events);

    if (enable == self->priv->messaging_unsolicited_events_enabled) {
        mm_dbg ("Messaging unsolicited events already %s; skipping",
                enable ? "enabled" : "disabled");
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    ctx = g_new0 (EnableMessagingUnsolicitedEventsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->enable = enable;
    ctx->result = result;

    input = qmi_message_wms_set_event_report_input_new ();

    qmi_message_wms_set_event_report_input_set_new_mt_message_indicator (
        input,
        ctx->enable,
        NULL);
    qmi_client_wms_set_event_report (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_messaging_indicator_ready,
        ctx);
    qmi_message_wms_set_event_report_input_unref (input);
}

static void
messaging_disable_unsolicited_events (MMIfaceModemMessaging *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    common_enable_disable_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                        FALSE,
                                                        callback,
                                                        user_data);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    common_enable_disable_messaging_unsolicited_events (MM_BROADBAND_MODEM_QMI (self),
                                                        TRUE,
                                                        callback,
                                                        user_data);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMSms *
messaging_create_sms (MMIfaceModemMessaging *self)
{
    return mm_sms_qmi_new (MM_BASE_MODEM (self));
}

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

static MMModemLocationSource
location_load_capabilities_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCATION_SOURCE_NONE;

    return (MMModemLocationSource) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                                         G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMModemLocationSource sources;
    GError *error = NULL;
    MMQmiPort *port;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    port = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));

    /* Now our own checks */

    /* If we have support for the PDS client, GPS location is supported */
    if (port && mm_qmi_port_peek_client (port,
                                         QMI_SERVICE_PDS,
                                         MM_QMI_PORT_FLAG_DEFAULT))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW);

    /* If the modem is CDMA, we have support for CDMA BS location */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        sources |= MM_MODEM_LOCATION_SOURCE_CDMA_BS;

    /* So we're done, complete */
    g_simple_async_result_set_op_res_gpointer (simple,
                                               GUINT_TO_POINTER (sources),
                                               NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        location_load_capabilities);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (
        self,
        (GAsyncReadyCallback)parent_load_capabilities_ready,
        result);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientPds *client;
    GSimpleAsyncResult *result;
} DisableLocationGatheringContext;

static void
disable_location_gathering_context_complete_and_free (DisableLocationGatheringContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->client)
        g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (DisableLocationGatheringContext, ctx);
}

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
gps_service_state_stop_ready (QmiClientPds *client,
                              GAsyncResult *res,
                              DisableLocationGatheringContext *ctx)
{
    QmiMessagePdsSetGpsServiceStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        disable_location_gathering_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_simple_async_result_take_error (ctx->result, error);
            disable_location_gathering_context_complete_and_free (ctx);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }

        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    mm_dbg ("Removing location event report indication handling");
    g_assert (ctx->self->priv->location_event_report_indication_id != 0);
    g_signal_handler_disconnect (client, ctx->self->priv->location_event_report_indication_id);
    ctx->self->priv->location_event_report_indication_id = 0;

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disable_location_gathering_context_complete_and_free (ctx);
}

static void
disable_location_gathering (MMIfaceModemLocation *self,
                            MMModemLocationSource source,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    DisableLocationGatheringContext *ctx;
    QmiClient *client = NULL;
    gboolean stop_gps = FALSE;
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        disable_location_gathering);

    /* Nothing to be done to disable 3GPP or CDMA locations */
    if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI ||
        source == MM_MODEM_LOCATION_SOURCE_CDMA_BS) {
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_PDS, &client,
                            callback, user_data)) {
        g_object_unref (result);
        return;
    }

    ctx = g_slice_new0 (DisableLocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = result;

    /* Only stop GPS engine if no GPS-related sources enabled */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                  MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        ctx->self->priv->enabled_sources &= ~source;

        if (!(ctx->self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_RAW)))
            stop_gps = TRUE;
    }

    if (stop_gps) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, FALSE, NULL);
        qmi_client_pds_set_gps_service_state (
            ctx->client,
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)gps_service_state_stop_ready,
            ctx);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    /* If still some GPS needed, just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disable_location_gathering_context_complete_and_free (ctx);
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
    MMBroadbandModemQmi *self;
    QmiClientPds *client;
    GSimpleAsyncResult *result;
    MMModemLocationSource source;
} EnableLocationGatheringContext;

static void
enable_location_gathering_context_complete_and_free (EnableLocationGatheringContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->client)
        g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_slice_free (EnableLocationGatheringContext, ctx);
}

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ser_location_ready (QmiClientPds *client,
                    GAsyncResult *res,
                    EnableLocationGatheringContext *ctx)
{
    QmiMessagePdsSetEventReportOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_event_report_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        enable_location_gathering_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_pds_set_event_report_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set event report: ");
        g_simple_async_result_take_error (ctx->result, error);
        enable_location_gathering_context_complete_and_free (ctx);
        qmi_message_pds_set_event_report_output_unref (output);
        return;
    }

    qmi_message_pds_set_event_report_output_unref (output);

    mm_dbg ("Adding location event report indication handling");
    g_assert (ctx->self->priv->location_event_report_indication_id == 0);
    ctx->self->priv->location_event_report_indication_id =
        g_signal_connect (client,
                          "event-report",
                          G_CALLBACK (location_event_report_indication_cb),
                          ctx->self);

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enable_location_gathering_context_complete_and_free (ctx);
}

static void
auto_tracking_state_start_ready (QmiClientPds *client,
                                 GAsyncResult *res,
                                 EnableLocationGatheringContext *ctx)
{
    QmiMessagePdsSetEventReportInput *input;
    QmiMessagePdsSetAutoTrackingStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_auto_tracking_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        enable_location_gathering_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_pds_set_auto_tracking_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set auto-tracking state: ");
            g_simple_async_result_take_error (ctx->result, error);
            enable_location_gathering_context_complete_and_free (ctx);
            qmi_message_pds_set_auto_tracking_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_auto_tracking_state_output_unref (output);

    /* Only gather standard NMEA traces */
    input = qmi_message_pds_set_event_report_input_new ();
    qmi_message_pds_set_event_report_input_set_nmea_position_reporting (input, TRUE, NULL);
    qmi_client_pds_set_event_report (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_location_ready,
        ctx);
    qmi_message_pds_set_event_report_input_unref (input);
}

static void
gps_service_state_start_ready (QmiClientPds *client,
                               GAsyncResult *res,
                               EnableLocationGatheringContext *ctx)
{
    QmiMessagePdsSetAutoTrackingStateInput *input;
    QmiMessagePdsSetGpsServiceStateOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        enable_location_gathering_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error,
                              QMI_PROTOCOL_ERROR,
                              QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_simple_async_result_take_error (ctx->result, error);
            enable_location_gathering_context_complete_and_free (ctx);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    /* Enable auto-tracking for a continuous fix */
    input = qmi_message_pds_set_auto_tracking_state_input_new ();
    qmi_message_pds_set_auto_tracking_state_input_set_state (input, TRUE, NULL);
    qmi_client_pds_set_auto_tracking_state (
        ctx->client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)auto_tracking_state_start_ready,
        ctx);
    qmi_message_pds_set_auto_tracking_state_input_unref (input);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        EnableLocationGatheringContext *ctx)
{
    gboolean start_gps = FALSE;
    GError *error = NULL;

    if (!iface_modem_location_parent->enable_location_gathering_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enable_location_gathering_context_complete_and_free (ctx);
        return;
    }

    /* Now our own enabling */

    /* CDMA modems need to re-run registration checks when enabling the CDMA BS
     * location source, so that we get up to date BS location information.
     * Note that we don't care for when the registration checks get finished.
     */
    if (ctx->source == MM_MODEM_LOCATION_SOURCE_CDMA_BS &&
        mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
        /* Reload registration to get LAC/CI */
        mm_iface_modem_cdma_run_registration_checks (MM_IFACE_MODEM_CDMA (self), NULL, NULL);
    }

    /* NMEA and RAW are both enabled in the same way */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        /* Only start GPS engine if not done already */
        if (!(ctx->self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_RAW)))
            start_gps = TRUE;
        ctx->self->priv->enabled_sources |= ctx->source;
    }

    if (start_gps) {
        QmiMessagePdsSetGpsServiceStateInput *input;
        QmiClient *client;

        client = peek_qmi_client (ctx->self, QMI_SERVICE_PDS, &error);
        if (!client) {
            g_simple_async_result_take_error (ctx->result, error);
            enable_location_gathering_context_complete_and_free (ctx);
            return;
        }

        /* Keep a ref around */
        ctx->client = g_object_ref (client);

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, TRUE, NULL);
        qmi_client_pds_set_gps_service_state (
            ctx->client,
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)gps_service_state_start_ready,
            ctx);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enable_location_gathering_context_complete_and_free (ctx);
}

static void
enable_location_gathering (MMIfaceModemLocation *self,
                           MMModemLocationSource source,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    EnableLocationGatheringContext *ctx;

    ctx = g_slice_new0 (EnableLocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enable_location_gathering);
    ctx->source = source;

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        ctx);
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
    MMBroadbandModemQmi *self;
    QmiClientDms *client;
    GSimpleAsyncResult *result;
    GList *pairs;
    GList *l;
} FirmwareCheckSupportContext;

static void
firmware_check_support_context_complete_and_free (FirmwareCheckSupportContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_list_free_full (ctx->pairs, (GDestroyNotify)firmware_pair_free);
    g_object_unref (ctx->self);
    g_object_unref (ctx->client);
    g_slice_free (FirmwareCheckSupportContext, ctx);
}

static gboolean
firmware_check_support_finish (MMIfaceModemFirmware *self,
                               GAsyncResult *res,
                               GError **error)
{
    /* Never fails, just says TRUE or FALSE */
    return g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res));
}

static void get_next_image_info (FirmwareCheckSupportContext *ctx);

static void
get_pri_image_info_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          FirmwareCheckSupportContext *ctx)
{
    QmiMessageDmsGetStoredImageInfoOutput *output;
    GError *error = NULL;
    FirmwarePair *current;

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
        ctx->self->priv->firmware_list = g_list_append (ctx->self->priv->firmware_list,
                                                        firmware);

        /* If this is is also the current image running, keep it */
        if (current->current) {
            if (ctx->self->priv->current_firmware)
                mm_warn ("A current firmware is already set (%s), not setting '%s' as current",
                         mm_firmware_properties_get_unique_id (ctx->self->priv->current_firmware),
                         current->build_id);
            else
                ctx->self->priv->current_firmware = g_object_ref (firmware);

        }

        qmi_message_dms_get_stored_image_info_output_unref (output);
    }

    /* Go on to the next one */
    ctx->l = g_list_next (ctx->l);
    get_next_image_info (ctx);
}

static void
get_next_image_info (FirmwareCheckSupportContext *ctx)
{
    QmiMessageDmsGetStoredImageInfoInputImage image_id;
    QmiMessageDmsGetStoredImageInfoInput *input;
    FirmwarePair *current;

    if (!ctx->l) {
        /* We're done */

        if (!ctx->self->priv->firmware_list) {
            mm_warn ("No valid firmware images listed. "
                     "Assuming firmware unsupported.");
            g_simple_async_result_set_op_res_gboolean (ctx->result, FALSE);
        } else
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        firmware_check_support_context_complete_and_free (ctx);
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
                                          ctx);
    qmi_message_dms_get_stored_image_info_input_unref (input);
}

static void
list_stored_images_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          FirmwareCheckSupportContext *ctx)
{
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
        g_simple_async_result_set_op_res_gboolean (ctx->result, FALSE);
        firmware_check_support_context_complete_and_free (ctx);
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
        g_simple_async_result_set_op_res_gboolean (ctx->result, FALSE);
        firmware_check_support_context_complete_and_free (ctx);
        qmi_message_dms_list_stored_images_output_unref (output);
        return;
    }

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

            if (g_str_equal (subimage_pri->build_id, subimage_modem->build_id)) {
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
        g_simple_async_result_set_op_res_gboolean (ctx->result, FALSE);
        firmware_check_support_context_complete_and_free (ctx);
        qmi_message_dms_list_stored_images_output_unref (output);
        return;
    }

    /* Firmware is supported; now keep on loading info for each image and cache it */
    qmi_message_dms_list_stored_images_output_unref (output);

    ctx->l = ctx->pairs;
    get_next_image_info (ctx);
}

static void
firmware_check_support (MMIfaceModemFirmware *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    FirmwareCheckSupportContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (FirmwareCheckSupportContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             firmware_check_support);

    mm_dbg ("loading firmware images...");
    qmi_client_dms_list_stored_images (QMI_CLIENT_DMS (client),
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)list_stored_images_ready,
                                       ctx);
}

/*****************************************************************************/
/* Load firmware list (Firmware interface) */

static GList *
firmware_load_list_finish (MMIfaceModemFirmware *self,
                           GAsyncResult *res,
                           GError **error)
{
    return (GList *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
firmware_load_list (MMIfaceModemFirmware *_self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GSimpleAsyncResult *result;
    GList *dup;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        firmware_load_list);

    /* We'll return the new list of new references we create here */
    dup = g_list_copy (self->priv->firmware_list);
    g_list_foreach (dup, (GFunc)g_object_ref, NULL);

    g_simple_async_result_set_op_res_gpointer (result, dup, NULL);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Load current firmware (Firmware interface) */

static MMFirmwareProperties *
firmware_load_current_finish (MMIfaceModemFirmware *self,
                              GAsyncResult *res,
                              GError **error)
{
    return (MMFirmwareProperties *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
firmware_load_current (MMIfaceModemFirmware *_self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        firmware_load_current);

    /* We'll return the reference we create here */
    g_simple_async_result_set_op_res_gpointer (
        result,
        self->priv->current_firmware ? g_object_ref (self->priv->current_firmware) : NULL,
        NULL);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Change current firmware (Firmware interface) */

typedef struct {
    MMBroadbandModemQmi *self;
    QmiClientDms *client;
    GSimpleAsyncResult *result;
    MMFirmwareProperties *firmware;
} FirmwareChangeCurrentContext;

static void
firmware_change_current_context_complete_and_free (FirmwareChangeCurrentContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->client);
    if (ctx->firmware)
        g_object_unref (ctx->firmware);
    g_slice_free (FirmwareChangeCurrentContext, ctx);
}

static gboolean
firmware_change_current_finish (MMIfaceModemFirmware *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
firmware_set_operating_mode_reset_ready (QmiClientDms *client,
                                         GAsyncResult *res,
                                         FirmwareChangeCurrentContext *ctx)
{
    QmiMessageDmsSetOperatingModeOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output ||
        !qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        mm_info ("Modem is being rebooted now");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    if (output)
        qmi_message_dms_set_operating_mode_output_unref (output);

    firmware_change_current_context_complete_and_free (ctx);
}

static void
firmware_set_operating_mode_offline_ready (QmiClientDms *client,
                                           GAsyncResult *res,
                                           FirmwareChangeCurrentContext *ctx)
{
    QmiMessageDmsSetOperatingModeInput *input;
    QmiMessageDmsSetOperatingModeOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        firmware_change_current_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        firmware_change_current_context_complete_and_free (ctx);
        qmi_message_dms_set_operating_mode_output_unref (output);
        return;
    }

    qmi_message_dms_set_operating_mode_output_unref (output);

    /* Now, go into reset mode. This will fully reboot the modem, and the current
     * modem object should get disposed. */
    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, QMI_DMS_OPERATING_MODE_RESET, NULL);
    qmi_client_dms_set_operating_mode (ctx->client,
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)firmware_set_operating_mode_reset_ready,
                                       ctx);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

static void
firmware_select_stored_image_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    FirmwareChangeCurrentContext *ctx)
{
    QmiMessageDmsSetOperatingModeInput *input;
    QmiMessageDmsSetFirmwarePreferenceOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_set_firmware_preference_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        firmware_change_current_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_dms_set_firmware_preference_output_get_result (output, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        firmware_change_current_context_complete_and_free (ctx);
        qmi_message_dms_set_firmware_preference_output_unref (output);
        return;
    }

    qmi_message_dms_set_firmware_preference_output_unref (output);

    /* Now, go into offline mode */
    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, QMI_DMS_OPERATING_MODE_OFFLINE, NULL);
    qmi_client_dms_set_operating_mode (ctx->client,
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)firmware_set_operating_mode_offline_ready,
                                       ctx);
    qmi_message_dms_set_operating_mode_input_unref (input);
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
firmware_change_current (MMIfaceModemFirmware *self,
                         const gchar *unique_id,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    QmiMessageDmsSetFirmwarePreferenceInput *input;
    FirmwareChangeCurrentContext *ctx;
    QmiClient *client = NULL;
    GArray *array;
    QmiMessageDmsSetFirmwarePreferenceInputListImage modem_image_id;
    QmiMessageDmsSetFirmwarePreferenceInputListImage pri_image_id;
    guint8 *tmp;
    gsize tmp_len;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_slice_new0 (FirmwareChangeCurrentContext);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             firmware_change_current);

    /* Look for the firmware image with the requested unique ID */
    ctx->firmware = find_firmware_properties_by_unique_id (ctx->self, unique_id);
    if (!ctx->firmware) {
        guint n = 0;

        /* Ok, let's look at the PRI info */
        ctx->firmware = find_firmware_properties_by_gobi_pri_info_substring (ctx->self, unique_id, &n);
        if (n > 1) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "Multiple firmware images (%u) found matching '%s' as PRI info substring",
                                             n, unique_id);
            firmware_change_current_context_complete_and_free (ctx);
            return;
        }

        if (n == 0) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "Firmware with unique ID '%s' wasn't found",
                                             unique_id);
            firmware_change_current_context_complete_and_free (ctx);
            return;
        }

        g_assert (n == 1 && MM_IS_FIRMWARE_PROPERTIES (ctx->firmware));
    }

    /* If we're already in the requested firmware, we're done */
    if (ctx->self->priv->current_firmware &&
        g_str_equal (mm_firmware_properties_get_unique_id (ctx->self->priv->current_firmware),
                     mm_firmware_properties_get_unique_id (ctx->firmware))) {
        mm_dbg ("Modem is already running firmware image '%s'",
                mm_firmware_properties_get_unique_id (ctx->self->priv->current_firmware));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        firmware_change_current_context_complete_and_free (ctx);
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
        ctx->client,
        input,
        10,
        NULL,
        (GAsyncReadyCallback)firmware_select_stored_image_ready,
        ctx);
    g_array_unref (modem_image_id.unique_id);
    g_array_unref (pri_image_id.unique_id);
    qmi_message_dms_set_firmware_preference_input_unref (input);
}

/*****************************************************************************/
/* First enabling step */

static gboolean
enabling_started_finish (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_enabling_started_ready (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
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

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
enabling_started (MMBroadbandModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        enabling_started);
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->enabling_started (
        self,
        (GAsyncReadyCallback)parent_enabling_started_ready,
        result);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQmiPort *qmi;
    QmiService services[32];
    guint service_index;
} InitializationStartedContext;

static void
initialization_started_context_complete_and_free (InitializationStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->qmi)
        g_object_unref (ctx->qmi);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* Just parent's pointer passed here */
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     InitializationStartedContext *ctx)
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

    g_simple_async_result_set_op_res_gpointer (ctx->result, parent_ctx, NULL);
    initialization_started_context_complete_and_free (ctx);
}

static void
parent_initialization_started (InitializationStartedContext *ctx)
{
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started (
        ctx->self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        ctx);
}

static void allocate_next_client (InitializationStartedContext *ctx);

static void
qmi_port_allocate_client_ready (MMQmiPort *qmi,
                                GAsyncResult *res,
                                InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_allocate_client_finish (qmi, res, &error)) {
        mm_dbg ("Couldn't allocate client for service '%s': %s",
                qmi_service_get_string (ctx->services[ctx->service_index]),
                error->message);
        g_error_free (error);
    }

    ctx->service_index++;
    allocate_next_client (ctx);
}

static void
allocate_next_client (InitializationStartedContext *ctx)
{
    if (ctx->services[ctx->service_index] == QMI_SERVICE_UNKNOWN) {
        /* Done we are, launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Otherwise, allocate next client */
    mm_qmi_port_allocate_client (ctx->qmi,
                                 ctx->services[ctx->service_index],
                                 MM_QMI_PORT_FLAG_DEFAULT,
                                 NULL,
                                 (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                 ctx);
}

static void
qmi_port_open_ready (MMQmiPort *qmi,
                     GAsyncResult *res,
                     InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_open_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    allocate_next_client (ctx);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_started);
    ctx->qmi = mm_base_modem_get_port_qmi (MM_BASE_MODEM (self));

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->qmi) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Cannot initialize: QMI port went missing");
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    if (mm_qmi_port_is_open (ctx->qmi)) {
        /* Nothing to be done, just launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Setup services to open */
    ctx->services[0] = QMI_SERVICE_DMS;
    ctx->services[1] = QMI_SERVICE_NAS;
    ctx->services[2] = QMI_SERVICE_WMS;
    ctx->services[3] = QMI_SERVICE_PDS;
    ctx->services[4] = QMI_SERVICE_UNKNOWN;

    /* Now open our QMI port */
    mm_qmi_port_open (ctx->qmi,
                      TRUE,
                      NULL,
                      (GAsyncReadyCallback)qmi_port_open_ready,
                      ctx);
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
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_QMI,
                                              MMBroadbandModemQmiPrivate);
}

static void
finalize (GObject *object)
{
    MMQmiPort *qmi;
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    qmi = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    /* If we did open the QMI port during initialization, close it now */
    if (qmi &&
        mm_qmi_port_is_open (qmi)) {
        mm_qmi_port_close (qmi);
    }

    g_free (self->priv->imei);
    g_free (self->priv->meid);
    g_free (self->priv->esn);
    g_free (self->priv->current_operator_id);
    g_free (self->priv->current_operator_description);
    if (self->priv->supported_bands)
        g_array_unref (self->priv->supported_bands);

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    g_list_free_full (self->priv->firmware_list, (GDestroyNotify)g_object_unref);
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
    iface->load_modem_capabilities = modem_load_modem_capabilities;
    iface->load_modem_capabilities_finish = modem_load_modem_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
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
    iface->modem_init_power_down = modem_power_down;
    iface->modem_init_power_down_finish = modem_power_up_down_finish;
    iface->load_power_state = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;

    /* Enabling/disabling */
    iface->modem_init = NULL;
    iface->modem_init_finish = NULL;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_down_finish;
    iface->modem_after_power_up = NULL;
    iface->modem_after_power_up_finish = NULL;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_up_down_finish;
    iface->setup_flow_control = NULL;
    iface->setup_flow_control_finish = NULL;
    iface->load_supported_charsets = NULL;
    iface->load_supported_charsets_finish = NULL;
    iface->setup_charset = NULL;
    iface->setup_charset_finish = NULL;
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
    iface->load_current_bands = modem_load_current_bands;
    iface->load_current_bands_finish = modem_load_current_bands_finish;
    iface->set_bands = set_bands;
    iface->set_bands_finish = set_bands_finish;

    /* Create QMI-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Create QMI-specific bearer */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;

    /* Other actions */
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
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->check_support = messaging_check_support;
    iface->check_support_finish = messaging_check_support_finish;
    iface->load_supported_storages = messaging_load_supported_storages;
    iface->load_supported_storages_finish = messaging_load_supported_storages_finish;
    iface->setup_sms_format = NULL;
    iface->setup_sms_format_finish = NULL;
    iface->set_default_storage = messaging_set_default_storage;
    iface->set_default_storage_finish = messaging_set_default_storage_finish;
    iface->load_initial_sms_parts = load_initial_sms_parts;
    iface->load_initial_sms_parts_finish = load_initial_sms_parts_finish;
    iface->setup_unsolicited_events = messaging_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = messaging_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = messaging_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = messaging_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = messaging_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = messaging_enable_disable_unsolicited_events_finish;
    iface->create_sms = messaging_create_sms;
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
}
