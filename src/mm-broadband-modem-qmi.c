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
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc.
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
#include <ModemManager-tags.h>
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-voice.h"
#include "mm-iface-modem-cdma.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-sar.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-oma.h"
#include "mm-shared-qmi.h"
#include "mm-sim-qmi.h"
#include "mm-bearer-qmi.h"
#include "mm-sms-qmi.h"
#include "mm-sms-part-3gpp.h"
#include "mm-sms-part-cdma.h"
#include "mm-call-qmi.h"
#include "mm-call-list.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface);
static void iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface);
static void iface_modem_voice_init (MMIfaceModemVoice *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_oma_init (MMIfaceModemOma *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);
static void iface_modem_sar_init (MMIfaceModemSar *iface);
static void iface_modem_signal_init (MMIfaceModemSignal *iface);
static void shared_qmi_init (MMSharedQmi *iface);

static MMIfaceModemLocation  *iface_modem_location_parent;
static MMIfaceModemMessaging *iface_modem_messaging_parent;
static MMIfaceModemVoice     *iface_modem_voice_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SAR, iface_modem_sar_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_OMA, iface_modem_oma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QMI, shared_qmi_init))

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
    guint nas_event_report_indication_id;
    guint wds_event_report_indication_id;
    guint nas_signal_info_indication_id;

    /* New devices may not support the legacy DMS UIM commands */
    gboolean dms_uim_deprecated;

    /* Whether autoconnect disabling needs to be checked up during
     * the device enabling */
    gboolean autoconnect_checked;

    /* Index of the WDS profile used as initial EPS bearer */
    guint16 default_attach_pdn;

    /* Support for the APN type mask in profiles */
    gboolean apn_type_not_supported;

    /* 3GPP/CDMA registration helpers */
    gchar *current_operator_id;
    gchar *current_operator_description;
    gboolean unsolicited_registration_events_enabled;
    gboolean unsolicited_registration_events_setup;
    guint serving_system_indication_id;
    guint system_info_indication_id;
    guint system_status_indication_id;
    guint network_reject_indication_id;

    /* CDMA activation helpers */
    MMModemCdmaActivationState activation_state;
    guint activation_event_report_indication_id;
    GTask *activation_task;

    /* Messaging helpers */
    gboolean messaging_fallback_at_only;
    gboolean messaging_unsolicited_events_enabled;
    gboolean messaging_unsolicited_events_setup;
    guint messaging_event_report_indication_id;

    /* Location helpers */
    MMModemLocationSource enabled_sources;

    /* Oma helpers */
    gboolean oma_unsolicited_events_enabled;
    gboolean oma_unsolicited_events_setup;
    guint oma_event_report_indication_id;

    /* 3GPP USSD helpers */
    guint ussd_indication_id;
    guint ussd_release_indication_id;
    gboolean ussd_unsolicited_events_enabled;
    gboolean ussd_unsolicited_events_setup;
    GTask *pending_ussd_action;

    /* Voice helpers */
    guint all_call_status_indication_id;
    gboolean all_call_status_unsolicited_events_enabled;
    gboolean all_call_status_unsolicited_events_setup;
    guint supplementary_service_indication_id;
    gboolean supplementary_service_unsolicited_events_setup;

    /* Firmware helpers */
    gboolean firmware_list_preloaded;
    GList *firmware_list;
    MMFirmwareProperties *current_firmware;

    /* For notifying when the qmi-proxy connection is dead */
    guint qmi_device_removed_id;

    /* Power Set Operating Mode Helper */
    GTask *set_operating_mode_task;

    /* PDC Refresh notifications ID (3gpp Profile Manager) */
    gboolean profile_manager_unsolicited_events_enabled;
    gboolean profile_manager_unsolicited_events_setup;
    guint refresh_indication_id;

    /* WDS Profile changed notification ID (3gpp Profile Manager) */
    guint profile_changed_indication_id;
    gint  profile_changed_indication_ignored;

    /* Packet service state helpers when using NAS System Info and DSD
     * (not applicable when using NAS Serving System) */
    gboolean dsd_supported;
};

/*****************************************************************************/

static QmiClient *
shared_qmi_peek_client (MMSharedQmi    *self,
                        QmiService      service,
                        MMPortQmiFlag   flag,
                        GError        **error)
{
    MMPortQmi *port;
    QmiClient *client;

    port = mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (self));
    if (!port) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek QMI port");
        return NULL;
    }

    client = mm_port_qmi_peek_client (port, service, flag);
    if (!client)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek client for service '%s'",
                     qmi_service_get_string (service));

    return client;
}

/*****************************************************************************/

MMPortQmi *
mm_broadband_modem_qmi_get_port_qmi (MMBroadbandModemQmi *self)
{
    MMPortQmi *primary_qmi_port;

    g_assert (MM_IS_BROADBAND_MODEM_QMI (self));

    primary_qmi_port = mm_broadband_modem_qmi_peek_port_qmi (self);
    return (primary_qmi_port ?
            MM_PORT_QMI (g_object_ref (primary_qmi_port)) :
            NULL);
}

MMPortQmi *
mm_broadband_modem_qmi_peek_port_qmi (MMBroadbandModemQmi *self)
{
    MMPortQmi *primary_qmi_port = NULL;
    GList     *qmi_ports;

    g_assert (MM_IS_BROADBAND_MODEM_QMI (self));

    qmi_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                          MM_PORT_SUBSYS_UNKNOWN,
                                          MM_PORT_TYPE_QMI);

    /* First QMI port in the list is the primary one always */
    if (qmi_ports)
        primary_qmi_port = MM_PORT_QMI (qmi_ports->data);

    g_list_free_full (qmi_ports, g_object_unref);

    return primary_qmi_port;
}

MMPortQmi *
mm_broadband_modem_qmi_get_port_qmi_for_data (MMBroadbandModemQmi  *self,
                                              MMPort               *data,
                                              MMQmiDataEndpoint    *out_endpoint,
                                              GError              **error)
{
    MMPortQmi *qmi_port;

    g_assert (MM_IS_BROADBAND_MODEM_QMI (self));

    qmi_port = mm_broadband_modem_qmi_peek_port_qmi_for_data (self, data, out_endpoint, error);
    return (qmi_port ?
            MM_PORT_QMI (g_object_ref (qmi_port)) :
            NULL);
}

static MMPortQmi *
peek_port_qmi_for_data_mhi (MMBroadbandModemQmi  *self,
                            MMPort               *data,
                            MMQmiDataEndpoint    *out_endpoint,
                            GError              **error)
{
    MMPortQmi *found = NULL;

    found = mm_broadband_modem_qmi_peek_port_qmi (self);

    if (!found)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find associated QMI port for 'net/%s'",
                     mm_port_get_device (data));
    else if (out_endpoint)
        mm_port_qmi_get_endpoint_info (found, out_endpoint);

    return found;
}

static MMPortQmi *
peek_port_qmi_for_data_usb (MMBroadbandModemQmi  *self,
                            MMPort               *data,
                            MMQmiDataEndpoint    *out_endpoint,
                            GError              **error)
{
    GList       *cdc_wdm_qmi_ports;
    GList       *l;
    const gchar *net_port_parent_path;
    MMPortQmi   *found = NULL;

    net_port_parent_path = mm_kernel_device_get_interface_sysfs_path (mm_port_peek_kernel_device (data));
    if (!net_port_parent_path) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "No parent path for 'net/%s'",
                     mm_port_get_device (data));
        return NULL;
    }

    /* Find the CDC-WDM port on the same USB interface as the given net port */
    cdc_wdm_qmi_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                                  MM_PORT_SUBSYS_USBMISC,
                                                  MM_PORT_TYPE_QMI);
    for (l = cdc_wdm_qmi_ports; l && !found; l = g_list_next (l)) {
        const gchar *wdm_port_parent_path;

        g_assert (MM_IS_PORT_QMI (l->data));
        wdm_port_parent_path = mm_kernel_device_get_interface_sysfs_path (mm_port_peek_kernel_device (MM_PORT (l->data)));
        if (wdm_port_parent_path && g_str_equal (wdm_port_parent_path, net_port_parent_path))
            found = MM_PORT_QMI (l->data);
    }

    g_list_free_full (cdc_wdm_qmi_ports, g_object_unref);

    if (!found)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find associated QMI port for 'net/%s'",
                     mm_port_get_device (data));
    else if (out_endpoint)
        mm_port_qmi_get_endpoint_info (found, out_endpoint);

    return found;
}


static MMPortQmi *
peek_port_qmi_for_data (MMBroadbandModemQmi  *self,
                        MMPort               *data,
                        MMQmiDataEndpoint    *out_endpoint,
                        GError              **error)
{
    const gchar *net_port_driver;

    g_assert (MM_IS_BROADBAND_MODEM_QMI (self));
    g_assert (mm_port_get_subsys (data) == MM_PORT_SUBSYS_NET);

    net_port_driver = mm_kernel_device_get_driver (mm_port_peek_kernel_device (data));

    if (!g_strcmp0 (net_port_driver, "qmi_wwan"))
        return peek_port_qmi_for_data_usb (self, data, out_endpoint, error);

    if (!g_strcmp0 (net_port_driver, "mhi_net"))
        return peek_port_qmi_for_data_mhi (self, data, out_endpoint, error);

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Unsupported QMI kernel driver for 'net/%s': %s",
                 mm_port_get_device (data),
                 net_port_driver);
    return NULL;
}

MMPortQmi *
mm_broadband_modem_qmi_peek_port_qmi_for_data (MMBroadbandModemQmi  *self,
                                               MMPort               *data,
                                               MMQmiDataEndpoint    *out_endpoint,
                                               GError              **error)
{
    g_assert (MM_BROADBAND_MODEM_QMI_GET_CLASS (self)->peek_port_qmi_for_data);

    return MM_BROADBAND_MODEM_QMI_GET_CLASS (self)->peek_port_qmi_for_data (self, data, out_endpoint, error);
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
/* Create Bearer List (Modem interface) */

static MMBearerList *
modem_create_bearer_list (MMIfaceModem *self)
{
    MMPortQmi *port;
    guint      n = 0;
    guint      n_multiplexed = 0;

    port = mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (self));
    if (!port) {
        /* this should not happen, just fallback to defaults */
        mm_obj_warn (self, "no port to query maximum number of supported network links");
    } else {
        MMPortQmiKernelDataMode kernel_data_modes;

        kernel_data_modes = mm_port_qmi_get_kernel_data_modes (port);

        /* There are setups, like IPA, where there is ONLY multiplexing expected
         * and supported. In those cases, there isn't any expected non-multiplexed
         * bearer */

        if (kernel_data_modes & (QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP | MM_PORT_QMI_KERNEL_DATA_MODE_802_3)) {
            /* The maximum number of available/connected modems is guessed from
             * the size of the data ports list. */
            n = g_list_length (mm_base_modem_peek_data_ports (MM_BASE_MODEM (self)));
            mm_obj_dbg (self, "allowed up to %u active bearers", n);
        }

        if (kernel_data_modes & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)) {
            /* The maximum number of multiplexed links is retrieved from the MMPortQmi */
            n_multiplexed = mm_port_qmi_get_max_multiplexed_links (port);
            mm_obj_dbg (self, "allowed up to %u active multiplexed bearers", n_multiplexed);

            if (mm_kernel_device_has_global_property (mm_port_peek_kernel_device (MM_PORT (port)),
                                                      ID_MM_MAX_MULTIPLEXED_LINKS)) {
                guint n_multiplexed_limited;

                n_multiplexed_limited = mm_kernel_device_get_global_property_as_int (
                    mm_port_peek_kernel_device (MM_PORT (port)),
                    ID_MM_MAX_MULTIPLEXED_LINKS);
                if (n_multiplexed_limited < n_multiplexed) {
                    n_multiplexed = n_multiplexed_limited;
                    mm_obj_dbg (self, "limited to %u active multiplexed bearers", n_multiplexed);
                }
            }
        }
    }

    /* by default, no multiplexing support */
    return mm_bearer_list_new (n, n_multiplexed);
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "loading manufacturer...");
    qmi_client_dms_get_manufacturer (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_manufacturer_ready,
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "loading revision...");
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "loading hardware revision...");
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
            mm_obj_dbg (self, "invalid ESN reported: '%s' (unexpected length)", str);
    }

    if (qmi_message_dms_get_ids_output_get_meid (output, &str, NULL) &&
        str[0] != '\0') {
        g_clear_pointer (&self->priv->meid, g_free);
        len = strlen (str);
        if (len == 14)
            self->priv->meid = g_strdup (str);
        else
            mm_obj_dbg (self, "invalid MEID reported: '%s' (unexpected length)", str);
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "loading equipment identifier...");
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
    gchar  *device_identifier;
    GTask  *task;
    GError *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading device identifier...");

    /* Just use placeholder ATI/ATI1 replies, all the other internal info should be
     * enough for uniqueness */
    device_identifier = mm_broadband_modem_create_device_identifier (MM_BROADBAND_MODEM (self), "", "", &error);
    if (!device_identifier)
        g_task_return_error (task, error);
    else
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "loading own numbers...");
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
    gboolean               last_attempt;
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

static void
unlock_required_uim_get_card_status_ready (QmiClientUim *client,
                                           GAsyncResult *res,
                                           GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadUnlockRequiredContext *ctx;
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output || !qmi_message_uim_get_card_status_output_get_result (output, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_qmi_uim_get_card_status_output_parse (self,
                                                  output,
                                                  &lock,
                                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                                  &error)) {
        /* The device may report a SIM NOT INSERTED error if we're querying the
         * card status soon after power on. We'll let the Modem interface generic
         * logic retry loading the info a bit later if that's the case. This will
         * make device detection slower when there's really no SIM card, but there's
         * no clear better way to handle it :/ */
        if (g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) && !ctx->last_attempt) {
            g_clear_error (&error);
            g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY, "No card found (retry)");
        }
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
    qmi_message_dms_uim_get_pin_status_output_unref (output);
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
        /* Fall through */

    case LOAD_UNLOCK_REQUIRED_STEP_CDMA:
        /* CDMA-only modems don't need this */
        if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (self))) {
            mm_obj_dbg (self, "skipping unlock check in CDMA-only modem...");
            g_task_return_int (task, MM_MODEM_LOCK_NONE);
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case LOAD_UNLOCK_REQUIRED_STEP_DMS:
        if (!self->priv->dms_uim_deprecated) {
            /* Failure to get DMS client is hard really */
            client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                                QMI_SERVICE_DMS,
                                                MM_PORT_QMI_FLAG_DEFAULT,
                                                &error);
            if (!client) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
            }

            mm_obj_dbg (self, "loading unlock required (DMS)...");
            qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                               NULL,
                                               5,
                                               NULL,
                                               (GAsyncReadyCallback) dms_uim_get_pin_status_ready,
                                               task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case LOAD_UNLOCK_REQUIRED_STEP_UIM:
        /* Failure to get UIM client at this point is hard as well */
        client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_UIM,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            &error);
        if (!client) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "loading unlock required (UIM)...");
        qmi_client_uim_get_card_status (QMI_CLIENT_UIM (client),
                                        NULL,
                                        5,
                                        NULL,
                                        (GAsyncReadyCallback) unlock_required_uim_get_card_status_ready,
                                        task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            gboolean last_attempt,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadUnlockRequiredContext *ctx;
    GTask *task;

    ctx = g_new0 (LoadUnlockRequiredContext, 1);
    ctx->step = LOAD_UNLOCK_REQUIRED_STEP_FIRST;
    ctx->last_attempt = last_attempt;

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
    MMBroadbandModemQmi *self;
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    guint pin1_retries = 0;
    guint puk1_retries = 0;
    guint pin2_retries = 0;
    guint puk2_retries = 0;
    guint pers_retries = 0;
    MMUnlockRetries *retries;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;

    self = g_task_get_source_object (task);

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output || !qmi_message_uim_get_card_status_output_get_result (output, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_qmi_uim_get_card_status_output_parse (self,
                                                  output,
                                                  &lock,
                                                  NULL, &pin1_retries, &puk1_retries,
                                                  NULL, &pin2_retries, &puk2_retries,
                                                  &pers_retries,
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
    if (lock >= MM_MODEM_LOCK_PH_SP_PIN)
        mm_unlock_retries_set (retries, lock, pers_retries);

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

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_UIM,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        &error);
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

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_DMS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
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

    mm_obj_dbg (self, "loading unlock retries...");
    if (!self->priv->dms_uim_deprecated)
        dms_uim_load_unlock_retries (MM_BROADBAND_MODEM_QMI (self), task);
    else
        uim_load_unlock_retries (MM_BROADBAND_MODEM_QMI (self), task);
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

#define RSSI_MAX -30
#define RSSI_MIN -125
#define RSRP_MAX -44
#define RSRP_MIN -140
#define SNR_MAX 40
#define SNR_MIN -23
#define RSRQ_MAX 20
#define RSRQ_MIN -43

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
    case QMI_NAS_RADIO_INTERFACE_UNKNOWN:
    case QMI_NAS_RADIO_INTERFACE_NONE:
    case QMI_NAS_RADIO_INTERFACE_AMPS:
    case QMI_NAS_RADIO_INTERFACE_GSM:
    case QMI_NAS_RADIO_INTERFACE_LTE:
    case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
    case QMI_NAS_RADIO_INTERFACE_5GNR:
        /* no explicit validation */
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

static gboolean
common_signal_info_get_quality (MMBroadbandModemQmi *self,
                                gint8 cdma1x_rssi,
                                gint8 evdo_rssi,
                                gint8 gsm_rssi,
                                gint8 wcdma_rssi,
                                gint8 lte_rssi,
                                gint16 nr5g_rsrp,
                                gint16 nr5g_snr,
                                gint16 nr5g_rsrq,
                                guint8 *out_quality)
{
    gint8 rssi_max = -125;
    gint8 signal_quality = -1;
    /* Valid nr5g signal quality will be in percentage [0,100].
     * It is minimum of (rsrp, snr, rsrq) signal quality for 5G. */
    guint8 nr5g_signal_quality_min = 101;

    g_assert (out_quality != NULL);

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. TODO: When several technologies are in use, if
     * the indication only contains the data of the one which passed a threshold
     * value, we'll need to have an internal cache of per-technology values, in
     * order to report always the one with the maximum value. */

    if (cdma1x_rssi < 0) {
        mm_obj_dbg (self, "RSSI (CDMA): %d dBm", cdma1x_rssi);
        if (qmi_dbm_valid (cdma1x_rssi, QMI_NAS_RADIO_INTERFACE_CDMA_1X))
            rssi_max = MAX (cdma1x_rssi, rssi_max);
    }

    if (evdo_rssi < 0) {
        mm_obj_dbg (self, "RSSI (HDR): %d dBm", evdo_rssi);
        if (qmi_dbm_valid (evdo_rssi, QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO))
            rssi_max = MAX (evdo_rssi, rssi_max);
    }

    if (gsm_rssi < 0) {
        mm_obj_dbg (self, "RSSI (GSM): %d dBm", gsm_rssi);
        if (qmi_dbm_valid (gsm_rssi, QMI_NAS_RADIO_INTERFACE_GSM))
            rssi_max = MAX (gsm_rssi, rssi_max);
    }

    if (wcdma_rssi < 0) {
        mm_obj_dbg (self, "RSSI (WCDMA): %d dBm", wcdma_rssi);
        if (qmi_dbm_valid (wcdma_rssi, QMI_NAS_RADIO_INTERFACE_UMTS))
            rssi_max = MAX (wcdma_rssi, rssi_max);
    }

    if (lte_rssi < 0) {
        mm_obj_dbg (self, "RSSI (LTE): %d dBm", lte_rssi);
        if (qmi_dbm_valid (lte_rssi, QMI_NAS_RADIO_INTERFACE_LTE))
            rssi_max = MAX (lte_rssi, rssi_max);
    }

    if (nr5g_rsrp <= RSRP_MAX && nr5g_rsrp >= RSRP_MIN) {
        mm_obj_dbg (self, "RSRP (5G): %d dBm", nr5g_rsrp);
        nr5g_signal_quality_min = MIN (nr5g_signal_quality_min,
                                       (guint8)((nr5g_rsrp - RSRP_MIN) * 100 / (RSRP_MAX - RSRP_MIN)));
    }

    if (nr5g_snr <= SNR_MAX && nr5g_snr >= SNR_MIN) {
        mm_obj_dbg (self, "SNR (5G): %d dB", nr5g_snr);
        nr5g_signal_quality_min = MIN (nr5g_signal_quality_min,
                                       (guint8)((nr5g_snr - SNR_MIN) * 100 / (SNR_MAX - SNR_MIN)));
    }

    if (nr5g_rsrq <= RSRQ_MAX && nr5g_rsrq >= RSRQ_MIN) {
        mm_obj_dbg (self, "RSRQ (5G): %d dB", nr5g_rsrq);
        nr5g_signal_quality_min = MIN (nr5g_signal_quality_min,
                                       (guint8)((nr5g_rsrq - RSRQ_MIN) * 100 / (RSRQ_MAX - RSRQ_MIN)));
    }

    if (rssi_max < 0 && rssi_max > -125) {
        /* This RSSI comes as negative dBms */
        signal_quality = MM_RSSI_TO_QUALITY (rssi_max);
        mm_obj_dbg (self, "RSSI: %d dBm --> %u%%", rssi_max, signal_quality);
    }

    if (nr5g_signal_quality_min < 101 && nr5g_signal_quality_min >= signal_quality) {
        signal_quality = nr5g_signal_quality_min;
        mm_obj_dbg (self, "5G signal quality: %d%%", signal_quality);
    }

    if (signal_quality >= 0) {
        *out_quality = signal_quality;
        return TRUE;
    }

    return FALSE;
}

static gboolean
signal_info_get_quality (MMBroadbandModemQmi *self,
                         QmiMessageNasGetSignalInfoOutput *output,
                         guint8 *out_quality)
{
    gint8 cdma1x_rssi = 0;
    gint8 evdo_rssi = 0;
    gint8 gsm_rssi = 0;
    gint8 wcdma_rssi = 0;
    gint8 lte_rssi = 0;
    gint16 nr5g_rsrp = RSRP_MAX + 1;
    /* Multiplying SNR_MAX by 10 as QMI gives SNR level
     * as a scaled integer in units of 0.1 dB. */
    gint16 nr5g_snr = 10 * SNR_MAX + 10;
    gint16 nr5g_rsrq = RSRQ_MAX + 1;

    qmi_message_nas_get_signal_info_output_get_cdma_signal_strength (output, &cdma1x_rssi, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_hdr_signal_strength (output, &evdo_rssi, NULL, NULL, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_gsm_signal_strength (output, &gsm_rssi, NULL);
    qmi_message_nas_get_signal_info_output_get_wcdma_signal_strength (output, &wcdma_rssi, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_lte_signal_strength (output, &lte_rssi, NULL, NULL, NULL, NULL);
    qmi_message_nas_get_signal_info_output_get_5g_signal_strength (output, &nr5g_rsrp, &nr5g_snr, NULL);
    qmi_message_nas_get_signal_info_output_get_5g_signal_strength_extended (output, &nr5g_rsrq, NULL);

    /* Scale to integer values in units of 1 dB/dBm, if any */
    nr5g_snr = 0.1 * nr5g_snr;

    return common_signal_info_get_quality (self,
                                           cdma1x_rssi,
                                           evdo_rssi,
                                           gsm_rssi,
                                           wcdma_rssi,
                                           lte_rssi,
                                           nr5g_rsrp,
                                           nr5g_snr,
                                           nr5g_rsrq,
                                           out_quality);
}

static gboolean
signal_strength_get_quality (MMBroadbandModemQmi *self,
                             QmiMessageNasGetSignalStrengthOutput *output,
                             guint8 *o_quality)
{
    GArray *array = NULL;
    gint8 signal_max = 0;
    QmiNasRadioInterface main_interface;

    /* We do not report per-technology signal quality, so just get the highest
     * one of the ones reported. */

    /* The mandatory one is always present */
    qmi_message_nas_get_signal_strength_output_get_signal_strength (output, &signal_max, &main_interface, NULL);
    mm_obj_dbg (self, "signal strength (%s): %d dBm",
                qmi_nas_radio_interface_get_string (main_interface),
                signal_max);

    /* Treat results as invalid if main signal strength is invalid */
    if (!qmi_dbm_valid (signal_max, main_interface))
        return FALSE;

    /* On multimode devices we may get more */
    if (qmi_message_nas_get_signal_strength_output_get_strength_list (output, &array, NULL)) {
        guint i;

        for (i = 0; i < array->len; i++) {
            QmiMessageNasGetSignalStrengthOutputStrengthListElement *element;

            element = &g_array_index (array, QmiMessageNasGetSignalStrengthOutputStrengthListElement, i);

            mm_obj_dbg (self, "signal strength (%s): %d dBm",
                        qmi_nas_radio_interface_get_string (element->radio_interface),
                        element->strength);

            if (qmi_dbm_valid (element->strength, element->radio_interface)) {
                signal_max = MAX (element->strength, signal_max);
            }
        }
    }

    if (signal_max < 0) {
        /* This signal strength comes as negative dBms */
        *o_quality = MM_RSSI_TO_QUALITY (signal_max);

        mm_obj_dbg (self, "signal strength: %d dBm --> %u%%", signal_max, *o_quality);
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

    if (!signal_strength_get_quality (self, output, &quality)) {
        qmi_message_nas_get_signal_strength_output_unref (output);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "GetSignalStrength signal strength invalid.");
        g_object_unref (task);
        return;
    }

    g_task_return_int (task, quality);
    g_object_unref (task);

    qmi_message_nas_get_signal_strength_output_unref (output);
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

    self = g_task_get_source_object (task);

    output = qmi_client_nas_get_signal_info_finish (client, res, &error);
    if (!output) {
        mm_obj_dbg (self, "couldn't get signal info: '%s': falling back to get signal strength",
                    error->message);
        qmi_client_nas_get_signal_strength (client,
                                            NULL,
                                            10,
                                            NULL,
                                            (GAsyncReadyCallback)get_signal_strength_ready,
                                            task);
        g_clear_error (&error);
        return;
    }

    if (!qmi_message_nas_get_signal_info_output_get_result (output, &error)) {
        qmi_message_nas_get_signal_info_output_unref (output);
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            mm_obj_dbg (self, "couldn't get signal info: '%s': falling back to get signal strength",
                        error->message);
            qmi_client_nas_get_signal_strength (client,
                                                NULL,
                                                10,
                                                NULL,
                                                (GAsyncReadyCallback)get_signal_strength_ready,
                                                task);
            g_clear_error (&error);
            return;
        }
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!signal_info_get_quality (self, output, &quality)) {
        qmi_message_nas_get_signal_info_output_unref (output);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Signal info reported invalid signal strength.");
        g_object_unref (task);
        return;
    }

    g_task_return_int (task, quality);
    g_object_unref (task);

    qmi_message_nas_get_signal_info_output_unref (output);
}

static void
load_signal_quality (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiClient *client = NULL;
    GTask *task;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "loading signal quality...");

    qmi_client_nas_get_signal_info (QMI_CLIENT_NAS (client),
                                    NULL,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback)get_signal_info_ready,
                                    task);
}

/*****************************************************************************/
/* Cell info */

static GList *
get_cell_info_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
cell_info_list_free (GList *cell_info_list)
{
    g_list_free_full (cell_info_list, g_object_unref);
}

/* Stolen from qmicli-nas.c as mm_bcd_to_string() doesn't correctly handle
 * special filler byte (0xF) for 2-digit MNCs.
 * ref: Table 10.5.3/3GPP TS 24.008 */
static gchar *
str_from_bcd_plmn (GArray *bcd)
{
    static const gchar bcd_chars[] = "0123456789*#abc\0\0";
    gchar *str;
    guint i;
    guint j;

    if (!bcd || !bcd->len)
        return NULL;

    str = g_malloc (1 + (bcd->len * 2));
    for (i = 0, j = 0 ; i < bcd->len; i++) {
        str[j] = bcd_chars[g_array_index (bcd, guint8, i) & 0xF];
        if (str[j])
            j++;
        str[j] = bcd_chars[(g_array_index (bcd, guint8, i) >> 4) & 0xF];
        if (str[j])
            j++;
    }
    str[j] = '\0';

    return str;
}

static void
get_cell_info_ready (QmiClientNas *client,
                     GAsyncResult *res,
                     GTask *task)
{
    QmiMessageNasGetCellLocationInfoOutput *output;
    GError *error = NULL;

    GList *list = NULL;

    /* common vars */
    GArray *operator;
    guint32 cell_id;
    guint16 arfcn;
    GArray* cell_array;
    GArray* frequency_array;

    guint16 lte_tac;
    guint16 lte_scell_id;
    guint32 lte_timing_advance;

    GArray *nr5g_tac;
    guint64 nr5g_global_ci;
    guint16 nr5g_pci;
    gint16 nr5g_rsrq;
    gint16 nr5g_rsrp;
    gint16 nr5g_snr;
    guint32 nr5g_arfcn;

    output = qmi_client_nas_get_cell_location_info_finish (client, res, &error);
    if (!output) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_cell_location_info_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_nas_get_cell_location_info_output_unref (output);
        return;
    }

    if (qmi_message_nas_get_cell_location_info_output_get_intrafrequency_lte_info_v2 (
            output,
            NULL /* ue in idle */,
            &operator,
            &lte_tac,
            &cell_id,
            &arfcn,
            &lte_scell_id,
            NULL /* cell reselect prio */,
            NULL /* non-intra search thres */,
            NULL /* scell low thres */,
            NULL /* s intra search thres */,
            &cell_array,
            &error)) {
        g_autofree gchar *operator_id = NULL;
        g_autofree gchar *tac = NULL;
        g_autofree gchar *ci = NULL;
        guint i;

        operator_id = str_from_bcd_plmn (operator);
        /* Encoded in upper-case hexadecimal format without leading zeros, as specified in 3GPP TS 27.007. */
        tac = g_strdup_printf ("%X", lte_tac);
        ci = g_strdup_printf ("%X", cell_id);

        for (i = 0; i < cell_array->len; i++) {
            QmiMessageNasGetCellLocationInfoOutputIntrafrequencyLteInfoV2CellElement *element;
            MMCellInfoLte    *lte_info;
            g_autofree gchar *pci = NULL;

            element = &g_array_index (cell_array, QmiMessageNasGetCellLocationInfoOutputIntrafrequencyLteInfoV2CellElement, i);
            lte_info = MM_CELL_INFO_LTE (mm_cell_info_lte_new_from_dictionary (NULL));

            /* valid for all cells */
            mm_cell_info_lte_set_operator_id (lte_info, operator_id);
            mm_cell_info_lte_set_tac (lte_info, tac);
            mm_cell_info_lte_set_earfcn (lte_info, arfcn);
            /* this cell */
            pci = g_strdup_printf ("%X", element->physical_cell_id);
            mm_cell_info_lte_set_physical_ci (lte_info, pci);
            mm_cell_info_lte_set_rsrp (lte_info, (0.1) * ((gdouble)element->rsrp));
            mm_cell_info_lte_set_rsrq (lte_info, (0.1) * ((gdouble)element->rsrq));

            /* only for serving cell, we get details about CGI and TA */
            if (element->physical_cell_id == lte_scell_id) {
                mm_cell_info_set_serving (MM_CELL_INFO (lte_info), TRUE);
                mm_cell_info_lte_set_ci (lte_info, ci);

                if (qmi_message_nas_get_cell_location_info_output_get_lte_info_timing_advance (output,
                                                                                               &lte_timing_advance,
                                                                                               NULL)) {
                    mm_cell_info_lte_set_timing_advance (lte_info, lte_timing_advance);
                }
            }

            list = g_list_append (list, g_steal_pointer (&lte_info));
        }
    }

    if (qmi_message_nas_get_cell_location_info_output_get_interfrequency_lte_info (output,
                                                                                   NULL /* UE in idle */,
                                                                                   &frequency_array,
                                                                                   NULL)) {
        guint i;

        for (i = 0; i < frequency_array->len; i++) {
            QmiMessageNasGetCellLocationInfoOutputInterfrequencyLteInfoFrequencyElement *frequency;
            MMCellInfoLte *lte_info;
            guint j;

            frequency = &g_array_index (frequency_array, QmiMessageNasGetCellLocationInfoOutputInterfrequencyLteInfoFrequencyElement, i);
            arfcn = frequency->eutra_absolute_rf_channel_number;
            cell_array = frequency->cell;

            for (j = 0; j < cell_array->len; j++) {
                QmiMessageNasGetCellLocationInfoOutputInterfrequencyLteInfoFrequencyElementCellElement *cell;
                g_autofree gchar *pci = NULL;

                cell = &g_array_index (cell_array, QmiMessageNasGetCellLocationInfoOutputInterfrequencyLteInfoFrequencyElementCellElement, j);
                lte_info = MM_CELL_INFO_LTE (mm_cell_info_lte_new_from_dictionary (NULL));
                pci = g_strdup_printf ("%X", cell->physical_cell_id);

                mm_cell_info_lte_set_earfcn (lte_info, arfcn);
                mm_cell_info_lte_set_physical_ci (lte_info, pci);
                mm_cell_info_lte_set_rsrp (lte_info, (0.1) * ((gdouble)cell->rsrp));
                mm_cell_info_lte_set_rsrq (lte_info, (0.1) * ((gdouble)cell->rsrq));

                list = g_list_append (list, g_steal_pointer (&lte_info));
            }
        }
    }

    if (qmi_message_nas_get_cell_location_info_output_get_nr5g_cell_information (output,
                                                                                 &operator,
                                                                                 &nr5g_tac,
                                                                                 &nr5g_global_ci,
                                                                                 &nr5g_pci,
                                                                                 &nr5g_rsrq,
                                                                                 &nr5g_rsrp,
                                                                                 &nr5g_snr,
                                                                                 &error)) {
        MMCellInfoNr5g   *nr5g_info;
        g_autofree gchar *operator_id = NULL;
        g_autofree gchar *tac = NULL;
        g_autofree gchar *global_ci = NULL;
        g_autofree gchar *pci = NULL;

        operator_id = str_from_bcd_plmn (operator);

        g_assert (nr5g_tac->len == 3);
        /* Encoded in upper-case hexadecimal format without leading zeros, as specified in 3GPP TS 27.007. */
        tac = g_strdup_printf ("%X", ((((g_array_index (nr5g_tac, guint8, 0) << 8) |
                                         g_array_index (nr5g_tac, guint8, 1)) << 8) |
                                         g_array_index (nr5g_tac, guint8, 2)));
        global_ci = g_strdup_printf ("%" G_GINT64_MODIFIER "X", nr5g_global_ci);
        pci = g_strdup_printf ("%X", nr5g_pci);

        nr5g_info = MM_CELL_INFO_NR5G (mm_cell_info_nr5g_new_from_dictionary (NULL));
        mm_cell_info_set_serving (MM_CELL_INFO (nr5g_info), TRUE);
        mm_cell_info_nr5g_set_operator_id (nr5g_info, operator_id);
        mm_cell_info_nr5g_set_tac (nr5g_info, tac);
        mm_cell_info_nr5g_set_ci (nr5g_info, global_ci);
        mm_cell_info_nr5g_set_physical_ci (nr5g_info, pci);
        mm_cell_info_nr5g_set_rsrq (nr5g_info, (0.1) * ((gdouble)nr5g_rsrq));
        mm_cell_info_nr5g_set_rsrp (nr5g_info, (0.1) * ((gdouble)nr5g_rsrp));
        mm_cell_info_nr5g_set_sinr (nr5g_info, (0.1) * ((gdouble)nr5g_snr));

        if (qmi_message_nas_get_cell_location_info_output_get_nr5g_arfcn (output, &nr5g_arfcn, &error)) {
            mm_cell_info_nr5g_set_nrarfcn (nr5g_info, nr5g_arfcn);
        }

        list = g_list_append (list, g_steal_pointer (&nr5g_info));
    }

    g_task_return_pointer (task, list, (GDestroyNotify)cell_info_list_free);
    g_object_unref (task);
    qmi_message_nas_get_cell_location_info_output_unref (output);
}

static void
get_cell_info (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    QmiClient *client = NULL;
    GTask *task;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self), QMI_SERVICE_NAS, &client, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "getting cell info...");
    qmi_client_nas_get_cell_location_info (QMI_CLIENT_NAS (client),
                                           NULL,
                                           10,
                                           NULL,
                                           (GAsyncReadyCallback)get_cell_info_ready,
                                           task);
}

