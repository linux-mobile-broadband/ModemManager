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
#include <string.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-sim-qmi.h"
#include "mm-shared-qmi.h"
#include "mm-modem-helpers-qmi.h"

/* Default session id to use in LOC operations */
#define DEFAULT_LOC_SESSION_ID 0x10

/* Default description for the default configuration of the firmware */
#define DEFAULT_CONFIG_DESCRIPTION "default"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-qmi-private-tag"
static GQuark private_quark;

typedef enum {
    FEATURE_UNKNOWN,
    FEATURE_UNSUPPORTED,
    FEATURE_SUPPORTED,
} Feature;

typedef struct {
    GArray                  *id;
    QmiPdcConfigurationType  config_type;
    guint32                  token;
    guint32                  version;
    gchar                   *description;
    guint32                  total_size;
} ConfigInfo;

static void
config_info_clear (ConfigInfo *config_info)
{
    g_array_unref (config_info->id);
    g_free (config_info->description);
}

typedef struct {
    /* Capabilities & modes helpers */
    MMModemCapability  current_capabilities;
    GArray            *supported_radio_interfaces;
    Feature            feature_nas_tp;
    Feature            feature_nas_ssp;
    Feature            feature_nas_ssp_extended_lte_band_preference;
    Feature            feature_nas_ssp_acquisition_order_preference;
    GArray            *feature_nas_ssp_acquisition_order_preference_array;
    gboolean           disable_4g_only_mode;
    GArray            *supported_bands;

    /* Location helpers */
    MMIfaceModemLocation   *iface_modem_location_parent;
    MMModemLocationSource   enabled_sources;
    QmiClient              *pds_client;
    gulong                  pds_location_event_report_indication_id;
    QmiClient              *loc_client;
    gulong                  loc_location_nmea_indication_id;
    gchar                 **loc_assistance_data_servers;
    guint32                 loc_assistance_data_max_file_size;
    guint32                 loc_assistance_data_max_part_size;

    /* Carrier config helpers */
    gboolean  config_active_default;
    GArray   *config_list;
    gint      config_active_i;

    /* Slot status monitoring */
    QmiClient *uim_client;
    gulong     uim_slot_status_indication_id;
    gulong     uim_refresh_indication_id;
    guint      uim_refresh_start_timeout_id;
} Private;

static void
private_free (Private *priv)
{
    if (priv->config_list)
        g_array_unref (priv->config_list);
    if (priv->supported_bands)
        g_array_unref (priv->supported_bands);
    if (priv->supported_radio_interfaces)
        g_array_unref (priv->supported_radio_interfaces);
    if (priv->pds_location_event_report_indication_id)
        g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
    if (priv->pds_client)
        g_object_unref (priv->pds_client);
    if (priv->loc_location_nmea_indication_id)
        g_signal_handler_disconnect (priv->loc_client, priv->loc_location_nmea_indication_id);
    if (priv->loc_client)
        g_object_unref (priv->loc_client);
    if (priv->uim_slot_status_indication_id)
        g_signal_handler_disconnect (priv->uim_client, priv->uim_slot_status_indication_id);
    if (priv->uim_refresh_indication_id)
        g_signal_handler_disconnect (priv->uim_client, priv->uim_refresh_indication_id);
    if (priv->uim_client)
        g_object_unref (priv->uim_client);
    if (priv->uim_refresh_start_timeout_id)
        g_source_remove (priv->uim_refresh_start_timeout_id);
    if (priv->feature_nas_ssp_acquisition_order_preference_array)
        g_array_unref (priv->feature_nas_ssp_acquisition_order_preference_array);
    g_strfreev (priv->loc_assistance_data_servers);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMSharedQmi *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        priv->feature_nas_tp = FEATURE_UNKNOWN;
        priv->feature_nas_ssp = FEATURE_UNKNOWN;
        priv->feature_nas_ssp_extended_lte_band_preference = FEATURE_UNKNOWN;
        priv->feature_nas_ssp_acquisition_order_preference = FEATURE_UNKNOWN;
        priv->config_active_i = -1;

        /* Setup parent class' MMIfaceModemLocation */
        g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/
/* Register in network (3GPP interface) */

/* wait this amount of time at most if we don't get the serving system
 * indication earlier */
#define REGISTER_IN_NETWORK_TIMEOUT_SECS 25

typedef struct {
    guint         timeout_id;
    gulong        serving_system_indication_id;
    GCancellable *cancellable;
    gulong        cancellable_id;
    QmiClientNas *client;
} RegisterInNetworkContext;

static void
register_in_network_context_free (RegisterInNetworkContext *ctx)
{
    g_assert (!ctx->cancellable_id);
    g_assert (!ctx->timeout_id);
    if (ctx->client) {
        g_assert (!ctx->serving_system_indication_id);
        g_object_unref (ctx->client);
    }
    g_clear_object (&ctx->cancellable);
    g_slice_free (RegisterInNetworkContext, ctx);
}

gboolean
mm_shared_qmi_3gpp_register_in_network_finish (MMIfaceModem3gpp  *self,
                                               GAsyncResult      *res,
                                               GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
register_in_network_cancelled (GCancellable *cancellable,
                               GTask        *task)
{
    RegisterInNetworkContext *ctx;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->cancellable);
    g_assert (ctx->cancellable_id);
    ctx->cancellable_id = 0;

    g_assert (ctx->timeout_id);
    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;

    g_assert (ctx->client);
    g_assert (ctx->serving_system_indication_id);
    g_signal_handler_disconnect (ctx->client, ctx->serving_system_indication_id);
    ctx->serving_system_indication_id = 0;

    g_task_return_error_if_cancelled (task);
    g_object_unref (task);
}

static gboolean
register_in_network_timeout (GTask *task)
{
    RegisterInNetworkContext *ctx;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->timeout_id);
    ctx->timeout_id = 0;

    g_assert (ctx->client);
    g_assert (ctx->serving_system_indication_id);
    g_signal_handler_disconnect (ctx->client, ctx->serving_system_indication_id);
    ctx->serving_system_indication_id = 0;

    g_assert (!ctx->cancellable || ctx->cancellable_id);
    g_cancellable_disconnect (ctx->cancellable, ctx->cancellable_id);
    ctx->cancellable_id = 0;

    /* the 3GPP interface will take care of checking if the registration is
     * the one we asked for */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
register_in_network_ready (GTask                               *task,
                           QmiIndicationNasServingSystemOutput *output)
{
    RegisterInNetworkContext *ctx;
    QmiNasRegistrationState   registration_state;

    /* ignore indication updates reporting "searching" */
    qmi_indication_nas_serving_system_output_get_serving_system (
            output,
            &registration_state,
            NULL, /* cs_attach_state  */
            NULL, /* ps_attach_state  */
            NULL, /* selected_network */
            NULL, /* radio_interfaces */
            NULL);
    if (registration_state == QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING)
        return;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->client);
    g_assert (ctx->serving_system_indication_id);
    g_signal_handler_disconnect (ctx->client, ctx->serving_system_indication_id);
    ctx->serving_system_indication_id = 0;

    g_assert (ctx->timeout_id);
    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;

    g_assert (!ctx->cancellable || ctx->cancellable_id);
    g_cancellable_disconnect (ctx->cancellable, ctx->cancellable_id);
    ctx->cancellable_id = 0;

    /* the 3GPP interface will take care of checking if the registration is
     * the one we asked for */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
initiate_network_register_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    GError                                     *error = NULL;
    QmiMessageNasInitiateNetworkRegisterOutput *output;
    RegisterInNetworkContext                   *ctx;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_initiate_network_register_finish (client, res, &error);
    if (!output || !qmi_message_nas_initiate_network_register_output_get_result (output, &error)) {
        /* No effect would mean we're already in the desired network */
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_task_return_boolean (task, TRUE);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "Couldn't initiate network register: ");
            g_task_return_error (task, error);
        }
        g_object_unref (task);
        goto out;
    }

    /* Registration attempt started, now we need to monitor "serving system" indications
     * to get notified when the registration changed. Note that we won't need to process
     * the indication, because we already have that logic setup (and it runs before this
     * new signal handler), we just need to get notified of when it happens. We will also
     * setup a maximum operation timeuot plus a cancellability point, as this operation
     * may be explicitly cancelled by the 3GPP interface if a new registration request
     * arrives while the current one is being processed.
     *
     * Task is shared among cancellable, indication and timeout. The first one triggered
     * will cancel the others.
     */

    if (ctx->cancellable)
        ctx->cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                     G_CALLBACK (register_in_network_cancelled),
                                                     task,
                                                     NULL);

    ctx->serving_system_indication_id = g_signal_connect_swapped (client,
                                                                  "serving-system",
                                                                  G_CALLBACK (register_in_network_ready),
                                                                  task);

    ctx->timeout_id = g_timeout_add_seconds (REGISTER_IN_NETWORK_TIMEOUT_SECS,
                                             (GSourceFunc) register_in_network_timeout,
                                             task);

out:

    if (output)
        qmi_message_nas_initiate_network_register_output_unref (output);
}

static void
register_in_network_inr (GTask        *task,
                         QmiClient    *client,
                         GCancellable *cancellable,
                         guint16       mcc,
                         guint16       mnc)
{
    QmiMessageNasInitiateNetworkRegisterInput *input;

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
            QMI_NAS_RADIO_INTERFACE_UNKNOWN, /* don't change radio interface */
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
        task);

    qmi_message_nas_initiate_network_register_input_unref (input);
}

static void
set_system_selection_preference_ready (QmiClientNas *client,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GError                                          *error = NULL;
    QmiMessageNasSetSystemSelectionPreferenceOutput *output;

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set network selection preference: ");
            g_task_return_error (task, error);
            goto out;
        }
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);

out:
    g_object_unref (task);

    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);
}

static void
register_in_network_sssp (GTask        *task,
                          QmiClient    *client,
                          GCancellable *cancellable,
                          guint16       mcc,
                          guint16       mnc)
{
    QmiMessageNasSetSystemSelectionPreferenceInput *input;

    input = qmi_message_nas_set_system_selection_preference_input_new ();

    qmi_message_nas_set_system_selection_preference_input_set_network_selection_preference (
        input,
        mcc ? QMI_NAS_NETWORK_SELECTION_PREFERENCE_MANUAL : QMI_NAS_NETWORK_SELECTION_PREFERENCE_AUTOMATIC,
        mcc,
        mnc,
        NULL);

    qmi_client_nas_set_system_selection_preference (
        QMI_CLIENT_NAS (client),
        input,
        120,
        cancellable,
        (GAsyncReadyCallback)set_system_selection_preference_ready,
        task);

    qmi_message_nas_set_system_selection_preference_input_unref (input);
}

void
mm_shared_qmi_3gpp_register_in_network (MMIfaceModem3gpp    *self,
                                        const gchar         *operator_id,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    GTask                    *task;
    RegisterInNetworkContext *ctx;
    guint16                   mcc = 0;
    guint16                   mnc;
    QmiClient                *client = NULL;
    GError                   *error = NULL;
    Private                  *priv = NULL;

    /* Get NAS client */
    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, cancellable, callback, user_data);

    ctx = g_slice_new0 (RegisterInNetworkContext);
    ctx->client = g_object_ref (client);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    g_task_set_task_data (task, ctx, (GDestroyNotify)register_in_network_context_free);

    /* Parse input MCC/MNC */
    if (operator_id && !mm_3gpp_parse_operator_id (operator_id, &mcc, &mnc, &error)) {
        g_assert (error != NULL);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    priv = get_private (MM_SHARED_QMI (self));
    if (priv->feature_nas_ssp == FEATURE_SUPPORTED)
        register_in_network_sssp (task, client, cancellable, mcc, mnc);
    else
        register_in_network_inr (task, client, cancellable, mcc, mnc);
}

/*****************************************************************************/
/* Current capabilities setting (Modem interface) */

typedef enum {
    SET_CURRENT_CAPABILITIES_STEP_FIRST,
    SET_CURRENT_CAPABILITIES_STEP_NAS_SYSTEM_SELECTION_PREFERENCE,
    SET_CURRENT_CAPABILITIES_STEP_NAS_TECHNOLOGY_PREFERENCE,
    SET_CURRENT_CAPABILITIES_STEP_RESET,
    SET_CURRENT_CAPABILITIES_STEP_LAST,
} SetCurrentCapabilitiesStep;

typedef struct {
    QmiClientNas               *client;
    MMModemCapability           capabilities;
    gboolean                    capabilities_updated;
    SetCurrentCapabilitiesStep  step;
} SetCurrentCapabilitiesContext;

static void
set_current_capabilities_context_free (SetCurrentCapabilitiesContext *ctx)
{
    g_object_unref (ctx->client);
    g_slice_free (SetCurrentCapabilitiesContext, ctx);
}

gboolean
mm_shared_qmi_set_current_capabilities_finish (MMIfaceModem  *self,
                                               GAsyncResult  *res,
                                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_current_capabilities_step (GTask *task);

static void
set_current_capabilities_reset_ready (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GTask        *task)
{
    SetCurrentCapabilitiesContext *ctx;
    GError                        *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_shared_qmi_reset_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_current_capabilities_step (task);
}

static void
set_current_capabilities_set_technology_preference_ready (QmiClientNas *client,
                                                          GAsyncResult *res,
                                                          GTask        *task)
{
    SetCurrentCapabilitiesContext              *ctx;
    QmiMessageNasSetTechnologyPreferenceOutput *output = NULL;
    GError                                     *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_set_technology_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_technology_preference_output_get_result (output, &error)) {
        /* A no-effect error here is not a real error */
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            goto out;
        }
        /* no effect, just end operation without reset */
        g_clear_error (&error);
        ctx->step = SET_CURRENT_CAPABILITIES_STEP_LAST;
        set_current_capabilities_step (task);
        goto out;
    }

    /* success! */
    ctx->step = SET_CURRENT_CAPABILITIES_STEP_RESET;
    set_current_capabilities_step (task);

out:
    if (output)
        qmi_message_nas_set_technology_preference_output_unref (output);
}

static void
set_current_capabilities_technology_preference (GTask *task)
{
    SetCurrentCapabilitiesContext             *ctx;
    QmiMessageNasSetTechnologyPreferenceInput *input;
    QmiNasRadioTechnologyPreference            pref;

    ctx = g_task_get_task_data (task);

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
        NULL,
        (GAsyncReadyCallback)set_current_capabilities_set_technology_preference_ready,
        task);
    qmi_message_nas_set_technology_preference_input_unref (input);
}

static void
set_current_capabilities_set_system_selection_preference_ready (QmiClientNas *client,
                                                                GAsyncResult *res,
                                                                GTask        *task)
{
    SetCurrentCapabilitiesContext                   *ctx;
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* success! */
    ctx->step = SET_CURRENT_CAPABILITIES_STEP_RESET;
    set_current_capabilities_step (task);

out:
    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);
}

static void
set_current_capabilities_system_selection_preference (GTask *task)
{
    SetCurrentCapabilitiesContext                  *ctx;
    QmiMessageNasSetSystemSelectionPreferenceInput *input;
    QmiNasRatModePreference                         pref;

    ctx  = g_task_get_task_data (task);

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
        NULL,
        (GAsyncReadyCallback)set_current_capabilities_set_system_selection_preference_ready,
        task);
    qmi_message_nas_set_system_selection_preference_input_unref (input);
}

static void
set_current_capabilities_step (GTask *task)
{
    MMSharedQmi                   *self;
    Private                       *priv;
    SetCurrentCapabilitiesContext *ctx;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case SET_CURRENT_CAPABILITIES_STEP_FIRST:
        /* Error out early if both unsupported */
        if ((priv->feature_nas_ssp != FEATURE_SUPPORTED) &&
            (priv->feature_nas_tp != FEATURE_SUPPORTED)) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                     "Setting capabilities is not supported by this device");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_CURRENT_CAPABILITIES_STEP_NAS_SYSTEM_SELECTION_PREFERENCE:
        if (priv->feature_nas_ssp == FEATURE_SUPPORTED) {
            set_current_capabilities_system_selection_preference (task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_CURRENT_CAPABILITIES_STEP_NAS_TECHNOLOGY_PREFERENCE:
        if (priv->feature_nas_tp == FEATURE_SUPPORTED) {
            set_current_capabilities_technology_preference (task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_CURRENT_CAPABILITIES_STEP_RESET:
        mm_shared_qmi_reset (MM_IFACE_MODEM (self),
                             (GAsyncReadyCallback)set_current_capabilities_reset_ready,
                             task);
        return;

    case SET_CURRENT_CAPABILITIES_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_shared_qmi_set_current_capabilities (MMIfaceModem        *self,
                                        MMModemCapability    capabilities,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    Private                       *priv;
    SetCurrentCapabilitiesContext *ctx;
    GTask                         *task;
    QmiClient                     *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->feature_nas_tp != FEATURE_UNKNOWN);
    g_assert (priv->feature_nas_ssp != FEATURE_UNKNOWN);

    ctx = g_slice_new0 (SetCurrentCapabilitiesContext);
    ctx->client       = g_object_ref (client);
    ctx->capabilities = capabilities;
    ctx->step = SET_CURRENT_CAPABILITIES_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_current_capabilities_context_free);

    set_current_capabilities_step (task);
}

/*****************************************************************************/
/* Current capabilities (Modem interface) */

typedef enum {
    LOAD_CURRENT_CAPABILITIES_STEP_FIRST,
    LOAD_CURRENT_CAPABILITIES_STEP_NAS_SYSTEM_SELECTION_PREFERENCE,
    LOAD_CURRENT_CAPABILITIES_STEP_NAS_TECHNOLOGY_PREFERENCE,
    LOAD_CURRENT_CAPABILITIES_STEP_DMS_GET_CAPABILITIES,
    LOAD_CURRENT_CAPABILITIES_STEP_LAST,
} LoadCurrentCapabilitiesStep;

typedef struct {
    QmiClientNas                *nas_client;
    QmiClientDms                *dms_client;
    LoadCurrentCapabilitiesStep  step;
    MMQmiCapabilitiesContext     capabilities_context;
} LoadCurrentCapabilitiesContext;

MMModemCapability
mm_shared_qmi_load_current_capabilities_finish (MMIfaceModem  *self,
                                                GAsyncResult  *res,
                                                GError       **error)
{
    GError *inner_error = NULL;
    gssize  value;

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

static void load_current_capabilities_step (GTask *task);

static void
load_current_capabilities_get_capabilities_ready (QmiClientDms *client,
                                                  GAsyncResult *res,
                                                  GTask        *task)
{
    MMSharedQmi                        *self;
    Private                            *priv;
    LoadCurrentCapabilitiesContext     *ctx;
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError                             *error = NULL;
    guint                               i;
    GArray                             *radio_interface_list;

    self = g_task_get_source_object (task);
    priv = get_private (self);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Capabilities: ");
        goto out;
    }

    qmi_message_dms_get_capabilities_output_get_info (
        output,
        NULL, /* info_max_tx_channel_rate */
        NULL, /* info_max_rx_channel_rate */
        NULL, /* info_data_service_capability */
        NULL, /* info_sim_capability */
        &radio_interface_list,
        NULL);

    /* Cache supported radio interfaces */
    g_assert (!priv->supported_radio_interfaces);
    priv->supported_radio_interfaces = g_array_ref (radio_interface_list);

    for (i = 0; i < radio_interface_list->len; i++)
        ctx->capabilities_context.dms_capabilities |=
            mm_modem_capability_from_qmi_radio_interface (g_array_index (radio_interface_list, QmiDmsRadioInterface, i), self);

out:
    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    /* Failure in DMS Get Capabilities is fatal */
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    load_current_capabilities_step (task);
}

static void
load_current_capabilities_get_technology_preference_ready (QmiClientNas *client,
                                                           GAsyncResult *res,
                                                           GTask        *task)
{
    MMSharedQmi                                *self;
    Private                                    *priv;
    LoadCurrentCapabilitiesContext             *ctx;
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError                                     *error = NULL;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));
    ctx  = g_task_get_task_data (task);

    output = qmi_client_nas_get_technology_preference_finish (client, res, &error);
    if (!output) {
        mm_obj_dbg (self, "QMI operation failed: %s", error->message);
        g_error_free (error);
        priv->feature_nas_tp = FEATURE_UNSUPPORTED;
    } else if (!qmi_message_nas_get_technology_preference_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't get technology preference: %s", error->message);
        g_error_free (error);
        priv->feature_nas_tp = FEATURE_UNSUPPORTED;
    } else {
        qmi_message_nas_get_technology_preference_output_get_active (
            output,
            &ctx->capabilities_context.nas_tp_mask,
            NULL, /* duration */
            NULL);
        priv->feature_nas_tp = FEATURE_SUPPORTED;
    }

    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);

    ctx->step++;
    load_current_capabilities_step (task);
}

static void
load_current_capabilities_get_system_selection_preference_ready (QmiClientNas *client,
                                                                 GAsyncResult *res,
                                                                 GTask        *task)
{
    MMSharedQmi                                     *self;
    Private                                         *priv;
    LoadCurrentCapabilitiesContext                  *ctx;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);
    priv = get_private (MM_SHARED_QMI (self));

    priv->feature_nas_ssp = FEATURE_UNSUPPORTED;
    priv->feature_nas_ssp_extended_lte_band_preference = FEATURE_UNSUPPORTED;
    priv->feature_nas_ssp_acquisition_order_preference = FEATURE_UNSUPPORTED;

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output) {
        mm_obj_dbg (self, "QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't get system selection preference: %s", error->message);
        g_error_free (error);
    } else {
        GArray *acquisition_order_preference_array = NULL;

        /* SSP is supported, perform feature checks */
        priv->feature_nas_ssp = FEATURE_SUPPORTED;
        if (qmi_message_nas_get_system_selection_preference_output_get_extended_lte_band_preference (output, NULL, NULL, NULL, NULL, NULL))
            priv->feature_nas_ssp_extended_lte_band_preference = FEATURE_SUPPORTED;
        if (qmi_message_nas_get_system_selection_preference_output_get_acquisition_order_preference (output, &acquisition_order_preference_array, NULL) &&
            acquisition_order_preference_array &&
            acquisition_order_preference_array->len) {
            priv->feature_nas_ssp_acquisition_order_preference = FEATURE_SUPPORTED;
            priv->feature_nas_ssp_acquisition_order_preference_array = g_array_ref (acquisition_order_preference_array);
        }

        qmi_message_nas_get_system_selection_preference_output_get_mode_preference (
            output,
            &ctx->capabilities_context.nas_ssp_mode_preference_mask,
            NULL);
    }

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    ctx->step++;
    load_current_capabilities_step (task);
}

static void
load_current_capabilities_step (GTask *task)
{
    MMSharedQmi                    *self;
    Private                        *priv;
    LoadCurrentCapabilitiesContext *ctx;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case LOAD_CURRENT_CAPABILITIES_STEP_FIRST:
        ctx->step++;
        /* fall-through */

    case LOAD_CURRENT_CAPABILITIES_STEP_NAS_SYSTEM_SELECTION_PREFERENCE:
        qmi_client_nas_get_system_selection_preference (
            ctx->nas_client, NULL, 5, NULL,
            (GAsyncReadyCallback)load_current_capabilities_get_system_selection_preference_ready,
            task);
        return;

    case LOAD_CURRENT_CAPABILITIES_STEP_NAS_TECHNOLOGY_PREFERENCE:
        qmi_client_nas_get_technology_preference (
            ctx->nas_client, NULL, 5, NULL,
            (GAsyncReadyCallback)load_current_capabilities_get_technology_preference_ready,
            task);
        return;

    case LOAD_CURRENT_CAPABILITIES_STEP_DMS_GET_CAPABILITIES:
        qmi_client_dms_get_capabilities (
            ctx->dms_client, NULL, 5, NULL,
            (GAsyncReadyCallback)load_current_capabilities_get_capabilities_ready,
            task);
        return;

    case LOAD_CURRENT_CAPABILITIES_STEP_LAST:
        g_assert (priv->feature_nas_tp != FEATURE_UNKNOWN);
        g_assert (priv->feature_nas_ssp != FEATURE_UNKNOWN);
        priv->current_capabilities = mm_modem_capability_from_qmi_capabilities_context (&ctx->capabilities_context, self);
        g_task_return_int (task, priv->current_capabilities);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_shared_qmi_load_current_capabilities (MMIfaceModem        *self,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    LoadCurrentCapabilitiesContext *ctx;
    GTask                          *task;
    QmiClient                      *nas_client = NULL;
    QmiClient                      *dms_client = NULL;
    Private                        *priv;

    /*
     * We assume that DMS Get Capabilities reports always the same result,
     * that will include all capabilities supported by the device regardless
     * of which ones are configured at the moment. E.g. for the Load Supported
     * Capabilities we base the logic exclusively on this method's output.
     *
     * We then consider 3 different cases:
     *  a) If the device supports NAS System Selection Preference, we use the
     *  "mode preference" TLV to select currently enabled capabilities.
     *  b) If the device supports NAS Technology Preference (older devices),
     *  we use this method to select currently enabled capabilities.
     *  c) If none of those messages is supported we don't allow swiching
     *  capabilities.
     */

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &nas_client,
                                      callback, user_data))
        return;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &dms_client,
                                      callback, user_data))
        return;

    /* Current capabilities is the first thing run, and will only be run once per modem,
     * so we should here check support for the optional features. */
    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->feature_nas_tp == FEATURE_UNKNOWN);
    g_assert (priv->feature_nas_ssp == FEATURE_UNKNOWN);

    ctx = g_slice_new0 (LoadCurrentCapabilitiesContext);
    ctx->nas_client = g_object_ref (nas_client);
    ctx->dms_client = g_object_ref (dms_client);
    ctx->step = LOAD_CURRENT_CAPABILITIES_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_current_capabilities_context_free);

    load_current_capabilities_step (task);
}

/*****************************************************************************/
/* Supported capabilities (Modem interface) */

GArray *
mm_shared_qmi_load_supported_capabilities_finish (MMIfaceModem  *self,
                                                  GAsyncResult  *res,
                                                  GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

void
mm_shared_qmi_load_supported_capabilities (MMIfaceModem        *self,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    GTask             *task;
    Private           *priv;
    MMModemCapability  mask;
    MMModemCapability  single;
    GArray            *supported_combinations;
    guint              i;

    task = g_task_new (self, NULL, callback, user_data);

    /* List of radio interfaces preloaded in current capabilities */
    priv = get_private (MM_SHARED_QMI (self));
    if (!priv->supported_radio_interfaces) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "cannot load current capabilities without radio interface information");
        g_object_unref (task);
        return;
    }

    /* Build mask with all supported capabilities */
    mask = MM_MODEM_CAPABILITY_NONE;
    for (i = 0; i < priv->supported_radio_interfaces->len; i++)
        mask |= mm_modem_capability_from_qmi_radio_interface (g_array_index (priv->supported_radio_interfaces, QmiDmsRadioInterface, i), self);

    supported_combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 3);

    /* Add all possible supported capability combinations.
     * In order to avoid unnecessary modem reboots, we will only implement capabilities
     * switching only when switching GSM/UMTS+CDMA/EVDO multimode devices, and only if
     * we have support for the commands doing it.
     */
    if (priv->feature_nas_tp == FEATURE_SUPPORTED || priv->feature_nas_ssp == FEATURE_SUPPORTED) {
        if (mask == (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO)) {
            /* Multimode GSM/UMTS+CDMA/EVDO device switched to GSM/UMTS only */
            single = MM_MODEM_CAPABILITY_GSM_UMTS;
            g_array_append_val (supported_combinations, single);
            /* Multimode GSM/UMTS+CDMA/EVDO device switched to CDMA/EVDO only */
            single = MM_MODEM_CAPABILITY_CDMA_EVDO;
            g_array_append_val (supported_combinations, single);
        } else if (mask == (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE)) {
            /* Multimode GSM/UMTS+CDMA/EVDO+LTE device switched to GSM/UMTS+LTE only */
            single = MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE;
            g_array_append_val (supported_combinations, single);
            /* Multimode GSM/UMTS+CDMA/EVDO+LTE device switched to CDMA/EVDO+LTE only */
            single = MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE;
            g_array_append_val (supported_combinations, single);
            /*
             * Multimode GSM/UMTS+CDMA/EVDO+LTE device switched to LTE only.
             *
             * This case is required because we use the same methods and operations to
             * switch capabilities and modes. For the LTE capability there is a direct
             * related 4G mode, and so we cannot select a '4G only' mode in this device
             * because we wouldn't be able to know the full list of current capabilities
             * if the device was rebooted, as we would only see LTE capability. So,
             * handle this special case so that the LTE/4G-only mode can exclusively be
             * selected as capability switching in this kind of devices.
             */
            priv->disable_4g_only_mode = TRUE;
            single = MM_MODEM_CAPABILITY_LTE;
            g_array_append_val (supported_combinations, single);
        }
    }

    /* Add the full mask itself */
    single = mask;
    g_array_append_val (supported_combinations, single);

    g_task_return_pointer (task, supported_combinations, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Allowed modes setting (Modem interface) */

typedef struct {
    QmiClientNas *client;
    MMModemMode   allowed;
    MMModemMode   preferred;
} SetCurrentModesContext;

static void
set_current_modes_context_free (SetCurrentModesContext *ctx)
{
    g_object_unref (ctx->client);
    g_slice_free (SetCurrentModesContext, ctx);
}

gboolean
mm_shared_qmi_set_current_modes_finish (MMIfaceModem  *self,
                                        GAsyncResult  *res,
                                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_current_modes_technology_preference_ready (QmiClientNas *client,
                                               GAsyncResult *res,
                                               GTask        *task)
{
    QmiMessageNasSetTechnologyPreferenceOutput *output = NULL;
    GError                                     *error = NULL;

    output = qmi_client_nas_set_technology_preference_finish (client, res, &error);
    if (!output ||
        (!qmi_message_nas_set_technology_preference_output_get_result (output, &error) &&
         !g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT))) {
        g_task_return_error (task, error);
    } else {
        g_clear_error (&error);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);

    if (output)
        qmi_message_nas_set_technology_preference_output_unref (output);
}

static void
set_current_modes_technology_preference (GTask *task)
{
    MMIfaceModem                              *self;
    SetCurrentModesContext                    *ctx;
    QmiMessageNasSetTechnologyPreferenceInput *input;
    QmiNasRadioTechnologyPreference            pref;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (ctx->preferred != MM_MODEM_MODE_NONE) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot set specific preferred mode");
        g_object_unref (task);
        return;
    }

    pref = mm_modem_mode_to_qmi_radio_technology_preference (ctx->allowed, mm_iface_modem_is_cdma (self));
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
        (GAsyncReadyCallback)set_current_modes_technology_preference_ready,
        task);
    qmi_message_nas_set_technology_preference_input_unref (input);
}

static void
set_current_modes_system_selection_preference_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     GTask        *task)
{
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_system_selection_preference_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);
}

static void
set_current_modes_system_selection_preference (GTask *task)
{
    MMIfaceModem                                   *self;
    Private                                        *priv;
    SetCurrentModesContext                         *ctx;
    QmiMessageNasSetSystemSelectionPreferenceInput *input;
    QmiNasRatModePreference                         pref;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));
    ctx  = g_task_get_task_data (task);

    input = qmi_message_nas_set_system_selection_preference_input_new ();
    qmi_message_nas_set_system_selection_preference_input_set_change_duration (input, QMI_NAS_CHANGE_DURATION_PERMANENT, NULL);

    /* Preferred modes */

    if (ctx->preferred != MM_MODEM_MODE_NONE) {
        if (priv->feature_nas_ssp_acquisition_order_preference == FEATURE_SUPPORTED) {
            GArray *array;

            /* Acquisition order array */
            array = mm_modem_mode_to_qmi_acquisition_order_preference (ctx->allowed,
                                                                       ctx->preferred,
                                                                       priv->feature_nas_ssp_acquisition_order_preference_array);
            g_assert (array);
            qmi_message_nas_set_system_selection_preference_input_set_acquisition_order_preference (input, array, NULL);
            g_array_unref (array);
        }

        /* Only set GSM/WCDMA acquisition order preference if both 2G and 3G given as allowed */
        if (mm_iface_modem_is_3gpp (self) && ((ctx->allowed & (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G))) {
            QmiNasGsmWcdmaAcquisitionOrderPreference order;

            order = mm_modem_mode_to_qmi_gsm_wcdma_acquisition_order_preference (ctx->preferred, self);
            qmi_message_nas_set_system_selection_preference_input_set_gsm_wcdma_acquisition_order_preference (input, order, NULL);
        }
    }

    /* Allowed modes */
    pref = mm_modem_mode_to_qmi_rat_mode_preference (ctx->allowed,
                                                     mm_iface_modem_is_cdma (self),
                                                     mm_iface_modem_is_3gpp (self));
    qmi_message_nas_set_system_selection_preference_input_set_mode_preference (input, pref, NULL);

    qmi_client_nas_set_system_selection_preference (
        ctx->client,
        input,
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)set_current_modes_system_selection_preference_ready,
        task);
    qmi_message_nas_set_system_selection_preference_input_unref (input);
}

void
mm_shared_qmi_set_current_modes (MMIfaceModem        *self,
                                 MMModemMode          allowed,
                                 MMModemMode          preferred,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    SetCurrentModesContext *ctx;
    GTask                  *task;
    QmiClient              *client = NULL;
    Private                *priv;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_current_modes_context_free);

    priv = get_private (MM_SHARED_QMI (self));

    if (priv->feature_nas_ssp == FEATURE_SUPPORTED) {
        set_current_modes_system_selection_preference (task);
        return;
    }

    if (priv->feature_nas_tp == FEATURE_SUPPORTED) {
        set_current_modes_technology_preference (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Setting allowed modes is not supported by this device");
    g_object_unref (task);
}

/*****************************************************************************/
/* Load current modes (Modem interface) */

typedef struct {
    QmiClientNas *client;
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

gboolean
mm_shared_qmi_load_current_modes_finish (MMIfaceModem  *self,
                                         GAsyncResult  *res,
                                         MMModemMode   *allowed,
                                         MMModemMode   *preferred,
                                         GError       **error)
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

static void
get_technology_preference_ready (QmiClientNas *client,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    LoadCurrentModesResult                     *result = NULL;
    QmiMessageNasGetTechnologyPreferenceOutput *output = NULL;
    GError                                     *error = NULL;
    MMModemMode                                 allowed;
    QmiNasRadioTechnologyPreference             preference_mask;

    output = qmi_client_nas_get_technology_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_get_technology_preference_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        goto out;
    }

    qmi_message_nas_get_technology_preference_output_get_active (
        output,
        &preference_mask,
        NULL, /* duration */
        NULL);
    allowed = mm_modem_mode_from_qmi_radio_technology_preference (preference_mask);
    if (allowed == MM_MODEM_MODE_NONE) {
        gchar *str;

        str = qmi_nas_radio_technology_preference_build_string_from_mask (preference_mask);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Unsupported modes reported: '%s'", str);
        g_free (str);
        goto out;
    }

    /* We got a valid value from here */
    result = g_new (LoadCurrentModesResult, 1);
    result->allowed = allowed;
    result->preferred = MM_MODEM_MODE_NONE;
    g_task_return_pointer (task, result, g_free);

out:
    if (output)
        qmi_message_nas_get_technology_preference_output_unref (output);
    g_object_unref (task);
}

static void
load_current_modes_technology_preference (GTask *task)
{
    LoadCurrentModesContext *ctx;

    ctx = g_task_get_task_data (task);

    qmi_client_nas_get_technology_preference (
        ctx->client,
        NULL, /* no input */
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)get_technology_preference_ready,
        task);
}

static void
load_current_modes_system_selection_preference_ready (QmiClientNas *client,
                                                      GAsyncResult *res,
                                                      GTask        *task)
{
    MMSharedQmi                                     *self;
    Private                                         *priv;
    LoadCurrentModesResult                          *result = NULL;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;
    QmiNasRatModePreference                          mode_preference_mask = 0;
    MMModemMode                                      allowed;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        goto out;
    }

    if (!qmi_message_nas_get_system_selection_preference_output_get_mode_preference (
            output,
            &mode_preference_mask,
            NULL)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Mode preference not reported in system selection preference");
        goto out;
    }

    allowed = mm_modem_mode_from_qmi_rat_mode_preference (mode_preference_mask);
    if (allowed == MM_MODEM_MODE_NONE) {
        gchar *str;

        str = qmi_nas_rat_mode_preference_build_string_from_mask (mode_preference_mask);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Unsupported modes reported: '%s'", str);
        g_free (str);
        goto out;
    }

    /* We got a valid value from here */
    result = g_new (LoadCurrentModesResult, 1);
    result->allowed = allowed;
    result->preferred = MM_MODEM_MODE_NONE;

    /* If acquisition order preference is available, always use that first */
    if (priv->feature_nas_ssp_acquisition_order_preference == FEATURE_SUPPORTED) {
        GArray *array;

        if (qmi_message_nas_get_system_selection_preference_output_get_acquisition_order_preference (output, &array, NULL) &&
            array->len > 0) {
            guint i;

            /* The array of preference contains the preference of the full list of supported
             * access technologies, regardless of whether they're enabled or not. So, look for
             * the first one that is flagged as enabled, not just the first one in the array.
             */
            for (i = 0; i < array->len; i++) {
                MMModemMode mode;

                mode = mm_modem_mode_from_qmi_nas_radio_interface (g_array_index (array, QmiNasRadioInterface, i));
                if (allowed == mode)
                    break;
                if (allowed & mode) {
                    result->preferred = mode;
                    break;
                }
            }
        }
    }
    /* For 2G+3G only rely on the GSM/WCDMA acquisition order preference TLV */
    else if (mode_preference_mask == (QMI_NAS_RAT_MODE_PREFERENCE_GSM | QMI_NAS_RAT_MODE_PREFERENCE_UMTS)) {
        QmiNasGsmWcdmaAcquisitionOrderPreference gsm_or_wcdma;

        if (qmi_message_nas_get_system_selection_preference_output_get_gsm_wcdma_acquisition_order_preference (
                output,
                &gsm_or_wcdma,
                NULL))
            result->preferred = mm_modem_mode_from_qmi_gsm_wcdma_acquisition_order_preference (gsm_or_wcdma, self);
    }

    g_task_return_pointer (task, result, g_free);

out:
    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);
    g_object_unref (task);
}

static void
load_current_modes_system_selection_preference (GTask *task)
{
    LoadCurrentModesContext *ctx;

    ctx = g_task_get_task_data (task);
    qmi_client_nas_get_system_selection_preference (
        ctx->client,
        NULL, /* no input */
        5,
        NULL, /* cancellable */
        (GAsyncReadyCallback)load_current_modes_system_selection_preference_ready,
        task);
}

void
mm_shared_qmi_load_current_modes (MMIfaceModem        *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    Private                 *priv;
    LoadCurrentModesContext *ctx;
    GTask                   *task;
    QmiClient               *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    ctx = g_new0 (LoadCurrentModesContext, 1);
    ctx->client = g_object_ref (client);
    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_current_modes_context_free);

    priv = get_private (MM_SHARED_QMI (self));

    if (priv->feature_nas_ssp != FEATURE_UNSUPPORTED) {
        load_current_modes_system_selection_preference (task);
        return;
    }

    if (priv->feature_nas_tp != FEATURE_UNSUPPORTED) {
        load_current_modes_technology_preference (task);
        return;
    }

    /* Default to supported */
    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Loading current modes is not supported by this device");
    g_object_unref (task);
}

/*****************************************************************************/
/* Supported modes (Modem interface) */

GArray *
mm_shared_qmi_load_supported_modes_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