/*****************************************************************************/
/* Powering up/down/off the modem (Modem interface) */

typedef struct {
    QmiDmsOperatingMode  mode;
    QmiClientDms        *client;
    guint                indication_id;
    guint                timeout_id;
} SetOperatingModeContext;

static void
set_operating_mode_context_free (SetOperatingModeContext *ctx)
{
    g_assert (ctx->indication_id == 0);
    g_assert (ctx->timeout_id == 0);
    g_clear_object (&ctx->client);
    g_slice_free (SetOperatingModeContext, ctx);
}

static gboolean
modem_power_up_down_off_finish (MMIfaceModem  *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_operating_mode_context_reset (SetOperatingModeContext *ctx)
{
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }

    if (ctx->indication_id) {
        g_autoptr(QmiMessageDmsSetEventReportInput) input = NULL;

        g_signal_handler_disconnect (ctx->client, ctx->indication_id);
        ctx->indication_id = 0;

        input = qmi_message_dms_set_event_report_input_new ();
        qmi_message_dms_set_event_report_input_set_operating_mode_reporting (input, FALSE, NULL);
        qmi_client_dms_set_event_report (ctx->client, input, 5, NULL, NULL, NULL);
    }
}

static void
dms_check_current_operating_mode_ready (QmiClientDms *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    QmiMessageDmsGetOperatingModeOutput *output = NULL;
    GError                              *error = NULL;
    SetOperatingModeContext             *ctx;

    ctx = g_task_get_task_data (task);

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

        if (mode == ctx->mode)
            g_task_return_boolean (task, TRUE);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Requested mode (%s) and mode received (%s) did not match",
                                     qmi_dms_operating_mode_get_string (ctx->mode),
                                     qmi_dms_operating_mode_get_string (mode));
    }

    if (output)
        qmi_message_dms_get_operating_mode_output_unref (output);

    g_object_unref (task);
}

static gboolean
dms_set_operating_mode_timeout_cb (MMBroadbandModemQmi *self)
{
    GTask                   *task;
    SetOperatingModeContext *ctx;

    g_assert (self->priv->set_operating_mode_task);
    task = g_steal_pointer (&self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (task);

    mm_obj_warn (self, "Power update operation timed out");

    set_operating_mode_context_reset (ctx);

    mm_obj_dbg (self, "check current device operating mode...");
    qmi_client_dms_get_operating_mode (ctx->client,
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)dms_check_current_operating_mode_ready,
                                       task);

    return G_SOURCE_REMOVE;
}

static void
set_operating_mode_complete (MMBroadbandModemQmi *self,
                             GError              *error)
{
    GTask                   *task;
    SetOperatingModeContext *ctx;

    g_assert (self->priv->set_operating_mode_task);
    task = g_steal_pointer (&self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (task);

    set_operating_mode_context_reset (ctx);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
power_event_report_indication_cb (QmiClientDms                      *client,
                                  QmiIndicationDmsEventReportOutput *output,
                                  MMBroadbandModemQmi               *self)
{
    QmiDmsOperatingMode      state;
    GError                  *error = NULL;
    SetOperatingModeContext *ctx;

    if (!qmi_indication_dms_event_report_output_get_operating_mode (output, &state, NULL)) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Invalid power indication received");
        set_operating_mode_complete (self, error);
        return;
    }

    g_assert (self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (self->priv->set_operating_mode_task);

    if (ctx->mode == state) {
        mm_obj_dbg (self, "Power state successfully updated: '%s'", qmi_dms_operating_mode_get_string (state));
        set_operating_mode_complete (self, NULL);
        return;
    }

    error = g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Requested mode (%s) and mode received (%s) did not match",
                         qmi_dms_operating_mode_get_string (ctx->mode),
                         qmi_dms_operating_mode_get_string (state));
    set_operating_mode_complete (self, error);
}

static void
dms_set_operating_mode_ready (QmiClientDms        *client,
                              GAsyncResult        *res,
                              MMBroadbandModemQmi *self) /* full reference */
{
    g_autoptr (QmiMessageDmsSetOperatingModeOutput)  output = NULL;
    GError                                          *error = NULL;
    SetOperatingModeContext                         *ctx;

    if (!self->priv->set_operating_mode_task) {
        /* We completed the operation already via indication */
        g_object_unref (self);
        return;
    }

    ctx = g_task_get_task_data (self->priv->set_operating_mode_task);

    output = qmi_client_dms_set_operating_mode_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_operating_mode_output_get_result (output, &error)) {
        /*
         * Some new devices, like the Dell DW5770, will return an internal error when
         * trying to bring the power mode to online.
         *
         * Other devices, like some rebranded EM7455 modules, will return an "invalid
         * transition" instead when trying to bring the power mode to online.
         *
         * We can avoid this by sending the magic "DMS Set FCC Auth" message before
         * retrying. Notify this to upper layers with the special MM_CORE_ERROR_RETRY
         * error.
         */
        if ((ctx->mode == QMI_DMS_OPERATING_MODE_ONLINE) &&
            ((g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INTERNAL) ||
              g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_TRANSITION)))) {
            g_clear_error (&error);
            error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_RETRY, "Invalid transition");
        } else
            g_prefix_error (&error, "Couldn't set operating mode: ");
    }

    /* If unsupported, just complete without errors */
    if (g_error_matches (error, QMI_CORE_ERROR, QMI_CORE_ERROR_UNSUPPORTED)) {
        mm_obj_dbg (self, "device doesn't support operating mode setting: ignoring power update");
        g_clear_error (&error);
        set_operating_mode_complete (self, NULL);
    } else if (error)
        set_operating_mode_complete (self, error);
    else if (ctx->timeout_id)
        mm_obj_dbg (self, "operating mode request sent, waiting for power update indication");
    else
        set_operating_mode_complete (self, NULL);

    g_object_unref (self);
}

static void
dms_set_operating_mode (MMBroadbandModemQmi *self,
                        gboolean             supports_power_indications)
{
    g_autoptr (QmiMessageDmsSetOperatingModeInput)  input = NULL;
    SetOperatingModeContext                        *ctx;

    g_assert (self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (self->priv->set_operating_mode_task);

    input = qmi_message_dms_set_operating_mode_input_new ();
    qmi_message_dms_set_operating_mode_input_set_mode (input, ctx->mode, NULL);
    qmi_client_dms_set_operating_mode (ctx->client,
                                       input,
                                       20,
                                       NULL,
                                       (GAsyncReadyCallback)dms_set_operating_mode_ready,
                                       g_object_ref (self));

    if (supports_power_indications) {
        mm_obj_dbg (self, "Starting timeout for indication receiving for 10 seconds");
        ctx->timeout_id = g_timeout_add_seconds (10,
                                                (GSourceFunc) dms_set_operating_mode_timeout_cb,
                                                self);
    }
}

static void
dms_set_event_report_operating_mode_activate_ready (QmiClientDms        *client,
                                                    GAsyncResult        *res,
                                                    MMBroadbandModemQmi *self) /* full reference */
{
    g_autoptr(QmiMessageDmsSetEventReportOutput)  output = NULL;
    GError                                       *error = NULL;
    SetOperatingModeContext                      *ctx;
    gboolean                                      supports_power_indications = TRUE;

    g_assert (self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (self->priv->set_operating_mode_task);

    output = qmi_client_dms_set_event_report_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_event_report_output_get_result (output, &error)) {
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_MISSING_ARGUMENT)) {
            mm_obj_dbg (self, "device doesn't support power indication registration: ignore it and continue");
            g_clear_error (&error);
            supports_power_indications = FALSE;
        } else {
            g_prefix_error (&error, "Couldn't register for power indications: ");
            set_operating_mode_complete (self, error);
            g_object_unref (self);
            return;
        }
    }

    g_assert (ctx->indication_id == 0);
    if (supports_power_indications) {
        ctx->indication_id = g_signal_connect (client,
                                               "event-report",
                                                G_CALLBACK (power_event_report_indication_cb),
                                                self);
        mm_obj_dbg (self, "Power operation is pending");
    }

    dms_set_operating_mode (self,
                            supports_power_indications);
    g_object_unref (self);
}

static void
modem_power_indication_register (MMBroadbandModemQmi *self)
{
    g_autoptr(QmiMessageDmsSetEventReportInput)  input = NULL;
    SetOperatingModeContext                     *ctx;

    g_assert (self->priv->set_operating_mode_task);
    ctx = g_task_get_task_data (self->priv->set_operating_mode_task);

    input = qmi_message_dms_set_event_report_input_new ();
    qmi_message_dms_set_event_report_input_set_operating_mode_reporting (input, TRUE, NULL);
    mm_obj_dbg (self, "Power indication registration request is sent");
    qmi_client_dms_set_event_report (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)dms_set_event_report_operating_mode_activate_ready,
        g_object_ref (self));
}

static void
common_power_up_down_off (MMIfaceModem        *_self,
                          QmiDmsOperatingMode  mode,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MMBroadbandModemQmi     *self = MM_BROADBAND_MODEM_QMI (_self);
    GError                  *error = NULL;
    GTask                   *task;
    SetOperatingModeContext *ctx;
    QmiClient               *client;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->set_operating_mode_task) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS, "Another operation in progress");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_DMS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        &error);
    if (!client) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetOperatingModeContext);
    ctx->mode = mode;
    ctx->client = QMI_CLIENT_DMS (g_object_ref (client));
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_operating_mode_context_free);

    self->priv->set_operating_mode_task = task;
    modem_power_indication_register (self);
}

static void
modem_power_off (MMIfaceModem        *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    common_power_up_down_off (self, QMI_DMS_OPERATING_MODE_OFFLINE, callback, user_data);
}

static void
modem_power_down (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    common_power_up_down_off (self, QMI_DMS_OPERATING_MODE_LOW_POWER, callback, user_data);
}

static void
modem_power_up (MMIfaceModem        *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    common_power_up_down_off (self, QMI_DMS_OPERATING_MODE_ONLINE, callback, user_data);
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
        case QMI_DMS_OPERATING_MODE_SHUTTING_DOWN:
        case QMI_DMS_OPERATING_MODE_FACTORY_TEST:
        case QMI_DMS_OPERATING_MODE_RESET:
        case QMI_DMS_OPERATING_MODE_UNKNOWN:
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "getting device operating mode...");
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

static void get_next_facility_lock_status_via_dms (GTask *task);

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
get_sim_lock_status_via_get_card_status_ready (QmiClientUim *client,
                                               GAsyncResult *res,
                                               GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadEnabledFacilityLocksContext *ctx;
    QmiMessageUimGetCardStatusOutput *output;
    GError *error = NULL;
    MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
    QmiUimPinState pin1_state;
    QmiUimPinState pin2_state;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_uim_get_card_status_finish (client, res, &error);
    if (!output || !qmi_message_uim_get_card_status_output_get_result (output, &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        if (output)
            qmi_message_uim_get_card_status_output_unref (output);
        return;
    }

    if (!mm_qmi_uim_get_card_status_output_parse (self,
                                                  output,
                                                  &lock,
                                                  &pin1_state, NULL, NULL, &pin2_state, NULL, NULL, NULL,
                                                  &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else {
        ctx->locks &= ~(MM_MODEM_3GPP_FACILITY_SIM);
        ctx->locks &= ~(MM_MODEM_3GPP_FACILITY_FIXED_DIALING);

        if (pin1_state == QMI_UIM_PIN_STATE_ENABLED_VERIFIED ||
            pin1_state == QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED ||
            pin1_state == QMI_UIM_PIN_STATE_BLOCKED) {
            ctx->locks |= (MM_MODEM_3GPP_FACILITY_SIM);
        }
        if (pin2_state == QMI_UIM_PIN_STATE_ENABLED_VERIFIED ||
            pin2_state == QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED ||
            pin2_state == QMI_UIM_PIN_STATE_BLOCKED) {
            ctx->locks |= (MM_MODEM_3GPP_FACILITY_FIXED_DIALING);
        }

        g_task_return_int (task, ctx->locks);
    }

    qmi_message_uim_get_card_status_output_unref (output);
    g_object_unref (task);
}

static void
get_pin_lock_status_via_get_configuration_ready (QmiClientUim *client,
                                                 GAsyncResult *res,
                                                 GTask *task)
{
    MMModem3gppFacility lock = MM_MODEM_3GPP_FACILITY_NONE;
    QmiMessageUimGetConfigurationOutput *output;
    LoadEnabledFacilityLocksContext *ctx;
    MMBroadbandModemQmi *self;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    output = qmi_client_uim_get_configuration_finish (client, res, &error);
    if (!output ||
        !qmi_message_uim_get_configuration_output_get_result (output, &error)) {
        g_prefix_error (&error, "QMI message Get Configuration failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        if (output)
            qmi_message_uim_get_configuration_output_unref (output);
        return;
    }

    if (!mm_qmi_uim_get_configuration_output_parse (self,
                                                    output,
                                                    &lock,
                                                    &error)) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_uim_get_configuration_output_unref (output);
        return;
    }

    ctx->locks = lock;
    qmi_message_uim_get_configuration_output_unref (output);

    mm_obj_dbg (self, "Getting UIM card status to read pin lock state...");
    qmi_client_uim_get_card_status (QMI_CLIENT_UIM (ctx->client),
                                    NULL,
                                    5,
                                    NULL,
                                    (GAsyncReadyCallback) get_sim_lock_status_via_get_card_status_ready,
                                    task);
}

static void
get_facility_lock_status_via_uim (GTask *task)
{
    QmiMessageUimGetConfigurationInput *input;
    LoadEnabledFacilityLocksContext *ctx;
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_dbg (self, "Getting UIM Get Configuration to read facility lock state...");
    input = qmi_message_uim_get_configuration_input_new ();
    qmi_message_uim_get_configuration_input_set_configuration_mask (
         input,
         QMI_UIM_CONFIGURATION_PERSONALIZATION_STATUS,
         NULL);

    qmi_client_uim_get_configuration (QMI_CLIENT_UIM (ctx->client),
                                      input,
                                      5,
                                      NULL,
                                      (GAsyncReadyCallback)get_pin_lock_status_via_get_configuration_ready,
                                      task);
    qmi_message_uim_get_configuration_input_unref (input);
}

static void
get_sim_lock_status_via_pin_status_ready (QmiClientDms *client,
                                          GAsyncResult *res,
                                          GTask *task)
{
    MMBroadbandModemQmi *self;
    LoadEnabledFacilityLocksContext *ctx;
    QmiMessageDmsUimGetPinStatusOutput *output;
    QmiDmsUimPinStatus current_status;
    GError *error = NULL;
    gboolean pin1_enabled;
    gboolean pin2_enabled;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output ||
        !qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        if (output)
            qmi_message_dms_uim_get_pin_status_output_unref (output);
        return;
    }

    if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
        output,
        &current_status,
        NULL, /* verify_retries_left */
        NULL, /* unblock_retries_left */
        &error)) {
        pin1_enabled = mm_pin_enabled_from_qmi_uim_pin_status (current_status);
        mm_obj_dbg (self, "PIN1 is reported %s", (pin1_enabled ? "enabled" : "disabled"));
    } else {
        qmi_message_dms_uim_get_pin_status_output_unref (output);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (qmi_message_dms_uim_get_pin_status_output_get_pin2_status (
        output,
        &current_status,
        NULL, /* verify_retries_left */
        NULL, /* unblock_retries_left */
        &error)) {
        pin2_enabled = mm_pin_enabled_from_qmi_uim_pin_status (current_status);
        mm_obj_dbg (self, "PIN2 is reported %s", (pin2_enabled ? "enabled" : "disabled"));
    } else {
        qmi_message_dms_uim_get_pin_status_output_unref (output);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_dms_uim_get_pin_status_output_unref (output);

    if (pin1_enabled)
        ctx->locks |= (MM_MODEM_3GPP_FACILITY_SIM);
    else
        ctx->locks &= ~(MM_MODEM_3GPP_FACILITY_SIM);

    if (pin2_enabled)
        ctx->locks |= (MM_MODEM_3GPP_FACILITY_FIXED_DIALING);
    else
        ctx->locks &= ~(MM_MODEM_3GPP_FACILITY_FIXED_DIALING);

    /* No more facilities to query, all done */
    g_task_return_int (task, ctx->locks);
    g_object_unref (task);
}

/* the SIM lock cannot be queried with the qmi_get_ck_status function,
 * therefore using the PIN status */
static void
get_sim_lock_status_via_pin_status (GTask *task)
{
    MMBroadbandModemQmi             *self;
    LoadEnabledFacilityLocksContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_obj_dbg (self, "retrieving PIN status to check for enabled PIN");
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
    MMBroadbandModemQmi *self;
    LoadEnabledFacilityLocksContext *ctx;
    gchar *facility_str;
    QmiMessageDmsUimGetCkStatusOutput *output;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    facility_str = mm_modem_3gpp_facility_build_string_from_mask (1 << ctx->current);
    output = qmi_client_dms_uim_get_ck_status_finish (client, res, NULL);
    if (!output ||
        !qmi_message_dms_uim_get_ck_status_output_get_result (output, NULL)) {
        /* On errors, we'll just assume disabled */
        mm_obj_dbg (self, "couldn't query facility '%s' status, assuming disabled", facility_str);
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

        mm_obj_dbg (self, "facility '%s' is: '%s'",
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
    get_next_facility_lock_status_via_dms (task);
}

static void
get_next_facility_lock_status_via_dms (GTask *task)
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
    MMPort *port;

    if (!MM_BROADBAND_MODEM_QMI (self)->priv->dms_uim_deprecated) {
        if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                          QMI_SERVICE_DMS, &client,
                                          callback, user_data))
            return;
    } else {
        if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                          QMI_SERVICE_UIM, &client,
                                          callback, user_data))
            return;
    }

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

    /* If tagged by udev, perform a reduced facility lock query via DMS
     * by skipping get_ck_status and process get_pin_status only
     */
    port = MM_PORT (mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (self)));
    if (mm_kernel_device_get_global_property_as_boolean (mm_port_peek_kernel_device (port),
                                                         "ID_MM_QMI_FACILITY_LOCK_QUERY_REDUCED")) {
        mm_obj_dbg (self, "performing reduced facility lock query (DMS)");
        get_sim_lock_status_via_pin_status (task);
        return;
    }

    /* Regular facility lock query
     * DMS uses get_ck_status and get_pin_status to probe facilities
     * UIM uses get_configuration and get_card_status
     */
    if (!MM_BROADBAND_MODEM_QMI (self)->priv->dms_uim_deprecated)
        get_next_facility_lock_status_via_dms (task);
    else
        get_facility_lock_status_via_uim (task);
}

/*****************************************************************************/
/* Facility locks disabling (3GPP interface) */

# define DISABLE_FACILITY_LOCK_CHECK_TIMEOUT_MS 100
# define DISABLE_FACILITY_LOCK_CHECK_ATTEMPTS    10

typedef struct _DisableFacilityLockContext DisableFacilityLockContext;
struct _DisableFacilityLockContext {
    MMModem3gppFacility facility;
    guint remaining_attempts;
    guint8 slot;
};

static gboolean disable_facility_lock_check (GTask *task);

static gboolean
modem_3gpp_disable_facility_lock_finish (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_facility_lock_check_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    DisableFacilityLockContext *ctx;
    MMModem3gppFacility facilities;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    facilities = modem_3gpp_load_enabled_facility_locks_finish (self, res, &error);
    if (error) {
        g_prefix_error (&error, "Failed to check the facility locks: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Check if the facility lock is still enabled */
    if (facilities & ctx->facility) {
        /* Wait again and retry */
        g_timeout_add (DISABLE_FACILITY_LOCK_CHECK_TIMEOUT_MS,
                       (GSourceFunc)disable_facility_lock_check,
                       task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
disable_facility_lock_check (GTask *task)
{
    DisableFacilityLockContext *ctx;
    MMIfaceModem3gpp *self;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);
    if (ctx->remaining_attempts) {
        ctx->remaining_attempts--;
        modem_3gpp_load_enabled_facility_locks (self,
                                                (GAsyncReadyCallback)disable_facility_lock_check_ready,
                                                task);
    } else {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed to disable the facility lock.");
        g_object_unref (task);
    }

    return G_SOURCE_REMOVE;
}

static void
disable_facility_lock_ready (QmiClientUim *client,
                             GAsyncResult *res,
                             GTask *task)
{
    QmiMessageUimDepersonalizationOutput *output;
    GError *error = NULL;

    output = qmi_client_uim_depersonalization_finish (client, res, &error);
    if (!output ||
        !qmi_message_uim_depersonalization_output_get_result (output, &error)) {
        g_prefix_error (&error, "QMI message Depersonalization failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
    } else {
        /* Wait defined time for lock change to propagate to Card Status */
        g_timeout_add (DISABLE_FACILITY_LOCK_CHECK_TIMEOUT_MS,
                       (GSourceFunc)disable_facility_lock_check,
                       task);
    }

    if (output)
        qmi_message_uim_depersonalization_output_unref (output);
}

static void
modem_3gpp_disable_facility_lock (MMIfaceModem3gpp *self,
                                  MMModem3gppFacility facility,
                                  guint8 slot,
                                  const gchar *key,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    QmiUimCardApplicationPersonalizationFeature feature;
    QmiMessageUimDepersonalizationInput *input;
    DisableFacilityLockContext *ctx;
    QmiClient *client = NULL;
    GTask *task;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_UIM, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Choose facility to disable */
    if (!qmi_personalization_feature_from_mm_modem_3gpp_facility (facility, &feature)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Not supported type of facility lock.");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "Trying to disable %s lock on slot %d using key: %s",
                qmi_uim_card_application_personalization_feature_get_string (feature),
                slot, key);

    input = qmi_message_uim_depersonalization_input_new ();
    qmi_message_uim_depersonalization_input_set_info (input,
                                                      feature,
                                                      QMI_UIM_DEPERSONALIZATION_OPERATION_DEACTIVATE,
                                                      key,
                                                      NULL);
    qmi_message_uim_depersonalization_input_set_slot (input, slot, NULL);

    ctx = g_new0 (DisableFacilityLockContext, 1);
    ctx->facility = facility;
    ctx->slot = slot;
    ctx->remaining_attempts = DISABLE_FACILITY_LOCK_CHECK_ATTEMPTS;
    g_task_set_task_data (task, ctx, g_free);

    qmi_client_uim_depersonalization (QMI_CLIENT_UIM (client),
                                      input,
                                      30,
                                      NULL,
                                      (GAsyncReadyCallback) disable_facility_lock_ready,
                                      task);
    qmi_message_uim_depersonalization_input_unref (input);
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    mm_obj_dbg (self, "scanning networks...");
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
modem_3gpp_load_operator_name_finish (MMIfaceModem3gpp  *self,
                                      GAsyncResult      *res,
                                      GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
get_plmn_name_ready (QmiClientNas *client,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMBroadbandModemQmi              *self;
    GError                           *error = NULL;
    QmiNasNetworkDescriptionEncoding  plmn_name_service_provider_name_encoding;
    QmiNasNetworkDescriptionEncoding  plmn_name_short_name_encoding;
    QmiNasNetworkDescriptionEncoding  plmn_name_long_name_encoding;
    GArray                           *plmn_name_service_provider_name;
    GArray                           *plmn_name_short_name;
    GArray                           *plmn_name_long_name;
    g_autoptr(QmiMessageNasGetPlmnNameOutput) output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_nas_get_plmn_name_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_nas_get_plmn_name_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (qmi_message_nas_get_plmn_name_output_get_3gpp_eons_plmn_name (
        output,
        &plmn_name_service_provider_name_encoding,
        &plmn_name_service_provider_name,
        &plmn_name_short_name_encoding,
        NULL,
        NULL,
        &plmn_name_short_name,
        &plmn_name_long_name_encoding,
        NULL,
        NULL,
        &plmn_name_long_name,
        NULL)) {
            g_autofree gchar *long_name = NULL;
            g_autofree gchar *short_name = NULL;
            g_autofree gchar *service_name = NULL;

            long_name = qmi_nas_read_string_from_network_description_encoded_array (plmn_name_long_name_encoding, plmn_name_long_name);
            short_name = qmi_nas_read_string_from_network_description_encoded_array (plmn_name_short_name_encoding, plmn_name_short_name);
            service_name = qmi_nas_read_string_from_network_description_encoded_array (plmn_name_service_provider_name_encoding, plmn_name_service_provider_name);
            mm_obj_dbg (self, "current operator long name: %s",    long_name);
            mm_obj_dbg (self, "current operator short name: %s",   short_name);
            mm_obj_dbg (self, "current operator service name: %s", service_name);
            if (!self->priv->current_operator_description) {
                self->priv->current_operator_description = (service_name ? g_steal_pointer (&service_name) :
                                                            (long_name   ? g_steal_pointer (&long_name)    :
                                                             (short_name ? g_steal_pointer (&short_name)   :
                                                              NULL)));
            }
    }

    if (!self->priv->current_operator_description)
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Current operator description is still unknown and cannot be retrieved from MCC/MNC");
    else
        g_task_return_pointer (task, g_strdup (self->priv->current_operator_description), g_free);

    g_object_unref (task);
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp    *_self,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask               *task;
    QmiClient           *client;
    guint16              mcc = 0;
    guint16              mnc = 0;
    gboolean             mnc_pcs_digit = FALSE;
    g_autoptr(GError)    error = NULL;
    g_autoptr(QmiMessageNasGetPlmnNameInput) input = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->current_operator_description) {
        g_task_return_pointer (task, g_strdup (self->priv->current_operator_description), g_free);
        g_object_unref (task);
        return;
    }

    /* Check if operator id is set */
    if (!self->priv->current_operator_id) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Current operator id is still unknown");
        g_object_unref (task);
        return;
    }

    /* Parse input MCC/MNC */
    if (!mm_3gpp_parse_operator_id (self->priv->current_operator_id, &mcc, &mnc, &mnc_pcs_digit, &error)) {
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* Try to get PLMN name from MCC/MNC */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_NAS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        &error);
    if (!client) {
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    input = qmi_message_nas_get_plmn_name_input_new ();
    qmi_message_nas_get_plmn_name_input_set_plmn (input, mcc, mnc, NULL);
    if (mnc_pcs_digit && mnc < 100)
        qmi_message_nas_get_plmn_name_input_set_mnc_pcs_digit_include_status (input, mnc_pcs_digit, NULL);

    qmi_client_nas_get_plmn_name (QMI_CLIENT_NAS (client),
                                  input,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback)get_plmn_name_ready,
                                  task);
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
    guint16 tac;
    guint32 cid;
    MMModemAccessTechnology mm_access_technologies;
    MMModem3gppRegistrationState mm_cs_registration_state;
    MMModem3gppRegistrationState mm_ps_registration_state;
    gboolean operator_updated = FALSE;

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

    if (data_service_capabilities && data_service_capabilities->len > 0)
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_data_capability_array (data_service_capabilities);
    else
        mm_access_technologies =
            mm_modem_access_technologies_from_qmi_radio_interface_array (radio_interfaces);

    /* Only process 3GPP info.
     * Seen the case already where 'selected_network' gives UNKNOWN but we still
     * have valid LTE/5GNR info around. */
    if (selected_network == QMI_NAS_NETWORK_TYPE_3GPP ||
        (selected_network == QMI_NAS_NETWORK_TYPE_UNKNOWN &&
         (mm_access_technologies & MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK))) {
        mm_obj_dbg (self, "processing 3GPP info...");
    } else {
        MMModem3gppRegistrationState reg_state_3gpp;

        mm_obj_dbg (self, "no 3GPP info given...");
        if (self->priv->current_operator_id || self->priv->current_operator_description)
            operator_updated = TRUE;
        g_free (self->priv->current_operator_id);
        self->priv->current_operator_id = NULL;
        g_free (self->priv->current_operator_description);
        self->priv->current_operator_description = NULL;

        if (registration_state == QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING)
            reg_state_3gpp = MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;
        else
            reg_state_3gpp = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp, TRUE);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp, TRUE);
        if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
            mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp, TRUE);
        if (mm_iface_modem_is_3gpp_5gnr (MM_IFACE_MODEM (self)))
            mm_iface_modem_3gpp_update_5gs_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_3gpp, TRUE);
        mm_iface_modem_3gpp_apply_deferred_registration_state (MM_IFACE_MODEM_3GPP (self));

        mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), 0, 0, 0);
        /* request to reload operator info explicitly, so that the new
         * operator name and code is propagated to the DBus interface */
        if (operator_updated)
            mm_iface_modem_3gpp_reload_current_registration_info (MM_IFACE_MODEM_3GPP (self), NULL, NULL);
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
        gchar *new_operator_id;

        /* When we don't have information about leading PCS digit, guess best */
        if (mnc >= 100)
            new_operator_id = g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.3" G_GUINT16_FORMAT, mcc, mnc);
        else
            new_operator_id = g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.2" G_GUINT16_FORMAT, mcc, mnc);

        if (!self->priv->current_operator_id || !g_str_equal (self->priv->current_operator_id, new_operator_id)) {
            operator_updated = TRUE;
            g_free (self->priv->current_operator_id);
            self->priv->current_operator_id = new_operator_id;
        } else
            g_free (new_operator_id);

        if (!self->priv->current_operator_description || !g_str_equal (self->priv->current_operator_description, description)) {
            operator_updated = TRUE;
            g_free (self->priv->current_operator_description);
            self->priv->current_operator_description = g_strdup (description);
        }
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
        gchar *new_operator_id;

        new_operator_id = g_strdup_printf ("%.3" G_GUINT16_FORMAT "%.3" G_GUINT16_FORMAT, mcc, mnc);
        if (!self->priv->current_operator_id || !g_str_equal (self->priv->current_operator_id, new_operator_id)) {
            operator_updated = TRUE;
            g_free (self->priv->current_operator_id);
            self->priv->current_operator_id = new_operator_id;
        } else
            g_free (new_operator_id);
    }

    /* Report new registration states. The QMI serving system API reports "CS"
     * and "PS" registration states, and if the device is in LTE, we'll take the "PS"
     * one as "EPS". But, if the device is not in LTE, we should also set the "EPS"
     * state as unknown, so that the "PS" one takes precedence when building
     * the consolidated registration state (otherwise we may be using some old cached
     * "EPS" state wrongly). */
    mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), mm_cs_registration_state, TRUE);
    mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), mm_ps_registration_state, TRUE);
    if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
        mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                           ((mm_access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_LTE) ?
                                                            mm_ps_registration_state : MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN),
                                                           TRUE);
    /* Same thing for "5GS" state */
    if (mm_iface_modem_is_3gpp_5gnr (MM_IFACE_MODEM (self)))
        mm_iface_modem_3gpp_update_5gs_registration_state (MM_IFACE_MODEM_3GPP (self),
                                                           ((mm_access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_5GNR) ?
                                                            mm_ps_registration_state : MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN),
                                                           TRUE);
    mm_iface_modem_3gpp_apply_deferred_registration_state (MM_IFACE_MODEM_3GPP (self));
    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), mm_access_technologies);

    /* Get 3GPP location LAC/TAC and CI */
    lac = 0;
    tac = 0;
    cid = 0;
    if (response_output) {
        qmi_message_nas_get_serving_system_output_get_lac_3gpp (response_output, &lac, NULL);
        qmi_message_nas_get_serving_system_output_get_lte_tac  (response_output, &tac, NULL);
        qmi_message_nas_get_serving_system_output_get_cid_3gpp (response_output, &cid, NULL);
    } else if (indication_output) {
        qmi_indication_nas_serving_system_output_get_lac_3gpp (indication_output, &lac, NULL);
        qmi_indication_nas_serving_system_output_get_lte_tac  (indication_output, &tac, NULL);
        qmi_indication_nas_serving_system_output_get_cid_3gpp (indication_output, &cid, NULL);
    }
    /* Only update info in the interface if we get something */
    if (cid || lac || tac)
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, tac, cid);

    /* request to reload operator info explicitly, so that the new
     * operator name and code is propagated to the DBus interface */
    if (operator_updated)
        mm_iface_modem_3gpp_reload_current_registration_info (MM_IFACE_MODEM_3GPP (self), NULL, NULL);
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