void
mm_shared_qmi_load_supported_modes (MMIfaceModem        *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask                  *task;
    GArray                 *combinations;
    MMModemModeCombination  mode;
    Private                *priv;
    MMModemMode             mask_all;
    guint                   i;
    GArray                 *all;
    GArray                 *filtered;

    task = g_task_new (self, NULL, callback, user_data);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->supported_radio_interfaces);

    /* Build all, based on the supported radio interfaces */
    mask_all = MM_MODEM_MODE_NONE;
    for (i = 0; i < priv->supported_radio_interfaces->len; i++)
        mask_all |= mm_modem_mode_from_qmi_radio_interface (g_array_index (priv->supported_radio_interfaces, QmiDmsRadioInterface, i), self);
    mode.allowed = mask_all;
    mode.preferred = MM_MODEM_MODE_NONE;
    all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    g_array_append_val (all, mode);

    /* If SSP and TP are not supported, ignore supported mode management */
    if (priv->feature_nas_ssp == FEATURE_UNSUPPORTED && priv->feature_nas_tp == FEATURE_UNSUPPORTED) {
        g_task_return_pointer (task, all, (GDestroyNotify) g_array_unref);
        g_object_unref (task);
        return;
    }

    combinations = g_array_new (FALSE, FALSE, sizeof (MMModemModeCombination));

#define ADD_MODE_PREFERENCE(MODE1, MODE2, MODE3, MODE4) do {            \
        mode.allowed = MODE1;                                           \
        if (MODE2 != MM_MODEM_MODE_NONE) {                              \
            mode.allowed |= MODE2;                                      \
            if (MODE3 != MM_MODEM_MODE_NONE) {                          \
                mode.allowed |= MODE3;                                  \
                if (MODE4 != MM_MODEM_MODE_NONE)                        \
                    mode.allowed |= MODE4;                              \
            }                                                           \
            if (priv->feature_nas_ssp != FEATURE_UNSUPPORTED) {         \
                if (MODE3 != MM_MODEM_MODE_NONE) {                      \
                    if (MODE4 != MM_MODEM_MODE_NONE) {                  \
                        mode.preferred = MODE4;                         \
                        g_array_append_val (combinations, mode);        \
                    }                                                   \
                    mode.preferred = MODE3;                             \
                    g_array_append_val (combinations, mode);            \
                }                                                       \
                mode.preferred = MODE2;                                 \
                g_array_append_val (combinations, mode);                \
                mode.preferred = MODE1;                                 \
                g_array_append_val (combinations, mode);                \
            } else {                                                    \
                mode.preferred = MM_MODEM_MODE_NONE;                    \
                g_array_append_val (combinations, mode);                \
            }                                                           \
        } else {                                                        \
            mode.allowed = MODE1;                                       \
            mode.preferred = MM_MODEM_MODE_NONE;                        \
            g_array_append_val (combinations, mode);                    \
        }                                                               \
    } while (0)

    /* 2G-only, 3G-only */
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);

    /* 4G-only mode is not possible in multimode GSM/UMTS+CDMA/EVDO+LTE
     * devices. This configuration may be selected as "LTE only" capability
     * instead. */
    if (!priv->disable_4g_only_mode)
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);

    /* 2G, 3G, 4G combinations */
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_3G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G, MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
    ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_3G, MM_MODEM_MODE_4G,   MM_MODEM_MODE_NONE);

    /* 5G related mode combinations are only supported when NAS SSP is supported,
     * as there is no 5G support in NAS TP. */
    if (priv->feature_nas_ssp != FEATURE_UNSUPPORTED) {
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_5G, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G, MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_4G, MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE, MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_3G,   MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_4G,   MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G, MM_MODEM_MODE_4G,   MM_MODEM_MODE_5G,   MM_MODEM_MODE_NONE);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G, MM_MODEM_MODE_3G,   MM_MODEM_MODE_4G,   MM_MODEM_MODE_5G);
    }

    /* Filter out unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

GArray *
mm_shared_qmi_load_supported_bands_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
dms_get_band_capabilities_ready (QmiClientDms *client,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    MMSharedQmi                            *self;
    Private                                *priv;
    QmiMessageDmsGetBandCapabilitiesOutput *output;
    GError                                 *error = NULL;
    GArray                                 *mm_bands = NULL;
    QmiDmsBandCapability                    qmi_bands = 0;
    QmiDmsLteBandCapability                 qmi_lte_bands = 0;
    GArray                                 *extended_qmi_lte_bands = NULL;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    output = qmi_client_dms_get_band_capabilities_finish (client, res, &error);
    if (!output || !qmi_message_dms_get_band_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get band capabilities: ");
        goto out;
    }

    qmi_message_dms_get_band_capabilities_output_get_band_capability (
        output,
        &qmi_bands,
        NULL);
    qmi_message_dms_get_band_capabilities_output_get_lte_band_capability (
        output,
        &qmi_lte_bands,
        NULL);
    qmi_message_dms_get_band_capabilities_output_get_extended_lte_band_capability (
        output,
        &extended_qmi_lte_bands,
        NULL);

    mm_bands = mm_modem_bands_from_qmi_band_capabilities (qmi_bands, qmi_lte_bands, extended_qmi_lte_bands, self);
    if (mm_bands->len == 0) {
        g_clear_pointer (&mm_bands, g_array_unref);
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't parse the list of supported bands");
        goto out;
    }

    /* Cache the result */
    g_clear_pointer (&priv->supported_bands, g_array_unref);
    priv->supported_bands = g_array_ref (mm_bands);

 out:
    if (output)
        qmi_message_dms_get_band_capabilities_output_unref (output);

    if (error)
        g_task_return_error (task, error);
    else if (mm_bands)
        g_task_return_pointer (task, mm_bands, (GDestroyNotify)g_array_unref);
    else
        g_assert_not_reached ();
    g_object_unref (task);
}

void
mm_shared_qmi_load_supported_bands (MMIfaceModem        *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_dms_get_band_capabilities (QMI_CLIENT_DMS (client),
                                          NULL,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)dms_get_band_capabilities_ready,
                                          task);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

GArray *
mm_shared_qmi_load_current_bands_finish (MMIfaceModem  *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_bands_get_system_selection_preference_ready (QmiClientNas *client,
                                                  GAsyncResult *res,
                                                  GTask        *task)
{
    MMSharedQmi                                     *self;
    Private                                         *priv;
    QmiMessageNasGetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;
    GArray                                          *mm_bands = NULL;
    QmiNasBandPreference                             band_preference_mask = 0;
    QmiNasLteBandPreference                          lte_band_preference_mask = 0;
    guint64                                          extended_lte_band_preference[4] = { 0 };
    guint                                            extended_lte_band_preference_size = 0;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    output = qmi_client_nas_get_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_get_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get system selection preference: ");
        goto out;
    }

    qmi_message_nas_get_system_selection_preference_output_get_band_preference (
        output,
        &band_preference_mask,
        NULL);

    qmi_message_nas_get_system_selection_preference_output_get_lte_band_preference (
        output,
        &lte_band_preference_mask,
        NULL);

    if ((priv->feature_nas_ssp_extended_lte_band_preference == FEATURE_SUPPORTED) &&
        qmi_message_nas_get_system_selection_preference_output_get_extended_lte_band_preference (
            output,
            &extended_lte_band_preference[0],
            &extended_lte_band_preference[1],
            &extended_lte_band_preference[2],
            &extended_lte_band_preference[3],
            NULL))
        extended_lte_band_preference_size = G_N_ELEMENTS (extended_lte_band_preference);

    mm_bands = mm_modem_bands_from_qmi_band_preference (band_preference_mask,
                                                        lte_band_preference_mask,
                                                        extended_lte_band_preference_size ? extended_lte_band_preference : NULL,
                                                        extended_lte_band_preference_size,
                                                        self);

    if (mm_bands->len == 0) {
        g_clear_pointer (&mm_bands, g_array_unref);
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't parse the list of current bands");
    }

 out:

    if (output)
        qmi_message_nas_get_system_selection_preference_output_unref (output);

    if (error)
        g_task_return_error (task, error);
    else if (mm_bands)
        g_task_return_pointer (task, mm_bands, (GDestroyNotify)g_array_unref);
    else
        g_assert_not_reached ();
    g_object_unref (task);
}

void
mm_shared_qmi_load_current_bands (MMIfaceModem        *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

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

gboolean
mm_shared_qmi_set_current_bands_finish (MMIfaceModem  *self,
                                        GAsyncResult  *res,
                                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
bands_set_system_selection_preference_ready (QmiClientNas *client,
                                             GAsyncResult *res,
                                             GTask        *task)
{
    QmiMessageNasSetSystemSelectionPreferenceOutput *output = NULL;
    GError                                          *error = NULL;

    output = qmi_client_nas_set_system_selection_preference_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_system_selection_preference_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set system selection preference: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    if (output)
        qmi_message_nas_set_system_selection_preference_output_unref (output);

    g_object_unref (task);
}

void
mm_shared_qmi_set_current_bands (MMIfaceModem        *self,
                                 GArray              *bands_array,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    QmiMessageNasSetSystemSelectionPreferenceInput *input;
    Private                                        *priv;
    GTask                                          *task;
    QmiClient                                      *client = NULL;
    QmiNasBandPreference                            qmi_bands = 0;
    QmiNasLteBandPreference                         qmi_lte_bands = 0;
    guint64                                         extended_qmi_lte_bands[4] = { 0 };

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_QMI (self));

    /* Handle ANY separately */
    if (bands_array->len == 1 && g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        if (!priv->supported_bands) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Cannot handle 'ANY' if supported bands are unknown");
            g_object_unref (task);
            return;
        }
        bands_array = priv->supported_bands;
    }

    mm_modem_bands_to_qmi_band_preference (bands_array,
                                           &qmi_bands,
                                           &qmi_lte_bands,
                                           priv->feature_nas_ssp_extended_lte_band_preference == FEATURE_SUPPORTED ? extended_qmi_lte_bands : NULL,
                                           G_N_ELEMENTS (extended_qmi_lte_bands),
                                           self);

    input = qmi_message_nas_set_system_selection_preference_input_new ();
    qmi_message_nas_set_system_selection_preference_input_set_band_preference (input, qmi_bands, NULL);
    if (mm_iface_modem_is_3gpp_lte (self)) {
        if (priv->feature_nas_ssp_extended_lte_band_preference == FEATURE_SUPPORTED)
            qmi_message_nas_set_system_selection_preference_input_set_extended_lte_band_preference (
                input,
                extended_qmi_lte_bands[0],
                extended_qmi_lte_bands[1],
                extended_qmi_lte_bands[2],
                extended_qmi_lte_bands[3],
                NULL);
        else
            qmi_message_nas_set_system_selection_preference_input_set_lte_band_preference (input, qmi_lte_bands, NULL);
    }
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
/* Reset (Modem interface) */

gboolean
mm_shared_qmi_reset_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
reset_set_operating_mode_reset_ready (QmiClientDms *client,
                                      GAsyncResult *res,
                                      GTask *task)
{
    MMSharedQmi                         *self;
    QmiMessageDmsSetOperatingModeOutput *output;
    GError                              *error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
    } else {
        mm_obj_info (self, "rebooting now");
        g_task_return_boolean (task, TRUE);
    }

    if (output)
        qmi_message_dms_set_operating_mode_output_unref (output);

    g_object_unref (task);
}

static void
reset_set_operating_mode_offline_ready (QmiClientDms *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    QmiMessageDmsSetOperatingModeInput  *input;
    QmiMessageDmsSetOperatingModeOutput *output;
    GError                              *error = NULL;

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
                                       (GAsyncReadyCallback)reset_set_operating_mode_reset_ready,
                                       task);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

void
mm_shared_qmi_reset (MMIfaceModem        *self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    QmiMessageDmsSetOperatingModeInput *input;
    GTask                              *task;
    QmiClient                          *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
                                       (GAsyncReadyCallback)reset_set_operating_mode_offline_ready,
                                       task);
    qmi_message_dms_set_operating_mode_input_unref (input);
}

/*****************************************************************************/
/* Factory reset (Modem interface) */

gboolean
mm_shared_qmi_factory_reset_finish (MMIfaceModem  *self,
                                    GAsyncResult  *res,
                                    GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
dms_restore_factory_defaults_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    GTask        *task)
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

void
mm_shared_qmi_factory_reset (MMIfaceModem        *self,
                             const gchar         *code,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    QmiMessageDmsRestoreFactoryDefaultsInput *input;
    GTask                                    *task;
    QmiClient                                *client = NULL;
    GError                                   *error = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    mm_obj_dbg (self, "performing a factory reset...");
    qmi_client_dms_restore_factory_defaults (QMI_CLIENT_DMS (client),
                                             input,
                                             10,
                                             NULL,
                                             (GAsyncReadyCallback)dms_restore_factory_defaults_ready,
                                             task);
}

/*****************************************************************************/
/* Setup carrier config (Modem interface) */

#define SETUP_CARRIER_CONFIG_STEP_TIMEOUT_SECS 10
#define GENERIC_CONFIG_FALLBACK "generic"

typedef enum {
    SETUP_CARRIER_CONFIG_STEP_FIRST,
    SETUP_CARRIER_CONFIG_STEP_FIND_REQUESTED,
    SETUP_CARRIER_CONFIG_STEP_CHECK_CHANGE_NEEDED,
    SETUP_CARRIER_CONFIG_STEP_UPDATE_CURRENT,
    SETUP_CARRIER_CONFIG_STEP_ACTIVATE_CURRENT,
    SETUP_CARRIER_CONFIG_STEP_LAST,
} SetupCarrierConfigStep;


typedef struct {
    SetupCarrierConfigStep  step;
    QmiClientPdc           *client;
    GKeyFile               *keyfile;
    gchar                  *imsi;

    gint                    config_requested_i;
    gchar                  *config_requested;

    guint                   token;
    guint                   timeout_id;
    gulong                  set_selected_config_indication_id;
    gulong                  activate_config_indication_id;
} SetupCarrierConfigContext;

/* Allow to cleanup action setup right away, without being tied
 * to the lifecycle of the GTask */
static void
setup_carrier_config_context_cleanup_action (SetupCarrierConfigContext *ctx)
{
    if (ctx->activate_config_indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->activate_config_indication_id);
        ctx->activate_config_indication_id = 0;
    }
    if (ctx->set_selected_config_indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->set_selected_config_indication_id);
        ctx->set_selected_config_indication_id = 0;
    }
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }
}

static void
setup_carrier_config_context_free (SetupCarrierConfigContext *ctx)
{
    setup_carrier_config_context_cleanup_action (ctx);

    g_free (ctx->config_requested);
    g_free (ctx->imsi);
    g_key_file_unref (ctx->keyfile);
    g_clear_object (&ctx->client);
    g_slice_free (SetupCarrierConfigContext, ctx);
}

gboolean
mm_shared_qmi_setup_carrier_config_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void setup_carrier_config_step (GTask *task);

static void
setup_carrier_config_abort (GTask  *task,
                            GError *error)
{
    SetupCarrierConfigContext *ctx;

    ctx = g_task_get_task_data (task);
    setup_carrier_config_context_cleanup_action (ctx);
    g_task_return_error (task, error);
    g_object_unref (task);
}

static gboolean
setup_carrier_config_timeout_no_error (GTask *task)
{
    SetupCarrierConfigContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->timeout_id);
    ctx->timeout_id = 0;

    setup_carrier_config_context_cleanup_action (ctx);
    ctx->step++;
    setup_carrier_config_step (task);

    return G_SOURCE_REMOVE;
}

static gboolean
setup_carrier_config_timeout (GTask *task)
{
    SetupCarrierConfigContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->timeout_id);
    ctx->timeout_id = 0;

    setup_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                                                   "Operation timed out"));

    return G_SOURCE_REMOVE;
}

static void
activate_config_indication (QmiClientPdc                         *client,
                            QmiIndicationPdcActivateConfigOutput *output,
                            GTask                                *task)
{
    SetupCarrierConfigContext *ctx;
    GError                    *error = NULL;
    guint16                    error_code = 0;

    ctx = g_task_get_task_data (task);

    if (!qmi_indication_pdc_activate_config_output_get_indication_result (output, &error_code, &error)) {
        setup_carrier_config_abort (task, error);
        return;
    }

    if (error_code != 0) {
        setup_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                       "couldn't activate config: %s",
                                                       qmi_protocol_error_get_string ((QmiProtocolError) error_code)));
        return;
    }

    /* Go on */
    setup_carrier_config_context_cleanup_action (ctx);
    ctx->step++;
    setup_carrier_config_step (task);
}

static void
activate_config_ready (QmiClientPdc *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    QmiMessagePdcActivateConfigOutput *output;
    SetupCarrierConfigContext         *ctx;
    GError                            *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pdc_activate_config_finish (client, res, &error);
    if (!output || !qmi_message_pdc_activate_config_output_get_result (output, &error)) {
        setup_carrier_config_abort (task, error);
        goto out;
    }

    /* When we activate the config, if the operation is successful, we'll just
     * see the modem going away completely. So, do not consider an error the timeout
     * waiting for the Activate Config indication, as that is actually a good
     * thing.
     */
    ctx->timeout_id = g_timeout_add_seconds (SETUP_CARRIER_CONFIG_STEP_TIMEOUT_SECS,
                                             (GSourceFunc) setup_carrier_config_timeout_no_error,
                                             task);
    ctx->activate_config_indication_id = g_signal_connect (ctx->client,
                                                           "activate-config",
                                                           G_CALLBACK (activate_config_indication),
                                                           task);
out:
    if (output)
        qmi_message_pdc_activate_config_output_unref (output);
}

static void
set_selected_config_indication (QmiClientPdc                            *client,
                                QmiIndicationPdcSetSelectedConfigOutput *output,
                                GTask                                   *task)
{
    SetupCarrierConfigContext *ctx;
    GError                    *error = NULL;
    guint16                    error_code = 0;

    ctx = g_task_get_task_data (task);

    if (!qmi_indication_pdc_set_selected_config_output_get_indication_result (output, &error_code, &error)) {
        setup_carrier_config_abort (task, error);
        return;
    }

    if (error_code != 0) {
        setup_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                       "couldn't set selected config: %s",
                                                       qmi_protocol_error_get_string ((QmiProtocolError) error_code)));
        return;
    }

    /* Go on */
    setup_carrier_config_context_cleanup_action (ctx);
    ctx->step++;
    setup_carrier_config_step (task);
}

static void
set_selected_config_ready (QmiClientPdc *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdcSetSelectedConfigOutput *output;
    SetupCarrierConfigContext            *ctx;
    GError                               *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pdc_set_selected_config_finish (client, res, &error);
    if (!output || !qmi_message_pdc_set_selected_config_output_get_result (output, &error)) {
        setup_carrier_config_abort (task, error);
        goto out;
    }

    ctx->timeout_id = g_timeout_add_seconds (SETUP_CARRIER_CONFIG_STEP_TIMEOUT_SECS,
                                             (GSourceFunc) setup_carrier_config_timeout,
                                             task);
    ctx->set_selected_config_indication_id = g_signal_connect (ctx->client,
                                                               "set-selected-config",
                                                               G_CALLBACK (set_selected_config_indication),
                                                               task);
out:
    if (output)
        qmi_message_pdc_set_selected_config_output_unref (output);
}

static gint
select_newest_carrier_config (MMSharedQmi *self,
                              gint         config_a_i,
                              gint         config_b_i)
{
    Private    *priv;
    ConfigInfo *config_a;
    ConfigInfo *config_b;

    priv = get_private (self);
    config_a = &g_array_index (priv->config_list, ConfigInfo, config_a_i);
    config_b = &g_array_index (priv->config_list, ConfigInfo, config_b_i);

    g_assert (!g_strcmp0 (config_a->description, config_b->description));

    if (config_a->version > config_b->version)
        return config_a_i;
    if (config_b->version > config_a->version)
        return config_b_i;
    /* if both are equal, return the first one found always */
    return config_a_i;
}

static void
find_requested_carrier_config (GTask *task)
{
    SetupCarrierConfigContext *ctx;
    MMSharedQmi               *self;
    Private                   *priv;
    gchar                      mccmnc[7];
    gchar                     *group;
    gint                       config_fallback_i = -1;
    gchar                     *config_fallback = NULL;

    ctx  = g_task_get_task_data (task);
    self = MM_SHARED_QMI (g_task_get_source_object (task));
    priv = get_private (self);

    /* Only one group expected per file, so get the start one */
    group = g_key_file_get_start_group (ctx->keyfile);

    /* Match generic configuration */
    config_fallback = g_key_file_get_string (ctx->keyfile, group, GENERIC_CONFIG_FALLBACK, NULL);
    mm_obj_dbg (self, "fallback carrier configuration %sfound in group '%s'", config_fallback ? "" : "not ", group);

    /* First, try to match 6 MCCMNC digits (3-digit MNCs) */
    strncpy (mccmnc, ctx->imsi, 6);
    mccmnc[6] = '\0';
    ctx->config_requested = g_key_file_get_string (ctx->keyfile, group, mccmnc, NULL);
    if (!ctx->config_requested) {
        /* If not found, try to match 5 MCCMNC digits (2-digit MNCs) */
        mccmnc[5] = '\0';
        ctx->config_requested = g_key_file_get_string (ctx->keyfile, group, mccmnc, NULL);
    }
    mm_obj_dbg (self, "requested carrier configuration %sfound for '%s' in group '%s': %s",
                ctx->config_requested ? "" : "not ", mccmnc, group, ctx->config_requested ? ctx->config_requested : "n/a");

    if (!ctx->config_requested && !config_fallback) {
        setup_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                                       "no valid configuration found in group '%s'", group));
        goto out;
    }

    /* Now, look for the configurations among the ones available in the device */
    if (priv->config_list) {
        guint i;

        for (i = 0; i < priv->config_list->len; i++) {
            ConfigInfo *config;

            config = &g_array_index (priv->config_list, ConfigInfo, i);
            if (ctx->config_requested && !g_strcmp0 (ctx->config_requested, config->description)) {
                mm_obj_dbg (self, "requested carrier configuration '%s' is available (version 0x%08x, size %u bytes)",
                            config->description, config->version, config->total_size);
                if (ctx->config_requested_i < 0)
                    ctx->config_requested_i = i;
                else
                    ctx->config_requested_i = select_newest_carrier_config (self, ctx->config_requested_i, i);
            }
            if (config_fallback && !g_strcmp0 (config_fallback, config->description)) {
                mm_obj_dbg (self, "fallback carrier configuration '%s' is available (version 0x%08x, size %u bytes)",
                            config->description, config->version, config->total_size);
                if (config_fallback_i < 0)
                    config_fallback_i = i;
                else
                    config_fallback_i = select_newest_carrier_config (self, config_fallback_i, i);
            }
        }
    }

    /* Fail operation if we didn't find the one we want */
    if ((ctx->config_requested_i < 0) && (config_fallback_i < 0)) {
        setup_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                       "carrier configurations (requested '%s', fallback '%s') are not available",
                                                       ctx->config_requested, config_fallback));
        goto out;
    }

    /* If the mapping expects a given config, but the config isn't installed,
     * we fallback to generic */
    if (ctx->config_requested_i < 0) {
        ConfigInfo *config;

        g_assert (config_fallback_i >= 0);

        config = &g_array_index (priv->config_list, ConfigInfo, config_fallback_i);
        mm_obj_info (self, "using fallback carrier configuration '%s' (version 0x%08x, size %u bytes)",
                     config->description, config->version, config->total_size);

        g_free (ctx->config_requested);
        ctx->config_requested = config_fallback;
        ctx->config_requested_i = config_fallback_i;
        config_fallback = NULL;
    } else {
        ConfigInfo *config;

        config = &g_array_index (priv->config_list, ConfigInfo, ctx->config_requested_i);
        mm_obj_dbg (self, "using requested carrier configuration '%s' (version 0x%08x, size %u bytes)",
                    config->description, config->version, config->total_size);
    }

    ctx->step++;
    setup_carrier_config_step (task);

out:
    g_free (config_fallback);
    g_free (group);
}

static void
setup_carrier_config_step (GTask *task)
{
    MMSharedQmi               *self;
    SetupCarrierConfigContext *ctx;
    Private                   *priv;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);
    priv = get_private (self);

    switch (ctx->step) {
    case SETUP_CARRIER_CONFIG_STEP_FIRST:
        ctx->step++;
        /* fall-through */

    case SETUP_CARRIER_CONFIG_STEP_FIND_REQUESTED:
        find_requested_carrier_config (task);
        return;

    case SETUP_CARRIER_CONFIG_STEP_CHECK_CHANGE_NEEDED:
        g_assert (ctx->config_requested_i >= 0);
        g_assert (priv->config_active_i >= 0 || priv->config_active_default);
        if (ctx->config_requested_i == priv->config_active_i) {
            mm_obj_info (self, "carrier config switching not needed: already using '%s'", ctx->config_requested);
            ctx->step = SETUP_CARRIER_CONFIG_STEP_LAST;
            setup_carrier_config_step (task);
            return;
        }

        ctx->step++;
        /* fall-through */

    case SETUP_CARRIER_CONFIG_STEP_UPDATE_CURRENT: {
        QmiMessagePdcSetSelectedConfigInput *input;
        ConfigInfo                          *requested_config;
        ConfigInfo                          *active_config;
        QmiConfigTypeAndId                   type_and_id;

        requested_config = &g_array_index (priv->config_list, ConfigInfo, ctx->config_requested_i);
        active_config = (priv->config_active_default ? NULL : &g_array_index (priv->config_list, ConfigInfo, priv->config_active_i));
        mm_obj_warn (self, "carrier config switching needed: '%s' -> '%s'",
                     active_config ? active_config->description : DEFAULT_CONFIG_DESCRIPTION, requested_config->description);

        type_and_id.config_type = requested_config->config_type;;
        type_and_id.id = requested_config->id;

        input = qmi_message_pdc_set_selected_config_input_new ();
        qmi_message_pdc_set_selected_config_input_set_type_with_id (input, &type_and_id, NULL);
        qmi_message_pdc_set_selected_config_input_set_token (input, ctx->token++, NULL);
        qmi_client_pdc_set_selected_config (ctx->client,
                                            input,
                                            10,
                                            NULL,
                                            (GAsyncReadyCallback)set_selected_config_ready,
                                            task);
        qmi_message_pdc_set_selected_config_input_unref (input);
        return;
    }

    case SETUP_CARRIER_CONFIG_STEP_ACTIVATE_CURRENT: {
        QmiMessagePdcActivateConfigInput *input;
        ConfigInfo                       *requested_config;

        requested_config = &g_array_index (priv->config_list, ConfigInfo, ctx->config_requested_i);

        input = qmi_message_pdc_activate_config_input_new ();
        qmi_message_pdc_activate_config_input_set_config_type (input, requested_config->config_type, NULL);
        qmi_message_pdc_activate_config_input_set_token (input, ctx->token++, NULL);
        qmi_client_pdc_activate_config (ctx->client,
                                        input,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback) activate_config_ready,
                                        task);
        qmi_message_pdc_activate_config_input_unref (input);
        return;
    }

    case SETUP_CARRIER_CONFIG_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        break;

    default:
        g_assert_not_reached ();
    }
}

void
mm_shared_qmi_setup_carrier_config (MMIfaceModem        *self,
                                    const gchar         *imsi,
                                    const gchar         *carrier_config_mapping,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    SetupCarrierConfigContext *ctx;
    GTask                     *task;
    QmiClient                 *client = NULL;
    GError                    *error = NULL;

    g_assert (imsi);
    g_assert (carrier_config_mapping);

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (SetupCarrierConfigContext);
    ctx->step = SETUP_CARRIER_CONFIG_STEP_FIRST;
    ctx->imsi = g_strdup (imsi);
    ctx->keyfile = g_key_file_new ();
    ctx->config_requested_i = -1;
    g_task_set_task_data (task, ctx, (GDestroyNotify)setup_carrier_config_context_free);

    /* Load mapping keyfile */
    if (!g_key_file_load_from_file (ctx->keyfile,
                                    carrier_config_mapping,
                                    G_KEY_FILE_NONE,
                                    &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Load PDC client */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (!client) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "QMI PDC not supported");
        g_object_unref (task);
        return;
    }
    ctx->client = g_object_ref (client);

    setup_carrier_config_step (task);
}

/*****************************************************************************/
/* Load carrier config (Modem interface) */

#define LOAD_CARRIER_CONFIG_STEP_TIMEOUT_SECS 5

typedef enum {
    LOAD_CARRIER_CONFIG_STEP_FIRST,
    LOAD_CARRIER_CONFIG_STEP_LIST_CONFIGS,
    LOAD_CARRIER_CONFIG_STEP_QUERY_CURRENT,
    LOAD_CARRIER_CONFIG_STEP_LAST,
} LoadCarrierConfigStep;

typedef struct {
    LoadCarrierConfigStep step;

    QmiClientPdc *client;

    GArray       *config_list;
    guint         configs_loaded;
    gboolean      config_active_default;
    gint          config_active_i;

    guint         token;
    guint         timeout_id;
    gulong        list_configs_indication_id;
    gulong        get_selected_config_indication_id;
    gulong        get_config_info_indication_id;
} LoadCarrierConfigContext;

/* Allow to cleanup action load right away, without being tied
 * to the lifecycle of the GTask */
static void
load_carrier_config_context_cleanup_action (LoadCarrierConfigContext *ctx)
{
    if (ctx->get_selected_config_indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->get_selected_config_indication_id);
        ctx->get_selected_config_indication_id = 0;
    }
    if (ctx->get_config_info_indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->get_config_info_indication_id);
        ctx->get_config_info_indication_id = 0;
    }
    if (ctx->list_configs_indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->list_configs_indication_id);
        ctx->list_configs_indication_id = 0;
    }
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }
}

static void
load_carrier_config_context_free (LoadCarrierConfigContext *ctx)
{
    load_carrier_config_context_cleanup_action (ctx);

    if (ctx->config_list)
        g_array_unref (ctx->config_list);
    g_clear_object (&ctx->client);
    g_slice_free (LoadCarrierConfigContext, ctx);
}

gboolean
mm_shared_qmi_load_carrier_config_finish (MMIfaceModem  *self,
                                          GAsyncResult  *res,
                                          gchar        **carrier_config_name,
                                          gchar        **carrier_config_revision,
                                          GError       **error)
{
    Private *priv;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->config_active_i >= 0 || priv->config_active_default);

    if (priv->config_active_i >= 0) {
        ConfigInfo *config;

        config = &g_array_index (priv->config_list, ConfigInfo, priv->config_active_i);
        *carrier_config_name = g_strdup (config->description);
        *carrier_config_revision = g_strdup_printf ("%08X", config->version);
    } else if (priv->config_active_default) {
        *carrier_config_name = g_strdup (DEFAULT_CONFIG_DESCRIPTION);
        *carrier_config_revision = NULL;
    } else
        g_assert_not_reached ();

    return TRUE;
}

static void load_carrier_config_step (GTask *task);

static void
load_carrier_config_abort (GTask  *task,
                           GError *error)
{
    LoadCarrierConfigContext *ctx;

    ctx = g_task_get_task_data (task);
    load_carrier_config_context_cleanup_action (ctx);
    g_task_return_error (task, error);
    g_object_unref (task);
}

static gboolean
load_carrier_config_timeout (GTask *task)
{
    LoadCarrierConfigContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->timeout_id);
    ctx->timeout_id = 0;

    load_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                                                  "Operation timed out"));

    return G_SOURCE_REMOVE;
}

static void
get_selected_config_indication (QmiClientPdc                            *client,
                                QmiIndicationPdcGetSelectedConfigOutput *output,
                                GTask                                   *task)
{
    MMSharedQmi              *self;
    LoadCarrierConfigContext *ctx;
    GArray                   *active_id = NULL;
    GError                   *error = NULL;
    guint16                   error_code = 0;
    guint                     i;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_indication_pdc_get_selected_config_output_get_indication_result (output, &error_code, &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    if (error_code != 0 &&
        error_code != QMI_PROTOCOL_ERROR_NOT_PROVISIONED) { /* No configs active */
        load_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                      "couldn't get selected config: %s",
                                                      qmi_protocol_error_get_string ((QmiProtocolError) error_code)));
        return;
    }

    qmi_indication_pdc_get_selected_config_output_get_active_id (output, &active_id, NULL);
    if (!active_id) {
        mm_obj_dbg (self, "no carrier config currently selected (default in use)");
        ctx->config_active_default = TRUE;
        goto next;
    }

    g_assert (ctx->config_list);
    g_assert (ctx->config_list->len);

    for (i = 0; i < ctx->config_list->len; i++) {
        ConfigInfo *config;

        config = &g_array_index (ctx->config_list, ConfigInfo, i);
        if ((config->id->len == active_id->len) &&
            !memcmp (config->id->data, active_id->data, active_id->len)) {
            ctx->config_active_i = i;
            break;
        }
    }

    if (i == ctx->config_list->len) {
        load_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                      "couldn't find currently selected config"));
        return;
    }

 next:

    /* Go on */
    load_carrier_config_context_cleanup_action (ctx);
    ctx->step++;
    load_carrier_config_step (task);
}

static void
get_selected_config_ready (QmiClientPdc *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdcGetSelectedConfigOutput *output;
    LoadCarrierConfigContext             *ctx;
    GError                               *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pdc_get_selected_config_finish (client, res, &error);
    if (!output || !qmi_message_pdc_get_selected_config_output_get_result (output, &error)) {
        load_carrier_config_abort (task, error);
        goto out;
    }

    ctx->timeout_id = g_timeout_add_seconds (LOAD_CARRIER_CONFIG_STEP_TIMEOUT_SECS,
                                             (GSourceFunc) load_carrier_config_timeout,
                                             task);
    ctx->get_selected_config_indication_id = g_signal_connect (ctx->client,
                                                               "get-selected-config",
                                                               G_CALLBACK (get_selected_config_indication),
                                                               task);

out:
    if (output)
        qmi_message_pdc_get_selected_config_output_unref (output);
}

static void
get_config_info_indication (QmiClientPdc                        *client,
                            QmiIndicationPdcGetConfigInfoOutput *output,
                            GTask                               *task)
{
    LoadCarrierConfigContext *ctx;
    GError                   *error = NULL;
    ConfigInfo               *current_config = NULL;
    guint32                   token;
    const gchar              *description;
    guint                     i;
    guint16                   error_code = 0;

    ctx = g_task_get_task_data (task);

    if (!qmi_indication_pdc_get_config_info_output_get_indication_result (output, &error_code, &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    if (error_code != 0) {
        load_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                      "couldn't get config info: %s",
                                                      qmi_protocol_error_get_string ((QmiProtocolError) error_code)));
        return;
    }

    if (!qmi_indication_pdc_get_config_info_output_get_token (output, &token, &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    /* Look for the current config in the list, match by token */
    for (i = 0; i < ctx->config_list->len; i++) {
        current_config = &g_array_index (ctx->config_list, ConfigInfo, i);
        if (current_config->token == token)
            break;
    }

    /* Ignore if not found in the list */
    if (i == ctx->config_list->len)
        return;

    /* Ignore if already set */
    if (current_config->description)
        return;

    /* Store total size, version and description of the current config */
    if (!qmi_indication_pdc_get_config_info_output_get_total_size  (output, &current_config->total_size, &error) ||
        !qmi_indication_pdc_get_config_info_output_get_version     (output, &current_config->version,    &error) ||
        !qmi_indication_pdc_get_config_info_output_get_description (output, &description,                &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    current_config->description = g_strdup (description);
    ctx->configs_loaded++;

    /* If not all loaded, wait for more */
    if (ctx->configs_loaded < ctx->config_list->len)
        return;

    /* Go on */
    load_carrier_config_context_cleanup_action (ctx);
    ctx->step++;
    load_carrier_config_step (task);
}

static void
list_configs_indication (QmiClientPdc                      *client,
                         QmiIndicationPdcListConfigsOutput *output,
                         GTask                             *task)
{
    MMSharedQmi              *self;
    LoadCarrierConfigContext *ctx;
    GError                   *error = NULL;
    GArray                   *configs = NULL;
    guint                     i;
    guint16                   error_code = 0;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_indication_pdc_list_configs_output_get_indication_result (output, &error_code, &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    if (error_code != 0) {
        load_carrier_config_abort (task, g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                      "couldn't list configs: %s",
                                                      qmi_protocol_error_get_string ((QmiProtocolError) error_code)));
        return;
    }

    if (!qmi_indication_pdc_list_configs_output_get_configs (output, &configs, &error)) {
        load_carrier_config_abort (task, error);
        return;
    }

    /* If no configs are installed, the module is running with the default one */
    if (!configs || !configs->len) {
        ctx->config_active_default = TRUE;
        ctx->step = LOAD_CARRIER_CONFIG_STEP_LAST;
        load_carrier_config_step (task);
        return;
    }

    /* Preallocate config list and request details for each */
    mm_obj_dbg (self, "found %u carrier configurations...", configs->len);
    ctx->config_list = g_array_sized_new (FALSE, TRUE, sizeof (ConfigInfo), configs->len);
    g_array_set_size (ctx->config_list, configs->len);
    g_array_set_clear_func (ctx->config_list, (GDestroyNotify) config_info_clear);

    ctx->get_config_info_indication_id = g_signal_connect (ctx->client,
                                                           "get-config-info",
                                                           G_CALLBACK (get_config_info_indication),
                                                           task);

    for (i = 0; i < configs->len; i++) {
        ConfigInfo                                      *current_info;
        QmiIndicationPdcListConfigsOutputConfigsElement *element;
        QmiConfigTypeAndId                               type_with_id;
        QmiMessagePdcGetConfigInfoInput                 *input;

        element = &g_array_index (configs, QmiIndicationPdcListConfigsOutputConfigsElement, i);

        current_info              = &g_array_index (ctx->config_list, ConfigInfo, i);
        current_info->token       = ctx->token++;
        current_info->id          = g_array_ref (element->id);
        current_info->config_type = element->config_type;

        input = qmi_message_pdc_get_config_info_input_new ();
        type_with_id.config_type = element->config_type;
        type_with_id.id = current_info->id;
        qmi_message_pdc_get_config_info_input_set_type_with_id (input, &type_with_id, NULL);
        qmi_message_pdc_get_config_info_input_set_token (input, current_info->token, NULL);
        qmi_client_pdc_get_config_info (ctx->client, input, 10, NULL, NULL, NULL); /* ignore response! */
        qmi_message_pdc_get_config_info_input_unref (input);
    }
}

static void
list_configs_ready (QmiClientPdc *client,
                    GAsyncResult *res,
                    GTask        *task)
{
    QmiMessagePdcListConfigsOutput *output;
    LoadCarrierConfigContext       *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pdc_list_configs_finish (client, res, &error);
    if (!output || !qmi_message_pdc_list_configs_output_get_result (output, &error)) {
        load_carrier_config_abort (task, error);
        goto out;
    }

    ctx->timeout_id = g_timeout_add_seconds (LOAD_CARRIER_CONFIG_STEP_TIMEOUT_SECS,
                                             (GSourceFunc) load_carrier_config_timeout,
                                             task);
    ctx->list_configs_indication_id = g_signal_connect (ctx->client,
                                                        "list-configs",
                                                        G_CALLBACK (list_configs_indication),
                                                        task);
out:
    if (output)
        qmi_message_pdc_list_configs_output_unref (output);
}

static void
load_carrier_config_step (GTask *task)
{
    LoadCarrierConfigContext *ctx;
    Private                  *priv;

    ctx  = g_task_get_task_data (task);
    priv = get_private (g_task_get_source_object (task));

    switch (ctx->step) {
    case LOAD_CARRIER_CONFIG_STEP_FIRST:
        ctx->step++;
        /* fall-through */

    case LOAD_CARRIER_CONFIG_STEP_LIST_CONFIGS: {
        QmiMessagePdcListConfigsInput *input;

        input = qmi_message_pdc_list_configs_input_new ();
        qmi_message_pdc_list_configs_input_set_config_type (input, QMI_PDC_CONFIGURATION_TYPE_SOFTWARE, NULL);
        qmi_message_pdc_list_configs_input_set_token (input, ctx->token++, NULL);
        qmi_client_pdc_list_configs (ctx->client,
                                     input,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)list_configs_ready,
                                     task);
        qmi_message_pdc_list_configs_input_unref (input);
        return;
    }

    case LOAD_CARRIER_CONFIG_STEP_QUERY_CURRENT: {
        QmiMessagePdcGetSelectedConfigInput *input;

        input = qmi_message_pdc_get_selected_config_input_new ();
        qmi_message_pdc_get_selected_config_input_set_config_type (input, QMI_PDC_CONFIGURATION_TYPE_SOFTWARE, NULL);
        qmi_message_pdc_get_selected_config_input_set_token (input, ctx->token++, NULL);
        qmi_client_pdc_get_selected_config (ctx->client,
                                            input,
                                            5,
                                            NULL,
                                            (GAsyncReadyCallback)get_selected_config_ready,
                                            task);
        qmi_message_pdc_get_selected_config_input_unref (input);
        return;
    }

    case LOAD_CARRIER_CONFIG_STEP_LAST:
        /* We will now store the loaded information so that we can later on use it
         * if needed during the automatic carrier config switching operation */
        g_assert (priv->config_active_i < 0 && !priv->config_active_default);
        g_assert (ctx->config_active_i >= 0 || ctx->config_active_default);
        priv->config_list = ctx->config_list ? g_array_ref (ctx->config_list) : NULL;
        priv->config_active_i = ctx->config_active_i;
        priv->config_active_default = ctx->config_active_default;

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        break;

    default:
        g_assert_not_reached ();
    }
}

void
mm_shared_qmi_load_carrier_config (MMIfaceModem        *self,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    LoadCarrierConfigContext *ctx;
    GTask                    *task;
    QmiClient                *client = NULL;


    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (LoadCarrierConfigContext);
    ctx->step = LOAD_CARRIER_CONFIG_STEP_FIRST;
    ctx->config_active_i = -1;
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_carrier_config_context_free);

    /* Load PDC client */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (!client) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "QMI PDC not supported");
        g_object_unref (task);
        return;
    }
    ctx->client = g_object_ref (client);

    load_carrier_config_step (task);
}