static void
common_process_system_info_3gpp (MMBroadbandModemQmi              *self,
                                 QmiMessageNasGetSystemInfoOutput *response_output,
                                 QmiIndicationNasSystemInfoOutput *indication_output)
{
    MMModem3gppRegistrationState  registration_state_cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState  registration_state_ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState  registration_state_eps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState  registration_state_5gs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModemAccessTechnology       act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    guint16                       lac = 0;
    guint16                       tac = 0;
    guint32                       cid = 0;
    gchar                        *operator_id = NULL;

    mm_modem_registration_state_from_qmi_system_info (response_output,
                                                      indication_output,
                                                      &registration_state_cs,
                                                      &registration_state_ps,
                                                      &registration_state_eps,
                                                      &registration_state_5gs,
                                                      &lac,
                                                      &tac,
                                                      &cid,
                                                      &operator_id,
                                                      &act,
                                                      self);

    /* Cache current operator ID */
    if (operator_id) {
        g_free (self->priv->current_operator_id);
        self->priv->current_operator_id = operator_id;
    }

    /* Update registration states */
    mm_iface_modem_3gpp_update_cs_registration_state  (MM_IFACE_MODEM_3GPP (self), registration_state_cs, TRUE);
    mm_iface_modem_3gpp_update_ps_registration_state  (MM_IFACE_MODEM_3GPP (self), registration_state_ps, TRUE);
    mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self), registration_state_eps, TRUE);
    mm_iface_modem_3gpp_update_5gs_registration_state (MM_IFACE_MODEM_3GPP (self), registration_state_5gs, TRUE);
    mm_iface_modem_3gpp_apply_deferred_registration_state (MM_IFACE_MODEM_3GPP (self));

    /* Update act and location info */
    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), act);
    mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, tac, cid);
}

static gboolean
get_3gpp_rat_data_available (QmiDsdRadioAccessTechnology rat)
{
    switch (rat) {
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_WCDMA:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_TDSCDMA:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_GERAN:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_LTE:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_5G:
            return TRUE;
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP_WLAN:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP2_1X:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP2_HRPD:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP2_EHRPD:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_3GPP2_WLAN:
        case QMI_DSD_RADIO_ACCESS_TECHNOLOGY_UNKNOWN:
        default:
            return FALSE;
    }
}

static void
common_process_system_status_3gpp (MMBroadbandModemQmi                *self,
                                   QmiMessageDsdGetSystemStatusOutput *response_output,
                                   QmiIndicationDsdSystemStatusOutput *indication_output)
{
    GArray   *available_systems = NULL;
    gboolean  data_rat_available = FALSE;

    if (response_output) {
        qmi_message_dsd_get_system_status_output_get_available_systems (response_output, &available_systems, NULL);

        if (available_systems && available_systems->len) {
            QmiMessageDsdGetSystemStatusOutputAvailableSystemsSystem *system;

            system = &g_array_index (available_systems, QmiMessageDsdGetSystemStatusOutputAvailableSystemsSystem, 0);

            if (system->technology == QMI_DSD_DATA_SYSTEM_NETWORK_TYPE_3GPP)
                data_rat_available = get_3gpp_rat_data_available (system->rat);
        }
    }  else {
        qmi_indication_dsd_system_status_output_get_available_systems (indication_output, &available_systems, NULL);

        if (available_systems && available_systems->len) {
            QmiIndicationDsdSystemStatusOutputAvailableSystemsSystem *system;

            system = &g_array_index (available_systems, QmiIndicationDsdSystemStatusOutputAvailableSystemsSystem, 0);

            if (system->technology == QMI_DSD_DATA_SYSTEM_NETWORK_TYPE_3GPP)
                data_rat_available = get_3gpp_rat_data_available (system->rat);
        }
    }

    /* Store DSD data RAT availability and update PS/EPS/5GS states accordingly */
    mm_iface_modem_3gpp_update_packet_service_state (
        MM_IFACE_MODEM_3GPP (self),
        data_rat_available ? MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED : MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED);
}

static void
get_system_status_3gpp_ready (QmiClientDsd *client,
                              GAsyncResult *res,
                              GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageDsdGetSystemStatusOutput *output;
    GError *error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_dsd_get_system_status_finish (client, res, &error);
    if (!output || !qmi_message_dsd_get_system_status_output_get_result (output, &error)) {
        /* If this command fails, flag that dsd is not unsupported */
        self->priv->dsd_supported = FALSE;
        g_error_free (error);
    }

    if (output) {
        common_process_system_status_3gpp (self, output, NULL);
        qmi_message_dsd_get_system_status_output_unref (output);
    }

    /* Just ignore errors for now */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
get_system_info_ready (QmiClientNas *client,
                       GAsyncResult *res,
                       GTask *task)
{
    MMBroadbandModemQmi *self;
    QmiMessageNasGetSystemInfoOutput *output;
    GError *error = NULL;
    QmiClient *client_dsd;

    self = g_task_get_source_object (task);

    output = qmi_client_nas_get_system_info_finish (client, res, &error);
    if (!output) {
        mm_obj_dbg (self, "couldn't get system info: '%s', falling back to nas get serving system 3gpp",
                    error->message);
        qmi_client_nas_get_serving_system (QMI_CLIENT_NAS (client),
                                           NULL,
                                           10,
                                           NULL,
                                           (GAsyncReadyCallback)get_serving_system_3gpp_ready,
                                           task);
        g_clear_error (&error);
        return;
    }

    if (!qmi_message_nas_get_system_info_output_get_result (output, &error)) {
        qmi_message_nas_get_system_info_output_unref (output);
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            mm_obj_dbg (self, "couldn't get system info: '%s', falling back to nas get serving system 3gpp",
                        error->message);
            qmi_client_nas_get_serving_system (QMI_CLIENT_NAS (client),
                                               NULL,
                                               10,
                                               NULL,
                                               (GAsyncReadyCallback)get_serving_system_3gpp_ready,
                                               task);
            g_clear_error (&error);
            return;
        }
        g_prefix_error (&error, "Couldn't get system info: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    common_process_system_info_3gpp (self, output, NULL);
    qmi_message_nas_get_system_info_output_unref (output);

    if (self->priv->dsd_supported) {
        client_dsd = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                                QMI_SERVICE_DSD,
                                                MM_PORT_QMI_FLAG_DEFAULT,
                                                &error);
        if (!client_dsd) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        qmi_client_dsd_get_system_status (QMI_CLIENT_DSD (client_dsd),
                                          NULL,
                                          10,
                                          NULL,
                                          (GAsyncReadyCallback)get_system_status_3gpp_ready,
                                          task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp    *self,
                                    gboolean             is_cs_supported,
                                    gboolean             is_ps_supported,
                                    gboolean             is_eps_supported,
                                    gboolean             is_5gs_supported,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_nas_get_system_info (QMI_CLIENT_NAS (client),
                                    NULL,
                                    10,
                                    NULL,
                                    (GAsyncReadyCallback)get_system_info_ready,
                                    task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events (3GPP interface) */

typedef struct {
    QmiClientNas *client;
    gboolean enable; /* TRUE for enabling, FALSE for disabling */
    gboolean system_info_checked;
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
    ctx->client = QMI_CLIENT_NAS (g_object_ref (client));
    ctx->enable = enable;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)unsolicited_registration_events_context_free);

    return task;
}

static gboolean
modem_3gpp_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp  *self,
                                                                  GAsyncResult      *res,
                                                                  GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_enable_disable_unsolicited_registration_events_serving_system (GTask *task);

static void
common_enable_disable_unsolicited_registration_events_system_status (GTask *task);

static void
ri_serving_system_or_system_info_ready (QmiClientNas *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MMBroadbandModemQmi                               *self;
    UnsolicitedRegistrationEventsContext              *ctx;
    g_autoptr(QmiMessageNasRegisterIndicationsOutput)  output = NULL;
    g_autoptr(GError)                                  error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_nas_register_indications_finish (client, res, &error);
    if (!output || !qmi_message_nas_register_indications_output_get_result (output, &error)) {
        if (!ctx->system_info_checked) {
            mm_obj_dbg (self, "couldn't register system info indication: '%s', falling-back to serving system", error->message);
            ctx->system_info_checked = TRUE;
            common_enable_disable_unsolicited_registration_events_serving_system (task);
            g_clear_error (&error);
            return;
        }


        if (ctx->enable)
            mm_obj_dbg (self, "couldn't register serving system indications: '%s', assuming always enabled", error->message);
    }

    if (!ctx->system_info_checked) {
        ctx->system_info_checked = TRUE;
        /* registered system info indications. now try to register for system status indications */
        if (self->priv->dsd_supported) {
            common_enable_disable_unsolicited_registration_events_system_status (task);
            return;
        }
    }

    /* Just ignore errors for now */
    self->priv->unsolicited_registration_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
ri_system_status_ready (QmiClientDsd *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMBroadbandModemQmi                              *self;
    UnsolicitedRegistrationEventsContext             *ctx;
    g_autoptr(QmiMessageDsdSystemStatusChangeOutput)  output = NULL;
    g_autoptr(GError)                                 error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_dsd_system_status_change_finish (client, res, &error);
    if (!output || !qmi_message_dsd_system_status_change_output_get_result (output, &error)) {
        /* If this command fails, flag that dsd is not unsupported */
        self->priv->dsd_supported = FALSE;
        if (ctx->enable)
            mm_obj_dbg (self, "couldn't register system status indications: '%s', assuming always enabled", error->message);
        g_clear_error (&error);
    }

    /* Just ignore errors for now */
    self->priv->unsolicited_registration_events_enabled = ctx->enable;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_registration_events_serving_system (GTask *task)
{
    UnsolicitedRegistrationEventsContext             *ctx;
    g_autoptr(QmiMessageNasRegisterIndicationsInput)  input = NULL;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_serving_system_events (input, ctx->enable, NULL);
    qmi_message_nas_register_indications_input_set_network_reject_information (input, ctx->enable, FALSE, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        task);
}

static void
common_enable_disable_unsolicited_registration_events_system_status (GTask *task)
{
    MMBroadbandModemQmi                             *self;
    QmiClient                                       *client;
    UnsolicitedRegistrationEventsContext            *ctx;
    g_autoptr(QmiMessageDsdSystemStatusChangeInput)  input = NULL;
    GError                                          *error = NULL;

    self = g_task_get_source_object (task);

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_DSD,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        &error);
    if (!client) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    input = qmi_message_dsd_system_status_change_input_new ();
    qmi_message_dsd_system_status_change_input_set_register_indication (input, ctx->enable, NULL);

    qmi_client_dsd_system_status_change (
        QMI_CLIENT_DSD (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_system_status_ready,
        task);
}

static void
common_enable_disable_unsolicited_registration_events_system_info (GTask *task)
{
    UnsolicitedRegistrationEventsContext             *ctx;
    g_autoptr(QmiMessageNasRegisterIndicationsInput)  input = NULL;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_system_info (input, ctx->enable, NULL);
    /* When enabling, serving system events are turned-off, since some modems have them
     * active by default. They will be turned-on again if setting system info events fails
     */
    if (ctx->enable)
        qmi_message_nas_register_indications_input_set_serving_system_events (input, FALSE, NULL);
    qmi_message_nas_register_indications_input_set_network_reject_information (input, ctx->enable, FALSE, NULL);
    qmi_client_nas_register_indications (
        ctx->client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_serving_system_or_system_info_ready,
        task);
}

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp    *self,
                                                    gboolean             cs_supported,
                                                    gboolean             ps_supported,
                                                    gboolean             eps_supported,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = unsolicited_registration_events_task_new (MM_BROADBAND_MODEM_QMI (self),
                                                     client,
                                                     FALSE,
                                                     callback,
                                                     user_data);

    common_enable_disable_unsolicited_registration_events_system_info (task);
}

static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp    *self,
                                                   gboolean             cs_supported,
                                                   gboolean             ps_supported,
                                                   gboolean             eps_supported,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    task = unsolicited_registration_events_task_new (MM_BROADBAND_MODEM_QMI (self),
                                                     client,
                                                     TRUE,
                                                     callback,
                                                     user_data);

    common_enable_disable_unsolicited_registration_events_system_info (task);
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
        mm_obj_dbg (self, "processing CDMA info...");
    } else {
        mm_obj_dbg (self, "no CDMA info given...");
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
    mm_iface_modem_cdma_update_access_technologies (MM_IFACE_MODEM_CDMA (self),
                                                    mm_access_technologies);

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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
    CDMA_ACTIVATION_STEP_RESET,
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
activation_reset_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        GTask *task)
{
    CdmaActivationContext *ctx;
    GError *error = NULL;

    if (!mm_shared_qmi_reset_finish (self, res, &error)) {
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
    MMBroadbandModemQmi *self;
    CdmaActivationContext *ctx;
    QmiMessageDmsGetMsisdnOutput *output = NULL;
    GError *error = NULL;
    const gchar *current_mdn = NULL;
    const gchar *expected_mdn = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

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
        mm_obj_dbg (self, "MDN successfully updated to '%s'", expected_mdn);
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
        mm_obj_dbg (self, "MDN not yet updated, retrying...");
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

    mm_obj_dbg (self, "activation state update: '%s'",
            qmi_dms_activation_state_get_string (state));

    new = mm_modem_cdma_activation_state_from_qmi_activation_state (state);

    if (self->priv->activation_state != new)
        mm_obj_msg (self, "activation state changed: '%s'-->'%s'",
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

    mm_obj_dbg (self, "activation process still ongoing...");
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
        /* Fall through */

    case CDMA_ACTIVATION_STEP_ENABLE_INDICATIONS:
        /* Indications needed in automatic activation */
        if (ctx->input_automatic) {
            QmiMessageDmsSetEventReportInput *input;

            mm_obj_msg (ctx->self, "activation step [1/5]: enabling indications");
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
        mm_obj_msg (ctx->self, "activation step [1/5]: indications not needed in manual activation");
        ctx->step++;
        /* Fall through */

    case CDMA_ACTIVATION_STEP_REQUEST_ACTIVATION:
        /* Automatic activation */
        if (ctx->input_automatic) {
            mm_obj_msg (ctx->self, "activation step [2/5]: requesting automatic (OTA) activation");
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
            mm_obj_msg (ctx->self, "activation step [2/5]: requesting manual activation");
        else {
            mm_obj_msg (ctx->self, "activation step [2/5]: requesting manual activation (PRL segment %u/%u)",
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
            mm_obj_msg (ctx->self, "activation step [3/5]: waiting for activation state updates");
            return;
        }

        /* Manual activation; needs MSISDN checks */
        g_assert (ctx->input_manual != NULL);
        ctx->n_mdn_check_retries++;
        mm_obj_msg (ctx->self, "activation step [3/5]: checking MDN update (retry %u)", ctx->n_mdn_check_retries);
        qmi_client_dms_get_msisdn (ctx->client,
                                   NULL,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback)activate_manual_get_msisdn_ready,
                                   task);
        return;

    case CDMA_ACTIVATION_STEP_RESET:
        mm_obj_msg (ctx->self, "activation step [4/5]: power-cycling...");
        mm_shared_qmi_reset (MM_IFACE_MODEM (ctx->self),
                             (GAsyncReadyCallback)activation_reset_ready,
                             task);
        return;

    case CDMA_ACTIVATION_STEP_LAST:
        mm_obj_msg (ctx->self, "activation step [5/5]: finished");
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
    ctx->client = QMI_CLIENT_DMS (g_object_ref (client));
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
    ctx->client = QMI_CLIENT_DMS (g_object_ref (client));

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

static void
system_info_indication_cb (QmiClientNas *client,
                           QmiIndicationNasSystemInfoOutput *output,
                           MMBroadbandModemQmi *self)
{
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        common_process_system_info_3gpp (self, NULL, output);
}

static void
system_status_indication_cb (QmiClientDsd *client,
                             QmiIndicationDsdSystemStatusOutput *output,
                             MMBroadbandModemQmi *self)
{
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        common_process_system_status_3gpp (self, NULL, output);
}


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

/* network reject indications enabled in both with/without newest QMI commands */
static void
network_reject_indication_cb (QmiClientNas                        *client,
                              QmiIndicationNasNetworkRejectOutput *output,
                              MMBroadbandModemQmi                 *self)
{
    QmiNasNetworkServiceDomain service_domain = QMI_NAS_NETWORK_SERVICE_DOMAIN_UNKNOWN;
    QmiNasRadioInterface       radio_interface = QMI_NAS_RADIO_INTERFACE_UNKNOWN;
    QmiNasRejectCause          reject_cause = QMI_NAS_REJECT_CAUSE_NONE;
    guint16                    mcc = 0;
    guint16                    mnc = 0;
    guint32                    closed_subscriber_group = 0;

    mm_obj_warn (self, "network reject indication received");
    if (qmi_indication_nas_network_reject_output_get_service_domain (output, &service_domain, NULL))
        mm_obj_warn (self, "  service domain: %s", qmi_nas_network_service_domain_get_string (service_domain));
    if (qmi_indication_nas_network_reject_output_get_radio_interface (output, &radio_interface, NULL))
        mm_obj_warn (self, "  radio interface: %s", qmi_nas_radio_interface_get_string (radio_interface));
    if (qmi_indication_nas_network_reject_output_get_reject_cause (output, &reject_cause, NULL))
        mm_obj_warn (self, "  reject cause: %s", qmi_nas_reject_cause_get_string (reject_cause));
    if (qmi_indication_nas_network_reject_output_get_plmn (output, &mcc, &mnc, NULL, NULL)) {
        mm_obj_warn (self, "  mcc: %" G_GUINT16_FORMAT, mcc);
        mm_obj_warn (self, "  mnc: %" G_GUINT16_FORMAT, mnc);
    }
    if (qmi_indication_nas_network_reject_output_get_closed_subscriber_group (output, &closed_subscriber_group, NULL))
        mm_obj_warn (self, "  closed subscriber group: %u", closed_subscriber_group);
}

static void
common_setup_cleanup_unsolicited_registration_events (MMBroadbandModemQmi *self,
                                                      gboolean enable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    GTask *task;
    QmiClient *client_nas = NULL;
    QmiClient *client_dsd = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client_nas,
                                      callback, user_data))
        return;

    client_dsd = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_DSD,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_registration_events_setup) {
        mm_obj_dbg (self, "unsolicited registration events already %s; skipping",
                enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Store new state */
    self->priv->unsolicited_registration_events_setup = enable;

    /* Connect/Disconnect "System Info" indications */
    if (enable) {
        g_assert (self->priv->system_info_indication_id == 0);
        self->priv->system_info_indication_id =
            g_signal_connect (client_nas,
                              "system-info",
                              G_CALLBACK (system_info_indication_cb),
                              self);
    } else {
        g_assert (self->priv->system_info_indication_id != 0);
        g_signal_handler_disconnect (client_nas, self->priv->system_info_indication_id);
        self->priv->system_info_indication_id = 0;
    }
    /* Connect/Disconnect "Serving System" indications */
    if (enable) {
        g_assert (self->priv->serving_system_indication_id == 0);
        self->priv->serving_system_indication_id =
            g_signal_connect (client_nas,
                              "serving-system",
                              G_CALLBACK (serving_system_indication_cb),
                              self);
    } else {
        g_assert (self->priv->serving_system_indication_id != 0);
        g_signal_handler_disconnect (client_nas, self->priv->serving_system_indication_id);
        self->priv->serving_system_indication_id = 0;
    }

    /* Connect/Disconnect "Network Reject" indications */
    if (enable) {
        g_assert (self->priv->network_reject_indication_id == 0);
        self->priv->network_reject_indication_id =
            g_signal_connect (client_nas,
                              "network-reject",
                              G_CALLBACK (network_reject_indication_cb),
                              self);
    } else {
        g_assert (self->priv->network_reject_indication_id != 0);
        g_signal_handler_disconnect (client_nas, self->priv->network_reject_indication_id);
        self->priv->network_reject_indication_id = 0;
    }

    /* Connect/Disconnect "System Status" indications */
    if (client_dsd) {
        self->priv->dsd_supported = TRUE;
        if (enable) {
            g_assert (self->priv->system_status_indication_id == 0);
            self->priv->system_status_indication_id =
                g_signal_connect (client_dsd,
                                  "system-status",
                                  G_CALLBACK (system_status_indication_cb),
                                  self);
        } else {
            g_assert (self->priv->system_status_indication_id != 0);
            g_signal_handler_disconnect (client_dsd, self->priv->system_status_indication_id);
            self->priv->system_status_indication_id = 0;
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
/* Enabling/disabling unsolicited events (3GPP and CDMA interface) */

typedef struct {
    QmiClientNas *client_nas;
    QmiClientWds *client_wds;
    gboolean      enable;
} EnableUnsolicitedEventsContext;

static void
enable_unsolicited_events_context_free (EnableUnsolicitedEventsContext *ctx)
{
    g_clear_object (&ctx->client_wds);
    g_clear_object (&ctx->client_nas);
    g_free (ctx);
}

/* default rssi delta value */
static const guint default_rssi_delta_dbm = 5;
/* RSSI values go between -105 and -60 for 3GPP technologies,
 * and from -105 to -90 in 3GPP2 technologies (approx). */
static const gint8 default_thresholds_data_dbm[] = { -100, -97, -95, -92, -90, -85, -80, -75, -70, -65 };

static gboolean
common_enable_disable_unsolicited_events_finish (MMBroadbandModemQmi  *self,
                                                 GAsyncResult         *res,
                                                 GError              **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ser_data_system_status_ready (QmiClientWds *client,
                              GAsyncResult *res,
                              GTask        *task)
{
    MMBroadbandModemQmi                          *self;
    g_autoptr(QmiMessageWdsSetEventReportOutput)  output = NULL;
    g_autoptr(GError)                             error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_set_event_report_finish (client, res, &error);
    if (!output || !qmi_message_wds_set_event_report_output_get_result (output, &error))
        mm_obj_dbg (self, "couldn't set event report: '%s'", error->message);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_events_data_system_status (GTask *task)
{
    EnableUnsolicitedEventsContext              *ctx;
    g_autoptr(QmiMessageWdsSetEventReportInput)  input = NULL;

    ctx = g_task_get_task_data     (task);

    input = qmi_message_wds_set_event_report_input_new ();
    qmi_message_wds_set_event_report_input_set_data_systems (input, ctx->enable, NULL);
    qmi_client_wds_set_event_report (ctx->client_wds,
                                     input,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)ser_data_system_status_ready,
                                     task);
}

static void
ser_signal_strength_ready (QmiClientNas *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    MMBroadbandModemQmi                          *self;
    EnableUnsolicitedEventsContext               *ctx;
    g_autoptr(QmiMessageNasSetEventReportOutput)  output = NULL;
    g_autoptr(GError)                             error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_nas_set_event_report_finish (client, res, &error);
    if (!output || !qmi_message_nas_set_event_report_output_get_result (output, &error))
        mm_obj_dbg (self, "couldn't enable signal strength indications: '%s'", error->message);
    else {
        /* Disable access technology and signal quality polling if we can use the indications */
        mm_obj_dbg (self, "signal strength indications enabled: polling disabled");
        g_object_set (self,
                      MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,      TRUE,
                      MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED, TRUE,
                      NULL);
    }

    if (!ctx->client_wds) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    common_enable_disable_unsolicited_events_data_system_status (task);
}

static void
common_enable_disable_unsolicited_events_signal_strength (GTask *task)
{
    EnableUnsolicitedEventsContext              *ctx;
    g_autoptr(QmiMessageNasSetEventReportInput)  input = NULL;
    g_autoptr(GArray)                            thresholds = NULL;

    /* The device doesn't really like to have many threshold values, so don't
     * grow this array without checking first
     * The values are chosen towards their results through MM_RSSI_TO_QUALITY
     *   -106 dBm gives 11%
     *    -94 dBm gives 30%
     *    -82 dBm gives 50%
     *    -69 dBm gives 70%
     *    -57 dBm gives 90%
     */
    static const gint8 thresholds_data[] = { -106, -94, -82, -69, -57 };

    ctx = g_task_get_task_data (task);

    /* Always set thresholds, both in enable and disable, or otherwise the protocol will
     * complain with FAILURE: NoThresholdsProvided */
    thresholds = g_array_sized_new (FALSE, FALSE, sizeof (gint8), G_N_ELEMENTS (thresholds_data));
    g_array_append_vals (thresholds, thresholds_data, G_N_ELEMENTS (thresholds_data));

    input = qmi_message_nas_set_event_report_input_new ();
    qmi_message_nas_set_event_report_input_set_signal_strength_indicator (
        input,
        ctx->enable,
        thresholds,
        NULL);
    qmi_client_nas_set_event_report (
        ctx->client_nas,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ser_signal_strength_ready,
        task);
}

static void
ri_signal_info_ready (QmiClientNas *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemQmi                               *self;
    EnableUnsolicitedEventsContext                    *ctx;
    g_autoptr(QmiMessageNasRegisterIndicationsOutput)  output = NULL;
    g_autoptr(GError)                                  error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_nas_register_indications_finish (client, res, &error);
    if (!output || !qmi_message_nas_register_indications_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't register signal info indications: '%s', falling back to signal strength", error->message);
        common_enable_disable_unsolicited_events_signal_strength (task);
        g_clear_error (&error);
        return;
    } else {
        /* Disable access technology and signal quality polling if we can use the indications */
        mm_obj_dbg (self, "signal info indications enabled: polling disabled");
        g_object_set (self,
                      MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,      TRUE,
                      MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED, TRUE,
                      NULL);
    }

    if (!ctx->client_wds) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    common_enable_disable_unsolicited_events_data_system_status (task);
}

static void
common_enable_disable_unsolicited_events_signal_info (GTask *task)
{
    EnableUnsolicitedEventsContext                   *ctx;
    g_autoptr(QmiMessageNasRegisterIndicationsInput)  input = NULL;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_register_indications_input_new ();
    qmi_message_nas_register_indications_input_set_signal_info (input, ctx->enable, NULL);
    qmi_client_nas_register_indications (
        ctx->client_nas,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)ri_signal_info_ready,
        task);
}

static void
config_signal_info_ready (QmiClientNas *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemQmi                            *self;
    g_autoptr(QmiMessageNasConfigSignalInfoOutput)  output = NULL;
    g_autoptr(GError)                               error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_nas_config_signal_info_finish (client, res, &error);
    if (!output || !qmi_message_nas_config_signal_info_output_get_result (output, &error))
        mm_obj_dbg (self, "couldn't config signal info: '%s'", error->message);

    /* Keep on */
    common_enable_disable_unsolicited_events_signal_info (task);
}

static void
config_signal_info_v2_ready (QmiClientNas *client,
                             GAsyncResult *res,
                             GTask        *task)
{
    MMBroadbandModemQmi                              *self;
    EnableUnsolicitedEventsContext                   *ctx;
    g_autoptr(QmiMessageNasConfigSignalInfoV2Output)  output = NULL;
    g_autoptr(QmiMessageNasConfigSignalInfoInput)     input = NULL;
    g_autoptr(GError)                                 error = NULL;
    g_autoptr(GArray)                                 thresholds = NULL;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    output = qmi_client_nas_config_signal_info_v2_finish (client, res, &error);
    if (!output || !qmi_message_nas_config_signal_info_v2_output_get_result (output, &error)) {
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            mm_obj_dbg (self, "couldn't config signal info using v2: '%s', falling back to config signal info using v1",
                        error->message);

            thresholds = g_array_sized_new (FALSE, FALSE, sizeof (gint8), G_N_ELEMENTS (default_thresholds_data_dbm));
            g_array_append_vals (thresholds, default_thresholds_data_dbm, G_N_ELEMENTS (default_thresholds_data_dbm));
            input = qmi_message_nas_config_signal_info_input_new ();
            qmi_message_nas_config_signal_info_input_set_rssi_threshold (input, thresholds, NULL);
            qmi_client_nas_config_signal_info (
                ctx->client_nas,
                input,
                5,
                NULL,
                (GAsyncReadyCallback)config_signal_info_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "couldn't config signal info: '%s'", error->message);
    }

    /* Keep on */
    common_enable_disable_unsolicited_events_signal_info (task);
}

static void
common_enable_disable_unsolicited_events_signal_info_config (GTask *task)
{
    EnableUnsolicitedEventsContext                  *ctx;
    g_autoptr(QmiMessageNasConfigSignalInfoV2Input)  input = NULL;
    guint                                            delta;

    ctx = g_task_get_task_data (task);

    /* Signal info config only to be run when enabling */
    if (!ctx->enable) {
        common_enable_disable_unsolicited_events_signal_info (task);
        return;
    }

    /* delta in units of 0.1dBm */
    delta = default_rssi_delta_dbm * 10;
    input = qmi_message_nas_config_signal_info_v2_input_new ();
    qmi_message_nas_config_signal_info_v2_input_set_cdma_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_hdr_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_gsm_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_wcdma_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_lte_rssi_delta (input, delta, NULL);
    /* use RSRP for 5GNR*/
    qmi_message_nas_config_signal_info_v2_input_set_nr5g_rsrp_delta (input, delta, NULL);
    qmi_client_nas_config_signal_info_v2 (
        ctx->client_nas,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)config_signal_info_v2_ready,
        task);
}

static void
common_enable_disable_unsolicited_events (MMBroadbandModemQmi *self,
                                          gboolean             enable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
    EnableUnsolicitedEventsContext *ctx;
    GTask                          *task;
    QmiClient                      *client_nas = NULL;
    QmiClient                      *client_wds = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_events_enabled) {
        mm_obj_dbg (self, "unsolicited events already %s; skipping",
                    enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->unsolicited_events_enabled = enable;

    client_nas = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_NAS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);
    client_wds = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_WDS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);

    ctx = g_new0 (EnableUnsolicitedEventsContext, 1);
    ctx->enable = enable;
    ctx->client_nas = client_nas ? QMI_CLIENT_NAS (g_object_ref (client_nas)) : NULL;
    ctx->client_wds = client_wds ? QMI_CLIENT_WDS (g_object_ref (client_wds)) : NULL;

    g_task_set_task_data (task, ctx, (GDestroyNotify)enable_unsolicited_events_context_free);

    if (ctx->client_nas) {
        common_enable_disable_unsolicited_events_signal_info_config (task);
        return;
    }

    if (ctx->client_wds) {
        common_enable_disable_unsolicited_events_data_system_status (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
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
wds_event_report_indication_cb (QmiClientWds                      *client,
                                QmiIndicationWdsEventReportOutput *output,
                                MMBroadbandModemQmi               *self)
{
    QmiWdsDataSystemNetworkType preferred_network;

    if (qmi_indication_wds_event_report_output_get_data_systems (output, &preferred_network, NULL, NULL)) {
        mm_obj_dbg (self, "data systems update, preferred network: %s",
                    qmi_wds_data_system_network_type_get_string (preferred_network));
        if (preferred_network == QMI_WDS_DATA_SYSTEM_NETWORK_TYPE_3GPP)
            mm_iface_modem_3gpp_reload_initial_eps_bearer (MM_IFACE_MODEM_3GPP (self));
        else
            mm_iface_modem_3gpp_update_initial_eps_bearer (MM_IFACE_MODEM_3GPP (self), NULL);
    }
}

static void
nas_event_report_indication_cb (QmiClientNas                      *client,
                                QmiIndicationNasEventReportOutput *output,
                                MMBroadbandModemQmi               *self)
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
            quality = MM_RSSI_TO_QUALITY (signal_strength);

            mm_obj_dbg (self, "signal strength indication (%s): %d dBm --> %u%%",
                        qmi_nas_radio_interface_get_string (signal_strength_radio_interface),
                        signal_strength,
                        quality);

            mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
        } else {
            mm_obj_dbg (self, "ignoring invalid signal strength (%s): %d dBm",
                        qmi_nas_radio_interface_get_string (signal_strength_radio_interface),
                        signal_strength);
        }
    }
}

static gdouble
get_db_from_sinr_level (MMBroadbandModemQmi *self,
                        QmiNasEvdoSinrLevel  level)
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
        mm_obj_warn (self, "invalid SINR level '%u'", level);
        return -G_MAXDOUBLE;
    }
}

static void
common_process_signal_info (MMBroadbandModemQmi               *self,
                            QmiMessageNasGetSignalInfoOutput  *response_output,
                            QmiIndicationNasSignalInfoOutput  *indication_output,
                            MMSignal                         **out_cdma,
                            MMSignal                         **out_evdo,
                            MMSignal                         **out_gsm,
                            MMSignal                         **out_umts,
                            MMSignal                         **out_lte,
                            MMSignal                         **out_nr5g)
{
    gint8               rssi;
    gint16              ecio;
    QmiNasEvdoSinrLevel sinr_level;
    gint32              io;
    gint8               rsrq;
    gint16              rsrp;
    gint16              snr;
    gint16              rscp_umts;
    gint16              rsrq_5g;

    *out_cdma = NULL;
    *out_evdo = NULL;
    *out_gsm = NULL;
    *out_umts = NULL;
    *out_lte = NULL;
    *out_nr5g = NULL;

    /* CDMA */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_cdma_signal_strength (response_output,
                                                                          &rssi,
                                                                          &ecio,
                                                                          NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_cdma_signal_strength (indication_output,
                                                                         &rssi,
                                                                         &ecio,
                                                                         NULL))) {
        *out_cdma = mm_signal_new ();
        mm_signal_set_rssi (*out_cdma, (gdouble)rssi);
        mm_signal_set_ecio (*out_cdma, ((gdouble)ecio) * (-0.5));
    }

    /* HDR... */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_hdr_signal_strength (response_output,
                                                                         &rssi,
                                                                         &ecio,
                                                                         &sinr_level,
                                                                         &io,
                                                                         NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_hdr_signal_strength (indication_output,
                                                                        &rssi,
                                                                        &ecio,
                                                                        &sinr_level,
                                                                        &io,
                                                                        NULL))) {
        *out_evdo = mm_signal_new ();
        mm_signal_set_rssi (*out_evdo, (gdouble)rssi);
        mm_signal_set_ecio (*out_evdo, ((gdouble)ecio) * (-0.5));
        mm_signal_set_sinr (*out_evdo, get_db_from_sinr_level (self, sinr_level));
        mm_signal_set_io (*out_evdo, (gdouble)io);
    }

    /* GSM */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_gsm_signal_strength (response_output,
                                                                         &rssi,
                                                                         NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_gsm_signal_strength (indication_output,
                                                                        &rssi,
                                                                        NULL))) {
        *out_gsm = mm_signal_new ();
        mm_signal_set_rssi (*out_gsm, (gdouble)rssi);
    }

    /* WCDMA... */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_wcdma_signal_strength (response_output,
                                                                           &rssi,
                                                                           &ecio,
                                                                           NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_wcdma_signal_strength (indication_output,
                                                                          &rssi,
                                                                          &ecio,
                                                                          NULL))) {
        *out_umts = mm_signal_new ();
        mm_signal_set_rssi (*out_umts, (gdouble)rssi);
        mm_signal_set_ecio (*out_umts, ((gdouble)ecio) * (-0.5));
    }

    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_wcdma_rscp (response_output,
                                                                &rscp_umts,
                                                                NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_wcdma_rscp (indication_output,
                                                               &rscp_umts,
                                                               NULL))) {
        if (G_UNLIKELY (!*out_umts))
            *out_umts = mm_signal_new ();
        mm_signal_set_rscp (*out_umts, (-1.0) * ((gdouble)rscp_umts));
    }

    /* LTE... */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_lte_signal_strength (response_output,
                                                                         &rssi,
                                                                         &rsrq,
                                                                         &rsrp,
                                                                         &snr,
                                                                         NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_lte_signal_strength (indication_output,
                                                                        &rssi,
                                                                        &rsrq,
                                                                        &rsrp,
                                                                        &snr,
                                                                        NULL))) {
        *out_lte = mm_signal_new ();
        mm_signal_set_rssi (*out_lte, (gdouble)rssi);
        mm_signal_set_rsrq (*out_lte, (gdouble)rsrq);
        mm_signal_set_rsrp (*out_lte, (gdouble)rsrp);
        mm_signal_set_snr (*out_lte, (0.1) * ((gdouble)snr));
    }

    /* 5G */
    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_5g_signal_strength (response_output,
                                                                        &rsrp,
                                                                        &snr,
                                                                        NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_5g_signal_strength (indication_output,
                                                                       &rsrp,
                                                                       &snr,
                                                                       NULL))) {
        *out_nr5g = mm_signal_new ();
        mm_signal_set_rsrp (*out_nr5g, (gdouble)rsrp);
        mm_signal_set_snr (*out_nr5g, (0.1) * ((gdouble)snr));
    }

    if ((response_output &&
         qmi_message_nas_get_signal_info_output_get_5g_signal_strength_extended (response_output,
                                                                                 &rsrq_5g,
                                                                                 NULL)) ||
        (indication_output &&
         qmi_indication_nas_signal_info_output_get_5g_signal_strength_extended (indication_output,
                                                                                &rsrq_5g,
                                                                                NULL))) {
        if (G_UNLIKELY (!*out_nr5g))
            *out_nr5g = mm_signal_new ();
        mm_signal_set_rsrq (*out_nr5g, (gdouble)rsrq_5g);
    }
}

static void
nas_signal_info_indication_cb (QmiClientNas                     *client,
                               QmiIndicationNasSignalInfoOutput *output,
                               MMBroadbandModemQmi              *self)
{
    gint8               cdma1x_rssi = 0;
    gint8               evdo_rssi = 0;
    gint8               gsm_rssi = 0;
    gint8               wcdma_rssi = 0;
    gint8               lte_rssi = 0;
    gint16              nr5g_rsrp = RSRP_MAX + 1;
    /* Multiplying SNR_MAX by 10 as QMI gives SNR level
     * as a scaled integer in units of 0.1 dB. */
    gint16              nr5g_snr = 10 * SNR_MAX + 10;
    gint16              nr5g_rsrq = RSRQ_MAX + 1;
    guint8              quality;
    g_autoptr(MMSignal) cdma = NULL;
    g_autoptr(MMSignal) evdo = NULL;
    g_autoptr(MMSignal) gsm = NULL;
    g_autoptr(MMSignal) umts = NULL;
    g_autoptr(MMSignal) lte = NULL;
    g_autoptr(MMSignal) nr5g = NULL;


    qmi_indication_nas_signal_info_output_get_cdma_signal_strength (output, &cdma1x_rssi, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_hdr_signal_strength (output, &evdo_rssi, NULL, NULL, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_gsm_signal_strength (output, &gsm_rssi, NULL);
    qmi_indication_nas_signal_info_output_get_wcdma_signal_strength (output, &wcdma_rssi, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_lte_signal_strength (output, &lte_rssi, NULL, NULL, NULL, NULL);
    qmi_indication_nas_signal_info_output_get_5g_signal_strength (output, &nr5g_rsrp, &nr5g_snr, NULL);
    qmi_indication_nas_signal_info_output_get_5g_signal_strength_extended (output, &nr5g_rsrq, NULL);

    /* Scale to integer values in units of 1 dB/dBm, if any */
    nr5g_snr = 0.1 * nr5g_snr;

    if (common_signal_info_get_quality (self,
                                        cdma1x_rssi,
                                        evdo_rssi,
                                        gsm_rssi,
                                        wcdma_rssi,
                                        lte_rssi,
                                        nr5g_rsrp,
                                        nr5g_snr,
                                        nr5g_rsrq,
                                        &quality)) {
        mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
    }

    common_process_signal_info (self, NULL, output, &cdma, &evdo, &gsm, &umts, &lte, &nr5g);
    mm_iface_modem_signal_update (MM_IFACE_MODEM_SIGNAL (self), cdma, evdo, gsm, umts, lte, nr5g);
}

static void
common_setup_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                         gboolean enable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GTask     *task;
    QmiClient *client_nas = NULL;
    QmiClient *client_wds = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->unsolicited_events_setup) {
        mm_obj_dbg (self, "unsolicited events already %s; skipping",
                    enable ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->unsolicited_events_setup = enable;

    client_nas = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_NAS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);
    client_wds = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_WDS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);

    /* Connect/Disconnect "Event Report" indications */
    if (client_nas) {
        if (enable) {
            g_assert (self->priv->nas_event_report_indication_id == 0);
            self->priv->nas_event_report_indication_id =
                g_signal_connect (client_nas,
                                  "event-report",
                                  G_CALLBACK (nas_event_report_indication_cb),
                                  self);
        } else if (self->priv->nas_event_report_indication_id != 0) {
            g_signal_handler_disconnect (client_nas, self->priv->nas_event_report_indication_id);
            self->priv->nas_event_report_indication_id = 0;
        }

        if (enable) {
            g_assert (self->priv->nas_signal_info_indication_id == 0);
            self->priv->nas_signal_info_indication_id =
                g_signal_connect (client_nas,
                                  "signal-info",
                                  G_CALLBACK (nas_signal_info_indication_cb),
                                  self);
        } else if (self->priv->nas_signal_info_indication_id != 0) {
            g_signal_handler_disconnect (client_nas, self->priv->nas_signal_info_indication_id);
            self->priv->nas_signal_info_indication_id = 0;
        }
    }

    if (client_wds) {
        if (enable) {
            g_assert (self->priv->wds_event_report_indication_id == 0);
            self->priv->wds_event_report_indication_id =
                g_signal_connect (client_wds,
                                  "event-report",
                                  G_CALLBACK (wds_event_report_indication_cb),
                                  self);
        } else if (self->priv->wds_event_report_indication_id != 0) {
            g_signal_handler_disconnect (client_wds, self->priv->wds_event_report_indication_id);
            self->priv->wds_event_report_indication_id = 0;
        }
    }

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
/* Check format (3gppProfileManager interface) */

static gboolean
modem_3gpp_profile_manager_check_format_finish (MMIfaceModem3gppProfileManager  *self,
                                                GAsyncResult                    *res,
                                                gboolean                        *new_id,
                                                gint                            *min_profile_id,
                                                gint                            *max_profile_id,
                                                GEqualFunc                      *apn_cmp,
                                                MM3gppProfileCmpFlags           *profile_cmp_flags,
                                                GError                         **error)
{
    if (!g_task_propagate_boolean (G_TASK (res), error)) {
        g_assert_not_reached ();
        return FALSE;
    }
    /* Generic WDS Create Profile method does NOT allow specifying a specific
     * profile id */
    if (new_id)
        *new_id = FALSE;
    if (min_profile_id)
        *min_profile_id = 1;
    if (max_profile_id)
        *max_profile_id = G_MAXINT - 1;
    /* use default string comparison method */
    if (apn_cmp)
        *apn_cmp = NULL;
    if (profile_cmp_flags)
        *profile_cmp_flags = (MM_3GPP_PROFILE_CMP_FLAGS_NO_ACCESS_TYPE_PREFERENCE |
                              MM_3GPP_PROFILE_CMP_FLAGS_NO_ROAMING_ALLOWANCE |
                              MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_SOURCE);
    return TRUE;
}

static void
modem_3gpp_profile_manager_check_format (MMIfaceModem3gppProfileManager *self,
                                         MMBearerIpFamily                ip_type,
                                         GAsyncReadyCallback             callback,
                                         gpointer                        user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Get profile (3GPP profile management interface) */

static MM3gppProfile *
modem_3gpp_profile_manager_get_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                               GAsyncResult                    *res,
                                               GError                         **error)
{
    return MM_3GPP_PROFILE (g_task_propagate_pointer (G_TASK (res), error));
}

static MM3gppProfile *
wds_profile_settings_to_3gpp_profile (MMBroadbandModemQmi                    *self,
                                      guint                                   profile_index,
                                      QmiMessageWdsGetProfileSettingsOutput  *output,
                                      GError                                **error)
{
    MM3gppProfile            *profile = NULL;
    const gchar              *str;
    QmiWdsPdpType             pdp_type;
    QmiWdsAuthentication      auth;
    QmiWdsApnTypeMask         apn_type;

    profile = mm_3gpp_profile_new ();

    /* On 3GPP modems, the modem seems to force profile-index = pdp-context-number,
     * and so, we're just going to rely on the profile-index ourselves.*/
    mm_3gpp_profile_set_profile_id (profile, (gint) profile_index);

    if (qmi_message_wds_get_profile_settings_output_get_apn_name (output, &str, NULL))
        mm_3gpp_profile_set_apn (profile, str);

    if (qmi_message_wds_get_profile_settings_output_get_profile_name (output, &str, NULL))
        mm_3gpp_profile_set_profile_name (profile, str);

    if (qmi_message_wds_get_profile_settings_output_get_pdp_type (output, &pdp_type, NULL))
        mm_3gpp_profile_set_ip_type (profile, mm_bearer_ip_family_from_qmi_pdp_type (pdp_type));

    if (qmi_message_wds_get_profile_settings_output_get_authentication (output, &auth, NULL))
        mm_3gpp_profile_set_allowed_auth (profile, mm_bearer_allowed_auth_from_qmi_authentication (auth));

    /* ignore empty user/pass strings */
    if (qmi_message_wds_get_profile_settings_output_get_username (output, &str, NULL) && str[0])
        mm_3gpp_profile_set_user (profile, str);
    if (qmi_message_wds_get_profile_settings_output_get_password (output, &str, NULL) && str[0])
        mm_3gpp_profile_set_password (profile, str);

    /* If loading APN type TLV fails, flag it as unsupported so that we don't try to use it any
     * more. */
    if (qmi_message_wds_get_profile_settings_output_get_apn_type_mask (output, &apn_type, NULL))
        mm_3gpp_profile_set_apn_type (profile, mm_bearer_apn_type_from_qmi_apn_type (apn_type));
    else if (!self->priv->apn_type_not_supported) {
        mm_obj_dbg (self, "APN type flagged as not supported: not given in profile settings");
        self->priv->apn_type_not_supported = TRUE;
    }

    return profile;
}

static void
get_profile_settings_ready (QmiClientWds *client,
                            GAsyncResult *res,
                            GTask        *task)
{
    MMBroadbandModemQmi *self;
    GError              *error = NULL;
    gint                 profile_id;
    gboolean             profile_disabled = FALSE;
    MM3gppProfile       *profile;
    g_autoptr(QmiMessageWdsGetProfileSettingsOutput) output = NULL;

    self = g_task_get_source_object (task);
    profile_id = GPOINTER_TO_INT (g_task_get_task_data (task));

    output = qmi_client_wds_get_profile_settings_finish (client, res, &error);
    if (!output || !qmi_message_wds_get_profile_settings_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't load settings from profile index %u: ", profile_id);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* just ignore the profile if it's disabled */
    qmi_message_wds_get_profile_settings_output_get_apn_disabled_flag (output, &profile_disabled, NULL);
    if (profile_disabled) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Profile '%d' is internally disabled", profile_id);
        g_object_unref (task);
        return;
    }

    profile = wds_profile_settings_to_3gpp_profile (self, profile_id, output, &error);
    if (!profile)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, profile, g_object_unref);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_get_profile (MMIfaceModem3gppProfileManager  *self,
                                        gint                             profile_id,
                                        GAsyncReadyCallback              callback,
                                        gpointer                         user_data)
{
    GTask     *task;
    QmiClient *client = NULL;
    g_autoptr(QmiMessageWdsGetProfileSettingsInput) input = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GINT_TO_POINTER (profile_id), NULL);

    input = qmi_message_wds_get_profile_settings_input_new ();
    qmi_message_wds_get_profile_settings_input_set_profile_id (
        input,
        QMI_WDS_PROFILE_TYPE_3GPP,
        profile_id,
        NULL);
    qmi_client_wds_get_profile_settings (QMI_CLIENT_WDS (client),
                                         input,
                                         3,
                                         NULL,
                                         (GAsyncReadyCallback)get_profile_settings_ready,
                                         task);
}

/*****************************************************************************/
/* List profiles (3GPP profile management interface) */

typedef struct {
    GList  *profiles;
    GArray *qmi_profiles;
    guint   i;
} ListProfilesContext;

static void
list_profiles_context_free (ListProfilesContext *ctx)
{
    g_clear_pointer (&ctx->qmi_profiles, (GDestroyNotify)g_array_unref);
    mm_3gpp_profile_list_free (ctx->profiles);
    g_slice_free (ListProfilesContext, ctx);
}

static gboolean
modem_3gpp_profile_manager_list_profiles_finish (MMIfaceModem3gppProfileManager  *self,
                                                 GAsyncResult                    *res,
                                                 GList                          **out_profiles,
                                                 GError                         **error)
{
    ListProfilesContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (out_profiles)
        *out_profiles = g_steal_pointer (&ctx->profiles);
    return TRUE;
}

static void get_next_profile_settings (GTask *task);

static void
get_next_profile_settings_ready (MMIfaceModem3gppProfileManager *self,
                                 GAsyncResult                   *res,
                                 GTask                          *task)
{
    ListProfilesContext *ctx;
    MM3gppProfile       *profile;
    GError              *error = NULL;

    ctx = g_task_get_task_data (task);

    profile = modem_3gpp_profile_manager_get_profile_finish (self, res, &error);
    if (!profile) {
        g_prefix_error (&error, "Couldn't load settings from profile index %u: ", ctx->i);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->profiles = g_list_append (ctx->profiles, profile);

    /* Keep on */
    ctx->i++;
    get_next_profile_settings (task);
}

static void
get_next_profile_settings (GTask *task)
{
    MMBroadbandModemQmi                                 *self;
    ListProfilesContext                                 *ctx;
    QmiMessageWdsGetProfileListOutputProfileListProfile *current;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (ctx->i >= ctx->qmi_profiles->len) {
        /* All done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    current = &g_array_index (ctx->qmi_profiles, QmiMessageWdsGetProfileListOutputProfileListProfile, ctx->i);
    modem_3gpp_profile_manager_get_profile (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
                                            current->profile_index,
                                            (GAsyncReadyCallback)get_next_profile_settings_ready,
                                            task);
}

static void
get_profile_list_ready (QmiClientWds *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    ListProfilesContext                          *ctx;
    GError                                       *error = NULL;
    GArray                                       *qmi_profiles = NULL;
    g_autoptr(QmiMessageWdsGetProfileListOutput)  output = NULL;

    ctx = g_slice_new0 (ListProfilesContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) list_profiles_context_free);

    output = qmi_client_wds_get_profile_list_finish (client, res, &error);
    if (!output || !qmi_message_wds_get_profile_list_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_wds_get_profile_list_output_get_profile_list (output, &qmi_profiles, NULL);

    /* empty list? */
    if (!qmi_profiles || !qmi_profiles->len) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->qmi_profiles = g_array_ref (qmi_profiles);
    get_next_profile_settings (task);
}

static void
modem_3gpp_profile_manager_list_profiles (MMIfaceModem3gppProfileManager  *self,
                                          GAsyncReadyCallback              callback,
                                          gpointer                         user_data)
{
    GTask      *task;
    QmiClient  *client = NULL;
    g_autoptr(QmiMessageWdsGetProfileListInput) input = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_wds_get_profile_list_input_new ();
    qmi_message_wds_get_profile_list_input_set_profile_type (input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);

    qmi_client_wds_get_profile_list (QMI_CLIENT_WDS (client),
                                     input,
                                     10,
                                     NULL,
                                     (GAsyncReadyCallback)get_profile_list_ready,
                                     task);
}

/*****************************************************************************/
/* Store profile (3GPP profile management interface) */

#define IGNORED_PROFILE_CHANGED_INDICATION_TIMEOUT_MS 100

static void profile_changed_indication_ignore (MMBroadbandModemQmi *self,
                                               gboolean             ignore);

typedef struct {
    QmiClientWds         *client;
    gint                  profile_id;
    gchar                *profile_name;
    gchar                *apn;
    gchar                *user;
    gchar                *password;
    QmiWdsApnTypeMask     qmi_apn_type;
    QmiWdsAuthentication  qmi_auth;
    QmiWdsPdpType         qmi_pdp_type;
} StoreProfileContext;

static void
store_profile_context_free (StoreProfileContext *ctx)
{
    g_free (ctx->profile_name);
    g_free (ctx->apn);
    g_free (ctx->user);
    g_free (ctx->password);
    g_clear_object (&ctx->client);
    g_slice_free (StoreProfileContext, ctx);
}

static gboolean
modem_3gpp_profile_manager_store_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                 GAsyncResult                    *res,
                                                 gint                            *out_profile_id,
                                                 MMBearerApnType                 *out_apn_type,
                                                 GError                         **error)
{
    StoreProfileContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (out_profile_id)
        *out_profile_id = ctx->profile_id;
    if (out_apn_type)
        *out_apn_type = MM_BEARER_APN_TYPE_NONE;
    return TRUE;
}

static gboolean
store_profile_complete_wait (GTask *task)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    /* On a successful operation, we were still ignoring the indications */
    profile_changed_indication_ignore (self, FALSE);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
store_profile_complete (GTask  *task,
                        GError *error)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    if (error) {
        /* On operation failure, we don't expect further profile update
         * indications, so we can safely stop ignoring them and return
         * the error without delay. */
        profile_changed_indication_ignore (self, FALSE);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_timeout_add (IGNORED_PROFILE_CHANGED_INDICATION_TIMEOUT_MS,
                   (GSourceFunc) store_profile_complete_wait,
                   task);
}

static void store_profile_run (GTask *task);

static void
modify_profile_ready (QmiClientWds *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemQmi *self;
    GError              *error = NULL;
    g_autoptr(QmiMessageWdsModifyProfileOutput) output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_modify_profile_finish (client, res, &error);
    if (!output) {
        store_profile_complete (task, error);
        return;
    }

    if (!qmi_message_wds_modify_profile_output_get_result (output, &error)) {
        QmiWdsDsProfileError ds_profile_error;

        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_PROFILE_TYPE) &&
            !self->priv->apn_type_not_supported) {
            /* we'll retry the operation without APN type, which is a setting available only
             * in newer devices. */
            mm_obj_dbg (self, "APN type flagged as not supported: failed to modify profile");
            self->priv->apn_type_not_supported = TRUE;
            g_clear_error (&error);
            store_profile_run (task);
            return;
        }

        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
            qmi_message_wds_modify_profile_output_get_extended_error_code (output, &ds_profile_error, NULL)) {
            g_prefix_error (&error, "DS profile error: %s: ", qmi_wds_ds_profile_error_get_string (ds_profile_error));
        }
        g_prefix_error (&error, "Couldn't modify profile: ");
        store_profile_complete (task, error);
        return;
    }

    /* success */
    store_profile_complete (task, NULL);
}

static void
create_profile_ready (QmiClientWds *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemQmi *self;
    StoreProfileContext *ctx;
    GError              *error = NULL;
    guint8               profile_index;
    g_autoptr(QmiMessageWdsCreateProfileOutput) output = NULL;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    output = qmi_client_wds_create_profile_finish (client, res, &error);
    if (!output) {
        store_profile_complete (task, error);
        return;
    }

    if (!qmi_message_wds_create_profile_output_get_result (output, &error)) {
        QmiWdsDsProfileError ds_profile_error;

        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_PROFILE_TYPE) &&
            !self->priv->apn_type_not_supported) {
            /* we'll retry the operation without APN type, which is a setting available only
             * in newer devices. */
            mm_obj_dbg (self, "APN type flagged as not supported: failed to create profile");
            self->priv->apn_type_not_supported = TRUE;
            g_clear_error (&error);
            store_profile_run (task);
            return;
        }

        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_EXTENDED_INTERNAL) &&
            qmi_message_wds_create_profile_output_get_extended_error_code (output, &ds_profile_error, NULL)) {
            g_prefix_error (&error, "DS profile error: %s: ", qmi_wds_ds_profile_error_get_string (ds_profile_error));
        }
        g_prefix_error (&error, "Couldn't create profile: ");
        store_profile_complete (task, error);
        return;
    }

    if (!qmi_message_wds_create_profile_output_get_profile_identifier (output, NULL, &profile_index, &error)) {
        store_profile_complete (task, error);
        return;
    }

    /* success */
    ctx->profile_id = profile_index;
    store_profile_complete (task, NULL);
}

static void
store_profile_run (GTask *task)
{
    MMBroadbandModemQmi *self;
    StoreProfileContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (ctx->profile_id == MM_3GPP_PROFILE_ID_UNKNOWN) {
        g_autoptr(QmiMessageWdsCreateProfileInput) input = NULL;

        /* when creating, we cannot select which profile id to use */
        input = qmi_message_wds_create_profile_input_new ();
        qmi_message_wds_create_profile_input_set_profile_type (input, QMI_WDS_PROFILE_TYPE_3GPP, NULL);
        qmi_message_wds_create_profile_input_set_profile_name (input, ctx->profile_name, NULL);
        qmi_message_wds_create_profile_input_set_pdp_type (input, ctx->qmi_pdp_type, NULL);
        qmi_message_wds_create_profile_input_set_apn_name (input, ctx->apn, NULL);
        qmi_message_wds_create_profile_input_set_authentication (input, ctx->qmi_auth, NULL);
        qmi_message_wds_create_profile_input_set_username (input, ctx->user, NULL);
        qmi_message_wds_create_profile_input_set_password (input, ctx->password, NULL);
        if (!self->priv->apn_type_not_supported)
            qmi_message_wds_create_profile_input_set_apn_type_mask (input, ctx->qmi_apn_type, NULL);

        qmi_client_wds_create_profile (ctx->client,
                                       input,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)create_profile_ready,
                                       task);
    } else {
        g_autoptr(QmiMessageWdsModifyProfileInput) input = NULL;

        input = qmi_message_wds_modify_profile_input_new ();
        qmi_message_wds_modify_profile_input_set_profile_identifier (input, QMI_WDS_PROFILE_TYPE_3GPP, ctx->profile_id, NULL);
        qmi_message_wds_modify_profile_input_set_profile_name (input, ctx->profile_name, NULL);
        qmi_message_wds_modify_profile_input_set_pdp_type (input, ctx->qmi_pdp_type, NULL);
        qmi_message_wds_modify_profile_input_set_apn_name (input, ctx->apn, NULL);
        qmi_message_wds_modify_profile_input_set_authentication (input, ctx->qmi_auth, NULL);
        qmi_message_wds_modify_profile_input_set_username (input, ctx->user, NULL);
        qmi_message_wds_modify_profile_input_set_password (input, ctx->password, NULL);
        if (!self->priv->apn_type_not_supported)
            qmi_message_wds_modify_profile_input_set_apn_type_mask (input, ctx->qmi_apn_type, NULL);

        qmi_client_wds_modify_profile (ctx->client,
                                       input,
                                       10,
                                       NULL,
                                       (GAsyncReadyCallback)modify_profile_ready,
                                       task);
    }
}

static void
modem_3gpp_profile_manager_store_profile (MMIfaceModem3gppProfileManager *self,
                                          MM3gppProfile                  *profile,
                                          const gchar                    *index_field,
                                          GAsyncReadyCallback             callback,
                                          gpointer                        user_data)
{
    GTask                *task;
    StoreProfileContext  *ctx;
    QmiClient            *client = NULL;
    MMBearerIpFamily      ip_type;
    MMBearerApnType       apn_type;
    MMBearerAllowedAuth   allowed_auth;

    g_assert (g_strcmp0 (index_field, "profile-id") == 0);

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (StoreProfileContext);
    ctx->client = QMI_CLIENT_WDS (g_object_ref (client));
    g_task_set_task_data (task, ctx, (GDestroyNotify)store_profile_context_free);

    /* Note: may be UNKNOWN */
    ctx->profile_id = mm_3gpp_profile_get_profile_id (profile);

    ctx->profile_name = g_strdup (mm_3gpp_profile_get_profile_name (profile));

    ctx->apn = g_strdup (mm_3gpp_profile_get_apn (profile));

    apn_type = mm_3gpp_profile_get_apn_type (profile);
    ctx->qmi_apn_type = mm_bearer_apn_type_to_qmi_apn_type (apn_type, self);

    ctx->user = g_strdup (mm_3gpp_profile_get_user (profile));
    ctx->password = g_strdup (mm_3gpp_profile_get_password (profile));
    allowed_auth = mm_3gpp_profile_get_allowed_auth (profile);
    if ((allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN) || ctx->user || ctx->password) {
        GError *error = NULL;

        ctx->qmi_auth = mm_bearer_allowed_auth_to_qmi_authentication (allowed_auth, self, &error);
        if (error) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    }

    ip_type = mm_3gpp_profile_get_ip_type (profile);
    if (!mm_bearer_ip_family_to_qmi_pdp_type (ip_type, &ctx->qmi_pdp_type)) {
        g_autofree gchar *ip_type_str = NULL;

        ip_type_str = mm_bearer_ip_family_build_string_from_mask (ip_type);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Invalid IP type specified: %s", ip_type_str);
        g_object_unref (task);
        return;
    }

    profile_changed_indication_ignore (MM_BROADBAND_MODEM_QMI (self), TRUE);
    store_profile_run (task);
}

/*****************************************************************************/
/* Delete profile (3GPP profile management interface) */

static gboolean
modem_3gpp_profile_manager_delete_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                  GAsyncResult                    *res,
                                                  GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
delete_profile_complete_wait (GTask *task)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    /* On a successful operation, we were still ignoring the indications */
    profile_changed_indication_ignore (self, FALSE);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
delete_profile_ready (QmiClientWds *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemQmi *self;
    GError              *error = NULL;
    g_autoptr(QmiMessageWdsDeleteProfileOutput) output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_delete_profile_finish (client, res, &error);
    if (!output || !qmi_message_wds_delete_profile_output_get_result (output, &error)) {
        profile_changed_indication_ignore (self, FALSE);
        g_prefix_error (&error, "Couldn't delete profile: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_timeout_add (IGNORED_PROFILE_CHANGED_INDICATION_TIMEOUT_MS,
                   (GSourceFunc) delete_profile_complete_wait,
                   task);
}

static void
modem_3gpp_profile_manager_delete_profile (MMIfaceModem3gppProfileManager *self,
                                           MM3gppProfile                  *profile,
                                           const gchar                    *index_field,
                                           GAsyncReadyCallback             callback,
                                           gpointer                        user_data)
{
    GTask     *task;
    QmiClient *client = NULL;
    gint       profile_id;
    g_autoptr(QmiMessageWdsDeleteProfileInput) input = NULL;

    g_assert (g_strcmp0 (index_field, "profile-id") == 0);

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    profile_id = mm_3gpp_profile_get_profile_id (profile);
    g_assert (profile_id != MM_3GPP_PROFILE_ID_UNKNOWN);

    mm_obj_dbg (self, "deleting profile '%d'", profile_id);

    input = qmi_message_wds_delete_profile_input_new ();
    qmi_message_wds_delete_profile_input_set_profile_identifier (input, QMI_WDS_PROFILE_TYPE_3GPP, profile_id, NULL);

    profile_changed_indication_ignore (MM_BROADBAND_MODEM_QMI (self), TRUE);
    qmi_client_wds_delete_profile (QMI_CLIENT_WDS (client),
                                   input,
                                   10,
                                   NULL,
                                   (GAsyncReadyCallback)delete_profile_ready,
                                   task);
}

/*****************************************************************************/
/* PDC Refresh events (3gppProfileManager interface) */

static void
pdc_refresh_received (QmiClientPdc                  *client,
                      QmiIndicationPdcRefreshOutput *output,
                      MMBroadbandModemQmi           *self)
{
    mm_obj_dbg (self, "profile refresh indication was received");
    mm_iface_modem_3gpp_profile_manager_updated (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self));
}

/*****************************************************************************/
/* Profile Changed events (3gppProfileManager interface) */

static void
profile_changed_indication_received (QmiClientWds                         *client,
                                     QmiIndicationWdsProfileChangedOutput *output,
                                     MMBroadbandModemQmi                  *self)
{
    if (self->priv->profile_changed_indication_ignored > 0) {
        mm_obj_dbg (self, "profile changed indication ignored");
        return;
    }

    mm_obj_dbg (self, "profile changed indication was received");
    mm_iface_modem_3gpp_profile_manager_updated (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self));
}

static void
profile_changed_indication_ignore (MMBroadbandModemQmi *self,
                                   gboolean             ignore)
{
    /* Note: multiple concurrent profile create/update/deletes may be happening,
     * so ensure the indication ignore logic applies as long as at least one
     * operation is ongoing. */
    if (ignore) {
        g_assert_cmpint (self->priv->profile_changed_indication_ignored, >=, 0);
        self->priv->profile_changed_indication_ignored++;
        mm_obj_dbg (self, "ignoring profile update indications during our own operations (%d ongoing)",
                    self->priv->profile_changed_indication_ignored);
    } else {
        g_assert_cmpint (self->priv->profile_changed_indication_ignored, >, 0);
        self->priv->profile_changed_indication_ignored--;
        if (self->priv->profile_changed_indication_ignored > 0)
            mm_obj_dbg (self, "still ignoring profile update indications during our own operations (%d ongoing)",
                        self->priv->profile_changed_indication_ignored);
        else
            mm_obj_dbg (self, "no longer ignoring profile update indications during our own operations");
    }
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3gppProfileManager interface) */

typedef enum {
    REGISTER_PROFILE_REFRESH_STEP_FIRST,
    REGISTER_PROFILE_REFRESH_STEP_PROFILE_REFRESH,
    REGISTER_PROFILE_REFRESH_STEP_PROFILE_CHANGE,
    REGISTER_PROFILE_REFRESH_STEP_CONFIGURE_PROFILE_EVENT,
    REGISTER_PROFILE_REFRESH_STEP_LAST,
} RegisterProfileRefreshStep;

typedef struct {
    QmiClientPdc               *client_pdc;
    QmiClientWds               *client_wds;
    RegisterProfileRefreshStep  step;
    gboolean                    enable;
} RegisterProfileRefreshContext;

static void
register_profile_refresh_context_free (RegisterProfileRefreshContext *ctx)
{
    g_clear_object (&ctx->client_pdc);
    g_clear_object (&ctx->client_wds);
    g_free (ctx);
}