/*****************************************************************************/
/* Load SIM slots (modem interface) */

typedef struct {
    QmiClientUim *client_uim;
    GPtrArray    *sim_slots;
    GList        *sorted_sims;
    MMBaseSim    *current_sim;
    guint         current_slot_number;
    guint         active_slot_number;
    guint         active_logical_id;
} LoadSimSlotsContext;

static void
load_sim_slots_context_free (LoadSimSlotsContext *ctx)
{
    g_clear_object (&ctx->current_sim);
    g_list_free_full (ctx->sorted_sims, (GDestroyNotify)g_object_unref);
    g_clear_pointer (&ctx->sim_slots, g_ptr_array_unref);
    g_clear_object (&ctx->client_uim);
    g_slice_free (LoadSimSlotsContext, ctx);
}

static void
sim_slot_free (MMBaseSim *sim)
{
    if (sim)
        g_object_unref (sim);
}

gboolean
mm_shared_qmi_load_sim_slots_finish (MMIfaceModem  *self,
                                     GAsyncResult  *res,
                                     GPtrArray    **sim_slots,
                                     guint         *primary_sim_slot,
                                     GError       **error)
{
    LoadSimSlotsContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (sim_slots)
        *sim_slots = g_steal_pointer (&ctx->sim_slots);
    if (primary_sim_slot)
        *primary_sim_slot = ctx->active_slot_number;
    return TRUE;
}

static void
active_slot_switch_ready (QmiClientUim *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    g_autoptr(QmiMessageUimSwitchSlotOutput) output = NULL;
    g_autoptr(GError)                        error = NULL;
    MMIfaceModem                            *self;
    LoadSimSlotsContext                     *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_uim_switch_slot_finish (client, res, &error);
    if ((!output || !qmi_message_uim_switch_slot_output_get_result (output, &error)) &&
        !g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
        mm_obj_err (self, "couldn't switch to original slot %u", ctx->active_slot_number);
        g_task_return_error (task, g_steal_pointer (&error));
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
reload_active_slot (GTask *task)
{
    g_autoptr(QmiMessageUimSwitchSlotInput)  input = NULL;
    LoadSimSlotsContext                     *ctx;
    MMIfaceModem                            *self;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* If we're already in the original active SIM slot, nothing else to do */
    if (ctx->current_slot_number == ctx->active_slot_number) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "switching to original active SIM at slot %u", ctx->active_slot_number);

    /* Switch to the original active slot */
    input = qmi_message_uim_switch_slot_input_new ();
    qmi_message_uim_switch_slot_input_set_logical_slot (input, (guint8) ctx->active_logical_id, NULL);
    qmi_message_uim_switch_slot_input_set_physical_slot (input, ctx->active_slot_number, NULL);
    qmi_client_uim_switch_slot (ctx->client_uim,
                                input,
                                10,
                                NULL,
                                (GAsyncReadyCallback) active_slot_switch_ready,
                                task);
}

static void load_next_sim_info (GTask *task);

static void
next_sim_initialize_ready (MMBaseSim    *sim,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autoptr(GError)    error = NULL;
    MMIfaceModem        *self;
    LoadSimSlotsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_base_sim_initialize_finish (sim, res, &error))
        mm_obj_dbg (self, "couldn't initialize SIM at slot %u: won't load additional info",
                    ctx->current_slot_number);
    else
        mm_obj_dbg (self, "initialized SIM at slot %u",
                    ctx->current_slot_number);

    /* Iterate to next SIM */
    load_next_sim_info (task);
}

static void
next_sim_switch_ready (QmiClientUim *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    g_autoptr(QmiMessageUimSwitchSlotOutput) output = NULL;
    g_autoptr(GError)                        error = NULL;
    MMIfaceModem                            *self;
    LoadSimSlotsContext                     *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_uim_switch_slot_finish (client, res, &error);
    if (!output || !qmi_message_uim_switch_slot_output_get_result (output, &error)) {
        /* ignore NoEffect errors on slot switch, because that indicates we're
         * already in the desired slot */
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            mm_obj_dbg (self, "couldn't switch to SIM at slot %u: won't load additional info",
                        ctx->current_slot_number);
            load_next_sim_info (task);
            return;
        }
    }

    mm_obj_dbg (self, "switched to SIM at slot %u: initializing...",
                ctx->current_slot_number);

    mm_base_sim_initialize (ctx->current_sim,
                            NULL,
                            (GAsyncReadyCallback) next_sim_initialize_ready,
                            task);
}

static void
load_next_sim_info (GTask *task)
{
    g_autoptr(QmiMessageUimSwitchSlotInput)  input = NULL;
    LoadSimSlotsContext                     *ctx;
    MMIfaceModem                            *self;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* All done? */
    if (!ctx->sorted_sims) {
        mm_obj_dbg (self, "no more SIMs to load info from");
        reload_active_slot (task);
        return;
    }

    /* Steal SIM from list */
    g_clear_object (&ctx->current_sim);
    ctx->current_sim = MM_BASE_SIM (ctx->sorted_sims->data);
    ctx->sorted_sims = g_list_delete_link (ctx->sorted_sims, ctx->sorted_sims);
    ctx->current_slot_number = mm_base_sim_get_slot_number (ctx->current_sim);

    mm_obj_dbg (self, "switching to SIM at slot %u: %s",
                ctx->current_slot_number, mm_base_sim_get_path (ctx->current_sim));

    /* Switch to the next slot */
    input = qmi_message_uim_switch_slot_input_new ();
    qmi_message_uim_switch_slot_input_set_logical_slot (input, (guint8) ctx->active_logical_id, NULL);
    qmi_message_uim_switch_slot_input_set_physical_slot (input, ctx->current_slot_number, NULL);
    qmi_client_uim_switch_slot (ctx->client_uim,
                                input,
                                10,
                                NULL,
                                (GAsyncReadyCallback) next_sim_switch_ready,
                                task);
}

static void
uim_get_slot_status_ready (QmiClientUim *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autoptr(QmiMessageUimGetSlotStatusOutput) output = NULL;
    LoadSimSlotsContext *ctx;
    MMIfaceModem              *self;
    GError                    *error = NULL;
    GArray                    *physical_slots = NULL;
    GArray                    *ext_information = NULL;
    GArray                    *slot_eids = NULL;
    guint                      i;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_uim_get_slot_status_finish (client, res, &error);
    if (!output ||
        !qmi_message_uim_get_slot_status_output_get_result (output, &error) ||
        !qmi_message_uim_get_slot_status_output_get_physical_slot_status (output, &physical_slots, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* It's fine if we don't have EID information, but it should be well-formed if present. If it's malformed,
     * there is probably a modem firmware bug. */
    if (qmi_message_uim_get_slot_status_output_get_physical_slot_information (output, &ext_information, NULL) &&
        qmi_message_uim_get_slot_status_output_get_slot_eid_information (output, &slot_eids, NULL) &&
        (ext_information->len != physical_slots->len || slot_eids->len != physical_slots->len)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "UIM Get Slot Status returned malformed response");
        g_object_unref (task);
        return;
    }

    ctx->sim_slots = g_ptr_array_new_full (physical_slots->len, (GDestroyNotify) sim_slot_free);

    for (i = 0; i < physical_slots->len; i++) {
        QmiPhysicalSlotStatusSlot      *slot_status;
        QmiPhysicalSlotInformationSlot *slot_info;
        MMBaseSim                      *sim;
        g_autofree gchar               *raw_iccid = NULL;
        g_autofree gchar               *iccid = NULL;
        g_autofree gchar               *eid = NULL;
        g_autoptr(GError)               inner_error = NULL;
        gboolean                        sim_active = FALSE;

        /* Store active slot info */
        slot_status = &g_array_index (physical_slots, QmiPhysicalSlotStatusSlot, i);
        if (slot_status->physical_slot_status == QMI_UIM_SLOT_STATE_ACTIVE) {
            sim_active = TRUE;
            ctx->active_logical_id = slot_status->logical_slot;
            ctx->active_slot_number = i + 1;
            ctx->current_slot_number = ctx->active_slot_number;
        }

        if (!slot_status->iccid->len) {
            mm_obj_dbg (self, "not creating SIM object: no SIM in slot %u", i + 1);
            g_ptr_array_add (ctx->sim_slots, NULL);
            continue;
        }

        raw_iccid = mm_bcd_to_string ((const guint8 *)slot_status->iccid->data, slot_status->iccid->len,
                                      TRUE /* low_nybble_first */);
        if (!raw_iccid) {
            mm_obj_warn (self, "not creating SIM object: failed to convert ICCID from BCD");
            g_ptr_array_add (ctx->sim_slots, NULL);
            continue;
        }

        iccid = mm_3gpp_parse_iccid (raw_iccid, &inner_error);
        if (!iccid) {
            mm_obj_warn (self, "not creating SIM object: couldn't parse SIM iccid: %s", inner_error->message);
            g_ptr_array_add (ctx->sim_slots, NULL);
            continue;
        }

        if (ext_information && slot_eids) {
            slot_info = &g_array_index (ext_information, QmiPhysicalSlotInformationSlot, i);
            if (slot_info->is_euicc) {
                GArray *slot_eid;

                slot_eid = g_array_index (slot_eids, GArray *, i);
                if (slot_eid->len)
                    eid = mm_qmi_uim_decode_eid (slot_eid->data, slot_eid->len);
                if (!eid)
                    mm_obj_dbg (self, "SIM in slot %d is marked as eUICC, but has malformed EID", i + 1);
            }
        }

        sim = mm_sim_qmi_new_initialized (MM_BASE_MODEM (self),
                                          TRUE, /* consider DMS UIM deprecated if we're creating SIM slots */
                                          i + 1, /* slot number is the array index starting at 1 */
                                          sim_active,
                                          iccid,
                                          NULL,  /* imsi unknown */
                                          eid,   /* may be NULL, which is fine */
                                          NULL,  /* operator id unknown */
                                          NULL,  /* operator name unknown */
                                          NULL); /* emergency numbers unknown */
        g_ptr_array_add (ctx->sim_slots, sim);

        if (sim_active)
            ctx->sorted_sims = g_list_append (ctx->sorted_sims, g_object_ref (sim));
        else
            ctx->sorted_sims = g_list_prepend (ctx->sorted_sims, g_object_ref (sim));
    }
    g_assert_cmpuint (ctx->sim_slots->len, ==, physical_slots->len);

    /* Now, iterate over all the SIMs, we'll attempt to load info from them by
     * quickly switching over to them, leaving the active SIM to the end */
    load_next_sim_info (task);
}

void
mm_shared_qmi_load_sim_slots (MMIfaceModem        *self,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    LoadSimSlotsContext *ctx;
    GTask               *task;
    QmiClient           *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_UIM, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (LoadSimSlotsContext);
    ctx->client_uim = g_object_ref (client);
    g_task_set_task_data (task, ctx, (GDestroyNotify) load_sim_slots_context_free);

    qmi_client_uim_get_slot_status (ctx->client_uim,
                                    NULL,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback) uim_get_slot_status_ready,
                                    task);
}

/*****************************************************************************/
/* Set Primary SIM slot (modem interface) */

gboolean
mm_shared_qmi_set_primary_sim_slot_finish (MMIfaceModem  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_switch_slot_ready (QmiClientUim *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    g_autoptr(QmiMessageUimSwitchSlotOutput)  output = NULL;
    g_autoptr(GError)                         error = NULL;
    MMIfaceModem                             *self;

    self = g_task_get_source_object (task);

    output = qmi_client_uim_switch_slot_finish (client, res, &error);
    if (!output || !qmi_message_uim_switch_slot_output_get_result (output, &error)) {
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_EXISTS,
                                     "SIM slot switch operation not needed");
        else
            g_task_return_error (task, g_steal_pointer (&error));
    } else {
        mm_obj_info (self, "SIM slot switch operation request successful");
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
uim_switch_get_slot_status_ready (QmiClientUim *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    g_autoptr(QmiMessageUimGetSlotStatusOutput) output = NULL;
    g_autoptr(QmiMessageUimSwitchSlotInput)     input = NULL;
    MMIfaceModem *self;
    GError       *error = NULL;
    GArray       *physical_slots = NULL;
    guint         i;
    guint         active_logical_id = 0;
    guint         active_slot_number;
    guint         slot_number;

    self = g_task_get_source_object (task);
    slot_number = GPOINTER_TO_UINT (g_task_get_task_data (task));

    output = qmi_client_uim_get_slot_status_finish (client, res, &error);
    if (!output ||
        !qmi_message_uim_get_slot_status_output_get_result (output, &error) ||
        !qmi_message_uim_get_slot_status_output_get_physical_slot_status (output, &physical_slots, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < physical_slots->len; i++) {
        QmiPhysicalSlotStatusSlot *slot_status;

        /* We look for the currently ACTIVE SIM card only! */
        slot_status = &g_array_index (physical_slots, QmiPhysicalSlotStatusSlot, i);
        if (slot_status->physical_slot_status != QMI_UIM_SLOT_STATE_ACTIVE)
            continue;

        active_logical_id = slot_status->logical_slot;
        active_slot_number = i + 1;
    }

    if (!active_logical_id) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "couldn't find active slot logical ID");
        g_object_unref (task);
        return;
    }

    if (active_slot_number == slot_number) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_EXISTS,
                                 "SIM slot switch operation not needed");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "requesting active logical id %d switch to SIM slot %u", active_logical_id, slot_number);

    input = qmi_message_uim_switch_slot_input_new ();
    qmi_message_uim_switch_slot_input_set_logical_slot (input, (guint8) active_logical_id, NULL);
    qmi_message_uim_switch_slot_input_set_physical_slot (input, slot_number, NULL);
    qmi_client_uim_switch_slot (client,
                                input,
                                10,
                                NULL,
                                (GAsyncReadyCallback) uim_switch_slot_ready,
                                task);
}

void
mm_shared_qmi_set_primary_sim_slot (MMIfaceModem        *self,
                                    guint                sim_slot,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_UIM, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (sim_slot), NULL);

    qmi_client_uim_get_slot_status (QMI_CLIENT_UIM (client),
                                    NULL,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback) uim_switch_get_slot_status_ready,
                                    task);
}

/*****************************************************************************/
/* SIM hot swap detection */

#define REFRESH_START_TIMEOUT_SECS  3

gboolean
mm_shared_qmi_setup_sim_hot_swap_finish (MMIfaceModem  *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
uim_refresh_complete (QmiClientUim      *client,
                      QmiUimSessionType  session_type)
{
    g_autoptr(QmiMessageUimRefreshCompleteInput)  refresh_complete_input = NULL;
    GArray                                       *dummy_aid;

    dummy_aid = g_array_new (FALSE, FALSE, sizeof (guint8));

    refresh_complete_input = qmi_message_uim_refresh_complete_input_new ();
    qmi_message_uim_refresh_complete_input_set_session (
        refresh_complete_input,
        session_type,
        dummy_aid, /* ignored */
        NULL);
    qmi_message_uim_refresh_complete_input_set_info (
        refresh_complete_input,
        TRUE,
        NULL);

    qmi_client_uim_refresh_complete (
        client,
        refresh_complete_input,
        10,
        NULL,
        NULL,
        NULL);
    g_array_unref (dummy_aid);
}

static gboolean
uim_start_refresh_timeout (MMSharedQmi *self)
{
    Private *priv;

    priv = get_private (self);
    priv->uim_refresh_start_timeout_id = 0;

    mm_obj_dbg (self, "refresh start timed out; trigger SIM change check");

    mm_iface_modem_check_for_sim_swap (MM_IFACE_MODEM (self), 0, NULL, NULL, NULL);

    return G_SOURCE_REMOVE;
}

static void
uim_refresh_indication_cb (QmiClientUim                  *client,
                           QmiIndicationUimRefreshOutput *output,
                           MMSharedQmi                   *self)
{
    QmiUimRefreshStage  stage;
    QmiUimRefreshMode   mode;
    QmiUimSessionType   session_type;
    Private            *priv;
    g_autoptr(GError)   error = NULL;

    priv = get_private (self);

    if (!qmi_indication_uim_refresh_output_get_event (output,
                                                      &stage,
                                                      &mode,
                                                      &session_type,
                                                      NULL,
                                                      NULL,
                                                      &error)) {
        mm_obj_warn (self, "couldn't process UIM refresh indication: %s", error->message);
        return;
    }

    mm_obj_dbg (self, "refresh indication received: session type '%s', stage '%s', mode '%s'",
                qmi_uim_session_type_get_string (session_type),
                qmi_uim_refresh_stage_get_string (stage),
                qmi_uim_refresh_mode_get_string (mode));

    /* Support only the first slot for now. Primary GW provisioning is used in old modems. */
    if (session_type != QMI_UIM_SESSION_TYPE_CARD_SLOT_1 &&
        session_type != QMI_UIM_SESSION_TYPE_PRIMARY_GW_PROVISIONING) {
        mm_obj_warn (self, "refresh session type not supported: %s", qmi_uim_session_type_get_string (session_type));
        return;
    }

    /* Currently we handle only UICC Reset type refresh, which can be used
     * in profile switch scenarios. In other cases we just trigger 'refresh
     * complete' during start phase. Signal to notify about potential SIM
     * profile switch is triggered when the refresh is ending. If it were
     * triggered in start phase, reading SIM files seems to fail with
     * an internal error.
     *
     * It's possible that 'end-with-success' stage never appears. For that,
     * we start a timer at 'start' stage and if it expires, the SIM change
     * check is triggered anyway. */
    if (stage == QMI_UIM_REFRESH_STAGE_START) {
        if (mode == QMI_UIM_REFRESH_MODE_RESET) {
            if (!priv->uim_refresh_start_timeout_id)
                priv->uim_refresh_start_timeout_id = g_timeout_add_seconds (REFRESH_START_TIMEOUT_SECS,
                                                                            (GSourceFunc)uim_start_refresh_timeout,
                                                                            self);
        } else
            uim_refresh_complete (client, session_type);
    } else if (stage == QMI_UIM_REFRESH_STAGE_END_WITH_SUCCESS) {
        if (mode == QMI_UIM_REFRESH_MODE_RESET) {
            if (priv->uim_refresh_start_timeout_id) {
                g_source_remove (priv->uim_refresh_start_timeout_id);
                priv->uim_refresh_start_timeout_id = 0;
            }
            mm_iface_modem_check_for_sim_swap (MM_IFACE_MODEM (self), 0, NULL, NULL, NULL);
        }
    }
}

static void
uim_slot_status_indication_cb (QmiClientUim                     *client,
                               QmiIndicationUimSlotStatusOutput *output,
                               MMSharedQmi                      *self)
{
    GArray            *physical_slots = NULL;
    guint              i;
    g_autoptr(GError)  error = NULL;

    mm_obj_dbg (self, "received slot status indication");

    if (!qmi_indication_uim_slot_status_output_get_physical_slot_status (output,
                                                                         &physical_slots,
                                                                         &error)) {
        mm_obj_warn (self, "could not process slot status indication: %s", error->message);
        return;
    }

    for (i = 0; i < physical_slots->len; i++) {
        QmiPhysicalSlotStatusSlot *slot_status;

        slot_status = &g_array_index (physical_slots, QmiPhysicalSlotStatusSlot, i);

        /* We only care about active slot changes */
        if (slot_status->physical_slot_status == QMI_UIM_SLOT_STATE_ACTIVE) {
            g_autofree gchar *iccid = NULL;

            if (slot_status->iccid && slot_status->iccid->len > 0) {
                iccid = mm_bcd_to_string ((const guint8 *) slot_status->iccid->data, slot_status->iccid->len,
                                          TRUE /* low_nybble_first */);
            }

            mm_iface_modem_check_for_sim_swap (MM_IFACE_MODEM (self),
                                               i + 1, /* Slot index */
                                               iccid,
                                               NULL,
                                               NULL);
        }
    }
}

static void
uim_refresh_register_iccid_change_ready (QmiClientUim *client,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    MMSharedQmi                                   *self;
    Private                                       *priv;
    g_autoptr(QmiMessageUimRefreshRegisterOutput)  output = NULL;
    g_autoptr(GError)                              error = NULL;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    output = qmi_client_uim_refresh_register_finish (client, res, &error);
    if (!output || !qmi_message_uim_refresh_register_output_get_result (output, &error)) {
        mm_obj_dbg (self, "refresh registration using 'refresh register' failed: %s", error->message);
        g_clear_object (&priv->uim_client);
        g_task_return_new_error (task, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,
                                 "SIM hot swap detection not supported by modem");
    } else {
        mm_obj_dbg (self, "registered for SIM refresh events using 'refresh register'");
        priv->uim_refresh_indication_id =
            g_signal_connect (client,
                              "refresh",
                              G_CALLBACK (uim_refresh_indication_cb),
                              self);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

/* This is the last resort if 'refresh register all' does not work. It works
 * on some older modems. Those modems may not also support QMI_UIM_SESSION_TYPE_CARD_SLOT_1
 * so we'll use QMI_UIM_SESSION_TYPE_PRIMARY_GW_PROVISIONING */
static void
uim_refresh_register_iccid_change (GTask *task)
{
    MMSharedQmi                                       *self;
    Private                                           *priv;
    QmiMessageUimRefreshRegisterInputInfoFilesElement  file_element;
    guint8                                             val;
    g_autoptr(QmiMessageUimRefreshRegisterInput)       refresh_register_input = NULL;
    g_autoptr(GArray)                                  dummy_aid = NULL;
    g_autoptr(GArray)                                  file = NULL;
    g_autoptr(GArray)                                  file_element_path = NULL;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));

    mm_obj_dbg (self, "register for refresh file indication");

    dummy_aid = g_array_new (FALSE, FALSE, sizeof (guint8));

    file = g_array_sized_new (FALSE, FALSE, sizeof (QmiMessageUimRefreshRegisterInputInfoFilesElement), 1);

    file_element_path = g_array_sized_new (FALSE, FALSE, sizeof (guint8), 2);
    val = 0x00;
    g_array_append_val (file_element_path, val);
    val = 0x3F;
    g_array_append_val (file_element_path, val);


    memset (&file_element, 0, sizeof (file_element));
    file_element.file_id = 0x2FE2; /* ICCID */
    file_element.path = file_element_path;
    g_array_append_val (file, file_element);

    refresh_register_input = qmi_message_uim_refresh_register_input_new ();
    qmi_message_uim_refresh_register_input_set_info (refresh_register_input,
                                                     TRUE,
                                                     FALSE,
                                                     file,
                                                     NULL);
    qmi_message_uim_refresh_register_input_set_session (refresh_register_input,
                                                        QMI_UIM_SESSION_TYPE_PRIMARY_GW_PROVISIONING,
                                                        dummy_aid,
                                                        NULL);

    qmi_client_uim_refresh_register (QMI_CLIENT_UIM (priv->uim_client),
                                     refresh_register_input,
                                     10,
                                     NULL,
                                     (GAsyncReadyCallback) uim_refresh_register_iccid_change_ready,
                                     task);
}

/* Refresh registration and event handling.
 * This is used only as fallback in case slot status indications do not work
 * in the particular modem (determined by UIM Get Slot Status failing) for
 * detecting ICCID changing due to a profile switch.
 *
 * We assume that devices not supporting UIM Get Slot Status only have a
 * single slot, for which we register refresh events.
 */

static void
uim_refresh_register_all_ready (QmiClientUim *client,
                                GAsyncResult *res,
                                GTask        *task)
{
    g_autoptr(QmiMessageUimRefreshRegisterAllOutput)  output = NULL;
    g_autoptr(GError)                                 error = NULL;
    MMIfaceModem                                     *self;
    Private                                          *priv;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));

    output = qmi_client_uim_refresh_register_all_finish (client, res, &error);
    if (!output || !qmi_message_uim_refresh_register_all_output_get_result (output, &error)) {
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NOT_SUPPORTED) ||
            g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND)) {
            /* As last resort, if 'refresh register all' fails, try a plain 'refresh register'.
             * Some older modems may not support 'refresh register all'. */
            uim_refresh_register_iccid_change (task);
            return;
        }

        mm_obj_dbg (self, "refresh register all operation failed: %s", error->message);
        g_clear_object (&priv->uim_client);
        g_task_return_error (task, g_steal_pointer (&error));
    } else {
        mm_obj_dbg (self, "registered for all SIM refresh events");
        priv->uim_refresh_indication_id =
            g_signal_connect (client,
                              "refresh",
                              G_CALLBACK (uim_refresh_indication_cb),
                              self);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
uim_slot_status_not_supported (GTask *task)
{
    MMIfaceModem                                    *self;
    Private                                         *priv;
    g_autoptr(QmiMessageUimRefreshRegisterAllInput)  refresh_register_all_input = NULL;
    g_autoptr(GArray)                                dummy_aid = NULL;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));

    g_assert (!priv->uim_refresh_indication_id);

    mm_obj_dbg (self, "slot status not supported by modem: register for refresh indications");

    dummy_aid = g_array_new (FALSE, FALSE, sizeof (guint8));
    refresh_register_all_input = qmi_message_uim_refresh_register_all_input_new ();

    qmi_message_uim_refresh_register_all_input_set_info (refresh_register_all_input,
                                                         TRUE,
                                                         NULL);
    qmi_message_uim_refresh_register_all_input_set_session (refresh_register_all_input,
                                                            QMI_UIM_SESSION_TYPE_CARD_SLOT_1,
                                                            dummy_aid,
                                                            NULL);

    qmi_client_uim_refresh_register_all (QMI_CLIENT_UIM (priv->uim_client),
                                         refresh_register_all_input,
                                         10,
                                         NULL,
                                         (GAsyncReadyCallback) uim_refresh_register_all_ready,
                                         task);
}

static void
uim_check_get_slot_status_ready (QmiClientUim *client,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    g_autoptr(QmiMessageUimGetSlotStatusOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;
    MMIfaceModem                                *self;
    Private                                     *priv;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));

    output = qmi_client_uim_get_slot_status_finish (client, res, &error);
    if (!output || !qmi_message_uim_get_slot_status_output_get_result (output, &error)) {
        if (priv->uim_slot_status_indication_id) {
            g_signal_handler_disconnect (client, priv->uim_slot_status_indication_id);
            priv->uim_slot_status_indication_id = 0;
        }

        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NOT_SUPPORTED) ||
            g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND)) {
            uim_slot_status_not_supported (task);
            return;
        }

        mm_obj_dbg (self, "slot status retrieval failed: %s", error->message);
        g_clear_object (&priv->uim_client);
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "slot status retrieval succeeded: monitoring slot status indications");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
uim_register_events_ready (QmiClientUim *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autoptr(QmiMessageUimRegisterEventsOutput)  output = NULL;
    g_autoptr(GError)                             error = NULL;
    MMIfaceModem                                 *self;
    Private                                      *priv;

    self = g_task_get_source_object (task);
    priv = get_private (MM_SHARED_QMI (self));

    /* If event registration fails, go on with initialization. In that case
     * we cannot use slot status indications to detect eUICC profile switches. */
    output = qmi_client_uim_register_events_finish (client, res, &error);
    if (output && qmi_message_uim_register_events_output_get_result (output, &error)) {
        g_assert (!priv->uim_slot_status_indication_id);
        priv->uim_slot_status_indication_id = g_signal_connect (priv->uim_client,
                                                                "slot-status",
                                                                G_CALLBACK (uim_slot_status_indication_cb),
                                                                self);
        mm_obj_dbg (self, "registered for slot status indications");

        /* Successful registration does not mean that the modem actually sends
         * physical slot status indications; invoke Get Slot Status to find out if
         * the modem really supports slot status. */
        qmi_client_uim_get_slot_status (client,
                                        NULL,
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback) uim_check_get_slot_status_ready,
                                        task);
        return;
    }

    mm_obj_dbg (self, "not registered for slot status indications: %s", error->message);
    uim_slot_status_not_supported (task);
}

void
mm_shared_qmi_setup_sim_hot_swap (MMIfaceModem        *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    g_autoptr(QmiMessageUimRegisterEventsInput)  register_events_input = NULL;
    GTask                                       *task;
    QmiClient                                   *client = NULL;
    Private                                     *priv;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_UIM, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    priv = get_private (MM_SHARED_QMI (self));

    g_assert (!priv->uim_slot_status_indication_id);
    g_assert (!priv->uim_client);
    priv->uim_client = g_object_ref (client);

    register_events_input = qmi_message_uim_register_events_input_new ();
    qmi_message_uim_register_events_input_set_event_registration_mask (register_events_input,
                                                                       QMI_UIM_EVENT_REGISTRATION_FLAG_PHYSICAL_SLOT_STATUS,
                                                                       NULL);
    qmi_client_uim_register_events (QMI_CLIENT_UIM (priv->uim_client),
                                    register_events_input,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback) uim_register_events_ready,
                                    task);
}

/*****************************************************************************/
/* FCC unlock (Modem interface) */

gboolean
mm_shared_qmi_fcc_unlock_finish (MMIfaceModem  *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
dms_set_fcc_authentication_ready (QmiClientDms *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    GError                                             *error = NULL;
    g_autoptr(QmiMessageDmsSetFccAuthenticationOutput)  output = NULL;

    output = qmi_client_dms_set_fcc_authentication_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_fcc_authentication_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_fcc_unlock (MMIfaceModem        *self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_dms_set_fcc_authentication (QMI_CLIENT_DMS (client),
                                           NULL,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback)dms_set_fcc_authentication_ready,
                                           task);
}

/*****************************************************************************/
/* Location: Set SUPL server */

typedef struct {
    QmiClient *client;
    gchar     *supl;
    glong      indication_id;
    guint      timeout_id;
} SetSuplServerContext;

static void
set_supl_server_context_free (SetSuplServerContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (SetSuplServerContext, ctx);
}

static GArray *
parse_as_utf16_url (const gchar *supl)
{
    GArray *url;
    gchar  *utf16;
    gsize   utf16_len;

    utf16 = g_convert (supl, -1, "UTF-16BE", "UTF-8", NULL, &utf16_len, NULL);
    url = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), utf16_len),
                               utf16, utf16_len);
    g_free (utf16);
    return url;
}