static gboolean
modem_3gpp_profile_manager_enable_disable_unsolicited_events_finish (MMIfaceModem3gppProfileManager  *self,
                                                                     GAsyncResult                    *res,
                                                                     GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void register_profile_refresh_context_step (GTask *task);

static void
register_pdc_refresh_ready (QmiClientPdc *client,
                            GAsyncResult *res,
                            GTask        *task)
{
    g_autoptr(QmiMessagePdcRegisterOutput)  output = NULL;
    RegisterProfileRefreshContext          *ctx;
    GError                                 *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pdc_register_finish (client, res, &error);
    if (!output || !qmi_message_pdc_register_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    register_profile_refresh_context_step (task);
}

static void
register_wds_profile_change_ready (QmiClientWds *client,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    g_autoptr(QmiMessageWdsIndicationRegisterOutput)  output = NULL;
    RegisterProfileRefreshContext                    *ctx;
    GError                                           *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_wds_indication_register_finish (client, res, &error);
    if (!output || !qmi_message_wds_indication_register_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    register_profile_refresh_context_step (task);
}

static void
register_wds_configure_profile_event_ready (QmiClientWds *client,
                                            GAsyncResult *res,
                                            GTask        *task)
{
    g_autoptr(QmiMessageWdsConfigureProfileEventListOutput)  output = NULL;
    RegisterProfileRefreshContext                           *ctx;
    GError                                                  *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_wds_configure_profile_event_list_finish (client, res, &error);
    if (!output || !qmi_message_wds_configure_profile_event_list_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    register_profile_refresh_context_step (task);
}

static void
register_profile_refresh_context_step (GTask *task)
{
    MMBroadbandModemQmi           *self;
    RegisterProfileRefreshContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case REGISTER_PROFILE_REFRESH_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case REGISTER_PROFILE_REFRESH_STEP_PROFILE_REFRESH:
        if (ctx->client_pdc) {
            g_autoptr(QmiMessagePdcRegisterInput) input = NULL;

            input = qmi_message_pdc_register_input_new ();
            qmi_message_pdc_register_input_set_enable_reporting (input, ctx->enable, NULL);
            qmi_client_pdc_register (ctx->client_pdc,
                                     input,
                                     10,
                                     NULL,
                                     (GAsyncReadyCallback) register_pdc_refresh_ready,
                                     task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case REGISTER_PROFILE_REFRESH_STEP_PROFILE_CHANGE:
        if (ctx->client_wds) {
            g_autoptr(QmiMessageWdsIndicationRegisterInput) input = NULL;

            input = qmi_message_wds_indication_register_input_new ();
            qmi_message_wds_indication_register_input_set_report_profile_changes (input, ctx->enable, NULL);
            qmi_client_wds_indication_register (ctx->client_wds,
                                                input,
                                                10,
                                                NULL,
                                                (GAsyncReadyCallback) register_wds_profile_change_ready,
                                                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case REGISTER_PROFILE_REFRESH_STEP_CONFIGURE_PROFILE_EVENT:
        if (ctx->client_wds) {
            g_autoptr(QmiMessageWdsConfigureProfileEventListInput)     input = NULL;
            g_autoptr(GArray)                                          array = NULL;
            QmiMessageWdsConfigureProfileEventListInputRegisterElement element = {0};

            element.profile_type = QMI_WDS_PROFILE_TYPE_ALL;
            element.profile_index = 0xFF;
            array = g_array_new (FALSE, FALSE, sizeof (QmiMessageWdsConfigureProfileEventListInputRegisterElement));
            g_array_append_val (array, element);

            input = qmi_message_wds_configure_profile_event_list_input_new ();
            qmi_message_wds_configure_profile_event_list_input_set_register (input, array, NULL);
            qmi_client_wds_configure_profile_event_list (ctx->client_wds,
                                                         input,
                                                         10,
                                                         NULL,
                                                         (GAsyncReadyCallback) register_wds_configure_profile_event_ready,
                                                         task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case REGISTER_PROFILE_REFRESH_STEP_LAST:
        self->priv->profile_manager_unsolicited_events_enabled = ctx->enable;
        mm_obj_dbg (self, "%s for refresh events", ctx->enable ? "registered" : "unregistered");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
common_enable_disable_unsolicited_events_3gpp_profile_manager (MMBroadbandModemQmi *self,
                                                               gboolean             enable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data)
{
    RegisterProfileRefreshContext *ctx;
    GTask                         *task;
    QmiClient                     *client_pdc = NULL;
    QmiClient                     *client_wds = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->profile_manager_unsolicited_events_enabled) {
        mm_obj_dbg (self, "profile manager unsolicited events already %s; skipping",
                    enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    client_pdc = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_PDC,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);
    client_wds = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_WDS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            NULL);

    /* Fail if none of the clients can be allocated */
    if (!client_pdc && !client_wds) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No support for profile refresh events");
        g_object_unref (task);
        return;
    }

    ctx = g_new0 (RegisterProfileRefreshContext, 1);
    ctx->step = REGISTER_PROFILE_REFRESH_STEP_FIRST;
    ctx->enable = enable;
    ctx->client_pdc = client_pdc ? QMI_CLIENT_PDC (g_object_ref (client_pdc)) : NULL;
    ctx->client_wds = client_wds ? QMI_CLIENT_WDS (g_object_ref (client_wds)) : NULL;

    g_task_set_task_data (task, ctx, (GDestroyNotify)register_profile_refresh_context_free);

    register_profile_refresh_context_step (task);
}

static void
modem_3gpp_profile_manager_disable_unsolicited_events (MMIfaceModem3gppProfileManager *self,
                                                       GAsyncReadyCallback             callback,
                                                       gpointer                        user_data)
{
    common_enable_disable_unsolicited_events_3gpp_profile_manager (MM_BROADBAND_MODEM_QMI (self),
                                                                   FALSE,
                                                                   callback,
                                                                   user_data);
}

static void
modem_3gpp_profile_manager_enable_unsolicited_events (MMIfaceModem3gppProfileManager *self,
                                                      GAsyncReadyCallback             callback,
                                                      gpointer                        user_data)
{
    common_enable_disable_unsolicited_events_3gpp_profile_manager (MM_BROADBAND_MODEM_QMI (self),
                                                                   TRUE,
                                                                   callback,
                                                                   user_data);
}

/*****************************************************************************/
/* Setup/cleanup unsolicited events (3gppProfileManager interface) */

static gboolean
modem_3gpp_profile_manager_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gppProfileManager  *self,
                                                                    GAsyncResult                    *res,
                                                                    GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_cleanup_unsolicited_events_3gpp_profile_manager (MMBroadbandModemQmi *self,
                                                              gboolean             enable,
                                                              GAsyncReadyCallback  callback,
                                                              gpointer             user_data)

{
    GTask     *task;
    QmiClient *client_pdc = NULL;
    QmiClient *client_wds = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_PDC, &client_pdc,
                                      callback, user_data))
        return;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client_wds,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->profile_manager_unsolicited_events_setup) {
        mm_obj_dbg (self, "profile manager unsolicited events already %s; skipping",
                    enable ? "set up" : "cleaned up");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    self->priv->profile_manager_unsolicited_events_setup = enable;

    if (enable) {
        g_assert (self->priv->refresh_indication_id == 0);
        self->priv->refresh_indication_id =
            g_signal_connect (client_pdc,
                              "refresh",
                              G_CALLBACK (pdc_refresh_received),
                              self);

        g_assert (self->priv->profile_changed_indication_id == 0);
        self->priv->profile_changed_indication_id =
            g_signal_connect (client_wds,
                              "profile-changed",
                              G_CALLBACK (profile_changed_indication_received),
                              self);
    } else {
        g_assert (self->priv->refresh_indication_id != 0);
        g_signal_handler_disconnect (client_pdc, self->priv->refresh_indication_id);
        self->priv->refresh_indication_id = 0;

        g_assert (self->priv->profile_changed_indication_id != 0);
        g_signal_handler_disconnect (client_wds, self->priv->profile_changed_indication_id);
        self->priv->profile_changed_indication_id = 0;
    }

    mm_obj_dbg (self, "%s profile events handler", enable ? "set up" : "cleaned up");

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_cleanup_unsolicited_events (MMIfaceModem3gppProfileManager *self,
                                                       GAsyncReadyCallback             callback,
                                                       gpointer                        user_data)
{
    common_setup_cleanup_unsolicited_events_3gpp_profile_manager (MM_BROADBAND_MODEM_QMI (self),
                                                                  FALSE,
                                                                  callback,
                                                                  user_data);
}

static void
modem_3gpp_profile_manager_setup_unsolicited_events (MMIfaceModem3gppProfileManager *self,
                                                     GAsyncReadyCallback             callback,
                                                     gpointer                        user_data)
{
    common_setup_cleanup_unsolicited_events_3gpp_profile_manager (MM_BROADBAND_MODEM_QMI (self),
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

    self->priv->messaging_fallback_at_only = iface_modem_messaging_parent->check_support_finish (_self, res, NULL);

    g_task_return_boolean (task, self->priv->messaging_fallback_at_only);
    g_object_unref (task);
}

static void
messaging_check_support (MMIfaceModemMessaging *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If we have support for the WMS client, messaging is supported */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_WMS,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        /* Try to fallback to AT support */
        iface_modem_messaging_parent->check_support (
            self,
            (GAsyncReadyCallback)parent_messaging_check_support_ready,
            task);
        return;
    }

    mm_obj_dbg (self, "messaging capabilities supported");
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        iface_modem_messaging_parent->set_default_storage (_self, storage, callback, user_data);
        return;
    }

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    mm_obj_dbg (self, "setting default messaging routes...");
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
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
                       gboolean transfer_route,
                       GArray *data)
{
    MMSmsPart *part = NULL;
    GError *error = NULL;

    switch (format) {
    case QMI_WMS_MESSAGE_FORMAT_CDMA:
        part = mm_sms_part_cdma_new_from_binary_pdu (index,
                                                     (guint8 *)data->data,
                                                     data->len,
                                                     self,
                                                     &error);

        break;
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT:
    case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_BROADCAST:
        part = mm_sms_part_3gpp_new_from_binary_pdu (index,
                                                     (guint8 *)data->data,
                                                     data->len,
                                                     self,
                                                     transfer_route,
                                                     &error);
        break;
    case QMI_WMS_MESSAGE_FORMAT_MWI:
        mm_obj_dbg (self, "don't know how to process 'message waiting indicator' messages");
        break;
    default:
        mm_obj_dbg (self, "unhandled message format '%u'", format);
        break;
    }

    if (part) {
        mm_obj_dbg (self, "correctly parsed PDU (%d)", index);
        mm_iface_modem_messaging_take_part (self,
                                            part,
                                            mm_sms_state_from_qmi_message_tag (tag),
                                            mm_sms_storage_from_qmi_storage_type (storage));
    } else if (error) {
        /* Don't treat the error as critical */
        mm_obj_dbg (self, "error parsing PDU (%d): %s", index, error->message);
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
        mm_obj_dbg (self, "QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_raw_read_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't read raw message: %s", error->message);
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
                               FALSE,
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
    MMBroadbandModemQmi *self;
    LoadInitialSmsPartsContext *ctx;
    QmiMessageWmsListMessagesOutput *output = NULL;
    GError *error = NULL;
    GArray *message_array;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_wms_list_messages_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_wms_list_messages_output_get_result (output, &error)) {
        /* Ignore error, keep on */
        mm_obj_dbg (self, "couldn't read SMS messages: %s", error->message);
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
        /* Fall through */

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_FIRST:
        /* If modem doesn't have 3GPP caps, skip 3GPP SMS */
        if (!mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST;
            load_initial_sms_parts_step (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_ALL:
        mm_obj_dbg (self, "loading all 3GPP messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_READ:
        mm_obj_dbg (self, "loading 3GPP MT-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_READ;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MT_NOT_READ:
        mm_obj_dbg (self, "loading 3GPP MT-not-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_SENT:
        mm_obj_dbg (self, "loading 3GPP MO-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_SENT;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LIST_MO_NOT_SENT:
        mm_obj_dbg (self, "loading 3GPP MO-not-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_NOT_SENT;
        mode = QMI_WMS_MESSAGE_MODE_GSM_WCDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_3GPP_LAST:
        ctx->step++;
        /* Fall through */

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_FIRST:
        /* If modem doesn't have CDMA caps, skip CDMA SMS */
        if (!mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
            ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST;
            load_initial_sms_parts_step (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_ALL:
        mm_obj_dbg (self, "loading all CDMA messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_READ:
        mm_obj_dbg (self, "loading CDMA MT-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_READ;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MT_NOT_READ:
        mm_obj_dbg (self, "loading CDMA MT-not-read messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_SENT:
        mm_obj_dbg (self, "loading CDMA MO-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_SENT;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LIST_MO_NOT_SENT:
        mm_obj_dbg (self, "loading CDMA MO-not-sent messages from storage '%s'...",
                mm_sms_storage_get_string (ctx->storage));
        tag_type = QMI_WMS_MESSAGE_TAG_TYPE_MO_NOT_SENT;
        mode = QMI_WMS_MESSAGE_MODE_CDMA;
        break;

    case LOAD_INITIAL_SMS_PARTS_STEP_CDMA_LAST:
        ctx->step++;
        /* Fall through */

    case LOAD_INITIAL_SMS_PARTS_STEP_LAST:
        /* All steps done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
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

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        return iface_modem_messaging_parent->load_initial_sms_parts (_self, storage, callback, user_data);
    }

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WMS, &client,
                                      callback, user_data))
        return;

    ctx = g_slice_new0 (LoadInitialSmsPartsContext);
    ctx->client = QMI_CLIENT_WMS (g_object_ref (client));
    ctx->storage = storage;
    ctx->step = LOAD_INITIAL_SMS_PARTS_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_initial_sms_parts_context_free);

    load_initial_sms_parts_step (task);
}

/*****************************************************************************/
/* Common setup/cleanup unsolicited event handlers (Messaging interface) */

typedef struct {
    MMBroadbandModemQmi *self;
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
        mm_obj_dbg (ctx->self, "QMI operation failed: %s", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_raw_read_output_get_result (output, &error)) {
        mm_obj_dbg (ctx->self, "couldn't read raw message: %s", error->message);
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
                               FALSE,
                               data);
    }

    if (output)
        qmi_message_wms_raw_read_output_unref (output);

    indication_raw_read_context_free (ctx);
}

static void
wms_send_ack_ready (QmiClientWms *client,
                    GAsyncResult *res,
                    MMBroadbandModemQmi *self)
{
    g_autoptr(QmiMessageWmsSendAckOutput) output = NULL;
    g_autoptr(GError) error= NULL;

    output = qmi_client_wms_send_ack_finish (client, res, &error);
    if (!output) {
        mm_obj_dbg (self, "QMI operation failed: '%s'", error->message);
    }
    g_object_unref (self);
}

static void
messaging_event_report_indication_cb (QmiClientNas *client,
                                      QmiIndicationWmsEventReportOutput *output,
                                      MMBroadbandModemQmi *self)
{
    QmiWmsStorageType storage;
    guint32 memory_index;
    QmiWmsAckIndicator ack_ind;
    guint32 transaction_id;
    QmiWmsMessageFormat msg_format;
    QmiWmsMessageTagType tag;
    GArray *raw_data = NULL;

    /* Handle transfer-route MT messages */
    if (qmi_indication_wms_event_report_output_get_transfer_route_mt_message (
            output,
            &ack_ind,
            &transaction_id,
            &msg_format,
            &raw_data,
            NULL)) {
        mm_obj_dbg (self, "Got transfer-route MT message");
        /* If this is the first of a multi-part message, send an ACK to get the
         * second part */
        if (ack_ind == QMI_WMS_ACK_INDICATOR_SEND) {
            g_autoptr(QmiMessageWmsSendAckInput) ack_input = NULL;
            QmiWmsMessageProtocol message_protocol;
            /* Need to ack message */
            mm_obj_dbg (self, "Need to ACK indicator");
            switch (msg_format) {
            case QMI_WMS_MESSAGE_FORMAT_CDMA:
                message_protocol = QMI_WMS_MESSAGE_PROTOCOL_CDMA;
                break;
            case QMI_WMS_MESSAGE_FORMAT_MWI:
            case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT:
            case QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_BROADCAST:
            default:
                message_protocol = QMI_WMS_MESSAGE_PROTOCOL_WCDMA;
                break;
            }
            ack_input = qmi_message_wms_send_ack_input_new();
            qmi_message_wms_send_ack_input_set_information (ack_input,
                                                            transaction_id,
                                                            message_protocol,
                                                            TRUE,
                                                            NULL);
            qmi_client_wms_send_ack (QMI_CLIENT_WMS (client),
                                     ack_input,
                                     MM_BASE_SMS_DEFAULT_SEND_TIMEOUT,
                                     NULL,
                                     (GAsyncReadyCallback)wms_send_ack_ready,
                                     g_object_ref (self));
        }

        /* Defaults for transfer-route messages, which are not stored anywhere */
        storage = QMI_WMS_STORAGE_TYPE_NONE;
        memory_index = 0;
        tag = QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ;
        add_new_read_sms_part (MM_IFACE_MODEM_MESSAGING (self),
                               storage,
                               memory_index,
                               tag,
                               msg_format,
                               TRUE,
                               raw_data);
        return;
    }

    if (qmi_indication_wms_event_report_output_get_mt_message (
            output,
            &storage,
            &memory_index,
            NULL)) {
        IndicationRawReadContext *ctx;
        QmiMessageWmsRawReadInput *input;

        ctx = g_slice_new (IndicationRawReadContext);
        ctx->self = g_object_ref (self);
        ctx->client = QMI_CLIENT_WMS (g_object_ref (client));
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
common_setup_cleanup_messaging_unsolicited_events (MMBroadbandModemQmi  *self,
                                                   gboolean              enable,
                                                   GError              **error)
{
    QmiClient *client = NULL;

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_WMS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        error);
    if (!client)
        return FALSE;

    if (enable == self->priv->messaging_unsolicited_events_setup) {
        mm_obj_dbg (self, "messaging unsolicited events already %s; skipping",
                    enable ? "setup" : "cleanup");
        return TRUE;
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

    return TRUE;
}

/*****************************************************************************/
/* Cleanup unsolicited event handlers (Messaging interface) */

static gboolean
messaging_cleanup_unsolicited_events_finish (MMIfaceModemMessaging  *self,
                                             GAsyncResult           *res,
                                             GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_messaging_cleanup_unsolicited_events_ready (MMIfaceModemMessaging *_self,
                                                   GAsyncResult          *res,
                                                   GTask                 *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GError              *error = NULL;

    if (!iface_modem_messaging_parent->cleanup_unsolicited_events_finish (_self, res, &error)) {
        if (self->priv->messaging_fallback_at_only) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "cleaning up parent messaging unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    /* handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Disable QMI indications */
    if (!common_setup_cleanup_messaging_unsolicited_events (self, FALSE, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
messaging_cleanup_unsolicited_events (MMIfaceModemMessaging *self,
                                      GAsyncReadyCallback    callback,
                                      gpointer               user_data)
{
    /* Disable AT URCs parent and chain QMI indications disabling */
    iface_modem_messaging_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_messaging_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Setup unsolicited event handlers (Messaging interface) */

static gboolean
messaging_setup_unsolicited_events_finish (MMIfaceModemMessaging  *self,
                                           GAsyncResult           *res,
                                           GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_messaging_setup_unsolicited_events_ready (MMIfaceModemMessaging *_self,
                                                 GAsyncResult          *res,
                                                 GTask                 *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GError              *error = NULL;

    if (!iface_modem_messaging_parent->setup_unsolicited_events_finish (_self, res, &error)) {
        if (self->priv->messaging_fallback_at_only) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "setting up parent messaging unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    /* handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enable QMI indications */
    if (!common_setup_cleanup_messaging_unsolicited_events (self, TRUE, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
messaging_setup_unsolicited_events (MMIfaceModemMessaging *self,
                                    GAsyncReadyCallback    callback,
                                    gpointer               user_data)
{
    /* Enable AT URCs parent and chain QMI indication enabling */
    iface_modem_messaging_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_messaging_setup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
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
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *_self,
                                            GAsyncResult *res,
                                            GError **error)
{
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
        mm_obj_dbg (self, "QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_wms_set_event_report_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't set event report: '%s'", error->message);
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
                                                    GTask *task)
{
    EnableMessagingUnsolicitedEventsContext *ctx;
    QmiClient *client = NULL;
    QmiMessageWmsSetEventReportInput *input;
    GError *error = NULL;

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_WMS, MM_PORT_QMI_FLAG_DEFAULT, &error);
    if (!client) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (enable == self->priv->messaging_unsolicited_events_enabled) {
        mm_obj_dbg (self, "messaging unsolicited events already %s; skipping",
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
parent_messaging_disable_unsolicited_events_ready (MMIfaceModemMessaging *_self,
                                                   GAsyncResult          *res,
                                                   GTask                 *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GError              *error = NULL;

    if (!iface_modem_messaging_parent->disable_unsolicited_events_finish (_self, res, &error)) {
        if (self->priv->messaging_fallback_at_only) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "disabling parent messaging unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    /* handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Disable QMI indications */
    common_enable_disable_messaging_unsolicited_events (self, FALSE, task);
}

static void
messaging_disable_unsolicited_events (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Generic implementation doesn't actually have a method to disable
     * unsolicited messaging events */
    if (iface_modem_messaging_parent->disable_unsolicited_events) {
        /* Disable AT URCs parent and chain QMI indication disabling */
        iface_modem_messaging_parent->disable_unsolicited_events (
            _self,
            (GAsyncReadyCallback)parent_messaging_disable_unsolicited_events_ready,
            task);
        return;
    }

    /* handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Disable QMI indications */
    common_enable_disable_messaging_unsolicited_events (self, FALSE, task);
}

static void
parent_messaging_enable_unsolicited_events_ready (MMIfaceModemMessaging *_self,
                                                  GAsyncResult          *res,
                                                  GTask                 *task)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GError              *error = NULL;

    if (!iface_modem_messaging_parent->enable_unsolicited_events_finish (_self, res, &error)) {
        if (self->priv->messaging_fallback_at_only) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        mm_obj_dbg (self, "enabling parent messaging unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    /* handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enable QMI indications */
    common_enable_disable_messaging_unsolicited_events (self, TRUE, task);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Enable AT URCs parent and chain QMI indication enabling */
    iface_modem_messaging_parent->enable_unsolicited_events (
        _self,
        (GAsyncReadyCallback)parent_messaging_enable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMBaseSms *
messaging_create_sms (MMIfaceModemMessaging *_self)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    /* Handle AT URC only fallback */
    if (self->priv->messaging_fallback_at_only) {
        return iface_modem_messaging_parent->create_sms (_self);
    }

    return mm_sms_qmi_new (MM_BASE_MODEM (self));
}


/*****************************************************************************/
/* Check support (SAR interface) */

/* SAR level 0 is assumed DISABLED, and any other level is assumed ENABLED */
#define QMI_SAR_ENABLE_POWER_INDEX   QMI_SAR_RF_STATE_1
#define QMI_SAR_DISABLED_POWER_INDEX QMI_SAR_RF_STATE_0

static gboolean
sar_check_support_finish (MMIfaceModemSar  *self,
                          GAsyncResult     *res,
                          GError          **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
sar_check_support (MMIfaceModemSar     *self,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If SAR service is available, assume sar state is OS */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_SAR,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        mm_obj_dbg (self, "SAR capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        mm_obj_dbg (self, "SAR capabilities supported");
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

/*****************************************************************************/
/* Load SAR state (SAR interface) */

static gboolean
sar_load_state_finish (MMIfaceModemSar *self,
                       GAsyncResult    *res,
                       gboolean        *out_state,
                       GError         **error)
{
    GError   *inner_error = NULL;
    gboolean  result;

    result = g_task_propagate_boolean (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_state)
        *out_state = result;
    return TRUE;
}

static void
sar_load_state_ready (QmiClientSar *client,
                      GAsyncResult *res,
                      GTask        *task)
{
    g_autoptr(QmiMessageSarRfGetStateOutput)  output = NULL;
    GError                                   *error = NULL;
    QmiSarRfState                             rf_state;

    output = qmi_client_sar_rf_get_state_finish (client, res, &error);
    if (output &&
        qmi_message_sar_rf_get_state_output_get_result (output, &error) &&
        qmi_message_sar_rf_get_state_output_get_state (output, &rf_state, &error)) {
        if (rf_state == QMI_SAR_DISABLED_POWER_INDEX)
            g_task_return_boolean (task, FALSE);
        else
            g_task_return_boolean (task, TRUE);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_load_state (MMIfaceModemSar     *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_SAR, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_sar_rf_get_state (
        QMI_CLIENT_SAR (client),
        NULL,
        5,
        NULL,
        (GAsyncReadyCallback)sar_load_state_ready,
        task);
}

/*****************************************************************************/
/* Load SAR power level (SAR interface) */

static gboolean
sar_load_power_level_finish (MMIfaceModemSar  *self,
                             GAsyncResult     *res,
                             guint            *out_power_level,
                             GError          **error)
{
    gssize result;

    result = g_task_propagate_int (G_TASK (res), error);
    if (result < 0)
        return FALSE;

    if (out_power_level)
        *out_power_level = (guint) result;
    return TRUE;
}

static void
sar_load_power_level_ready (QmiClientSar *client,
                            GAsyncResult *res,
                            GTask        *task)
{
    g_autoptr(QmiMessageSarRfGetStateOutput)  output = NULL;
    GError                                   *error = NULL;
    QmiSarRfState                             rf_state;

    output = qmi_client_sar_rf_get_state_finish (client, res, &error);
    if (output &&
        qmi_message_sar_rf_get_state_output_get_result (output, &error) &&
        qmi_message_sar_rf_get_state_output_get_state (output, &rf_state, &error))
        g_task_return_int (task, rf_state);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_load_power_level (MMIfaceModemSar     *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_SAR, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_sar_rf_get_state (
        QMI_CLIENT_SAR (client),
        NULL,
        5,
        NULL,
        (GAsyncReadyCallback)sar_load_power_level_ready,
        task);
}

/*****************************************************************************/
/* Enable/Disable SAR (SAR interface) */

static gboolean
sar_enable_finish (MMIfaceModemSar *self,
                   GAsyncResult    *res,
                   guint           *out_sar_power_level,
                   GError         **error)
{
    QmiSarRfState level;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    level = GPOINTER_TO_UINT (g_task_get_task_data (G_TASK (res)));
    if (out_sar_power_level)
        *out_sar_power_level = level;
    return TRUE;
}

static void
sar_enable_ready (QmiClientSar *client,
                  GAsyncResult *res,
                  GTask        *task)
{
    g_autoptr(QmiMessageSarRfSetStateOutput)  output = NULL;
    GError                                   *error = NULL;

    output = qmi_client_sar_rf_set_state_finish (client, res, &error);
    if (output && qmi_message_sar_rf_set_state_output_get_result (output, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_enable (MMIfaceModemSar     *self,
            gboolean             enable,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    g_autoptr(QmiMessageSarRfSetStateInput)  input = NULL;
    GTask                                   *task;
    QmiClient                               *client = NULL;
    QmiSarRfState                            level;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_SAR, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    input = qmi_message_sar_rf_set_state_input_new ();

    /* When enabling, try to set the last valid known power level used, instead
     * of defaulting to level 1 */
    if (enable) {
        level = mm_iface_modem_sar_get_power_level (self);
        if (level == QMI_SAR_DISABLED_POWER_INDEX)
            level = QMI_SAR_ENABLE_POWER_INDEX;
    } else
        level = QMI_SAR_DISABLED_POWER_INDEX;

    qmi_message_sar_rf_set_state_input_set_state (input, level, NULL);
    g_task_set_task_data (task, GUINT_TO_POINTER (level), NULL);

    qmi_client_sar_rf_set_state (
        QMI_CLIENT_SAR (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)sar_enable_ready,
        task);
}

/*****************************************************************************/
/* Set SAR power level (SAR interface) */

static gboolean
sar_set_power_level_finish (MMIfaceModemSar *self,
                            GAsyncResult    *res,
                            GError         **error)
{
     return g_task_propagate_boolean (G_TASK (res), error);
}

static void
sar_set_power_level_ready (QmiClientSar *client,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autoptr(QmiMessageSarRfSetStateOutput) output = NULL;
    GError                                  *error = NULL;

    output = qmi_client_sar_rf_set_state_finish (client, res, &error);
    if (output && qmi_message_sar_rf_set_state_output_get_result (output, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_set_power_level (MMIfaceModemSar    *self,
                     guint               power_level,
                     GAsyncReadyCallback callback,
                     gpointer            user_data)
{
    g_autoptr(QmiMessageSarRfSetStateInput)  input = NULL;
    GTask                                   *task;
    QmiClient                               *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_SAR, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (power_level == QMI_SAR_DISABLED_POWER_INDEX) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Unsupported power level");
        g_object_unref (task);
        return;
    }

    input = qmi_message_sar_rf_set_state_input_new ();
    qmi_message_sar_rf_set_state_input_set_state (input, power_level, NULL);
    qmi_client_sar_rf_set_state (
        QMI_CLIENT_SAR (client),
        input,
        5,
        NULL,
        (GAsyncReadyCallback)sar_set_power_level_ready,
        task);
}

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

static MMModemLocationSource
location_load_capabilities_finish (MMIfaceModemLocation  *self,
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
shared_qmi_location_load_capabilities_ready (MMIfaceModemLocation *self,
                                             GAsyncResult         *res,
                                             GTask                *task)
{
    MMModemLocationSource sources;
    GError *error = NULL;

    sources = mm_shared_qmi_location_load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If the modem is CDMA, we have support for CDMA BS location */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        sources |= MM_MODEM_LOCATION_SOURCE_CDMA_BS;

    /* If the modem is 3GPP, we have support for 3GPP LAC/CI location */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        sources |= MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI;

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback   callback,
                            gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up shared QMI setup, which takes care of running the PARENT
     * setup as well as processing GPS-related checks. */
    mm_shared_qmi_location_load_capabilities (
        self,
        (GAsyncReadyCallback)shared_qmi_location_load_capabilities_ready,
        task);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                   GAsyncResult          *res,
                                   GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
shared_qmi_disable_location_gathering_ready (MMIfaceModemLocation *self,
                                             GAsyncResult         *res,
                                             GTask                *task)
{
    GError *error = NULL;

    if (!mm_shared_qmi_disable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disable_location_gathering (MMIfaceModemLocation *_self,
                            MMModemLocationSource source,
                            GAsyncReadyCallback   callback,
                            gpointer              user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask               *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Nothing to be done to disable 3GPP or CDMA locations */
    if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI || source == MM_MODEM_LOCATION_SOURCE_CDMA_BS)
        self->priv->enabled_sources &= ~source;

    mm_shared_qmi_disable_location_gathering (
        _self,
        source,
        (GAsyncReadyCallback) shared_qmi_disable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
shared_qmi_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                            GAsyncResult         *res,
                                            GTask                *task)
{
    MMBroadbandModemQmi   *self = MM_BROADBAND_MODEM_QMI (_self);
    MMModemLocationSource  source;
    GError                *error = NULL;

    if (!mm_shared_qmi_enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* Nothing else needed in the QMI side for LAC/CI */
    if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI &&
        mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
        self->priv->enabled_sources |= source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* CDMA modems need to re-run registration checks when enabling the CDMA BS
     * location source, so that we get up to date BS location information.
     * Note that we don't care for when the registration checks get finished.
     */
    if (source == MM_MODEM_LOCATION_SOURCE_CDMA_BS &&
        mm_iface_modem_is_cdma (MM_IFACE_MODEM (self))) {
        /* Reload registration to get LAC/CI */
        mm_iface_modem_cdma_run_registration_checks (MM_IFACE_MODEM_CDMA (self), NULL, NULL);
        /* Just mark it as enabled */
        self->priv->enabled_sources |= source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Otherwise, we're done */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
enable_location_gathering (MMIfaceModemLocation  *self,
                           MMModemLocationSource  source,
                           GAsyncReadyCallback    callback,
                           gpointer               user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    mm_shared_qmi_enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)shared_qmi_enable_location_gathering_ready,
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

    task = g_task_new (self, NULL, callback, user_data);

    /* If we have support for the OMA client, OMA is supported */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_OMA,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        mm_obj_dbg (self, "OMA capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        mm_obj_dbg (self, "OMA capabilities supported");
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
            mm_obj_warn (self, "unknown QMI OMA session type '%u'", network_initiated_alert_session_type);
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_OMA, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->oma_unsolicited_events_setup) {
        mm_obj_dbg (self, "OMA unsolicited events already %s; skipping",
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
        mm_obj_dbg (self, "QMI operation failed: '%s'", error->message);
        g_error_free (error);
    } else if (!qmi_message_oma_set_event_report_output_get_result (output, &error)) {
        mm_obj_dbg (self, "couldn't set event report: '%s'", error->message);
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

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_OMA, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->oma_unsolicited_events_enabled) {
        mm_obj_dbg (self, "OMA unsolicited events already %s; skipping",
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
/* Check support (3GPP USSD interface) */

static gboolean
modem_3gpp_ussd_check_support_finish (MMIfaceModem3gppUssd  *self,
                                      GAsyncResult          *res,
                                      GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_3gpp_ussd_check_support (MMIfaceModem3gppUssd *self,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If we have support for the Voice client, USSD is supported */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_VOICE,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        mm_obj_dbg (self, "USSD capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        mm_obj_dbg (self, "USSD capabilities supported");
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

/*****************************************************************************/
/* USSD encode/decode helpers */

static GArray *
ussd_encode (const gchar                  *command,
             QmiVoiceUssDataCodingScheme  *scheme,
             GError                      **error)
{
    gsize                 command_len;
    g_autoptr(GByteArray) barray = NULL;
    g_autoptr(GError)     inner_error = NULL;

    command_len = strlen (command);

    if (g_str_is_ascii (command)) {
        barray = g_byte_array_sized_new (command_len);
        g_byte_array_append (barray, (const guint8 *)command, command_len);

        *scheme = QMI_VOICE_USS_DATA_CODING_SCHEME_ASCII;
        return (GArray *) g_steal_pointer (&barray);
    }

    barray = mm_modem_charset_bytearray_from_utf8 (command, MM_MODEM_CHARSET_UCS2, FALSE, &inner_error);
    if (!barray) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Failed to encode USSD command in UCS2 charset: %s", inner_error->message);
        return NULL;
    }

    *scheme = QMI_VOICE_USS_DATA_CODING_SCHEME_UCS2;
    return (GArray *) g_steal_pointer (&barray);
}

static gchar *
ussd_decode (QmiVoiceUssDataCodingScheme   scheme,
             GArray                       *data,
             GError                      **error)
{
    gchar *decoded = NULL;

    if (scheme == QMI_VOICE_USS_DATA_CODING_SCHEME_ASCII) {
        decoded = g_strndup ((const gchar *) data->data, data->len);
        if (!decoded)
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                         "Error decoding USSD command in 0x%04x scheme (ASCII charset)",
                         scheme);
    } else if (scheme == QMI_VOICE_USS_DATA_CODING_SCHEME_UCS2) {
        decoded = mm_modem_charset_bytearray_to_utf8 ((GByteArray *) data, MM_MODEM_CHARSET_UCS2, FALSE, error);
        if (!decoded)
            g_prefix_error (error, "Error decoding USSD command in 0x%04x scheme (UCS2 charset): ", scheme);
    } else
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Failed to decode USSD command in unsupported 0x%04x scheme", scheme);

    return decoded;
}

/*****************************************************************************/
/* USSD indications */

static void
process_ussd_message (MMBroadbandModemQmi *self,
                      QmiVoiceUserAction   user_action,
                      gchar               *utf8_take,
                      GError              *error_take)
{
    MMModem3gppUssdSessionState  ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE;
    g_autoptr(GTask)             task = NULL;
    g_autofree gchar            *utf8 = utf8_take;
    g_autoptr(GError)            error = error_take;

    task = g_steal_pointer (&self->priv->pending_ussd_action);

    if (error) {
        g_assert (!utf8);
        if (task)
            g_task_return_error (task, g_steal_pointer (&error));
        else
            mm_obj_dbg (self, "USSD operation failed: %s", error->message);
        return;
    }

    switch (user_action) {
        case QMI_VOICE_USER_ACTION_NOT_REQUIRED:
        case QMI_VOICE_USER_ACTION_UNKNOWN: /* Treat unknown user action as user action not required. */
            /* no response, or a response to user's request? */
            if (!utf8 || task)
                break;
            /* Network-initiated USSD-Notify */
            mm_iface_modem_3gpp_ussd_update_network_notification (MM_IFACE_MODEM_3GPP_USSD (self), utf8);
            g_clear_pointer (&utf8, g_free);
            break;
        case QMI_VOICE_USER_ACTION_REQUIRED:
            /* further action required */
            ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE;
            /* no response, or a response to user's request? */
            if (!utf8 || task)
                break;
            /* Network-initiated USSD-Request */
            mm_iface_modem_3gpp_ussd_update_network_request (MM_IFACE_MODEM_3GPP_USSD (self), utf8);
            g_clear_pointer (&utf8, g_free);
            break;
        default:
            /* Not an indication */
            break;
    }

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self), ussd_state);

    if (!task) {
        if (utf8)
            mm_obj_dbg (self, "ignoring unprocessed USSD message: %s", utf8);
        return;
    }

    /* Complete the pending action, if any */
    if (utf8)
        g_task_return_pointer (task, g_steal_pointer (&utf8), g_free);
    else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "USSD action response not processed correctly");
}

static void
ussd_indication_cb (QmiClientVoice               *client,
                    QmiIndicationVoiceUssdOutput *output,
                    MMBroadbandModemQmi          *self)
{
    QmiVoiceUserAction           user_action = QMI_VOICE_USER_ACTION_UNKNOWN;
    QmiVoiceUssDataCodingScheme  scheme;
    GArray                      *uss_data = NULL;
    gchar                       *utf8 = NULL;
    GError                      *error = NULL;

    qmi_indication_voice_ussd_output_get_user_action (output, &user_action, NULL);
    if (qmi_indication_voice_ussd_output_get_uss_data_utf16 (output, &uss_data, NULL) && uss_data)
        utf8 = g_convert ((const gchar *) uss_data->data, (2 * uss_data->len), "UTF-8", "UTF-16LE", NULL, NULL, &error);
    else if (qmi_indication_voice_ussd_output_get_uss_data (output, &scheme, &uss_data, NULL) && uss_data)
        utf8 = ussd_decode(scheme, uss_data, &error);

    process_ussd_message (self, user_action, utf8, error);
}

static void
ussd_release_indication_cb (QmiClientVoice      *client,
                            MMBroadbandModemQmi *self)
{
    GTask  *pending_task;
    GError *error;

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                           MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);

    error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_ABORTED, "USSD terminated by network");

    pending_task = g_steal_pointer (&self->priv->pending_ussd_action);
    if (pending_task) {
        g_task_return_error (pending_task, error);
        g_object_unref (pending_task);
        return;
    }

    /* If no pending task, just report the error */
    mm_obj_dbg (self, "USSD release indication: %s", error->message);
    g_error_free (error);
}

/*****************************************************************************/
/* Setup/cleanup unsolicited events */

static gboolean
common_3gpp_ussd_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gppUssd  *self,
                                                          GAsyncResult          *res,
                                                          GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_3gpp_ussd_setup_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                                   gboolean             setup,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (setup == self->priv->ussd_unsolicited_events_setup) {
        mm_obj_dbg (self, "USSD unsolicited events already %s; skipping",
                    setup ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->ussd_unsolicited_events_setup = setup;

    if (setup) {
        g_assert (self->priv->ussd_indication_id == 0);
        self->priv->ussd_indication_id =
            g_signal_connect (client,
                              "ussd",
                              G_CALLBACK (ussd_indication_cb),
                              self);
        g_assert (self->priv->ussd_release_indication_id == 0);
        self->priv->ussd_release_indication_id =
            g_signal_connect (client,
                              "release-ussd",
                              G_CALLBACK (ussd_release_indication_cb),
                              self);
    } else {
        g_assert (self->priv->ussd_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->ussd_indication_id);
        self->priv->ussd_indication_id = 0;
        g_assert (self->priv->ussd_release_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->ussd_release_indication_id);
        self->priv->ussd_release_indication_id = 0;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_ussd_setup_unsolicited_events (MMIfaceModem3gppUssd *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    common_3gpp_ussd_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), TRUE, callback, user_data);
}

static void
modem_3gpp_ussd_cleanup_unsolicited_events (MMIfaceModem3gppUssd *self,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data)
{
    common_3gpp_ussd_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), FALSE, callback, user_data);
}

/*****************************************************************************/
/* Enable/disable unsolicited events */

static gboolean
common_3gpp_ussd_enable_disable_unsolicited_events_finish (MMIfaceModem3gppUssd  *self,
                                                           GAsyncResult          *res,
                                                           GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ussd_indication_register_ready (QmiClientVoice *client,
                                GAsyncResult   *res,
                                GTask          *task)
{
    g_autoptr(QmiMessageVoiceIndicationRegisterOutput) output = NULL;
    GError *error = NULL;

    output = qmi_client_voice_indication_register_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_indication_register_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't register voice USSD indications: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_3gpp_ussd_enable_disable_unsolicited_events (MMBroadbandModemQmi *self,
                                                    gboolean             enable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceIndicationRegisterInput) input = NULL;
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->ussd_unsolicited_events_enabled) {
        mm_obj_dbg (self, "USSD unsolicited events already %s; skipping",
                    enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->ussd_unsolicited_events_enabled = enable;

    input = qmi_message_voice_indication_register_input_new ();
    qmi_message_voice_indication_register_input_set_ussd_notification_events (input, enable, NULL);
    qmi_client_voice_indication_register (QMI_CLIENT_VOICE (client),
                                          input,
                                          10,
                                          NULL,
                                          (GAsyncReadyCallback) ussd_indication_register_ready,
                                          task);
}

static void
modem_3gpp_ussd_enable_unsolicited_events (MMIfaceModem3gppUssd *self,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data)
{
    common_3gpp_ussd_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), TRUE, callback, user_data);
}

static void
modem_3gpp_ussd_disable_unsolicited_events (MMIfaceModem3gppUssd *self,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data)
{
    common_3gpp_ussd_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), FALSE, callback, user_data);
}

/*****************************************************************************/
/* Send command (3GPP/USSD interface) */

static gchar *
modem_3gpp_ussd_send_finish (MMIfaceModem3gppUssd  *self,
                             GAsyncResult          *res,
                             GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
voice_answer_ussd_ready (QmiClientVoice      *client,
                         GAsyncResult        *res,
                         MMBroadbandModemQmi *self)
{
    g_autoptr(QmiMessageVoiceAnswerUssdOutput) output = NULL;
    GError *error = NULL;

    output = qmi_client_voice_answer_ussd_finish (client, res, &error);
    if (!output)
        g_prefix_error (&error, "QMI operation failed: ");
    else if (!qmi_message_voice_answer_ussd_output_get_result (output, &error))
        g_prefix_error (&error, "Couldn't answer USSD operation: ");

    process_ussd_message (self, QMI_VOICE_USER_ACTION_UNKNOWN, error ? NULL : g_strdup (""), error);

    /* balance out the full reference we received */
    g_object_unref (self);
}

static void
voice_originate_ussd_ready (QmiClientVoice      *client,
                            GAsyncResult        *res,
                            MMBroadbandModemQmi *self)
{
    g_autoptr(QmiMessageVoiceOriginateUssdOutput) output = NULL;
    QmiVoiceUssDataCodingScheme  scheme;
    GError                      *error = NULL;
    GArray                      *uss_data = NULL;
    gchar                       *utf8 = NULL;

    output = qmi_client_voice_originate_ussd_finish (client, res, &error);
    if (!output)
        g_prefix_error (&error, "QMI operation failed: ");
    else if (!qmi_message_voice_originate_ussd_output_get_result (output, &error))
        g_prefix_error (&error, "Couldn't originate USSD operation: ");
    else if (qmi_message_voice_originate_ussd_output_get_uss_data_utf16 (output, &uss_data, NULL) && uss_data)
        utf8 = g_convert ((const gchar *) uss_data->data, (2 * uss_data->len), "UTF-8", "UTF-16LE", NULL, NULL, &error);
    else if (qmi_message_voice_originate_ussd_output_get_uss_data (output, &scheme, &uss_data, NULL) && uss_data)
        utf8 = ussd_decode (scheme, uss_data, &error);

    process_ussd_message (self, QMI_VOICE_USER_ACTION_UNKNOWN, utf8, error);

    /* balance out the full reference we received */
    g_object_unref (self);
}

static void
modem_3gpp_ussd_send (MMIfaceModem3gppUssd *_self,
                      const gchar          *command,
                      GAsyncReadyCallback   callback,
                      gpointer              user_data)
{
    MMBroadbandModemQmi         *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask                       *task;
    QmiClient                   *client;
    QmiVoiceUssDataCodingScheme  scheme = QMI_VOICE_USS_DATA_CODING_SCHEME_UNKNOWN;
    g_autoptr(GArray)            encoded = NULL;
    GError                      *error = NULL;
    MMModem3gppUssdSessionState  state;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Fail if there is an ongoing operation already */
    if (self->priv->pending_ussd_action) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS,
                                 "there is already an ongoing USSD operation");
        g_object_unref (task);
        return;
    }

    encoded = ussd_encode (command, &scheme, &error);
    if (!encoded) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    state = mm_iface_modem_3gpp_ussd_get_state (MM_IFACE_MODEM_3GPP_USSD (self));

    /* Cache the action, as it may be completed via URCs */
    self->priv->pending_ussd_action = task;
    mm_iface_modem_3gpp_ussd_update_state (_self, MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE);

    switch (state) {
        case MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE: {
            g_autoptr(QmiMessageVoiceOriginateUssdInput) input = NULL;

            input = qmi_message_voice_originate_ussd_input_new ();
            qmi_message_voice_originate_ussd_input_set_uss_data (input, scheme, encoded, NULL);
            qmi_client_voice_originate_ussd (QMI_CLIENT_VOICE (client), input, 100, NULL,
                                             (GAsyncReadyCallback) voice_originate_ussd_ready,
                                             g_object_ref (self)); /* full reference! */
            return;
        }
        case MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE: {
            g_autoptr(QmiMessageVoiceAnswerUssdInput) input = NULL;

            input = qmi_message_voice_answer_ussd_input_new ();
            qmi_message_voice_answer_ussd_input_set_uss_data (input, scheme, encoded, NULL);
            qmi_client_voice_answer_ussd (QMI_CLIENT_VOICE (client), input, 100, NULL,
                                          (GAsyncReadyCallback) voice_answer_ussd_ready,
                                          g_object_ref (self)); /* full reference! */
            return;
        }
        case MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN:
        case MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE:
        default:
            g_assert_not_reached ();
            return;
    }
}

/*****************************************************************************/
/* Cancel USSD (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_cancel_finish (MMIfaceModem3gppUssd  *self,
                               GAsyncResult          *res,
                               GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_cancel_ussd_ready (QmiClientVoice *client,
                         GAsyncResult   *res,
                         GTask          *task)
{
    g_autoptr(QmiMessageVoiceCancelUssdOutput) output = NULL;
    MMBroadbandModemQmi *self;
    GTask               *pending_task;
    GError              *error = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_voice_cancel_ussd_finish (client, res, &error);
    if (!output)
        g_prefix_error (&error, "QMI operation failed: ");
    else if (!qmi_message_voice_cancel_ussd_output_get_result (output, &error))
        g_prefix_error (&error, "Couldn't cancel USSD operation: ");

    /* Complete the pending action, regardless of the operation result */
    pending_task = g_steal_pointer (&self->priv->pending_ussd_action);
    if (pending_task) {
        g_task_return_new_error (pending_task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                                 "USSD session was cancelled");
        g_object_unref (pending_task);
    }

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                           MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_ussd_cancel (MMIfaceModem3gppUssd *_self,
                        GAsyncReadyCallback   callback,
                        gpointer              user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask               *task;
    QmiClient           *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    qmi_client_voice_cancel_ussd (QMI_CLIENT_VOICE (client), NULL, 100, NULL,
                                  (GAsyncReadyCallback) voice_cancel_ussd_ready,
                                  task);
}

/*****************************************************************************/
/* Check support (Voice interface) */

static gboolean
modem_voice_check_support_finish (MMIfaceModemVoice  *self,
                                  GAsyncResult       *res,
                                  GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_voice_check_support (MMIfaceModemVoice   *self,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If we have support for the Voice client, Voice is supported */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_VOICE,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        mm_obj_dbg (self, "Voice capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        /*
         * In case of QMI, we don't need polling as call list
         * will be dynamically updated by All Call Status indication.
         * If an AT URC is received, reload the call list through QMI.
         */
        g_object_set (self,
                      MM_IFACE_MODEM_VOICE_PERIODIC_CALL_LIST_CHECK_DISABLED, TRUE,
                      MM_IFACE_MODEM_VOICE_INDICATION_CALL_LIST_RELOAD_ENABLED, TRUE,
                      NULL);
        mm_obj_dbg (self, "Voice capabilities supported");
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

/*****************************************************************************/
/* All Call Status indications */

static void
all_call_status_indication_cb (QmiClientVoice                        *client,
                               QmiIndicationVoiceAllCallStatusOutput *output,
                               MMBroadbandModemQmi                   *self)
{
    GArray *qmi_remote_party_number_list = NULL;
    GArray *qmi_call_information_list = NULL;
    GList  *call_info_list = NULL;
    guint   i;
    guint   j;

    qmi_indication_voice_all_call_status_output_get_remote_party_number (output, &qmi_remote_party_number_list, NULL);
    qmi_indication_voice_all_call_status_output_get_call_information (output, &qmi_call_information_list, NULL);

    if (!qmi_remote_party_number_list || !qmi_call_information_list) {
        mm_obj_dbg (self, "Ignoring All Call Status indication. Remote party number or call information not available");
        return;
    }

    for (i = 0; i < qmi_call_information_list->len; i++) {
        QmiIndicationVoiceAllCallStatusOutputCallInformationCall qmi_call_information;

        qmi_call_information = g_array_index (qmi_call_information_list,
                                              QmiIndicationVoiceAllCallStatusOutputCallInformationCall,
                                              i);
        for (j = 0; j < qmi_remote_party_number_list->len; j++) {
            QmiIndicationVoiceAllCallStatusOutputRemotePartyNumberCall qmi_remote_party_number;

            qmi_remote_party_number = g_array_index (qmi_remote_party_number_list,
                                                     QmiIndicationVoiceAllCallStatusOutputRemotePartyNumberCall,
                                                     j);
            if (qmi_call_information.id == qmi_remote_party_number.id) {
                MMCallInfo *call_info;

                call_info = g_slice_new0 (MMCallInfo);
                call_info->index = qmi_call_information.id;
                call_info->number = g_strdup (qmi_remote_party_number.type);

                switch (qmi_call_information.state) {
                case QMI_VOICE_CALL_STATE_UNKNOWN:
                    call_info->state = MM_CALL_STATE_UNKNOWN;
                    break;
                case QMI_VOICE_CALL_STATE_ORIGINATION:
                case QMI_VOICE_CALL_STATE_CC_IN_PROGRESS:
                    call_info->state = MM_CALL_STATE_DIALING;
                    break;
                case QMI_VOICE_CALL_STATE_ALERTING:
                    call_info->state = MM_CALL_STATE_RINGING_OUT;
                    break;
                case QMI_VOICE_CALL_STATE_SETUP:
                case QMI_VOICE_CALL_STATE_INCOMING:
                    call_info->state = MM_CALL_STATE_RINGING_IN;
                    break;
                case QMI_VOICE_CALL_STATE_CONVERSATION:
                    call_info->state = MM_CALL_STATE_ACTIVE;
                    break;
                case QMI_VOICE_CALL_STATE_HOLD:
                    call_info->state = MM_CALL_STATE_HELD;
                    break;
                case QMI_VOICE_CALL_STATE_WAITING:
                    call_info->state = MM_CALL_STATE_WAITING;
                    break;
                case QMI_VOICE_CALL_STATE_DISCONNECTING:
                case QMI_VOICE_CALL_STATE_END:
                    call_info->state = MM_CALL_STATE_TERMINATED;
                    break;
                default:
                    call_info->state = MM_CALL_STATE_UNKNOWN;
                    break;
                }

                switch (qmi_call_information.direction) {
                case QMI_VOICE_CALL_DIRECTION_UNKNOWN:
                    call_info->direction = MM_CALL_DIRECTION_UNKNOWN;
                    break;
                case QMI_VOICE_CALL_DIRECTION_MO:
                    call_info->direction = MM_CALL_DIRECTION_OUTGOING;
                    break;
                case QMI_VOICE_CALL_DIRECTION_MT:
                    call_info->direction = MM_CALL_DIRECTION_INCOMING;
                    break;
                default:
                    call_info->direction = MM_CALL_DIRECTION_UNKNOWN;
                    break;
                }

                call_info_list = g_list_append (call_info_list, call_info);
            }
        }
    }

    mm_iface_modem_voice_report_all_calls (MM_IFACE_MODEM_VOICE (self), call_info_list);
    mm_3gpp_call_info_list_free (call_info_list);
}

/*****************************************************************************/
/* Supplementary service indication */

static void
supplementary_service_indication_cb (QmiClientVoice                               *client,
                                     QmiIndicationVoiceSupplementaryServiceOutput *output,
                                     MMBroadbandModemQmi                          *self)
{
    QmiVoiceSupplementaryServiceNotificationType notification_type;
    guint8                 call_id = 0;
    g_autoptr(MMCallList)  call_list = NULL;
    MMBaseCall            *call = NULL;

    if (!qmi_indication_voice_supplementary_service_output_get_info (output, &call_id, &notification_type, NULL)) {
        mm_obj_dbg (self, "Ignoring supplementary service indication: no call id or notification type given");
        return;
    }

    /* Retrieve list of known calls */
    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &call_list,
                  NULL);
    if (!call_list) {
        mm_obj_dbg (self, "Ignoring supplementary service indication: no call list exists");
        return;
    }

    call = mm_call_list_get_call_by_index (call_list, call_id);
    if (!call) {
        mm_obj_dbg (self, "Ignoring supplementary service indication: no matching call exists");
        return;
    }

    if (notification_type == QMI_VOICE_SUPPLEMENTARY_SERVICE_NOTIFICATION_TYPE_CALL_IS_ON_HOLD)
        mm_base_call_change_state (call, MM_CALL_STATE_HELD, MM_CALL_STATE_REASON_UNKNOWN);
    else if (notification_type == QMI_VOICE_SUPPLEMENTARY_SERVICE_NOTIFICATION_TYPE_CALL_IS_RETRIEVED)
        mm_base_call_change_state (call, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_UNKNOWN);
    else if (notification_type == QMI_VOICE_SUPPLEMENTARY_SERVICE_NOTIFICATION_TYPE_OUTGOING_CALL_IS_WAITING)
        mm_base_call_change_state (call, MM_CALL_STATE_WAITING, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
    else
        mm_obj_dbg (self, "Ignoring supplementary service indication: unhandled notification type");
}

/*****************************************************************************/
/* Setup/cleanup unsolicited events */

static void
parent_voice_setup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                             GAsyncResult      *res,
                                             GTask             *task)
{
    GError *error = NULL;

    if (!iface_modem_voice_parent->setup_unsolicited_events_finish (self, res, &error)) {
        mm_obj_warn (self, "setting up parent voice unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_voice_cleanup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                               GAsyncResult      *res,
                                               GTask             *task)
{
    GError *error = NULL;

    if (!iface_modem_voice_parent->cleanup_unsolicited_events_finish (self, res, &error)) {
        mm_obj_warn (self, "cleaning up parent voice unsolicited events failed: %s", error->message);
        g_clear_error (&error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
common_voice_setup_cleanup_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                                      GAsyncResult       *res,
                                                      GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_voice_setup_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                               gboolean             setup,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (setup == self->priv->all_call_status_unsolicited_events_setup) {
        mm_obj_dbg (self, "voice unsolicited events already %s; skipping",
                    setup ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->all_call_status_unsolicited_events_setup = setup;

    if (setup) {
        /* Connect QMI indications signals for calls */
        g_assert (self->priv->all_call_status_indication_id == 0);
        self->priv->all_call_status_indication_id =
            g_signal_connect (client,
                              "all-call-status",
                              G_CALLBACK (all_call_status_indication_cb),
                              self);

        /* Setup AT URCs as fall back for calls */
        if (iface_modem_voice_parent->setup_unsolicited_events) {
            iface_modem_voice_parent->setup_unsolicited_events (
                MM_IFACE_MODEM_VOICE (self),
                (GAsyncReadyCallback) parent_voice_setup_unsolicited_events_ready,
                task);
            return;
        }

    } else {
        /* Disconnect QMI indications signals for calls */
        g_assert (self->priv->all_call_status_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->all_call_status_indication_id);
        self->priv->all_call_status_indication_id = 0;

        /* Cleanup AT URCs as fall back for calls */
        if (iface_modem_voice_parent->cleanup_unsolicited_events) {
            iface_modem_voice_parent->cleanup_unsolicited_events (
                MM_IFACE_MODEM_VOICE (self),
                (GAsyncReadyCallback) parent_voice_cleanup_unsolicited_events_ready,
                task);
            return;
        }
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_setup_unsolicited_events (MMIfaceModemVoice   *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    common_voice_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), TRUE, callback, user_data);
}

static void
modem_voice_cleanup_unsolicited_events (MMIfaceModemVoice   *self,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    common_voice_setup_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), FALSE, callback, user_data);
}

/*****************************************************************************/
/* Enable/disable unsolicited events */

static gboolean
common_voice_enable_disable_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                                       GAsyncResult       *res,
                                                       GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
call_indication_register_ready (QmiClientVoice *client,
                                GAsyncResult   *res,
                                GTask          *task)
{
    g_autoptr(QmiMessageVoiceIndicationRegisterOutput) output = NULL;
    GError *error = NULL;

    output = qmi_client_voice_indication_register_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_indication_register_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't register voice call indications: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_voice_enable_disable_unsolicited_events (MMBroadbandModemQmi *self,
                                                gboolean             enable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceIndicationRegisterInput) input = NULL;
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (enable == self->priv->all_call_status_unsolicited_events_enabled) {
        mm_obj_dbg (self, "voice unsolicited events already %s; skipping",
                    enable ? "enabled" : "disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->all_call_status_unsolicited_events_enabled = enable;

    input = qmi_message_voice_indication_register_input_new ();
    qmi_message_voice_indication_register_input_set_call_notification_events (input, enable, NULL);
    qmi_message_voice_indication_register_input_set_supplementary_service_notification_events (input, enable, NULL);
    qmi_client_voice_indication_register (QMI_CLIENT_VOICE (client),
                                          input,
                                          10,
                                          NULL,
                                          (GAsyncReadyCallback) call_indication_register_ready,
                                          task);
}

static void
modem_voice_enable_unsolicited_events (MMIfaceModemVoice   *self,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    common_voice_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), TRUE, callback, user_data);
}

static void
modem_voice_disable_unsolicited_events (MMIfaceModemVoice   *self,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    common_voice_enable_disable_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), FALSE, callback, user_data);
}

/*****************************************************************************/
/* Setup/cleanup in-call unsolicited events */

static gboolean
common_voice_setup_cleanup_in_call_unsolicited_events_finish (MMIfaceModemVoice  *self,
                                                              GAsyncResult       *res,
                                                              GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_voice_setup_in_call_cleanup_unsolicited_events (MMBroadbandModemQmi *self,
                                                       gboolean             setup,
                                                       GAsyncReadyCallback  callback,
                                                       gpointer             user_data)
{
    GTask     *task;
    QmiClient *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (setup == self->priv->supplementary_service_unsolicited_events_setup) {
        mm_obj_dbg (self, "Supplementary service unsolicited events already %s; skipping",
                    setup ? "setup" : "cleanup");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->supplementary_service_unsolicited_events_setup = setup;

    if (setup) {
        g_assert (self->priv->supplementary_service_indication_id == 0);
        self->priv->supplementary_service_indication_id =
            g_signal_connect (client,
                              "supplementary-service",
                              G_CALLBACK (supplementary_service_indication_cb),
                              self);
    } else {
        g_assert (self->priv->supplementary_service_indication_id != 0);
        g_signal_handler_disconnect (client, self->priv->supplementary_service_indication_id);
        self->priv->supplementary_service_indication_id = 0;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_voice_setup_in_call_unsolicited_events (MMIfaceModemVoice   *self,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
    common_voice_setup_in_call_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), TRUE, callback, user_data);
}

static void
modem_voice_cleanup_in_call_unsolicited_events (MMIfaceModemVoice   *self,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data)
{
    common_voice_setup_in_call_cleanup_unsolicited_events (MM_BROADBAND_MODEM_QMI (self), FALSE, callback, user_data);
}

/*****************************************************************************/
/* Load full list of calls (Voice interface) */

static gboolean
modem_voice_load_call_list_finish (MMIfaceModemVoice  *self,
                                   GAsyncResult       *res,
                                   GList             **out_call_info_list,
                                   GError            **error)
{
    GList  *call_info_list;
    GError *inner_error = NULL;

    call_info_list = g_task_propagate_pointer (G_TASK (res), &inner_error);
    if (inner_error) {
        g_assert (!call_info_list);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *out_call_info_list = call_info_list;
    return TRUE;
}

static gboolean
process_get_all_call_info (QmiClientVoice                       *client,
                           QmiMessageVoiceGetAllCallInfoOutput  *output,
                           GList                               **out_call_info_list,
                           GError                              **error)
{
    GArray *qmi_remote_party_number_list = NULL;
    GArray *qmi_call_information_list = NULL;
    GList  *call_info_list = NULL;
    guint   i;
    guint   j;

    /* If TLVs missing, report an error */
    if (!qmi_message_voice_get_all_call_info_output_get_remote_party_number (output, &qmi_remote_party_number_list, NULL) ||
        !qmi_message_voice_get_all_call_info_output_get_call_information (output, &qmi_call_information_list, NULL)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Remote party number or call information not available");
        return FALSE;
    }

    /* If there are no ongoing calls, the lists will be NULL */
    if (!qmi_remote_party_number_list || !qmi_call_information_list) {
        *out_call_info_list = NULL;
        return TRUE;
    }

    for (i = 0; i < qmi_call_information_list->len; i++) {
        QmiMessageVoiceGetAllCallInfoOutputCallInformationCall qmi_call_information;

        qmi_call_information = g_array_index (qmi_call_information_list,
                                              QmiMessageVoiceGetAllCallInfoOutputCallInformationCall,
                                              i);
        for (j = 0; j < qmi_remote_party_number_list->len; j++) {
            QmiMessageVoiceGetAllCallInfoOutputRemotePartyNumberCall qmi_remote_party_number;

            qmi_remote_party_number = g_array_index (qmi_remote_party_number_list,
                                                     QmiMessageVoiceGetAllCallInfoOutputRemotePartyNumberCall,
                                                     j);
            if (qmi_call_information.id == qmi_remote_party_number.id) {
                MMCallInfo *call_info;

                call_info = g_slice_new0 (MMCallInfo);
                call_info->index = qmi_call_information.id;
                call_info->number = g_strdup (qmi_remote_party_number.type);

                switch (qmi_call_information.state) {
                case QMI_VOICE_CALL_STATE_UNKNOWN:
                    call_info->state = MM_CALL_STATE_UNKNOWN;
                    break;
                case QMI_VOICE_CALL_STATE_ORIGINATION:
                case QMI_VOICE_CALL_STATE_CC_IN_PROGRESS:
                    call_info->state = MM_CALL_STATE_DIALING;
                    break;
                case QMI_VOICE_CALL_STATE_ALERTING:
                    call_info->state = MM_CALL_STATE_RINGING_OUT;
                    break;
                case QMI_VOICE_CALL_STATE_SETUP:
                case QMI_VOICE_CALL_STATE_INCOMING:
                    call_info->state = MM_CALL_STATE_RINGING_IN;
                    break;
                case QMI_VOICE_CALL_STATE_CONVERSATION:
                    call_info->state = MM_CALL_STATE_ACTIVE;
                    break;
                case QMI_VOICE_CALL_STATE_HOLD:
                    call_info->state = MM_CALL_STATE_HELD;
                    break;
                case QMI_VOICE_CALL_STATE_WAITING:
                    call_info->state = MM_CALL_STATE_WAITING;
                    break;
                case QMI_VOICE_CALL_STATE_DISCONNECTING:
                case QMI_VOICE_CALL_STATE_END:
                    call_info->state = MM_CALL_STATE_TERMINATED;
                    break;
                default:
                    call_info->state = MM_CALL_STATE_UNKNOWN;
                    break;
                }

                switch (qmi_call_information.direction) {
                case QMI_VOICE_CALL_DIRECTION_UNKNOWN:
                    call_info->direction = MM_CALL_DIRECTION_UNKNOWN;
                    break;
                case QMI_VOICE_CALL_DIRECTION_MO:
                    call_info->direction = MM_CALL_DIRECTION_OUTGOING;
                    break;
                case QMI_VOICE_CALL_DIRECTION_MT:
                    call_info->direction = MM_CALL_DIRECTION_INCOMING;
                    break;
                default:
                    call_info->direction = MM_CALL_DIRECTION_UNKNOWN;
                    break;
                }

                call_info_list = g_list_append (call_info_list, call_info);
            }
        }
    }

    *out_call_info_list = call_info_list;
    return TRUE;
}

static void
modem_voice_load_call_list_ready (QmiClientVoice *client,
                                  GAsyncResult   *res,
                                  GTask          *task)
{
    g_autoptr(QmiMessageVoiceGetAllCallInfoOutput)  output = NULL;
    GError                                         *error = NULL;
    GList                                          *call_info_list = NULL;

    /* Parse QMI message */
    output = qmi_client_voice_get_all_call_info_finish (client, res, &error);

    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_get_all_call_info_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't run Get All Call Info action: ");
        g_task_return_error (task, error);
    } else if (!process_get_all_call_info (client, output, &call_info_list, &error)) {
        g_prefix_error (&error, "Couldn't process Get All Call Info action: ");
        g_task_return_error (task, error);
    } else
        g_task_return_pointer (task, call_info_list, (GDestroyNotify)mm_3gpp_call_info_list_free);

    g_object_unref (task);
}

static void
modem_voice_load_call_list (MMIfaceModemVoice   *self,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    QmiClient *client = NULL;
    GTask *task;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Update call list through QMI instead of AT+CLCC */
    qmi_client_voice_get_all_call_info (QMI_CLIENT_VOICE (client),
                                        NULL, /* no input data */
                                        10,
                                        NULL,
                                        (GAsyncReadyCallback) modem_voice_load_call_list_ready,
                                        task);
}

/*****************************************************************************/
/* Create CALL (Voice interface) */

static MMBaseCall *
modem_voice_create_call (MMIfaceModemVoice *self,
                         MMCallDirection    direction,
                         const gchar       *number)
{
    return mm_call_qmi_new (MM_BASE_MODEM (self),
                            direction,
                            number);
}

/*****************************************************************************/
/* Common manage calls (Voice interface) */

static gboolean
common_manage_calls_finish (MMIfaceModemVoice  *self,
                            GAsyncResult       *res,
                            GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_manage_calls_ready (QmiClientVoice *client,
                           GAsyncResult   *res,
                           GTask          *task)
{
    g_autoptr(QmiMessageVoiceManageCallsOutput) output = NULL;
    GError     *error = NULL;

    output = qmi_client_voice_manage_calls_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_manage_calls_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't process manage calls action: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
common_manage_calls (MMIfaceModemVoice               *self,
                     GAsyncReadyCallback              callback,
                     gpointer                         user_data,
                     QmiMessageVoiceManageCallsInput *input)
{
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!input) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Cannot perform call management operation: invalid input");
        g_object_unref (task);
        return;
    }

    qmi_client_voice_manage_calls (QMI_CLIENT_VOICE (client),
                                   input,
                                   10,
                                   NULL,
                                   (GAsyncReadyCallback) common_manage_calls_ready,
                                   task);
}

/*****************************************************************************/
/* Hold and accept (Voice interface) */

static gboolean
modem_voice_hold_and_accept_finish (MMIfaceModemVoice  *self,
                                    GAsyncResult       *res,
                                    GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_hold_and_accept (MMIfaceModemVoice   *self,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;

    input = qmi_message_voice_manage_calls_input_new ();
    qmi_message_voice_manage_calls_input_set_service_type (
        input,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_HOLD_ACTIVE_ACCEPT_WAITING_OR_HELD,
        NULL);

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Hangup and accept (Voice interface) */

static gboolean
modem_voice_hangup_and_accept_finish (MMIfaceModemVoice  *self,
                                      GAsyncResult       *res,
                                      GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_hangup_and_accept (MMIfaceModemVoice   *self,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;

    input = qmi_message_voice_manage_calls_input_new ();
    qmi_message_voice_manage_calls_input_set_service_type (
        input,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_RELEASE_ACTIVE_ACCEPT_HELD_OR_WAITING,
        NULL);

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Hangup all (Voice interface) */

static gboolean
modem_voice_hangup_all_finish (MMIfaceModemVoice  *self,
                               GAsyncResult       *res,
                               GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_hangup_all (MMIfaceModemVoice   *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;

    input = qmi_message_voice_manage_calls_input_new ();
    qmi_message_voice_manage_calls_input_set_service_type (
        input,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_END_ALL_CALLS,
        NULL);

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Join multiparty (Voice interface) */

static gboolean
modem_voice_join_multiparty_finish (MMIfaceModemVoice  *self,
                                    GAsyncResult       *res,
                                    GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_join_multiparty (MMIfaceModemVoice   *self,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;

    input = qmi_message_voice_manage_calls_input_new ();
    qmi_message_voice_manage_calls_input_set_service_type (
        input,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_MAKE_CONFERENCE_CALL,
        NULL);

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Leave multiparty (Voice interface) */

static gboolean
modem_voice_leave_multiparty_finish (MMIfaceModemVoice  *self,
                                     GAsyncResult       *res,
                                     GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_leave_multiparty (MMIfaceModemVoice   *self,
                              MMBaseCall          *call,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;
    guint      idx = 0;

    idx = mm_base_call_get_index (call);
    if (idx != 0) {
        input = qmi_message_voice_manage_calls_input_new ();
        qmi_message_voice_manage_calls_input_set_call_id (
            input,
            idx,
            NULL );
        qmi_message_voice_manage_calls_input_set_service_type (
            input,
            QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_HOLD_ALL_EXCEPT_SPECIFIED_CALL,
            NULL);
    }

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Transfer (Voice interface) */

static gboolean
modem_voice_transfer_finish (MMIfaceModemVoice  *self,
                             GAsyncResult       *res,
                             GError            **error)
{
    return common_manage_calls_finish (self, res, error);
}

static void
modem_voice_transfer (MMIfaceModemVoice   *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceManageCallsInput) input = NULL;

    input = qmi_message_voice_manage_calls_input_new ();
    qmi_message_voice_manage_calls_input_set_service_type (
        input,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_TYPE_EXPLICIT_CALL_TRANSFER,
        NULL);

    common_manage_calls (self, callback, user_data, input);
}

/*****************************************************************************/
/* Call waiting setup (Voice interface) */

static gboolean
modem_voice_call_waiting_setup_finish (MMIfaceModemVoice  *self,
                                       GAsyncResult       *res,
                                       GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
call_waiting_setup_ready (QmiClientVoice *client,
                          GAsyncResult   *res,
                          GTask          *task)
{
    g_autoptr(QmiMessageVoiceSetSupplementaryServiceOutput) output = NULL;
    GError *error = NULL;

    output = qmi_client_voice_set_supplementary_service_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_set_supplementary_service_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't setup call waiting: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
modem_voice_call_waiting_setup (MMIfaceModemVoice   *self,
                                gboolean             enable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceSetSupplementaryServiceInput) input = NULL;
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_voice_set_supplementary_service_input_new ();
    qmi_message_voice_set_supplementary_service_input_set_supplementary_service_information (
        input,
        enable ? QMI_VOICE_SUPPLEMENTARY_SERVICE_ACTION_ACTIVATE : QMI_VOICE_SUPPLEMENTARY_SERVICE_ACTION_DEACTIVATE,
        QMI_VOICE_SUPPLEMENTARY_SERVICE_REASON_CALL_WAITING,
        NULL);

    qmi_client_voice_set_supplementary_service (QMI_CLIENT_VOICE (client),
                                                input,
                                                30,
                                                NULL,
                                                (GAsyncReadyCallback) call_waiting_setup_ready,
                                                task);
}

/*****************************************************************************/
/* Call waiting query (Voice interface) */

typedef enum {
    CLASS_NONE  = 0x00,
    CLASS_VOICE = 0x01,
    CLASS_DATA  = 0x02,
    CLASS_FAX   = 0x04,
    CLASS_SMS   = 0x08,
    CLASS_DATACIRCUITSYNC  = 0x10,
    CLASS_DATACIRCUITASYNC = 0x20,
    CLASS_PACKETACCESS     = 0x40,
    CLASS_PADACCESS        = 0x80
} SupplementaryServiceInformationClass;

typedef enum {
    ALL_TELESERVICES                = CLASS_VOICE + CLASS_FAX + CLASS_SMS,
    ALL_DATA_TELESERVICES           = CLASS_FAX + CLASS_SMS,
    ALL_TELESERVICES_EXCEPT_SMS     = CLASS_VOICE + CLASS_FAX,
    ALL_BEARER_SERVICES             = CLASS_DATACIRCUITSYNC + CLASS_DATACIRCUITASYNC,
    ALL_ASYNC_SERVICES              = CLASS_DATACIRCUITASYNC + CLASS_PACKETACCESS,
    ALL_SYNC_SERVICES               = CLASS_DATACIRCUITSYNC + CLASS_PACKETACCESS,
    ALL_SYNC_SERVICES_AND_TELEPHONY = CLASS_DATACIRCUITSYNC + CLASS_VOICE
} SupplementaryServiceInformationClassCombination;

static gboolean
modem_voice_call_waiting_query_finish (MMIfaceModemVoice  *self,
                                       GAsyncResult       *res,
                                       gboolean           *status,
                                       GError            **error)
{
    gboolean  ret;
    GError   *inner_error = NULL;

    ret = g_task_propagate_boolean (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *status = ret;
    return TRUE;
}

static void
call_wait_query_ready (QmiClientVoice *client,
                       GAsyncResult   *res,
                       GTask          *task)
{
    g_autoptr(QmiMessageVoiceGetCallWaitingOutput) output = NULL;
    GError *error = NULL;

    output = qmi_client_voice_get_call_waiting_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_get_call_waiting_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't query call waiting: ");
        g_task_return_error (task, error);
    } else {
        guint8 service_class = 0;

        if (!qmi_message_voice_get_call_waiting_output_get_service_class (output, &service_class, &error)) {
            g_prefix_error (&error, "Couldn't get call waiting service class: ");
            g_task_return_error (task, error);
        } else if (service_class == CLASS_VOICE || service_class == ALL_TELESERVICES || service_class == ALL_TELESERVICES_EXCEPT_SMS || service_class == ALL_SYNC_SERVICES_AND_TELEPHONY) {
            g_task_return_boolean (task, TRUE);
        } else {
            g_task_return_boolean (task, FALSE);
        }
    }

    g_object_unref (task);
}

static void
modem_voice_call_waiting_query (MMIfaceModemVoice   *self,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    g_autoptr(QmiMessageVoiceGetCallWaitingInput) input = NULL;
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_VOICE, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_voice_get_call_waiting_input_new ();
    qmi_client_voice_get_call_waiting (QMI_CLIENT_VOICE (client),
                                       input,
                                       30,
                                       NULL,
                                       (GAsyncReadyCallback) call_wait_query_ready,
                                       task);
}

/*****************************************************************************/
/* Initial EPS bearer info loading */

static MMBearerProperties *
modem_3gpp_load_initial_eps_bearer_finish (MMIfaceModem3gpp  *self,
                                           GAsyncResult      *res,
                                           GError           **error)
{
    return MM_BEARER_PROPERTIES (g_task_propagate_pointer (G_TASK (res), error));
}

static void
get_lte_attach_parameters_ready (QmiClientWds *client,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    g_autoptr(QmiMessageWdsGetLteAttachParametersOutput) output = NULL;
    GError              *error = NULL;
    MMBearerProperties  *properties;
    const gchar         *apn;
    QmiWdsIpSupportType  ip_support_type;

    output = qmi_client_wds_get_lte_attach_parameters_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_wds_get_lte_attach_parameters_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get LTE attach parameters: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    properties = mm_bearer_properties_new ();
    if (qmi_message_wds_get_lte_attach_parameters_output_get_apn (output, &apn, NULL))
        mm_bearer_properties_set_apn (properties, apn);
    if (qmi_message_wds_get_lte_attach_parameters_output_get_ip_support_type (output, &ip_support_type, NULL)) {
        MMBearerIpFamily ip_family;

        ip_family = mm_bearer_ip_family_from_qmi_ip_support_type (ip_support_type);
        if (ip_family != MM_BEARER_IP_FAMILY_NONE)
            mm_bearer_properties_set_ip_type (properties, ip_family);
    }
    g_task_return_pointer (task, properties, g_object_unref);
    g_object_unref (task);
}

static void
modem_3gpp_load_initial_eps_bearer (MMIfaceModem3gpp    *self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    GTask     *task;
    QmiClient *client;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_WDS, &client,
                                      callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    qmi_client_wds_get_lte_attach_parameters (QMI_CLIENT_WDS (client),
                                              NULL,
                                              10,
                                              NULL,
                                              (GAsyncReadyCallback) get_lte_attach_parameters_ready,
                                              task);
}

/*****************************************************************************/
/* Initial EPS bearer settings setting */

typedef enum {
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LOAD_POWER_STATE,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_MODIFY_PROFILE,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_UP,
    SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST_SETTING,
} SetInitialEpsBearerSettingsStep;

typedef struct {
    SetInitialEpsBearerSettingsStep  step;
    MM3gppProfile                   *profile;
    MMModemPowerState                power_state;
} SetInitialEpsBearerSettingsContext;

static void
set_initial_eps_bearer_settings_context_free (SetInitialEpsBearerSettingsContext *ctx)
{
    g_clear_object (&ctx->profile);
    g_slice_free (SetInitialEpsBearerSettingsContext, ctx);
}

static gboolean
modem_3gpp_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_initial_eps_bearer_settings_step (GTask *task);

static void
set_initial_eps_bearer_power_up_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    GError                             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!modem_power_up_down_off_finish (self, res, &error)) {
        g_prefix_error (&error, "Couldn't power up modem: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_bearer_settings_step (task);
}

static void
set_initial_eps_bearer_modify_profile_ready (MMIfaceModem3gppProfileManager *self,
                                             GAsyncResult                   *res,
                                             GTask                          *task)
{
    GError                             *error = NULL;
    SetInitialEpsBearerSettingsContext *ctx;
    g_autoptr(MM3gppProfile)            stored = NULL;

    ctx = g_task_get_task_data (task);

    stored = mm_iface_modem_3gpp_profile_manager_set_profile_finish (self, res, &error);
    if (!stored) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_bearer_settings_step (task);
}

static void
set_initial_eps_bearer_modify_profile (GTask *task)
{
    MMBroadbandModemQmi                *self;
    SetInitialEpsBearerSettingsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_iface_modem_3gpp_profile_manager_set_profile (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
                                                     ctx->profile,
                                                     "profile-id",
                                                     TRUE,
                                                     (GAsyncReadyCallback)set_initial_eps_bearer_modify_profile_ready,
                                                     task);
}

static void
set_initial_eps_bearer_power_down_ready (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    GError                             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!modem_power_up_down_off_finish (self, res, &error)) {
        g_prefix_error (&error, "Couldn't power down modem: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_bearer_settings_step (task);
}

static void
set_initial_eps_bearer_load_power_state_ready (MMIfaceModem *self,
                                               GAsyncResult *res,
                                               GTask        *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    GError                             *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->power_state = load_power_state_finish (self, res, &error);
    if (ctx->power_state == MM_MODEM_POWER_STATE_UNKNOWN) {
        g_prefix_error (&error, "Couldn't load power state: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->step++;
    set_initial_eps_bearer_settings_step (task);
}

static void
set_initial_eps_bearer_settings_step (GTask *task)
{
    SetInitialEpsBearerSettingsContext *ctx;
    MMBroadbandModemQmi                *self;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST:
            ctx->step++;
            /* fall through */

        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LOAD_POWER_STATE:
            mm_obj_dbg (self, "querying current power state...");
            load_power_state (MM_IFACE_MODEM (self),
                              (GAsyncReadyCallback) set_initial_eps_bearer_load_power_state_ready,
                              task);
            return;

        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN:
            if (ctx->power_state == MM_MODEM_POWER_STATE_ON) {
                mm_obj_dbg (self, "powering down before changing initial EPS bearer settings...");
                modem_power_down (MM_IFACE_MODEM (self),
                                  (GAsyncReadyCallback) set_initial_eps_bearer_power_down_ready,
                                  task);
                return;
            }
            ctx->step++;
            /* fall through */

        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_MODIFY_PROFILE:
            mm_obj_dbg (self, "modifying initial EPS bearer settings profile...");
            set_initial_eps_bearer_modify_profile (task);
            return;

        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_UP:
            if (ctx->power_state == MM_MODEM_POWER_STATE_ON) {
                mm_obj_dbg (self, "powering up after changing initial EPS bearer settings...");
                modem_power_up (MM_IFACE_MODEM (self),
                                (GAsyncReadyCallback) set_initial_eps_bearer_power_up_ready,
                                task);
                return;
            }
            ctx->step++;
            /* fall through */

        case SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST_SETTING:
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        default:
            g_assert_not_reached ();
    }
}

static void
modem_3gpp_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                            MMBearerProperties  *config,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    MMBroadbandModemQmi                *self = MM_BROADBAND_MODEM_QMI (_self);
    SetInitialEpsBearerSettingsContext *ctx;
    GTask                              *task;
    MM3gppProfile                      *profile;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->default_attach_pdn) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Unknown default LTE attach APN index");
        g_object_unref (task);
        return;
    }

    profile = mm_bearer_properties_peek_3gpp_profile (config);
    mm_3gpp_profile_set_profile_id (profile, self->priv->default_attach_pdn);

    ctx = g_slice_new0 (SetInitialEpsBearerSettingsContext);
    ctx->profile = g_object_ref (profile);
    ctx->step = SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_initial_eps_bearer_settings_context_free);

    set_initial_eps_bearer_settings_step (task);
}

/*****************************************************************************/
/* Initial EPS bearer settings loading */

static MMBearerProperties *
modem_3gpp_load_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                    GAsyncResult      *res,
                                                    GError           **error)
{
    return MM_BEARER_PROPERTIES (g_task_propagate_pointer (G_TASK (res), error));
}

static void
load_initial_eps_bearer_get_profile_ready (MMIfaceModem3gppProfileManager *_self,
                                           GAsyncResult                   *res,
                                           GTask                          *task)
{
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;
    MMBearerProperties       *properties;

    profile = mm_iface_modem_3gpp_profile_manager_get_profile_finish (_self, res, &error);
    if (!profile) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    properties = mm_bearer_properties_new_from_profile (profile, &error);
    if (!properties)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, properties, g_object_unref);
    g_object_unref (task);
}

static void
load_initial_eps_bearer_get_profile_settings (GTask *task)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    mm_iface_modem_3gpp_profile_manager_get_profile (
        MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self),
        self->priv->default_attach_pdn,
        (GAsyncReadyCallback)load_initial_eps_bearer_get_profile_ready,
        task);
}

static void
load_initial_eps_bearer_get_lte_attach_pdn_list_ready (QmiClientWds *client,
                                                       GAsyncResult *res,
                                                       GTask        *task)
{
    g_autoptr(QmiMessageWdsGetLteAttachPdnListOutput) output = NULL;
    MMBroadbandModemQmi *self;
    GError              *error = NULL;
    GArray              *current_list = NULL;
    guint                i;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_get_lte_attach_pdn_list_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_wds_get_lte_attach_pdn_list_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get LTE attach PDN list: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    qmi_message_wds_get_lte_attach_pdn_list_output_get_current_list (output, &current_list, NULL);
    if (!current_list || !current_list->len) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Undefined list of LTE attach PDN");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "Found %u LTE attach PDNs defined", current_list->len);
    for (i = 0; i < current_list->len; i++) {
        if (i == 0) {
            self->priv->default_attach_pdn = g_array_index (current_list, guint16, i);
            mm_obj_dbg (self, "Default LTE attach PDN profile: %u", self->priv->default_attach_pdn);
        } else
            mm_obj_dbg (self, "Additional LTE attach PDN profile: %u", g_array_index (current_list, guint16, i));
    }

    load_initial_eps_bearer_get_profile_settings (task);
}

static void
modem_3gpp_load_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);
    GTask               *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Default attach PDN is assumed to never change during runtime
     * (we don't change it) so just load it the first time */
    if (!self->priv->default_attach_pdn) {
        QmiClient *client;
        GError    *error = NULL;

        client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                            QMI_SERVICE_WDS,
                                            MM_PORT_QMI_FLAG_DEFAULT,
                                            &error);
        if (!client) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        mm_obj_dbg (self, "querying LTE attach PDN list...");
        qmi_client_wds_get_lte_attach_pdn_list (QMI_CLIENT_WDS (client),
                                                NULL,
                                                10,
                                                NULL,
                                                (GAsyncReadyCallback)load_initial_eps_bearer_get_lte_attach_pdn_list_ready,
                                                task);
        return;
    }

    load_initial_eps_bearer_get_profile_settings (task);
}

/*****************************************************************************/
/* Check firmware support (Firmware interface) */

typedef struct {
    gchar    *build_id;
    GArray   *modem_unique_id;
    GArray   *pri_unique_id;
    gboolean  current;
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
    QmiClientDms         *client;
    GList                *pairs;
    FirmwarePair         *current_pair;
    MMFirmwareProperties *current_firmware;
    gboolean              skip_image_info;
} FirmwareListPreloadContext;

static void
firmware_list_preload_context_free (FirmwareListPreloadContext *ctx)
{
    g_clear_object (&ctx->current_firmware);
    g_clear_pointer (&ctx->current_pair, (GDestroyNotify)firmware_pair_free);
    g_list_free_full (ctx->pairs, (GDestroyNotify)firmware_pair_free);
    g_object_unref (ctx->client);
    g_slice_free (FirmwareListPreloadContext, ctx);
}

static gboolean
firmware_list_preload_finish (MMBroadbandModemQmi  *self,
                              GAsyncResult         *res,
                              GError              **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
store_preloaded_firmware_image_info (MMBroadbandModemQmi  *self,
                                     MMFirmwareProperties *firmware,
                                     gboolean              running)
{
    self->priv->firmware_list = g_list_append (self->priv->firmware_list, g_object_ref (firmware));

    /* If this is is also the running image, keep an extra reference to it */
    if (running) {
        if (self->priv->current_firmware)
            mm_obj_warn (self, "a running firmware is already set (%s), not setting '%s'",
                         mm_firmware_properties_get_unique_id (self->priv->current_firmware),
                         mm_firmware_properties_get_unique_id (firmware));
        else
            self->priv->current_firmware = g_object_ref (firmware);
    }
}

static void get_next_image_info (GTask *task);

static void
get_pri_image_info_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemQmi                   *self;
    FirmwareListPreloadContext            *ctx;
    QmiMessageDmsGetStoredImageInfoOutput *output;
    GError                                *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->current_pair);
    g_assert (ctx->current_firmware);

    output = qmi_client_dms_get_stored_image_info_finish (client, res, &error);
    if (!output || !qmi_message_dms_get_stored_image_info_output_get_result (output, &error)) {
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND))
            ctx->skip_image_info = TRUE;
        else
            mm_obj_dbg (self, "couldn't get detailed info for PRI image with build ID '%s': %s",
                        ctx->current_pair->build_id, error->message);
        g_error_free (error);
        goto out;
    }

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
            mm_firmware_properties_set_gobi_boot_version (ctx->current_firmware, aux);
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
            mm_firmware_properties_set_gobi_pri_version (ctx->current_firmware, aux);
            g_free (aux);

            mm_firmware_properties_set_gobi_pri_info (ctx->current_firmware, pri_info);
        }
    }

out:

    /* We're done with this image */
    store_preloaded_firmware_image_info (self, ctx->current_firmware, ctx->current_pair->current);
    g_clear_object (&ctx->current_firmware);
    g_clear_pointer (&ctx->current_pair, (GDestroyNotify)firmware_pair_free);

    /* Go on to the next one */
    get_next_image_info (task);

    if (output)
        qmi_message_dms_get_stored_image_info_output_unref (output);
}

static MMFirmwareProperties *
create_firmware_properties_from_pair (FirmwarePair  *pair,
                                      GError       **error)
{
    gchar                *pri_unique_id_str = NULL;
    gchar                *modem_unique_id_str = NULL;
    gchar                *firmware_unique_id_str = NULL;
    MMFirmwareProperties *firmware = NULL;

    /* If the string is ASCII, use it without converting to HEX */

    pri_unique_id_str = mm_qmi_unique_id_to_firmware_unique_id (pair->pri_unique_id, error);
    if (!pri_unique_id_str)
        goto out;

    modem_unique_id_str = mm_qmi_unique_id_to_firmware_unique_id (pair->modem_unique_id, error);
    if (!modem_unique_id_str)
        goto out;

    /* We will always append the PRI unique ID to the build id to form the unique id
     * used by the API, because it may happen that a device holds multiple PRI images
     * for the same build ID.
     *
     * E.g. we could have a single modem image (e.g. 02.14.03.00) and then two or more
     * different PRI images with the same build ID (e.g. 02.14.03.00_VODAFONE) but
     * different unique IDs (e.g. 000.008_000 and 000.016_000).
     */
    firmware_unique_id_str = g_strdup_printf ("%s_%s", pair->build_id, pri_unique_id_str);

    firmware = mm_firmware_properties_new (MM_FIRMWARE_IMAGE_TYPE_GOBI, firmware_unique_id_str);
    mm_firmware_properties_set_gobi_pri_unique_id (firmware, pri_unique_id_str);
    mm_firmware_properties_set_gobi_modem_unique_id (firmware, modem_unique_id_str);

out:
    g_free (firmware_unique_id_str);
    g_free (pri_unique_id_str);
    g_free (modem_unique_id_str);

    return firmware;
}

static void
get_next_image_info (GTask *task)
{
    MMBroadbandModemQmi        *self;
    FirmwareListPreloadContext *ctx;
    GError                     *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->pairs) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Take next pair to process from list head */
    ctx->current_pair = (FirmwarePair *)ctx->pairs->data;
    ctx->pairs = g_list_delete_link (ctx->pairs, ctx->pairs);

    /* Build firmware properties */
    ctx->current_firmware = create_firmware_properties_from_pair (ctx->current_pair, &error);
    if (!ctx->current_firmware) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now, load additional optional information for the PRI image */
    if (!ctx->skip_image_info) {
        g_autoptr(QmiMessageDmsGetStoredImageInfoInput) input = NULL;

        input = qmi_message_dms_get_stored_image_info_input_new ();
        qmi_message_dms_get_stored_image_info_input_set_image_details (input,
                                                                       QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI,
                                                                       ctx->current_pair->pri_unique_id,
                                                                       ctx->current_pair->build_id,
                                                                       NULL);
        qmi_client_dms_get_stored_image_info (ctx->client,
                                              input,
                                              10,
                                              NULL,
                                              (GAsyncReadyCallback)get_pri_image_info_ready,
                                              task);
        return;
    }

    /* If we shouldn't be loading additional image info, we're done with this image */
    store_preloaded_firmware_image_info (self, ctx->current_firmware, ctx->current_pair->current);
    g_clear_object (&ctx->current_firmware);
    g_clear_pointer (&ctx->current_pair, (GDestroyNotify)firmware_pair_free);

    /* Go on to the next one */
    get_next_image_info (task);
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

static GList *
find_image_pairs (MMBroadbandModemQmi                           *self,
                  QmiMessageDmsListStoredImagesOutputListImage  *image_pri,
                  QmiMessageDmsListStoredImagesOutputListImage  *image_modem,
                  GError                                       **error)
{
    guint  i, j;
    GList *pairs = NULL;

    /* Loop PRI images and try to find a pairing MODEM image with same build ID */
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

                mm_obj_dbg (self, "found pairing PRI+MODEM images with build ID '%s'", subimage_pri->build_id);
                pair = g_slice_new (FirmwarePair);
                pair->build_id = g_strdup (subimage_pri->build_id);
                pair->modem_unique_id = g_array_ref (subimage_modem->unique_id);
                pair->pri_unique_id = g_array_ref (subimage_pri->unique_id);

                /* We're using the PRI 'index_of_running_image' only as source to select
                 * which is the current running firmware. This avoids issues with the wrong
                 * 'index_of_running_image' reported for the MODEM images, see:
                 *   https://forum.sierrawireless.com/t/mc74xx-wrong-running-image-in-qmi-get-stored-images/8998
                 */
                pair->current = (image_pri->index_of_running_image == i ? TRUE : FALSE);

                pairs = g_list_append (pairs, pair);
                break;
            }
        }

        if (j == image_modem->sublist->len)
            mm_obj_dbg (self, "pairing for PRI image with build ID '%s' not found", subimage_pri->build_id);
    }

    if (!pairs)
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND, "No valid PRI+MODEM pairs found");

    return pairs;
}

static gboolean
find_image_type_indices (MMBroadbandModemQmi                           *self,
                         GArray                                        *array,
                         QmiMessageDmsListStoredImagesOutputListImage **image_pri,
                         QmiMessageDmsListStoredImagesOutputListImage **image_modem,
                         GError                                       **error)
{
    guint i;

    g_assert (array);
    g_assert (image_pri);
    g_assert (image_modem);

    *image_pri = NULL;
    *image_modem = NULL;

    /* The MODEM image list is usually given before the PRI image list, but
     * let's not assume that. Try to find both lists and report at which index
     * we can find each. */

    for (i = 0; i < array->len; i++) {
        QmiMessageDmsListStoredImagesOutputListImage *image;

        image = &g_array_index (array, QmiMessageDmsListStoredImagesOutputListImage, i);
        switch (image->type) {
        case QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI:
            if (*image_pri != NULL)
                mm_obj_dbg (self, "multiple array elements found with PRI type: ignoring additional list at index %u", i);
            else
                *image_pri = image;
            break;
        case QMI_DMS_FIRMWARE_IMAGE_TYPE_MODEM:
            if (*image_modem != NULL)
                mm_obj_dbg (self, "multiple array elements found with MODEM type: ignoring additional list at index %u", i);
            else
                *image_modem = image;
            break;
        default:
            break;
        }
    }

    if (!(*image_pri) || !(*image_modem)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                     "Missing image list: pri list %s, modem list %s",
                     !(*image_pri) ? "not found" : "found",
                     !(*image_modem) ? "not found" : "found");
        return FALSE;
    }

    return TRUE;
}

static void
list_stored_images_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemQmi                          *self;
    FirmwareListPreloadContext                   *ctx;
    GArray                                       *array;
    QmiMessageDmsListStoredImagesOutputListImage *image_pri;
    QmiMessageDmsListStoredImagesOutputListImage *image_modem;
    QmiMessageDmsListStoredImagesOutput          *output;
    GError                                       *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Read array from output */
    output = qmi_client_dms_list_stored_images_finish (client, res, &error);
    if (!output ||
        !qmi_message_dms_list_stored_images_output_get_result (output, &error) ||
        !qmi_message_dms_list_stored_images_output_get_list (output, &array, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* Find which index corresponds to each image type */
    if (!find_image_type_indices (self, array, &image_pri, &image_modem, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* Build firmware PRI+MODEM pair list */
    ctx->pairs = find_image_pairs (self, image_pri, image_modem, &error);
    if (!ctx->pairs) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* Now keep on loading info for each image and cache it */
    get_next_image_info (task);

out:

    if (output)
        qmi_message_dms_list_stored_images_output_unref (output);
}

static void
firmware_list_preload (MMBroadbandModemQmi *self,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    FirmwareListPreloadContext *ctx;
    GTask                      *task;
    QmiClient                  *client = NULL;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_DMS, &client,
                                      callback, user_data))
        return;

    ctx = g_slice_new0 (FirmwareListPreloadContext);
    ctx->client = QMI_CLIENT_DMS (g_object_ref (client));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)firmware_list_preload_context_free);

    mm_obj_dbg (self, "loading firmware images...");
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
firmware_load_list_preloaded (GTask *task)
{
    MMBroadbandModemQmi *self;
    GList               *dup;

    self = g_task_get_source_object (task);
    g_assert (self->priv->firmware_list_preloaded);

    dup = g_list_copy_deep (self->priv->firmware_list, (GCopyFunc)g_object_ref, NULL);
    if (dup)
        g_task_return_pointer (task, dup, (GDestroyNotify)firmware_list_free);
    else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "firmware list unknown");
    g_object_unref (task);
}

static void
firmware_list_preload_ready (MMBroadbandModemQmi *self,
                             GAsyncResult        *res,
                             GTask               *task)
{
    GError *error = NULL;

    if (!firmware_list_preload_finish (self, res, &error)) {
        mm_obj_dbg (self, "firmware list loading failed: %s", error ? error->message : "unsupported");
        g_clear_error (&error);
    }

    firmware_load_list_preloaded (task);
}

static void
firmware_load_list (MMIfaceModemFirmware *_self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (_self);

    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->firmware_list_preloaded) {
        firmware_load_list_preloaded (task);
        return;
    }

    self->priv->firmware_list_preloaded = TRUE;
    firmware_list_preload (self,
                           (GAsyncReadyCallback)firmware_list_preload_ready,
                           task);
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
firmware_reset_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;

    if (!mm_shared_qmi_reset_finish (self, res, &error))
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
    mm_shared_qmi_reset (MM_IFACE_MODEM (self),
                         (GAsyncReadyCallback)firmware_reset_ready,
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
                         const gchar          *unique_id,
                         GAsyncReadyCallback   callback,
                         gpointer              user_data)
{
    MMBroadbandModemQmi                              *self;
    GTask                                            *task;
    FirmwareChangeCurrentContext                     *ctx;
    GError                                           *error = NULL;
    QmiClient                                        *client = NULL;
    GArray                                           *array;
    QmiMessageDmsSetFirmwarePreferenceInput          *input = NULL;
    QmiMessageDmsSetFirmwarePreferenceInputListImage  modem_image_id = { 0 };
    QmiMessageDmsSetFirmwarePreferenceInputListImage  pri_image_id   = { 0 };

    self = MM_BROADBAND_MODEM_QMI (_self);
    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
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
        mm_obj_dbg (self, "modem is already running firmware image '%s'",
                mm_firmware_properties_get_unique_id (self->priv->current_firmware));
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Modem image ID */
    modem_image_id.type = QMI_DMS_FIRMWARE_IMAGE_TYPE_MODEM;
    modem_image_id.build_id = (gchar *)mm_firmware_properties_get_unique_id (ctx->firmware);
    modem_image_id.unique_id = mm_firmware_unique_id_to_qmi_unique_id (mm_firmware_properties_get_gobi_modem_unique_id (ctx->firmware), &error);
    if (!modem_image_id.unique_id) {
        g_prefix_error (&error, "Couldn't build modem image unique id: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* PRI image ID */
    pri_image_id.type = QMI_DMS_FIRMWARE_IMAGE_TYPE_PRI;
    pri_image_id.build_id = (gchar *)mm_firmware_properties_get_unique_id (ctx->firmware);
    pri_image_id.unique_id = mm_firmware_unique_id_to_qmi_unique_id (mm_firmware_properties_get_gobi_pri_unique_id (ctx->firmware), &error);
    if (!pri_image_id.unique_id) {
        g_prefix_error (&error, "Couldn't build PRI image unique id: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    mm_obj_dbg (self, "changing Gobi firmware to MODEM '%s' and PRI '%s' with Build ID '%s'...",
                mm_firmware_properties_get_gobi_modem_unique_id (ctx->firmware),
                mm_firmware_properties_get_gobi_pri_unique_id (ctx->firmware),
                unique_id);

    /* Build array of image IDs */
    array = g_array_sized_new (FALSE, FALSE, sizeof (QmiMessageDmsSetFirmwarePreferenceInputListImage), 2);
    g_array_append_val (array, modem_image_id);
    g_array_append_val (array, pri_image_id);

    input = qmi_message_dms_set_firmware_preference_input_new ();
    qmi_message_dms_set_firmware_preference_input_set_list (input, array, NULL);
    g_array_unref (array);

    qmi_client_dms_set_firmware_preference (
        QMI_CLIENT_DMS (client),
        input,
        10,
        NULL,
        (GAsyncReadyCallback)firmware_select_stored_image_ready,
        task);

out:
    if (modem_image_id.unique_id)
        g_array_unref (modem_image_id.unique_id);
    if (pri_image_id.unique_id)
        g_array_unref (pri_image_id.unique_id);
    if (input)
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
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If NAS service is available, assume either signal info or signal strength are supported */
    if (!mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                    QMI_SERVICE_NAS,
                                    MM_PORT_QMI_FLAG_DEFAULT,
                                    NULL)) {
        mm_obj_dbg (self, "extended signal capabilities not supported");
        g_task_return_boolean (task, FALSE);
    } else {
        mm_obj_dbg (self, "extended signal capabilities supported");
        g_task_return_boolean (task, TRUE);
    }
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
    MMSignal *nr5g;
} SignalLoadValuesResult;

typedef struct {
    QmiClientNas           *client;
    SignalLoadValuesStep    step;
    SignalLoadValuesResult *values_result;
} SignalLoadValuesContext;

static void
signal_load_values_result_free (SignalLoadValuesResult *result)
{
    g_clear_object (&result->cdma);
    g_clear_object (&result->evdo);
    g_clear_object (&result->gsm);
    g_clear_object (&result->umts);
    g_clear_object (&result->lte);
    g_clear_object (&result->nr5g);
    g_slice_free (SignalLoadValuesResult, result);
}

static void
signal_load_values_context_free (SignalLoadValuesContext *ctx)
{
    g_clear_pointer (&ctx->values_result, (GDestroyNotify)signal_load_values_result_free);
    g_slice_free (SignalLoadValuesContext, ctx);
}

static gboolean
signal_load_values_finish (MMIfaceModemSignal *self,
                           GAsyncResult       *res,
                           MMSignal          **cdma,
                           MMSignal          **evdo,
                           MMSignal          **gsm,
                           MMSignal          **umts,
                           MMSignal          **lte,
                           MMSignal          **nr5g,
                           GError            **error)
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
    *nr5g = values_result->nr5g ? g_object_ref (values_result->nr5g) : NULL;
    signal_load_values_result_free (values_result);
    return TRUE;
}

static void signal_load_values_context_step (GTask *task);

static void
signal_load_values_get_signal_strength_ready (QmiClientNas *client,
                                              GAsyncResult *res,
                                              GTask        *task)
{
    MMBroadbandModemQmi                             *self;
    SignalLoadValuesContext                         *ctx;
    GArray                                          *array;
    gint32                                           aux_int32;
    gint16                                           aux_int16;
    gint8                                            aux_int8;
    QmiNasRadioInterface                             radio_interface;
    QmiNasEvdoSinrLevel                              sinr;
    g_autoptr(QmiMessageNasGetSignalStrengthOutput)  output = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_nas_get_signal_strength_finish (client, res, NULL);
    if (!output || !qmi_message_nas_get_signal_strength_output_get_result (output, NULL)) {
        /* No hard errors, go on to next step */
        ctx->step++;
        signal_load_values_context_step (task);
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
            case QMI_NAS_RADIO_INTERFACE_UNKNOWN:
            case QMI_NAS_RADIO_INTERFACE_NONE:
            case QMI_NAS_RADIO_INTERFACE_AMPS:
            case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
            case QMI_NAS_RADIO_INTERFACE_5GNR:
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
            case QMI_NAS_RADIO_INTERFACE_GSM:
            case QMI_NAS_RADIO_INTERFACE_LTE:
            case QMI_NAS_RADIO_INTERFACE_UNKNOWN:
            case QMI_NAS_RADIO_INTERFACE_NONE:
            case QMI_NAS_RADIO_INTERFACE_AMPS:
            case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
            case QMI_NAS_RADIO_INTERFACE_5GNR:
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
            mm_signal_set_sinr (ctx->values_result->evdo, get_db_from_sinr_level (self, sinr));
    }

    /* Go on */
    ctx->step++;
    signal_load_values_context_step (task);
}

static void
signal_load_values_get_signal_info_ready (QmiClientNas *client,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    MMBroadbandModemQmi     *self;
    SignalLoadValuesContext *ctx;

    g_autoptr(QmiMessageNasGetSignalInfoOutput) output = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    output = qmi_client_nas_get_signal_info_finish (client, res, NULL);
    if (!output || !qmi_message_nas_get_signal_info_output_get_result (output, NULL)) {
        /* No hard errors, go on to next step */
        ctx->step++;
        signal_load_values_context_step (task);
        return;
    }

    /* Good, we have results */
    ctx->values_result = g_slice_new0 (SignalLoadValuesResult);

    common_process_signal_info (self,
                                output,
                                NULL,
                                &ctx->values_result->cdma,
                                &ctx->values_result->evdo,
                                &ctx->values_result->gsm,
                                &ctx->values_result->umts,
                                &ctx->values_result->lte,
                                &ctx->values_result->nr5g);

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
      ctx->values_result->lte  ||    \
      ctx->values_result->nr5g))

    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_FIRST:
        ctx->step++;
        /* Fall through */

    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_INFO:
        qmi_client_nas_get_signal_info (ctx->client,
                                        NULL,
                                        5,
                                        NULL,
                                        (GAsyncReadyCallback)signal_load_values_get_signal_info_ready,
                                        task);
        return;

    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_STRENGTH:
        /* If already loaded with signal info, don't try signal strength */
        if (!VALUES_RESULT_LOADED (ctx)) {
            g_autoptr(QmiMessageNasGetSignalStrengthInput) input = NULL;

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
            return;
        }
        ctx->step++;
        /* Fall through */

    case SIGNAL_LOAD_VALUES_STEP_SIGNAL_LAST:
        /* If any result is set, succeed */
        if (VALUES_RESULT_LOADED (ctx))
            g_task_return_pointer (task,
                                   g_steal_pointer (&ctx->values_result),
                                   (GDestroyNotify)signal_load_values_result_free);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "No way to load extended signal information");
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();

#undef VALUES_RESULT_LOADED
}

static void
signal_load_values (MMIfaceModemSignal  *self,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    SignalLoadValuesContext *ctx;
    GTask                   *task;
    QmiClient               *client = NULL;

    mm_obj_dbg (self, "loading extended signal information...");

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    ctx = g_slice_new0 (SignalLoadValuesContext);
    ctx->client = QMI_CLIENT_NAS (g_object_ref (client));
    ctx->step = SIGNAL_LOAD_VALUES_STEP_SIGNAL_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)signal_load_values_context_free);

    signal_load_values_context_step (task);
}

/*****************************************************************************/
/* Setup threshold values (Signal interface) */

typedef struct {
    QmiClientNas *client;
    guint rssi_threshold;
} SignalSetupThresholdsContext;

static void
signal_setup_thresholds_context_free (SignalSetupThresholdsContext *ctx)
{
    g_clear_object (&ctx->client);
    g_slice_free (SignalSetupThresholdsContext, ctx);
}

static gboolean
signal_setup_thresholds_finish (MMIfaceModemSignal  *self,
                                GAsyncResult        *res,
                                GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
signal_setup_thresholds_config_signal_info_ready (QmiClientNas *client,
                                                  GAsyncResult *res,
                                                  GTask        *task)
{
    GError                                         *error = NULL;
    g_autoptr(QmiMessageNasConfigSignalInfoOutput)  output = NULL;

    output = qmi_client_nas_config_signal_info_finish (client, res, &error);
    if (!output || !qmi_message_nas_config_signal_info_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
config_signal_info (GTask *task)
{
    SignalSetupThresholdsContext                  *ctx;
    g_autoptr(QmiMessageNasConfigSignalInfoInput)  input = NULL;
    g_autoptr(GArray)                              rssi_thresholds = NULL;

    ctx = g_task_get_task_data (task);
    input = qmi_message_nas_config_signal_info_input_new ();

    if (ctx->rssi_threshold) {
        gint8 threshold;

        rssi_thresholds = g_array_new (FALSE, FALSE, sizeof (gint8));
        threshold = RSSI_MIN;
        while (RSSI_MIN <= threshold && threshold <= RSSI_MAX) {
            g_array_append_val (rssi_thresholds, threshold);
            threshold += ctx->rssi_threshold;
        }
    } else {
        rssi_thresholds = g_array_sized_new (FALSE, FALSE, sizeof (gint8), G_N_ELEMENTS (default_thresholds_data_dbm));
        g_array_append_vals (rssi_thresholds, default_thresholds_data_dbm, G_N_ELEMENTS (default_thresholds_data_dbm));
    }
    qmi_message_nas_config_signal_info_input_set_rssi_threshold (input, rssi_thresholds, NULL);
    qmi_client_nas_config_signal_info (ctx->client,
                                       input,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)signal_setup_thresholds_config_signal_info_ready,
                                       task);
}

static void
signal_setup_thresholds_config_signal_info_v2_ready (QmiClientNas *client,
                                                     GAsyncResult *res,
                                                     GTask        *task)
{
    MMBroadbandModemQmi                              *self;
    GError                                           *error = NULL;
    g_autoptr(QmiMessageNasConfigSignalInfoV2Output)  output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_nas_config_signal_info_v2_finish (client, res, &error);
    if (!output || !qmi_message_nas_config_signal_info_v2_output_get_result (output, &error)) {
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INVALID_QMI_COMMAND) ||
            g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NOT_SUPPORTED)) {
            mm_obj_dbg (self, "couldn't config signal info using v2: '%s', falling back to config signal info using v1",
                        error->message);
            config_signal_info (task);
            g_clear_error (&error);
            return;
        }
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
config_signal_info_v2 (GTask *task)
{
    SignalSetupThresholdsContext                    *ctx;
    g_autoptr(QmiMessageNasConfigSignalInfoV2Input)  input = NULL;
    guint                                            delta;

    ctx = g_task_get_task_data (task);

    /* delta in units of 0.1dBm */
    delta = ctx->rssi_threshold ? ctx->rssi_threshold * 10 : default_rssi_delta_dbm * 10;
    input = qmi_message_nas_config_signal_info_v2_input_new ();
    qmi_message_nas_config_signal_info_v2_input_set_cdma_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_hdr_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_gsm_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_wcdma_rssi_delta (input, delta, NULL);
    qmi_message_nas_config_signal_info_v2_input_set_lte_rssi_delta (input, delta, NULL);
    /* use RSRP for 5GNR*/
    qmi_message_nas_config_signal_info_v2_input_set_nr5g_rsrp_delta (input, delta, NULL);

    qmi_client_nas_config_signal_info_v2 (ctx->client,
                                          input,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)signal_setup_thresholds_config_signal_info_v2_ready,
                                          task);
}

static void
signal_setup_thresholds (MMIfaceModemSignal  *self,
                         guint                rssi_threshold,
                         gboolean             error_rate_threshold,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask                        *task;
    SignalSetupThresholdsContext *ctx;
    QmiClient                    *client = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (error_rate_threshold) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Setting error rate threshold not supported");
        g_object_unref (task);
        return;
    }

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_NAS, &client,
                                      callback, user_data))
        return;

    ctx = g_slice_new0 (SignalSetupThresholdsContext);
    ctx->client = QMI_CLIENT_NAS (g_object_ref (client));
    ctx->rssi_threshold = rssi_threshold;
    g_task_set_task_data (task, ctx, (GDestroyNotify)signal_setup_thresholds_context_free);

    config_signal_info_v2 (task);
}

/*****************************************************************************/
/* Reset data interfaces during initialization */

typedef struct {
    GList     *ports;
    MMPort    *data;
    MMPortQmi *qmi;
} ResetPortsContext;

static void
reset_ports_context_free (ResetPortsContext *ctx)
{
    g_assert (!ctx->data);
    g_assert (!ctx->qmi);
    g_list_free_full (ctx->ports, g_object_unref);
    g_slice_free (ResetPortsContext, ctx);
}

static gboolean
reset_ports_finish (MMBroadbandModemQmi  *self,
                    GAsyncResult         *res,
                    GError              **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void reset_next_port (GTask *task);

static void
port_qmi_reset_ready (MMPortQmi    *qmi,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemQmi *self;
    ResetPortsContext   *ctx;
    g_autoptr(GError)    error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_qmi_reset_finish (qmi, res, &error))
        mm_obj_warn (self, "couldn't reset QMI port '%s' with data interface '%s': %s",
                     mm_port_get_device (MM_PORT (ctx->qmi)),
                     mm_port_get_device (ctx->data),
                     error->message);

    g_clear_object (&ctx->data);
    g_clear_object (&ctx->qmi);
    reset_next_port (task);
}

static void
reset_next_port (GTask *task)
{
    MMBroadbandModemQmi *self;
    ResetPortsContext   *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!ctx->ports) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* steal full data port reference from list head */
    ctx->data = ctx->ports->data;
    ctx->ports = g_list_delete_link (ctx->ports, ctx->ports);

    ctx->qmi = mm_broadband_modem_qmi_get_port_qmi_for_data (self, ctx->data, NULL, NULL);
    if (!ctx->qmi) {
        mm_obj_dbg (self, "no QMI port associated to data port '%s': ignoring data interface reset",
                    mm_port_get_device (ctx->data));
        g_clear_object (&ctx->data);
        reset_next_port (task);
        return;
    }

    mm_obj_dbg (self, "running QMI port '%s' reset with data interface '%s'",
                mm_port_get_device (MM_PORT (ctx->qmi)), mm_port_get_device (ctx->data));

    mm_port_qmi_reset (ctx->qmi,
                       ctx->data,
                       (GAsyncReadyCallback) port_qmi_reset_ready,
                       task);
}

static void
reset_ports (MMBroadbandModemQmi *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    GTask             *task;
    ResetPortsContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (ResetPortsContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify)reset_ports_context_free);

    ctx->ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                           MM_PORT_SUBSYS_UNKNOWN,
                                           MM_PORT_TYPE_NET);

    reset_next_port (task);
}

/*****************************************************************************/
/* First enabling step */

static gboolean
enabling_started_finish (MMBroadbandModem  *self,
                         GAsyncResult      *res,
                         GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
wds_set_autoconnect_settings_ready (QmiClientWds *client,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    MMBroadbandModemQmi *self;
    g_autoptr(GError)    error = NULL;
    g_autoptr(QmiMessageWdsSetAutoconnectSettingsOutput) output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_set_autoconnect_settings_finish (client, res, &error);
    if (!output || !qmi_message_wds_set_autoconnect_settings_output_get_result (output, &error))
        mm_obj_warn (self, "failed disabling autoconnect: %s", error->message);
    else
        mm_obj_msg (self, "autoconnect explicitly disabled");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
wds_get_autoconnect_settings_ready (QmiClientWds *client,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    MMBroadbandModemQmi      *self;
    QmiWdsAutoconnectSetting  autoconnect_setting;
    g_autoptr(GError)         error = NULL;
    g_autoptr(QmiMessageWdsSetAutoconnectSettingsInput)  input  = NULL;
    g_autoptr(QmiMessageWdsGetAutoconnectSettingsOutput) output = NULL;

    self = g_task_get_source_object (task);

    output = qmi_client_wds_get_autoconnect_settings_finish (client, res, &error);
    if (!output ||
        !qmi_message_wds_get_autoconnect_settings_output_get_result (output, &error) ||
        !qmi_message_wds_get_autoconnect_settings_output_get_status (output, &autoconnect_setting, &error)) {
        mm_obj_warn (self, "failed checking whether autoconnect is disabled or not: %s", error->message);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (autoconnect_setting != QMI_WDS_AUTOCONNECT_SETTING_ENABLED) {
        mm_obj_dbg (self, "autoconnect is already disabled");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "need to explicitly disable autoconnect");
    input = qmi_message_wds_set_autoconnect_settings_input_new ();
    qmi_message_wds_set_autoconnect_settings_input_set_status (input, QMI_WDS_AUTOCONNECT_SETTING_DISABLED, NULL);
    qmi_client_wds_set_autoconnect_settings (client,
                                             input,
                                             10,
                                             NULL,
                                             (GAsyncReadyCallback) wds_set_autoconnect_settings_ready,
                                             task);
}

static void
parent_enabling_started_ready (MMBroadbandModem *_self,
                               GAsyncResult     *res,
                               GTask            *task)
{
    MMBroadbandModemQmi    *self = MM_BROADBAND_MODEM_QMI (_self);
    QmiClient              *client = NULL;
    g_autoptr(GError)       error = NULL;

    if (!MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->enabling_started_finish (_self, res, &error)) {
        /* Don't treat this as fatal. Parent enabling may fail if it cannot grab a primary
         * AT port, which isn't really an issue in QMI-based modems */
        mm_obj_dbg (self, "couldn't start parent enabling: %s", error->message);
    }

    /* If the autoconnect check has already been done, we're finished */
    if (self->priv->autoconnect_checked) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* The connection logic doesn't work properly when the device is already set
     * to autoconnect, so automatically disable autoconnect ourselves. */
    mm_obj_dbg (self, "need to check whether autoconnect is disabled or not...");
    self->priv->autoconnect_checked = TRUE;

    /* Use default WDS client to query autoconnect settings */
    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_WDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);

    if (!client) {
        mm_obj_warn (self, "cannot check whether autoconnect is disabled or not: couldn't peek default WDS client");
        /* not fatal, just assume autoconnect is disabled */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    qmi_client_wds_get_autoconnect_settings (QMI_CLIENT_WDS (client),
                                             NULL,
                                             5,
                                             NULL,
                                             (GAsyncReadyCallback) wds_get_autoconnect_settings_ready,
                                             task);
}

static void
enabling_started (MMBroadbandModem    *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
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

static const QmiService qmi_services[] = {
    QMI_SERVICE_DMS,
    QMI_SERVICE_NAS,
    QMI_SERVICE_WDS,
    QMI_SERVICE_WMS,
    QMI_SERVICE_PDS,
    QMI_SERVICE_OMA,
    QMI_SERVICE_UIM,
    QMI_SERVICE_LOC,
    QMI_SERVICE_PDC,
    QMI_SERVICE_VOICE,
    QMI_SERVICE_DSD,
    QMI_SERVICE_SAR,
};

typedef struct {
    MMPortQmi *qmi;
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
        mm_obj_dbg (self, "couldn't start parent initialization: %s", error->message);
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

static void allocate_next_client (GTask *task);

static void
qmi_port_allocate_client_ready (MMPortQmi *qmi,
                                GAsyncResult *res,
                                GTask *task)
{
    MMBroadbandModemQmi *self;
    InitializationStartedContext *ctx;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_qmi_allocate_client_finish (qmi, res, &error)) {
        mm_obj_dbg (self, "couldn't allocate client for service '%s': %s",
                    qmi_service_get_string (qmi_services[ctx->service_index]),
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

    ctx = g_task_get_task_data (task);

    if (ctx->service_index == G_N_ELEMENTS (qmi_services)) {
        parent_initialization_started (task);
        return;
    }

    /* Otherwise, allocate next client */
    mm_port_qmi_allocate_client (ctx->qmi,
                                 qmi_services[ctx->service_index],
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
    MMBroadbandModemQmi *self;
    InitializationStartedContext *ctx;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_qmi_open_finish (qmi, res, &error)) {
        /* Really, really old devices (Gobi 1K, 2008-era firmware) may not
         * support SetDataFormat, so if we get an error opening the port
         * try without it.  The qmi_wwan driver will fix up any issues that
         * the device might have between raw-ip and 802.3 mode anyway.
         */
        mm_obj_dbg (self, "couldn't open QMI port with data format update: %s", error->message);
        g_error_free (error);
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
initialization_open_port (GTask *task)
{
    InitializationStartedContext *ctx;

    ctx = g_task_get_task_data (task);

    mm_port_qmi_open (ctx->qmi,
                      TRUE,
                      NULL,
                      (GAsyncReadyCallback)qmi_port_open_ready,
                      task);
}

static void
reset_ports_ready (MMBroadbandModemQmi *self,
                   GAsyncResult        *res,
                   GTask               *task)
{
    GError *error = NULL;

    if (!reset_ports_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    initialization_open_port (task);
}

static void
initialization_reset_ports (GTask *task)
{
    MMBroadbandModemQmi *self;

    self = g_task_get_source_object (task);

    /* reseting the data interfaces is really only needed if the device
     * hasn't been hotplugged */
    if (mm_base_modem_get_hotplugged (MM_BASE_MODEM (self))) {
        mm_obj_dbg (self, "not running data interface reset procedure: device is hotplugged");
        initialization_open_port (task);
        return;
    }

    reset_ports (self, (GAsyncReadyCallback)reset_ports_ready, task);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;
    GTask                        *task;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->qmi = mm_broadband_modem_qmi_get_port_qmi (MM_BROADBAND_MODEM_QMI (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_started_context_free);

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->qmi) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot initialize: QMI port no longer available");
        g_object_unref (task);
        return;
    }

    if (mm_port_qmi_is_open (ctx->qmi)) {
        parent_initialization_started (task);
        return;
    }

    initialization_reset_ports (task);
}

/*****************************************************************************/

MMBroadbandModemQmi *
mm_broadband_modem_qmi_new (const gchar *device,
                            const gchar *physdev,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* QMI bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
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
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

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
    MMPortQmi *qmi;

    /* If any port cleanup is needed, it must be done during dispose(), as
     * the modem object will be affected by an explicit g_object_run_dispose()
     * that will remove all port references right away */
    qmi = mm_broadband_modem_qmi_peek_port_qmi (self);
    if (qmi) {
        /* If we did open the QMI port during initialization, close it now */
        if (mm_port_qmi_is_open (qmi))
            mm_port_qmi_close (qmi, NULL, NULL);
    }

    g_list_free_full (self->priv->firmware_list, g_object_unref);
    self->priv->firmware_list = NULL;

    g_clear_object (&self->priv->current_firmware);

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->dispose (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = mm_shared_qmi_load_current_capabilities;
    iface->load_current_capabilities_finish = mm_shared_qmi_load_current_capabilities_finish;
    iface->load_supported_capabilities = mm_shared_qmi_load_supported_capabilities;
    iface->load_supported_capabilities_finish = mm_shared_qmi_load_supported_capabilities_finish;
    iface->set_current_capabilities = mm_shared_qmi_set_current_capabilities;
    iface->set_current_capabilities_finish = mm_shared_qmi_set_current_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = mm_shared_qmi_load_model;
    iface->load_model_finish = mm_shared_qmi_load_model_finish;
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
    iface->load_supported_bands = mm_shared_qmi_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_qmi_load_supported_bands_finish;
    iface->load_supported_modes = mm_shared_qmi_load_supported_modes;
    iface->load_supported_modes_finish = mm_shared_qmi_load_supported_modes_finish;
    iface->load_power_state = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->load_supported_ip_families = modem_load_supported_ip_families;
    iface->load_supported_ip_families_finish = modem_load_supported_ip_families_finish;
    iface->load_carrier_config = mm_shared_qmi_load_carrier_config;
    iface->load_carrier_config_finish = mm_shared_qmi_load_carrier_config_finish;
    iface->setup_carrier_config = mm_shared_qmi_setup_carrier_config;
    iface->setup_carrier_config_finish = mm_shared_qmi_setup_carrier_config_finish;

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
    iface->load_current_modes = mm_shared_qmi_load_current_modes;
    iface->load_current_modes_finish = mm_shared_qmi_load_current_modes_finish;
    iface->set_current_modes = mm_shared_qmi_set_current_modes;
    iface->set_current_modes_finish = mm_shared_qmi_set_current_modes_finish;
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
    iface->get_cell_info = get_cell_info;
    iface->get_cell_info_finish = get_cell_info_finish;
    iface->load_current_bands = mm_shared_qmi_load_current_bands;
    iface->load_current_bands_finish = mm_shared_qmi_load_current_bands_finish;
    iface->set_current_bands = mm_shared_qmi_set_current_bands;
    iface->set_current_bands_finish = mm_shared_qmi_set_current_bands_finish;

    /* Don't try to load access technologies, as we would be using parent's
     * generic method (QCDM based). Access technologies are already reported via
     * QMI when we load signal quality. */
    iface->load_access_technologies = NULL;
    iface->load_access_technologies_finish = NULL;

    /* Create QMI-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;
    iface->load_sim_slots = mm_shared_qmi_load_sim_slots;
    iface->load_sim_slots_finish = mm_shared_qmi_load_sim_slots_finish;
    iface->set_primary_sim_slot = mm_shared_qmi_set_primary_sim_slot;
    iface->set_primary_sim_slot_finish = mm_shared_qmi_set_primary_sim_slot_finish;
    iface->setup_sim_hot_swap = mm_shared_qmi_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_qmi_setup_sim_hot_swap_finish;

    /* Create QMI-specific bearer and bearer list */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->create_bearer_list = modem_create_bearer_list;

    /* Other actions */
    iface->reset = mm_shared_qmi_reset;
    iface->reset_finish = mm_shared_qmi_reset_finish;
    iface->factory_reset = mm_shared_qmi_factory_reset;
    iface->factory_reset_finish = mm_shared_qmi_factory_reset_finish;
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
    iface->register_in_network = mm_shared_qmi_3gpp_register_in_network;
    iface->register_in_network_finish = mm_shared_qmi_3gpp_register_in_network_finish;
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
    iface->load_initial_eps_bearer = modem_3gpp_load_initial_eps_bearer;
    iface->load_initial_eps_bearer_finish = modem_3gpp_load_initial_eps_bearer_finish;
    iface->load_initial_eps_bearer_settings = modem_3gpp_load_initial_eps_bearer_settings;
    iface->load_initial_eps_bearer_settings_finish = modem_3gpp_load_initial_eps_bearer_settings_finish;
    iface->set_initial_eps_bearer_settings = modem_3gpp_set_initial_eps_bearer_settings;
    iface->set_initial_eps_bearer_settings_finish = modem_3gpp_set_initial_eps_bearer_settings_finish;
    iface->disable_facility_lock = modem_3gpp_disable_facility_lock;
    iface->disable_facility_lock_finish = modem_3gpp_disable_facility_lock_finish;
    iface->set_packet_service_state = mm_shared_qmi_set_packet_service_state;
    iface->set_packet_service_state_finish = mm_shared_qmi_set_packet_service_state_finish;
}

static void
iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface)
{
    /* No explicit check support for the profile management feature, just
     * rely on the generic way to check for support */
    iface->check_support = NULL;
    iface->check_support_finish = NULL;

    iface->setup_unsolicited_events = modem_3gpp_profile_manager_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_profile_manager_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_profile_manager_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_profile_manager_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_profile_manager_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_profile_manager_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_profile_manager_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_profile_manager_enable_disable_unsolicited_events_finish;

    /* Additional actions */
    iface->get_profile = modem_3gpp_profile_manager_get_profile;
    iface->get_profile_finish = modem_3gpp_profile_manager_get_profile_finish;
    iface->list_profiles = modem_3gpp_profile_manager_list_profiles;
    iface->list_profiles_finish = modem_3gpp_profile_manager_list_profiles_finish;
    iface->check_format = modem_3gpp_profile_manager_check_format;
    iface->check_format_finish = modem_3gpp_profile_manager_check_format_finish;
    iface->check_activated_profile = NULL;
    iface->check_activated_profile_finish = NULL;
    iface->deactivate_profile = NULL;
    iface->deactivate_profile_finish = NULL;
    iface->store_profile = modem_3gpp_profile_manager_store_profile;
    iface->store_profile_finish = modem_3gpp_profile_manager_store_profile_finish;
    iface->delete_profile = modem_3gpp_profile_manager_delete_profile;
    iface->delete_profile_finish = modem_3gpp_profile_manager_delete_profile_finish;
}

static void
iface_modem_3gpp_ussd_init (MMIfaceModem3gppUssd *iface)
{
    iface->check_support = modem_3gpp_ussd_check_support;
    iface->check_support_finish = modem_3gpp_ussd_check_support_finish;
    iface->setup_unsolicited_events = modem_3gpp_ussd_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = common_3gpp_ussd_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_ussd_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = common_3gpp_ussd_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_ussd_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = common_3gpp_ussd_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_ussd_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = common_3gpp_ussd_enable_disable_unsolicited_events_finish;
    iface->send = modem_3gpp_ussd_send;
    iface->send_finish = modem_3gpp_ussd_send_finish;
    iface->cancel = modem_3gpp_ussd_cancel;
    iface->cancel_finish = modem_3gpp_ussd_cancel_finish;
}

static void
iface_modem_voice_init (MMIfaceModemVoice *iface)
{
    iface_modem_voice_parent = g_type_interface_peek_parent (iface);

    iface->check_support = modem_voice_check_support;
    iface->check_support_finish = modem_voice_check_support_finish;
    iface->setup_unsolicited_events = modem_voice_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = common_voice_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_voice_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = common_voice_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_voice_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = common_voice_enable_disable_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_voice_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = common_voice_setup_cleanup_unsolicited_events_finish;

    iface->setup_in_call_unsolicited_events = modem_voice_setup_in_call_unsolicited_events;
    iface->setup_in_call_unsolicited_events_finish = common_voice_setup_cleanup_in_call_unsolicited_events_finish;
    iface->cleanup_in_call_unsolicited_events = modem_voice_cleanup_in_call_unsolicited_events;
    iface->cleanup_in_call_unsolicited_events_finish = common_voice_setup_cleanup_in_call_unsolicited_events_finish;

    iface->create_call = modem_voice_create_call;
    iface->load_call_list = modem_voice_load_call_list;
    iface->load_call_list_finish = modem_voice_load_call_list_finish;
    iface->hold_and_accept = modem_voice_hold_and_accept;
    iface->hold_and_accept_finish = modem_voice_hold_and_accept_finish;
    iface->hangup_and_accept = modem_voice_hangup_and_accept;
    iface->hangup_and_accept_finish = modem_voice_hangup_and_accept_finish;
    iface->hangup_all = modem_voice_hangup_all;
    iface->hangup_all_finish = modem_voice_hangup_all_finish;
    iface->join_multiparty = modem_voice_join_multiparty;
    iface->join_multiparty_finish = modem_voice_join_multiparty_finish;
    iface->leave_multiparty = modem_voice_leave_multiparty;
    iface->leave_multiparty_finish = modem_voice_leave_multiparty_finish;
    iface->transfer = modem_voice_transfer;
    iface->transfer_finish = modem_voice_transfer_finish;
    iface->call_waiting_setup = modem_voice_call_waiting_setup;
    iface->call_waiting_setup_finish = modem_voice_call_waiting_setup_finish;
    iface->call_waiting_query = modem_voice_call_waiting_query;
    iface->call_waiting_query_finish = modem_voice_call_waiting_query_finish;
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
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
    iface->disable_location_gathering = disable_location_gathering;
    iface->disable_location_gathering_finish = disable_location_gathering_finish;

    iface->load_supl_server = mm_shared_qmi_location_load_supl_server;
    iface->load_supl_server_finish = mm_shared_qmi_location_load_supl_server_finish;
    iface->set_supl_server = mm_shared_qmi_location_set_supl_server;
    iface->set_supl_server_finish = mm_shared_qmi_location_set_supl_server_finish;
    iface->load_supported_assistance_data = mm_shared_qmi_location_load_supported_assistance_data;
    iface->load_supported_assistance_data_finish = mm_shared_qmi_location_load_supported_assistance_data_finish;
    iface->inject_assistance_data = mm_shared_qmi_location_inject_assistance_data;
    iface->inject_assistance_data_finish = mm_shared_qmi_location_inject_assistance_data_finish;
    iface->load_assistance_data_servers = mm_shared_qmi_location_load_assistance_data_servers;
    iface->load_assistance_data_servers_finish = mm_shared_qmi_location_load_assistance_data_servers_finish;
}

static void
iface_modem_sar_init (MMIfaceModemSar *iface)
{
    iface->check_support = sar_check_support;
    iface->check_support_finish  = sar_check_support_finish;
    iface->load_state = sar_load_state;
    iface->load_state_finish = sar_load_state_finish;
    iface->load_power_level = sar_load_power_level;
    iface->load_power_level_finish = sar_load_power_level_finish;
    iface->enable = sar_enable;
    iface->enable_finish = sar_enable_finish;
    iface->set_power_level = sar_set_power_level;
    iface->set_power_level_finish = sar_set_power_level_finish;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface->check_support = signal_check_support;
    iface->check_support_finish = signal_check_support_finish;
    iface->load_values = signal_load_values;
    iface->load_values_finish = signal_load_values_finish;
    iface->setup_thresholds = signal_setup_thresholds;
    iface->setup_thresholds_finish = signal_setup_thresholds_finish;
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
    iface->load_list = firmware_load_list;
    iface->load_list_finish = firmware_load_list_finish;
    iface->load_current = firmware_load_current;
    iface->load_current_finish = firmware_load_current_finish;
    iface->change_current = firmware_change_current;
    iface->change_current_finish = firmware_change_current_finish;
}

static MMIfaceModemLocation *
peek_parent_location_interface (MMSharedQmi *self)
{
    return iface_modem_location_parent;
}

static void
shared_qmi_init (MMSharedQmi *iface)
{
    iface->peek_client = shared_qmi_peek_client;
    iface->peek_parent_location_interface = peek_parent_location_interface;
}

static void
mm_broadband_modem_qmi_class_init (MMBroadbandModemQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemQmiPrivate));

    klass->peek_port_qmi_for_data = peek_port_qmi_for_data;

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