gboolean
mm_shared_qmi_location_set_supl_server_finish (MMIfaceModemLocation  *self,
                                               GAsyncResult          *res,
                                               GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_set_agps_config_ready (QmiClientPds *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdsSetAgpsConfigOutput *output;
    GError                           *error = NULL;

    output = qmi_client_pds_set_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_agps_config_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    qmi_message_pds_set_agps_config_output_unref (output);
}

static void
pds_set_supl_server (GTask *task)
{
    MMSharedQmi                     *self;
    SetSuplServerContext            *ctx;
    QmiMessagePdsSetAgpsConfigInput *input;
    guint32                          ip;
    guint16                          port;
    GArray                          *url;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    input = qmi_message_pds_set_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    if (mm_parse_supl_address (ctx->supl, NULL, &ip, &port, NULL))
        qmi_message_pds_set_agps_config_input_set_location_server_address (input, ip, port, NULL);
    else {
        url = parse_as_utf16_url (ctx->supl);
        qmi_message_pds_set_agps_config_input_set_location_server_url (input, url, NULL);
        g_array_unref (url);
    }

    qmi_client_pds_set_agps_config (
        QMI_CLIENT_PDS (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_set_agps_config_ready,
        task);
    qmi_message_pds_set_agps_config_input_unref (input);
}

static gboolean
loc_location_set_server_indication_timed_out (GTask *task)
{
    SetSuplServerContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the server update result");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_set_server_indication_cb (QmiClientLoc                    *client,
                                       QmiIndicationLocSetServerOutput *output,
                                       GTask                           *task)
{
    QmiLocIndicationStatus  status;
    GError                 *error = NULL;

    if (!qmi_indication_loc_set_server_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    mm_error_from_qmi_loc_indication_status (status, &error);

out:
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_set_server_ready (QmiClientLoc *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    SetSuplServerContext         *ctx;
    QmiMessageLocSetServerOutput *output;
    GError                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_set_server_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_set_server_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_set_server_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "set-server",
                                           G_CALLBACK (loc_location_set_server_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_set_server_indication_timed_out,
                                             task);

    qmi_message_loc_set_server_output_unref (output);
}

static void
loc_set_supl_server (GTask *task)
{
    MMSharedQmi                 *self;
    SetSuplServerContext        *ctx;
    QmiMessageLocSetServerInput *input;
    guint32                      ip;
    guint16                      port;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_loc_set_server_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_loc_set_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_UMTS_SLP, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_loc_set_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_CDMA_PDE, NULL);

    if (mm_parse_supl_address (ctx->supl, NULL, &ip, &port, NULL))
        qmi_message_loc_set_server_input_set_ipv4 (input, ip, (guint32) port, NULL);
    else
        qmi_message_loc_set_server_input_set_url (input, ctx->supl, NULL);

    qmi_client_loc_set_server (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_set_server_ready,
        task);
    qmi_message_loc_set_server_input_unref (input);
}

void
mm_shared_qmi_location_set_supl_server (MMIfaceModemLocation *self,
                                        const gchar          *supl,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data)
{
    GTask                *task;
    SetSuplServerContext *ctx;
    QmiClient            *client = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SetSuplServerContext);
    ctx->supl = g_strdup (supl);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_supl_server_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        pds_set_supl_server (task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        loc_set_supl_server (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: Load SUPL server */

typedef struct {
    QmiClient *client;
    glong      indication_id;
    guint      timeout_id;
} LoadSuplServerContext;

static void
load_supl_server_context_free (LoadSuplServerContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (LoadSuplServerContext, ctx);
}

gchar *
mm_shared_qmi_location_load_supl_server_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
pds_get_agps_config_ready (QmiClientPds *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    QmiMessagePdsGetAgpsConfigOutput *output;
    GError                           *error = NULL;
    guint32                           ip = 0;
    guint32                           port = 0;
    GArray                           *url = NULL;
    gchar                            *str = NULL;

    output = qmi_client_pds_get_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!qmi_message_pds_get_agps_config_output_get_result (output, &error))
        goto out;

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
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot convert numeric IP address (%u) to string", ip);
            goto out;
        }

        str = g_strdup_printf ("%s:%u", buf, port);
        goto out;
    }

    if (qmi_message_pds_get_agps_config_output_get_location_server_url (
            output,
            &url,
            NULL) &&
        url->len > 0) {
        str = g_convert (url->data, url->len, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    }

    if (!str)
        str = g_strdup ("");

out:
    if (error)
        g_task_return_error (task, error);
    else {
        g_assert (str);
        g_task_return_pointer (task, str, g_free);
    }
    g_object_unref (task);

    if (output)
        qmi_message_pds_get_agps_config_output_unref (output);
}

static void
pds_load_supl_server (GTask *task)
{
    MMSharedQmi                     *self;
    LoadSuplServerContext           *ctx;
    QmiMessagePdsGetAgpsConfigInput *input;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_pds_get_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    qmi_client_pds_get_agps_config (
        QMI_CLIENT_PDS (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_get_agps_config_ready,
        task);
    qmi_message_pds_get_agps_config_input_unref (input);
}

static gboolean
loc_location_get_server_indication_timed_out (GTask *task)
{
    LoadSuplServerContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the current server settings");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_get_server_indication_cb (QmiClientLoc                    *client,
                                       QmiIndicationLocGetServerOutput *output,
                                       GTask                           *task)
{
    QmiLocIndicationStatus  status;
    const gchar            *url          = NULL;
    guint32                 ipv4_address = 0;
    guint16                 ipv4_port    = 0;
    GError                 *error        = NULL;
    gchar                  *str          = NULL;

    if (!qmi_indication_loc_get_server_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error))
        goto out;

    /* Prefer IP/PORT to URL */

    if (qmi_indication_loc_get_server_output_get_ipv4 (
            output,
            &ipv4_address,
            &ipv4_port,
            NULL) &&
        ipv4_address != 0 && ipv4_port != 0) {
        struct in_addr a = { .s_addr = ipv4_address };
        gchar buf[INET_ADDRSTRLEN + 1];

        memset (buf, 0, sizeof (buf));

        if (!inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1)) {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot convert numeric IP address (%u) to string", ipv4_address);
            goto out;
        }

        str = g_strdup_printf ("%s:%u", buf, ipv4_port);
        goto out;
    }

    if (qmi_indication_loc_get_server_output_get_url (
            output,
            &url,
            NULL) &&
        url && url [0]) {
        str = g_strdup (url);
    }

    if (!str)
        str = g_strdup ("");

out:
    if (error)
        g_task_return_error (task, error);
    else {
        g_assert (str);
        g_task_return_pointer (task, str, g_free);
    }
    g_object_unref (task);

}

static void
loc_get_server_ready (QmiClientLoc *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    LoadSuplServerContext        *ctx;
    QmiMessageLocGetServerOutput *output;
    GError                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_get_server_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_get_server_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_get_server_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-server",
                                           G_CALLBACK (loc_location_get_server_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_get_server_indication_timed_out,
                                             task);

    qmi_message_loc_get_server_output_unref (output);
}

static void
loc_load_supl_server (GTask *task)
{
    MMSharedQmi                 *self;
    LoadSuplServerContext       *ctx;
    QmiMessageLocGetServerInput *input;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    input = qmi_message_loc_get_server_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_loc_get_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_UMTS_SLP, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_loc_get_server_input_set_server_type (input, QMI_LOC_SERVER_TYPE_CDMA_PDE, NULL);

    qmi_message_loc_get_server_input_set_server_address_type (
        input,
        (QMI_LOC_SERVER_ADDRESS_TYPE_IPV4 | QMI_LOC_SERVER_ADDRESS_TYPE_URL),
        NULL);

    qmi_client_loc_get_server (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_get_server_ready,
        task);
    qmi_message_loc_get_server_input_unref (input);
}

void
mm_shared_qmi_location_load_supl_server (MMIfaceModemLocation *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    QmiClient             *client;
    GTask                 *task;
    LoadSuplServerContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (LoadSuplServerContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_supl_server_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        pds_load_supl_server (task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        loc_load_supl_server (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: stop gps engine */

static gboolean
stop_gps_engine_finish (MMSharedQmi   *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_gps_service_state_stop_ready (QmiClientPds *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMSharedQmi                           *self;
    Private                               *priv;
    QmiMessagePdsSetGpsServiceStateOutput *output;
    GError                                *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }

        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (priv->pds_client) {
        if (priv->pds_location_event_report_indication_id != 0) {
            g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
            priv->pds_location_event_report_indication_id = 0;
        }
        g_clear_object (&priv->pds_client);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_stop_ready (QmiClientLoc *client,
                GAsyncResult *res,
                GTask        *task)
{
    MMSharedQmi             *self;
    Private                 *priv;
    QmiMessageLocStopOutput *output;
    GError                  *error = NULL;

    output = qmi_client_loc_stop_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_stop_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't stop GPS engine: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_stop_output_unref (output);
        return;
    }

    qmi_message_loc_stop_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (priv->loc_client) {
        if (priv->loc_location_nmea_indication_id != 0) {
            g_signal_handler_disconnect (priv->loc_client, priv->loc_location_nmea_indication_id);
            priv->loc_location_nmea_indication_id = 0;
        }
        g_clear_object (&priv->loc_client);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
stop_gps_engine (MMSharedQmi         *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);

    if (priv->pds_client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, FALSE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (priv->pds_client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_stop_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    if (priv->loc_client) {
        QmiMessageLocStopInput *input;

        input = qmi_message_loc_stop_input_new ();
        qmi_message_loc_stop_input_set_session_id (input, DEFAULT_LOC_SESSION_ID, NULL);
        qmi_client_loc_stop (QMI_CLIENT_LOC (priv->loc_client),
                             input,
                             10,
                             NULL,
                             (GAsyncReadyCallback) loc_stop_ready,
                             task);
        qmi_message_loc_stop_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helpers: NMEA indication callbacks */

static void
pds_location_event_report_indication_cb (QmiClientPds                      *client,
                                         QmiIndicationPdsEventReportOutput *output,
                                         MMSharedQmi                       *self)
{
    QmiPdsPositionSessionStatus  session_status;
    const gchar                 *nmea;

    if (qmi_indication_pds_event_report_output_get_position_session_status (
            output,
            &session_status,
            NULL)) {
        mm_obj_dbg (self, "[GPS] session status changed: '%s'",
                    qmi_pds_position_session_status_get_string (session_status));
    }

    if (qmi_indication_pds_event_report_output_get_nmea_position (
            output,
            &nmea,
            NULL)) {
        mm_obj_dbg (self, "[NMEA] %s", nmea);
        mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
    }
}

static void
loc_location_nmea_indication_cb (QmiClientLoc               *client,
                                 QmiIndicationLocNmeaOutput *output,
                                 MMSharedQmi                *self)
{
    const gchar *nmea = NULL;

    qmi_indication_loc_nmea_output_get_nmea_string (output, &nmea, NULL);
    if (!nmea)
        return;

    mm_obj_dbg (self, "[NMEA] %s", nmea);
    mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
}

/*****************************************************************************/
/* Location: internal helper: setup minimum required NMEA traces */

typedef struct {
    QmiClientLoc *client;
    guint         timeout_id;
    gulong        indication_id;
} SetupRequiredNmeaTracesContext;

static void
setup_required_nmea_traces_cleanup_action (SetupRequiredNmeaTracesContext *ctx)
{
    if (ctx->indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        ctx->indication_id = 0;
    }
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }
}

static void
setup_required_nmea_traces_context_free (SetupRequiredNmeaTracesContext *ctx)
{
    setup_required_nmea_traces_cleanup_action (ctx);
    g_clear_object (&ctx->client);
    g_slice_free (SetupRequiredNmeaTracesContext, ctx);
}

static gboolean
setup_required_nmea_traces_finish (MMSharedQmi   *self,
                                   GAsyncResult  *res,
                                   GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
setup_required_nmea_traces_timeout (GTask *task)
{
    SetupRequiredNmeaTracesContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->timeout_id);
    setup_required_nmea_traces_cleanup_action (ctx);

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Operation timed out");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_set_nmea_types_indication_cb (QmiClientLoc                       *client,
                                  QmiIndicationLocSetNmeaTypesOutput *output,
                                  GTask                              *task)
{
    SetupRequiredNmeaTracesContext *ctx;
    QmiLocIndicationStatus          status;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->indication_id);
    setup_required_nmea_traces_cleanup_action (ctx);

    if (!qmi_indication_loc_set_nmea_types_output_get_indication_status (output, &status, &error) ||
        !mm_error_from_qmi_loc_indication_status (status, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_set_nmea_types_ready (QmiClientLoc *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    SetupRequiredNmeaTracesContext             *ctx;
    GError                                     *error = NULL;
    g_autoptr(QmiMessageLocSetNmeaTypesOutput)  output = NULL;

    output = qmi_client_loc_set_nmea_types_finish (client, res, &error);
    if (!output || !qmi_message_loc_set_nmea_types_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx = g_task_get_task_data (task);
    g_assert (!ctx->indication_id);
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "set-nmea-types",
                                           G_CALLBACK (loc_set_nmea_types_indication_cb),
                                           task);
    g_assert (!ctx->timeout_id);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)setup_required_nmea_traces_timeout,
                                             task);
}

static void
loc_get_nmea_types_indication_cb (QmiClientLoc                       *client,
                                  QmiIndicationLocGetNmeaTypesOutput *output,
                                  GTask                              *task)
{
    SetupRequiredNmeaTracesContext            *ctx;
    QmiLocIndicationStatus                     status;
    QmiLocNmeaType                             nmea_types_mask = 0;
    QmiLocNmeaType                             desired_nmea_types_mask = (QMI_LOC_NMEA_TYPE_GGA | QMI_LOC_NMEA_TYPE_GSA | QMI_LOC_NMEA_TYPE_GSV);
    GError                                    *error = NULL;
    g_autoptr(QmiMessageLocSetNmeaTypesInput)  input = NULL;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->indication_id);
    setup_required_nmea_traces_cleanup_action (ctx);

    if (!qmi_indication_loc_get_nmea_types_output_get_indication_status (output, &status, &error) ||
        !mm_error_from_qmi_loc_indication_status (status, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_indication_loc_get_nmea_types_output_get_nmea_types (output, &nmea_types_mask, NULL);

    /* If the configured NMEA types already include GGA, GSV and GSA, we're fine. For raw
     * GPS sources GGA is the only required one, the other two are given for completeness */
    if ((nmea_types_mask & desired_nmea_types_mask) == desired_nmea_types_mask) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    input = qmi_message_loc_set_nmea_types_input_new ();
    qmi_message_loc_set_nmea_types_input_set_nmea_types (input, (nmea_types_mask | desired_nmea_types_mask), NULL);
    qmi_client_loc_set_nmea_types (ctx->client,
                                   input,
                                   10,
                                   NULL,
                                   (GAsyncReadyCallback)loc_set_nmea_types_ready,
                                   task);
}

static void
loc_get_nmea_types_ready (QmiClientLoc *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    SetupRequiredNmeaTracesContext             *ctx;
    GError                                     *error = NULL;
    g_autoptr(QmiMessageLocGetNmeaTypesOutput)  output = NULL;

    output = qmi_client_loc_get_nmea_types_finish (client, res, &error);
    if (!output || !qmi_message_loc_get_nmea_types_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx = g_task_get_task_data (task);
    g_assert (!ctx->indication_id);
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-nmea-types",
                                           G_CALLBACK (loc_get_nmea_types_indication_cb),
                                           task);
    g_assert (!ctx->timeout_id);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)setup_required_nmea_traces_timeout,
                                             task);
}

static void
setup_required_nmea_traces (MMSharedQmi         *self,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    QmiClient *client;
    GTask     *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If using PDS, no further setup required */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        SetupRequiredNmeaTracesContext *ctx;

        ctx = g_slice_new0 (SetupRequiredNmeaTracesContext);
        ctx->client = g_object_ref (client);
        g_task_set_task_data (task, ctx, (GDestroyNotify)setup_required_nmea_traces_context_free);

        qmi_client_loc_get_nmea_types (ctx->client,
                                       NULL,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)loc_get_nmea_types_ready,
                                       task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: start gps engine */

static gboolean
start_gps_engine_finish (MMSharedQmi   *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_ser_location_ready (QmiClientPds *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMSharedQmi                       *self;
    Private                           *priv;
    QmiMessagePdsSetEventReportOutput *output;
    GError                            *error = NULL;

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
    priv = get_private (self);

    g_assert (!priv->pds_client);
    g_assert (priv->pds_location_event_report_indication_id == 0);
    priv->pds_client = g_object_ref (client);
    priv->pds_location_event_report_indication_id =
        g_signal_connect (priv->pds_client,
                          "event-report",
                          G_CALLBACK (pds_location_event_report_indication_cb),
                          self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_auto_tracking_state_start_ready (QmiClientPds *client,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    QmiMessagePdsSetEventReportInput        *input;
    QmiMessagePdsSetAutoTrackingStateOutput *output = NULL;
    GError                                  *error = NULL;

    output = qmi_client_pds_set_auto_tracking_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_auto_tracking_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set auto-tracking state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
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
        client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)pds_ser_location_ready,
        task);
    qmi_message_pds_set_event_report_input_unref (input);
}

static void
pds_gps_service_state_start_ready (QmiClientPds *client,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    QmiMessagePdsSetAutoTrackingStateInput *input;
    QmiMessagePdsSetGpsServiceStateOutput  *output;
    GError                                 *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
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
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_auto_tracking_state_start_ready,
        task);
    qmi_message_pds_set_auto_tracking_state_input_unref (input);
}

static void
loc_register_events_ready (QmiClientLoc *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    MMSharedQmi                       *self;
    Private                           *priv;
    QmiMessageLocRegisterEventsOutput *output;
    GError                            *error = NULL;

   output = qmi_client_loc_register_events_finish (client, res, &error);
   if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
   }

    if (!qmi_message_loc_register_events_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't not register tracking events: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_register_events_output_unref (output);
        return;
    }

    qmi_message_loc_register_events_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    g_assert (!priv->loc_client);
    g_assert (!priv->loc_location_nmea_indication_id);
    priv->loc_client = g_object_ref (client);
    priv->loc_location_nmea_indication_id =
        g_signal_connect (client,
                          "nmea",
                          G_CALLBACK (loc_location_nmea_indication_cb),
                          self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_start_ready (QmiClientLoc *client,
                 GAsyncResult *res,
                 GTask        *task)
{
    QmiMessageLocRegisterEventsInput *input;
    QmiMessageLocStartOutput         *output;
    GError                           *error = NULL;

    output = qmi_client_loc_start_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_start_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't start GPS engine: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_start_output_unref (output);
        return;
    }

    qmi_message_loc_start_output_unref (output);

    input = qmi_message_loc_register_events_input_new ();
    qmi_message_loc_register_events_input_set_event_registration_mask (
        input, QMI_LOC_EVENT_REGISTRATION_FLAG_NMEA, NULL);
    qmi_client_loc_register_events (client,
                                    input,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback) loc_register_events_ready,
                                    task);
    qmi_message_loc_register_events_input_unref (input);
}

static void
start_gps_engine (MMSharedQmi         *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    QmiClient *client;
    GTask     *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, TRUE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_start_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        QmiMessageLocStartInput *input;

        input = qmi_message_loc_start_input_new ();
        qmi_message_loc_start_input_set_session_id (input, DEFAULT_LOC_SESSION_ID, NULL);
        qmi_message_loc_start_input_set_intermediate_report_state (input, QMI_LOC_INTERMEDIATE_REPORT_STATE_DISABLE, NULL);
        qmi_message_loc_start_input_set_minimum_interval_between_position_reports (input, 1000, NULL);
        qmi_message_loc_start_input_set_fix_recurrence_type (input, QMI_LOC_FIX_RECURRENCE_TYPE_REQUEST_PERIODIC_FIXES, NULL);
        qmi_client_loc_start (QMI_CLIENT_LOC (client),
                              input,
                              10,
                              NULL,
                              (GAsyncReadyCallback) loc_start_ready,
                              task);
        qmi_message_loc_start_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: select operation mode (msa/msb/standalone) */

typedef enum {
    GPS_OPERATION_MODE_UNKNOWN,
    GPS_OPERATION_MODE_STANDALONE,
    GPS_OPERATION_MODE_AGPS_MSA,
    GPS_OPERATION_MODE_AGPS_MSB,
} GpsOperationMode;

typedef struct {
    QmiClient        *client;
    GpsOperationMode  mode;
    glong             indication_id;
    guint             timeout_id;
} SetGpsOperationModeContext;

static void
set_gps_operation_mode_context_free (SetGpsOperationModeContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove  (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (SetGpsOperationModeContext, ctx);
}

static gboolean
set_gps_operation_mode_finish (MMSharedQmi   *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_set_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MMSharedQmi                                  *self;
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

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

    switch (ctx->mode) {
        case GPS_OPERATION_MODE_AGPS_MSA:
            mm_obj_dbg (self, "MSA A-GPS operation mode enabled");
            break;
        case GPS_OPERATION_MODE_AGPS_MSB:
            mm_obj_dbg (self, "MSB A-GPS operation mode enabled");
            break;
        case GPS_OPERATION_MODE_STANDALONE:
            mm_obj_dbg (self, "standalone mode enabled (A-GPS disabled)");
            break;
        case GPS_OPERATION_MODE_UNKNOWN:
        default:
            g_assert_not_reached ();
    }
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_get_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MMSharedQmi                                  *self;
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionInput  *input;
    QmiMessagePdsGetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;
    QmiPdsOperatingMode                           session_operation;
    guint8                                        data_timeout;
    guint32                                       interval;
    guint32                                       accuracy_threshold;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

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

    qmi_message_pds_get_default_tracking_session_output_get_info (
        output,
        &session_operation,
        &data_timeout,
        &interval,
        &accuracy_threshold,
        NULL);

    qmi_message_pds_get_default_tracking_session_output_unref (output);

    if (ctx->mode == GPS_OPERATION_MODE_AGPS_MSA) {
        if (session_operation == QMI_PDS_OPERATING_MODE_MS_ASSISTED) {
            mm_obj_dbg (self, "MSA A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to enable MSA A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_MS_ASSISTED;
    } else if (ctx->mode == GPS_OPERATION_MODE_AGPS_MSB) {
        if (session_operation == QMI_PDS_OPERATING_MODE_MS_BASED) {
            mm_obj_dbg (self, "MSB A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to enable MSB A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_MS_BASED;
    } else if (ctx->mode == GPS_OPERATION_MODE_STANDALONE) {
        if (session_operation == QMI_PDS_OPERATING_MODE_STANDALONE) {
            mm_obj_dbg (self, "A-GPS already disabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to disable A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_STANDALONE;
    } else
        g_assert_not_reached ();

    input = qmi_message_pds_set_default_tracking_session_input_new ();
    qmi_message_pds_set_default_tracking_session_input_set_info (
        input,
        session_operation,
        data_timeout,
        interval,
        accuracy_threshold,
        NULL);
    qmi_client_pds_set_default_tracking_session (
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_set_default_tracking_session_ready,
        task);
    qmi_message_pds_set_default_tracking_session_input_unref (input);
}

static gboolean
loc_location_operation_mode_indication_timed_out (GTask *task)
{
    SetGpsOperationModeContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive operation mode indication");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_set_operation_mode_indication_cb (QmiClientLoc                           *client,
                                               QmiIndicationLocSetOperationModeOutput *output,
                                               GTask                                  *task)
{
    MMSharedQmi                *self;
    SetGpsOperationModeContext *ctx;
    QmiLocIndicationStatus      status;
    GError                     *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_indication_loc_set_operation_mode_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    switch (ctx->mode) {
        case GPS_OPERATION_MODE_AGPS_MSA:
            mm_obj_dbg (self, "MSA A-GPS operation mode enabled");
            break;
        case GPS_OPERATION_MODE_AGPS_MSB:
            mm_obj_dbg (self, "MSB A-GPS operation mode enabled");
            break;
        case GPS_OPERATION_MODE_STANDALONE:
            mm_obj_dbg (self, "standalone mode enabled (A-GPS disabled)");
            break;
        case GPS_OPERATION_MODE_UNKNOWN:
        default:
            g_assert_not_reached ();
    }
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
loc_set_operation_mode_ready (QmiClientLoc *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetGpsOperationModeContext          *ctx;
    QmiMessageLocSetOperationModeOutput *output;
    GError                              *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_set_operation_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_set_operation_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_set_operation_mode_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "set-operation-mode",
                                           G_CALLBACK (loc_location_set_operation_mode_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_operation_mode_indication_timed_out,
                                             task);

    qmi_message_loc_set_operation_mode_output_unref (output);
}

static void
loc_location_get_operation_mode_indication_cb (QmiClientLoc                           *client,
                                               QmiIndicationLocGetOperationModeOutput *output,
                                               GTask                                  *task)
{
    MMSharedQmi                        *self;
    SetGpsOperationModeContext         *ctx;
    QmiLocIndicationStatus              status;
    GError                             *error = NULL;
    QmiLocOperationMode                 mode = QMI_LOC_OPERATION_MODE_DEFAULT;
    QmiMessageLocSetOperationModeInput *input;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_indication_loc_get_operation_mode_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_indication_loc_get_operation_mode_output_get_operation_mode (output, &mode, NULL);

    if (ctx->mode == GPS_OPERATION_MODE_AGPS_MSA) {
        if (mode == QMI_LOC_OPERATION_MODE_MSA) {
            mm_obj_dbg (self, "MSA A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to enable MSA A-GPS");
        mode = QMI_LOC_OPERATION_MODE_MSA;
    } else if (ctx->mode == GPS_OPERATION_MODE_AGPS_MSB) {
        if (mode == QMI_LOC_OPERATION_MODE_MSB) {
            mm_obj_dbg (self, "MSB A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to enable MSB A-GPS");
        mode = QMI_LOC_OPERATION_MODE_MSB;
    } else if (ctx->mode == GPS_OPERATION_MODE_STANDALONE) {
        if (mode == QMI_LOC_OPERATION_MODE_STANDALONE) {
            mm_obj_dbg (self, "A-GPS already disabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "need to disable A-GPS");
        mode = QMI_LOC_OPERATION_MODE_STANDALONE;
    } else
        g_assert_not_reached ();

    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }

    if (ctx->indication_id) {
        g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        ctx->indication_id = 0;
    }

    input = qmi_message_loc_set_operation_mode_input_new ();
    qmi_message_loc_set_operation_mode_input_set_operation_mode (input, mode, NULL);
    qmi_client_loc_set_operation_mode (
        QMI_CLIENT_LOC (ctx->client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)loc_set_operation_mode_ready,
        task);
    qmi_message_loc_set_operation_mode_input_unref (input);
}

static void
loc_get_operation_mode_ready (QmiClientLoc *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetGpsOperationModeContext          *ctx;
    QmiMessageLocGetOperationModeOutput *output;
    GError                              *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_get_operation_mode_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_get_operation_mode_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_get_operation_mode_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-operation-mode",
                                           G_CALLBACK (loc_location_get_operation_mode_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_operation_mode_indication_timed_out,
                                             task);

    qmi_message_loc_get_operation_mode_output_unref (output);
}

static void
set_gps_operation_mode (MMSharedQmi         *self,
                        GpsOperationMode     mode,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    SetGpsOperationModeContext *ctx;
    GTask                      *task;
    QmiClient                  *client;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SetGpsOperationModeContext);
    ctx->mode = mode;
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_gps_operation_mode_context_free);

    /* Prefer PDS */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        qmi_client_pds_get_default_tracking_session (
            QMI_CLIENT_PDS (ctx->client),
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_get_default_tracking_session_ready,
            task);
        return;
    }

    /* Otherwise LOC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_LOC,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        qmi_client_loc_get_operation_mode (
            QMI_CLIENT_LOC (ctx->client),
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)loc_get_operation_mode_ready,
            task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS/LOC client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: disable */

gboolean
mm_shared_qmi_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
stop_gps_engine_ready (MMSharedQmi  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!stop_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources &= ~source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_gps_operation_mode_standalone_ready (MMSharedQmi  *self,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources &= ~source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_disable_location_gathering (MMIfaceModemLocation  *_self,
                                          MMModemLocationSource  source,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data)
{
    MMSharedQmi            *self;
    Private                *priv;
    GTask                  *task;
    MMModemLocationSource   tmp;

    self = MM_SHARED_QMI (_self);
    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    /* NOTE: no parent disable_location_gathering() implementation */

    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSB))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert (!(priv->pds_client && priv->loc_client));

    /* Disable A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS_MSA || source == MM_MODEM_LOCATION_SOURCE_AGPS_MSB) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_STANDALONE,
                                (GAsyncReadyCallback)set_gps_operation_mode_standalone_ready,
                                task);
        return;
    }

    /* If no more GPS sources enabled, stop GPS */
    tmp = priv->enabled_sources;
    tmp &= ~source;
    if (!(tmp & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        stop_gps_engine (self,
                         (GAsyncReadyCallback)stop_gps_engine_ready,
                         task);
        return;
    }

    /* Otherwise, we have more GPS sources enabled, we shouldn't stop GPS, just
     * return */
    priv->enabled_sources &= ~source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: enable */

gboolean
mm_shared_qmi_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
start_gps_engine_ready (MMSharedQmi  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!start_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources |= source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
setup_required_nmea_traces_ready (MMSharedQmi  *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    g_autoptr(GError) error = NULL;

    /* don't treat this error as fatal */
    if (!setup_required_nmea_traces_finish (self, res, &error))
        mm_obj_warn (self, "couldn't setup required NMEA traces: %s", error->message);

    start_gps_engine (self,
                      (GAsyncReadyCallback)start_gps_engine_ready,
                      task);
}

static void
set_gps_operation_mode_agps_ready (MMSharedQmi  *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources |= source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    MMSharedQmi           *self = MM_SHARED_QMI (_self);
    Private               *priv;
    MMModemLocationSource  source;
    GError                *error = NULL;

    priv = get_private (self);

    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* We only consider GPS related sources in this shared QMI implementation */
    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSB))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enabling MSA A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS_MSA) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_AGPS_MSA,
                                (GAsyncReadyCallback)set_gps_operation_mode_agps_ready,
                                task);
        return;
    }

    /* Enabling MSB A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS_MSB) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_AGPS_MSB,
                                (GAsyncReadyCallback)set_gps_operation_mode_agps_ready,
                                task);
        return;
    }

    /* Only setup NMEA traces and start GPS engine if not done already */
    if (!(priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        setup_required_nmea_traces (self,
                                    (GAsyncReadyCallback)setup_required_nmea_traces_ready,
                                    task);
        return;
    }

    /* GPS already started, we're done */
    priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_enable_location_gathering (MMIfaceModemLocation  *self,
                                         MMModemLocationSource  source,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering_finish);

    /* Chain up parent's gathering enable */
    priv->iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Location: load capabilities */

MMModemLocationSource
mm_shared_qmi_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
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
                                GAsyncResult         *res,
                                GTask                *task)
{
    MMModemLocationSource  sources;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_QMI (self));

    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own checks */

    /* If we have support for the PDS or LOC client, GPS and A-GPS location is supported */
    if ((mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_PDS, MM_PORT_QMI_FLAG_DEFAULT, NULL)) ||
        (mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_LOC, MM_PORT_QMI_FLAG_DEFAULT, NULL)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                    MM_MODEM_LOCATION_SOURCE_AGPS_MSB);

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

void
mm_shared_qmi_location_load_capabilities (MMIfaceModemLocation *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->load_capabilities);
    g_assert (priv->iface_modem_location_parent->load_capabilities_finish);

    priv->iface_modem_location_parent->load_capabilities (self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/
/* Location: load supported assistance data */

gchar **
mm_shared_qmi_location_load_assistance_data_servers_finish (MMIfaceModemLocation  *self,
                                                            GAsyncResult          *res,
                                                            GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

void
mm_shared_qmi_location_load_assistance_data_servers (MMIfaceModemLocation *self,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data)
{
    Private *priv;
    GTask   *task;

    priv = get_private (MM_SHARED_QMI (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, g_strdupv (priv->loc_assistance_data_servers), (GDestroyNotify) g_strfreev);
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: load supported assistance data */

typedef struct {
    QmiClientLoc *client;
    glong         indication_id;
    guint         timeout_id;
} LoadSupportedAssistanceDataContext;

static void
load_supported_assistance_data_context_free (LoadSupportedAssistanceDataContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_slice_free (LoadSupportedAssistanceDataContext, ctx);
}

MMModemLocationAssistanceDataType
mm_shared_qmi_location_load_supported_assistance_data_finish (MMIfaceModemLocation  *self,
                                                              GAsyncResult          *res,
                                                              GError               **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE;
    }
    return (MMModemLocationAssistanceDataType)value;
}

static gboolean
loc_location_get_predicted_orbits_data_source_indication_timed_out (GTask *task)
{
    LoadSupportedAssistanceDataContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the predicted orbits data source");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
loc_location_get_predicted_orbits_data_source_indication_cb (QmiClientLoc                                       *client,
                                                             QmiIndicationLocGetPredictedOrbitsDataSourceOutput *output,
                                                             GTask                                              *task)
{
    MMSharedQmi            *self;
    Private                *priv;
    QmiLocIndicationStatus  status;
    GError                 *error = NULL;
    GArray                 *server_list = NULL;
    gboolean                supported = FALSE;

    if (!qmi_indication_loc_get_predicted_orbits_data_source_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!mm_error_from_qmi_loc_indication_status (status, &error))
        goto out;

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (qmi_indication_loc_get_predicted_orbits_data_source_output_get_server_list (
            output,
            &server_list,
            NULL) &&
        server_list->len > 0) {
        guint      i;
        GPtrArray *tmp;

        tmp = g_ptr_array_sized_new (server_list->len + 1);
        for (i = 0; i < server_list->len; i++) {
            const gchar *server;

            server = g_array_index (server_list, gchar *, i);
            g_ptr_array_add (tmp, g_strdup (server));
        }
        g_ptr_array_add (tmp, NULL);

        g_assert (!priv->loc_assistance_data_servers);
        priv->loc_assistance_data_servers = (gchar **) g_ptr_array_free (tmp, FALSE);

        supported = TRUE;
    }

    if (qmi_indication_loc_get_predicted_orbits_data_source_output_get_allowed_sizes (
            output,
            &priv->loc_assistance_data_max_file_size,
            &priv->loc_assistance_data_max_part_size,
            NULL) &&
        priv->loc_assistance_data_max_file_size > 0 &&
        priv->loc_assistance_data_max_part_size > 0) {
        supported = TRUE;
    }

out:
    if (error)
        g_task_return_error (task, error);
    else if (!supported)
        g_task_return_int (task, MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE);
    else
        g_task_return_int (task, MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_XTRA);
    g_object_unref (task);
}

static void
loc_location_get_predicted_orbits_data_source_ready (QmiClientLoc *client,
                                                     GAsyncResult *res,
                                                     GTask        *task)
{
    LoadSupportedAssistanceDataContext              *ctx;
    QmiMessageLocGetPredictedOrbitsDataSourceOutput *output;
    GError                                          *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_get_predicted_orbits_data_source_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_loc_get_predicted_orbits_data_source_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_loc_get_predicted_orbits_data_source_output_unref (output);
        return;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "get-predicted-orbits-data-source",
                                           G_CALLBACK (loc_location_get_predicted_orbits_data_source_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_get_predicted_orbits_data_source_indication_timed_out,
                                             task);

    qmi_message_loc_get_predicted_orbits_data_source_output_unref (output);
}

void
mm_shared_qmi_location_load_supported_assistance_data (MMIfaceModemLocation  *self,
                                                       GAsyncReadyCallback    callback,
                                                       gpointer               user_data)
{
    LoadSupportedAssistanceDataContext *ctx;
    GTask                              *task;
    QmiClient                          *client;

    task = g_task_new (self, NULL, callback, user_data);

    /* If no LOC client, no assistance data right away */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_LOC, MM_PORT_QMI_FLAG_DEFAULT, NULL);
    if (!client) {
        g_task_return_int (task, MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (LoadSupportedAssistanceDataContext);
    ctx->client = g_object_ref (client);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_supported_assistance_data_context_free);

    qmi_client_loc_get_predicted_orbits_data_source (ctx->client,
                                                     NULL,
                                                     10,
                                                     NULL,
                                                     (GAsyncReadyCallback)loc_location_get_predicted_orbits_data_source_ready,
                                                     task);
}

/*****************************************************************************/
/* Location: inject assistance data */

#define MAX_BYTES_PER_REQUEST 1024

typedef struct {
    QmiClientLoc *client;
    guint8       *data;
    goffset       data_size;
    gulong        total_parts;
    guint32       part_size;
    glong         indication_id;
    guint         timeout_id;
    goffset       i;
    gulong        n_part;
} InjectAssistanceDataContext;

static void
inject_assistance_data_context_free (InjectAssistanceDataContext *ctx)
{
    if (ctx->client) {
        if (ctx->timeout_id)
            g_source_remove (ctx->timeout_id);
        if (ctx->indication_id)
            g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        g_object_unref (ctx->client);
    }
    g_free (ctx->data);
    g_slice_free (InjectAssistanceDataContext, ctx);
}

gboolean
mm_shared_qmi_location_inject_assistance_data_finish (MMIfaceModemLocation  *self,
                                                      GAsyncResult          *res,
                                                      GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
loc_location_inject_data_indication_timed_out (GTask *task)
{
    InjectAssistanceDataContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->timeout_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Failed to receive indication with the server update result");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void inject_xtra_data_next (GTask *task);

static void
loc_location_inject_xtra_data_indication_cb (QmiClientLoc                         *client,
                                             QmiIndicationLocInjectXtraDataOutput *output,
                                             GTask                                *task)
{
    InjectAssistanceDataContext *ctx;
    QmiLocIndicationStatus       status;
    GError                      *error = NULL;

    if (!qmi_indication_loc_inject_xtra_data_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    mm_error_from_qmi_loc_indication_status (status, &error);

out:
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;

    g_signal_handler_disconnect (ctx->client, ctx->indication_id);
    ctx->indication_id = 0;

    inject_xtra_data_next (task);
}

static void
inject_xtra_data_ready (QmiClientLoc *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    QmiMessageLocInjectXtraDataOutput *output;
    InjectAssistanceDataContext       *ctx;
    GError                            *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_inject_xtra_data_finish (client, res, &error);
    if (!output || !qmi_message_loc_inject_xtra_data_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "inject-xtra-data",
                                           G_CALLBACK (loc_location_inject_xtra_data_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_inject_data_indication_timed_out,
                                             task);
out:
    if (output)
        qmi_message_loc_inject_xtra_data_output_unref (output);
}

static void
inject_xtra_data_next (GTask *task)
{
    MMSharedQmi                      *self;
    QmiMessageLocInjectXtraDataInput *input;
    InjectAssistanceDataContext      *ctx;
    goffset                           total_bytes_left;
    gsize                             count;
    GArray                           *data;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->data_size >= ctx->i);
    total_bytes_left = ctx->data_size - ctx->i;
    if (total_bytes_left == 0) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->n_part++;
    count = (total_bytes_left >= ctx->part_size) ? ctx->part_size : total_bytes_left;

    input = qmi_message_loc_inject_xtra_data_input_new ();
    qmi_message_loc_inject_xtra_data_input_set_total_size (
        input,
        (guint32)ctx->data_size,
        NULL);
    qmi_message_loc_inject_xtra_data_input_set_total_parts (
        input,
        (guint16)ctx->total_parts,
        NULL);
    qmi_message_loc_inject_xtra_data_input_set_part_number (
        input,
        (guint16)ctx->n_part,
        NULL);
    data = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), count), &(ctx->data[ctx->i]), count);
    qmi_message_loc_inject_xtra_data_input_set_part_data (
        input,
        data,
        NULL);
    g_array_unref (data);

    ctx->i += count;

    mm_obj_info (self, "injecting xtra data: %" G_GSIZE_FORMAT " bytes (%u/%u)",
                 count, (guint) ctx->n_part, (guint) ctx->total_parts);
    qmi_client_loc_inject_xtra_data (ctx->client,
                                     input,
                                     10,
                                     NULL,
                                     (GAsyncReadyCallback) inject_xtra_data_ready,
                                     task);

    qmi_message_loc_inject_xtra_data_input_unref (input);
}

static void
inject_xtra_data (GTask *task)
{
    InjectAssistanceDataContext *ctx;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->timeout_id == 0);
    g_assert (ctx->indication_id == 0);

    ctx->n_part = 0;
    ctx->i = 0;

    inject_xtra_data_next (task);
}

static void inject_assistance_data_next (GTask *task);

static void
loc_location_inject_predicted_orbits_data_indication_cb (QmiClientLoc                                    *client,
                                                         QmiIndicationLocInjectPredictedOrbitsDataOutput *output,
                                                         GTask                                           *task)
{
    InjectAssistanceDataContext *ctx;
    QmiLocIndicationStatus       status;
    GError                      *error = NULL;

    if (!qmi_indication_loc_inject_predicted_orbits_data_output_get_indication_status (output, &status, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    mm_error_from_qmi_loc_indication_status (status, &error);

out:
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;

    g_signal_handler_disconnect (ctx->client, ctx->indication_id);
    ctx->indication_id = 0;

    inject_assistance_data_next (task);
}

static void
inject_predicted_orbits_data_ready (QmiClientLoc *client,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    QmiMessageLocInjectPredictedOrbitsDataOutput *output;
    InjectAssistanceDataContext                  *ctx;
    GError                                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_loc_inject_predicted_orbits_data_finish (client, res, &error);
    if (!output || !qmi_message_loc_inject_predicted_orbits_data_output_get_result (output, &error)) {
        /* Try with InjectXtra if InjectPredictedOrbits is unsupported */
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            g_error_free (error);
            inject_xtra_data (task);
            goto out;
        }
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* The task ownership is shared between signal and timeout; the one which is
     * scheduled first will cancel the other. */
    ctx->indication_id = g_signal_connect (ctx->client,
                                           "inject-predicted-orbits-data",
                                           G_CALLBACK (loc_location_inject_predicted_orbits_data_indication_cb),
                                           task);
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)loc_location_inject_data_indication_timed_out,
                                             task);
out:
    if (output)
        qmi_message_loc_inject_predicted_orbits_data_output_unref (output);
}

static void
inject_assistance_data_next (GTask *task)
{
    MMSharedQmi                                 *self;
    QmiMessageLocInjectPredictedOrbitsDataInput *input;
    InjectAssistanceDataContext                 *ctx;
    goffset                                      total_bytes_left;
    gsize                                        count;
    GArray                                      *data;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->data_size >= ctx->i);
    total_bytes_left = ctx->data_size - ctx->i;
    if (total_bytes_left == 0) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->n_part++;
    count = (total_bytes_left >= ctx->part_size) ? ctx->part_size : total_bytes_left;

    input = qmi_message_loc_inject_predicted_orbits_data_input_new ();
    qmi_message_loc_inject_predicted_orbits_data_input_set_format_type (
        input,
        QMI_LOC_PREDICTED_ORBITS_DATA_FORMAT_XTRA,
        NULL);
    qmi_message_loc_inject_predicted_orbits_data_input_set_total_size (
        input,
        (guint32)ctx->data_size,
        NULL);
    qmi_message_loc_inject_predicted_orbits_data_input_set_total_parts (
        input,
        (guint16)ctx->total_parts,
        NULL);
    qmi_message_loc_inject_predicted_orbits_data_input_set_part_number (
        input,
        (guint16)ctx->n_part,
        NULL);
    data = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), count), &(ctx->data[ctx->i]), count);
    qmi_message_loc_inject_predicted_orbits_data_input_set_part_data (
        input,
        data,
        NULL);
    g_array_unref (data);

    ctx->i += count;

    mm_obj_info (self, "injecting predicted orbits data: %" G_GSIZE_FORMAT " bytes (%u/%u)",
                 count, (guint) ctx->n_part, (guint) ctx->total_parts);
    qmi_client_loc_inject_predicted_orbits_data (ctx->client,
                                                 input,
                                                 10,
                                                 NULL,
                                                 (GAsyncReadyCallback) inject_predicted_orbits_data_ready,
                                                 task);

    qmi_message_loc_inject_predicted_orbits_data_input_unref (input);
}

void
mm_shared_qmi_location_inject_assistance_data (MMIfaceModemLocation *self,
                                               const guint8         *data,
                                               gsize                 data_size,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
    InjectAssistanceDataContext *ctx;
    QmiClient                   *client;
    GTask                       *task;
    Private                     *priv;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_LOC, &client,
                                      callback, user_data))
        return;

    priv = get_private (MM_SHARED_QMI (self));

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (InjectAssistanceDataContext);
    ctx->client = g_object_ref (client);
    ctx->data = g_memdup (data, data_size);
    ctx->data_size = data_size;
    ctx->part_size = ((priv->loc_assistance_data_max_part_size > 0) ? priv->loc_assistance_data_max_part_size : MAX_BYTES_PER_REQUEST);
    g_task_set_task_data (task, ctx, (GDestroyNotify) inject_assistance_data_context_free);

    if ((ctx->data_size > (G_MAXUINT16 * ctx->part_size)) ||
        ((priv->loc_assistance_data_max_file_size > 0) && (ctx->data_size > priv->loc_assistance_data_max_file_size))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_TOO_MANY,
                                 "Assistance data file is too big");
        g_object_unref (task);
        return;
    }

    ctx->total_parts = (ctx->data_size / ctx->part_size);
    if (ctx->data_size % ctx->part_size)
        ctx->total_parts++;
    g_assert (ctx->total_parts <= G_MAXUINT16);

    mm_obj_dbg (self, "injecting gpsOneXTRA data (%" G_GOFFSET_FORMAT " bytes)...", ctx->data_size);

    inject_assistance_data_next (task);
}

/*****************************************************************************/

QmiClient *
mm_shared_qmi_peek_client (MMSharedQmi    *self,
                           QmiService      service,
                           MMPortQmiFlag   flag,
                           GError        **error)
{
    g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_client);
    return MM_SHARED_QMI_GET_INTERFACE (self)->peek_client (self, service, flag, error);
}

gboolean
mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                             QmiService            service,
                             QmiClient           **o_client,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
    GError    *error = NULL;
    QmiClient *client;

    client = mm_shared_qmi_peek_client (self, service, MM_PORT_QMI_FLAG_DEFAULT, &error);
    if (!client) {
        g_task_report_error (self, callback, user_data, mm_shared_qmi_ensure_client, error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

static void
shared_qmi_init (gpointer g_iface)
{
}

GType
mm_shared_qmi_get_type (void)
{
    static GType shared_qmi_type = 0;

    if (!G_UNLIKELY (shared_qmi_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQmi),  /* class_size */
            shared_qmi_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_qmi_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQmi", &info, 0);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM_3GPP);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM_LOCATION);
    }

    return shared_qmi_type;
}
