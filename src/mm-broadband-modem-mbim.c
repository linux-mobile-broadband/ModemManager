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
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>

#include "mm-modem-helpers-mbim.h"
#include "mm-broadband-modem-mbim.h"
#include "mm-bearer-mbim.h"
#include "mm-sim-mbim.h"
#include "mm-sms-mbim.h"

#include "ModemManager.h"
#include <ModemManager-tags.h>
#include "mm-context.h"
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-error-helpers.h"
#include "mm-modem-helpers.h"
#include "mm-bearer-list.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-signal.h"
#include "mm-iface-modem-sar.h"
#include "mm-sms-part-3gpp.h"

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
# include <libqmi-glib.h>
# include "mm-shared-qmi.h"
#endif

static void iface_modem_init                      (MMIfaceModem                   *iface);
static void iface_modem_3gpp_init                 (MMIfaceModem3gpp               *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface);
static void iface_modem_3gpp_ussd_init            (MMIfaceModem3gppUssd           *iface);
static void iface_modem_location_init             (MMIfaceModemLocation           *iface);
static void iface_modem_messaging_init            (MMIfaceModemMessaging          *iface);
static void iface_modem_signal_init               (MMIfaceModemSignal             *iface);
static void iface_modem_sar_init                  (MMIfaceModemSar                *iface);
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
static void shared_qmi_init                       (MMSharedQmi                    *iface);
#endif

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
static MMIfaceModemLocation *iface_modem_location_parent;
#endif
static MMIfaceModemSignal *iface_modem_signal_parent;
static MMIfaceModem       *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbim, mm_broadband_modem_mbim, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_USSD, iface_modem_3gpp_ussd_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SAR, iface_modem_sar_init)
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QMI, shared_qmi_init)
#endif
)

typedef enum {
    PROCESS_NOTIFICATION_FLAG_NONE                 = 0,
    PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY       = 1 << 0,
    PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES = 1 << 1,
    PROCESS_NOTIFICATION_FLAG_SMS_READ             = 1 << 2,
    PROCESS_NOTIFICATION_FLAG_CONNECT              = 1 << 3,
    PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO      = 1 << 4,
    PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE       = 1 << 5,
    PROCESS_NOTIFICATION_FLAG_PCO                  = 1 << 6,
    PROCESS_NOTIFICATION_FLAG_USSD                 = 1 << 7,
    PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO      = 1 << 8,
    PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS = 1 << 9,
    PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS     = 1 << 10,
} ProcessNotificationFlag;

enum {
    PROP_0,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    PROP_QMI_UNSUPPORTED,
#endif
    PROP_INTEL_FIRMWARE_UPDATE_UNSUPPORTED,
    PROP_LAST
};

/* Runtime cache while enabled */
typedef struct {
    gchar                   *current_operator_id;
    gchar                   *current_operator_name;
    GList                   *pco_list;
    MbimDataClass            available_data_classes;
    MbimDataClass            highest_available_data_class;
    MbimRegisterState        reg_state;
    MbimPacketServiceState   packet_service_state;
    guint64                  packet_service_uplink_speed;
    guint64                  packet_service_downlink_speed;
    MbimSubscriberReadyState last_ready_state;
} EnabledCache;

struct _MMBroadbandModemMbimPrivate {
    /* Queried and cached capabilities */
    MbimCellularClass caps_cellular_class;
    MbimDataClass caps_data_class;
    gchar *caps_custom_data_class;
    MbimSmsCaps caps_sms;
    guint caps_max_sessions;
    gchar *caps_device_id;
    gchar *caps_firmware_info;
    gchar *caps_hardware_info;

    /* Supported features */
    gboolean is_profile_management_supported;
    gboolean is_profile_management_ext_supported;
    gboolean is_context_type_ext_supported;
    gboolean is_pco_supported;
    gboolean is_lte_attach_info_supported;
    gboolean is_nr5g_registration_settings_supported;
    gboolean is_base_stations_info_supported;
    gboolean is_ussd_supported;
    gboolean is_atds_location_supported;
    gboolean is_atds_signal_supported;
    gboolean is_intel_reset_supported;
    gboolean is_slot_info_status_supported;
    gboolean is_ms_sar_supported;
    gboolean is_google_carrier_lock_supported;

    /* Process unsolicited notifications */
    guint notification_id;
    ProcessNotificationFlag setup_flags;
    ProcessNotificationFlag enable_flags;

    /* State while enabled */
    EnabledCache enabled_cache;

    /* 3GPP registration helpers */
    gulong         enabling_signal_id;
    gchar         *requested_operator_id;
    MbimDataClass  requested_data_class; /* 0 for defaults/auto */

    /* Allowed modes helpers */
    GTask *pending_allowed_modes_action;

    /* USSD helpers */
    GTask *pending_ussd_action;

    /* SIM hot swap setup */
    gboolean sim_hot_swap_configured;

    /* For notifying when the mbim-proxy connection is dead */
    gulong mbim_device_removed_id;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    gboolean qmi_unsupported;
    /* Flag when QMI-based capability/mode switching is in use */
    gboolean qmi_capability_and_mode_switching;
#endif

    gboolean intel_firmware_update_unsupported;

    /* Multi-SIM support */
    guint32  executor_index;
    guint    active_slot_index;
    gboolean pending_sim_slot_switch_action;

    MMUnlockRetries *unlock_retries;
};

/*****************************************************************************/

static gboolean
peek_device (gpointer              self,
             MbimDevice          **o_device,
             GAsyncReadyCallback   callback,
             gpointer              user_data)
{
    MMPortMbim *port;

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_report_new_error (self, callback, user_data, peek_device,
                                 MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't peek MBIM port");
        return FALSE;
    }

    *o_device = mm_port_mbim_peek_device (port);
    return TRUE;
}

static gboolean
peek_device_in_task (gpointer     self,
                     MbimDevice **o_device,
                     GTask       *task)

{
    MMPortMbim *port;

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't peek MBIM port");
        g_object_unref (task);
        return FALSE;
    }

    *o_device = mm_port_mbim_peek_device (port);
    return TRUE;
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static QmiClient *
shared_qmi_peek_client (MMSharedQmi    *self,
                        QmiService      service,
                        MMPortQmiFlag   flag,
                        GError        **error)
{
    MMPortMbim *port;
    QmiClient  *client;

    g_assert (flag == MM_PORT_QMI_FLAG_DEFAULT);

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek MBIM port");
        return NULL;
    }

    if (!mm_port_mbim_supports_qmi (port)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported");
        return NULL;
    }

    client = mm_port_mbim_peek_qmi_client (port, service);
    if (!client)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't peek client for service '%s'",
                     qmi_service_get_string (service));

    return client;
}

#endif

/*****************************************************************************/

MMPortMbim *
mm_broadband_modem_mbim_get_port_mbim (MMBroadbandModemMbim *self)
{
    MMPortMbim *primary_mbim_port;

    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));

    primary_mbim_port = mm_broadband_modem_mbim_peek_port_mbim (self);
    return (primary_mbim_port ?
            MM_PORT_MBIM (g_object_ref (primary_mbim_port)) :
            NULL);
}

MMPortMbim *
mm_broadband_modem_mbim_peek_port_mbim (MMBroadbandModemMbim *self)
{
    MMPortMbim *primary_mbim_port = NULL;
    GList      *mbim_ports;

    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));

    mbim_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                           MM_PORT_SUBSYS_UNKNOWN,
                                           MM_PORT_TYPE_MBIM);

    /* First MBIM port in the list is the primary one always */
    if (mbim_ports)
        primary_mbim_port = MM_PORT_MBIM (mbim_ports->data);

    g_list_free_full (mbim_ports, g_object_unref);

    return primary_mbim_port;
}

MMPortMbim *
mm_broadband_modem_mbim_get_port_mbim_for_data (MMBroadbandModemMbim  *self,
                                                MMPort                *data,
                                                GError               **error)
{
    MMPortMbim *mbim_port;

    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));

    mbim_port = mm_broadband_modem_mbim_peek_port_mbim_for_data (self, data, error);
    return (mbim_port ?
            MM_PORT_MBIM (g_object_ref (mbim_port)) :
            NULL);
}

static MMPortMbim *
peek_port_mbim_for_data (MMBroadbandModemMbim  *self,
                         MMPort                *data,
                         GError               **error)
{
    GList       *cdc_wdm_mbim_ports;
    GList       *l;
    const gchar *net_port_parent_path;
    MMPortMbim  *found = NULL;
    const gchar *net_port_driver;

    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));
    g_assert (mm_port_get_subsys (data) == MM_PORT_SUBSYS_NET);

    net_port_driver = mm_kernel_device_get_driver (mm_port_peek_kernel_device (data));
    if (g_strcmp0 (net_port_driver, "cdc_mbim") != 0 && g_strcmp0 (net_port_driver, "mhi_net")) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unsupported MBIM kernel driver for 'net/%s': %s",
                     mm_port_get_device (data),
                     net_port_driver);
        return NULL;
    }

    if (!g_strcmp0 (net_port_driver, "mhi_net"))
        return mm_broadband_modem_mbim_peek_port_mbim (self);

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
    cdc_wdm_mbim_ports = mm_base_modem_find_ports (MM_BASE_MODEM (self),
                                                   MM_PORT_SUBSYS_USBMISC,
                                                   MM_PORT_TYPE_MBIM);

    for (l = cdc_wdm_mbim_ports; l && !found; l = g_list_next (l)) {
        const gchar *wdm_port_parent_path;

        g_assert (MM_IS_PORT_MBIM (l->data));
        wdm_port_parent_path = mm_kernel_device_get_interface_sysfs_path (mm_port_peek_kernel_device (MM_PORT (l->data)));
        if (wdm_port_parent_path && g_str_equal (wdm_port_parent_path, net_port_parent_path))
            found = MM_PORT_MBIM (l->data);
    }

    g_list_free_full (cdc_wdm_mbim_ports, g_object_unref);

    if (!found)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "Couldn't find associated MBIM port for 'net/%s'",
                     mm_port_get_device (data));

    return found;
}

MMPortMbim *
mm_broadband_modem_mbim_peek_port_mbim_for_data (MMBroadbandModemMbim  *self,
                                                 MMPort                *data,
                                                 GError               **error)
{
    g_assert (MM_BROADBAND_MODEM_MBIM_GET_CLASS (self)->peek_port_mbim_for_data);

    return MM_BROADBAND_MODEM_MBIM_GET_CLASS (self)->peek_port_mbim_for_data (self, data, error);
}

/*****************************************************************************/

gboolean
mm_broadband_modem_mbim_is_context_type_ext_supported (MMBroadbandModemMbim *self)
{
    return self->priv->is_context_type_ext_supported;
}

/*****************************************************************************/
/* Current capabilities (Modem interface) */

typedef struct {
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    MMModemCapability  current_qmi;
#endif
    MbimDevice        *device;
    MMModemCapability  current_mbim;
} LoadCurrentCapabilitiesContext;

static void
load_current_capabilities_context_free (LoadCurrentCapabilitiesContext *ctx)
{
    g_object_unref (ctx->device);
    g_free (ctx);
}

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
complete_current_capabilities (GTask *task)
{
    LoadCurrentCapabilitiesContext *ctx;
    MMModemCapability               result = 0;

    ctx  = g_task_get_task_data (task);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    {
        MMBroadbandModemMbim *self;

        self = g_task_get_source_object (task);
        /* Warn if the MBIM loaded capabilities isn't a subset of the QMI loaded ones */
        if (ctx->current_qmi && ctx->current_mbim) {
            gchar *mbim_caps_str;
            gchar *qmi_caps_str;

            mbim_caps_str = mm_common_build_capabilities_string ((const MMModemCapability *)&(ctx->current_mbim), 1);
            qmi_caps_str = mm_common_build_capabilities_string ((const MMModemCapability *)&(ctx->current_qmi), 1);

            if ((ctx->current_mbim & ctx->current_qmi) != ctx->current_mbim)
                mm_obj_warn (self, "MBIM reported current capabilities (%s) not found in QMI-over-MBIM reported ones (%s)",
                             mbim_caps_str, qmi_caps_str);
            else
                mm_obj_dbg (self, "MBIM reported current capabilities (%s) is a subset of the QMI-over-MBIM reported ones (%s)",
                            mbim_caps_str, qmi_caps_str);
            g_free (mbim_caps_str);
            g_free (qmi_caps_str);

            result = ctx->current_qmi;
            self->priv->qmi_capability_and_mode_switching = TRUE;
        } else if (ctx->current_qmi) {
            result = ctx->current_qmi;
            self->priv->qmi_capability_and_mode_switching = TRUE;
        } else
            result = ctx->current_mbim;

        /* If current capabilities loading is done via QMI, we can safely assume that all the other
         * capability and mode related operations are going to be done via QMI as well, so that we
         * don't mix both logics */
        if (self->priv->qmi_capability_and_mode_switching)
            mm_obj_dbg (self, "QMI-based capability and mode switching support enabled");
    }
#else
    result = ctx->current_mbim;
#endif

    g_task_return_int (task, (gint) result);
    g_object_unref (task);
}

static void
device_caps_query_ready (MbimDevice   *device,
                         GAsyncResult *res,
                         GTask        *task)
{
    g_autoptr(MbimMessage)          response = NULL;
    MMBroadbandModemMbim           *self;
    GError                         *error = NULL;
    LoadCurrentCapabilitiesContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        MbimDataClassV3  data_class_v3;
        MbimDataSubclass data_subclass;

        if (!mbim_message_ms_basic_connect_extensions_v3_device_caps_response_parse (
                response,
                NULL, /* device_type */
                &self->priv->caps_cellular_class,
                NULL, /* voice_class */
                NULL, /* sim_class */
                &data_class_v3,
                &self->priv->caps_sms,
                NULL, /* ctrl_caps */
                &data_subclass,
                &self->priv->caps_max_sessions,
                NULL, /* executor_index */
                NULL, /* wcdma_band_class */
                NULL, /* lte_band_class_array_size */
                NULL, /* lte_band_class_array */
                NULL, /* nr_band_class_array_size */
                NULL, /* nr_band_class_array */
                &self->priv->caps_custom_data_class,
                &self->priv->caps_device_id,
                &self->priv->caps_firmware_info,
                &self->priv->caps_hardware_info,
                &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        /* Translate data class v3 to standard data class to simplify further usage of the field */
        self->priv->caps_data_class = mm_mbim_data_class_from_mbim_data_class_v3_and_subclass (data_class_v3, data_subclass);
    } else if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (!mbim_message_ms_basic_connect_extensions_device_caps_response_parse (
                response,
                NULL, /* device_type */
                &self->priv->caps_cellular_class,
                NULL, /* voice_class */
                NULL, /* sim_class */
                &self->priv->caps_data_class,
                &self->priv->caps_sms,
                NULL, /* ctrl_caps */
                &self->priv->caps_max_sessions,
                &self->priv->caps_custom_data_class,
                &self->priv->caps_device_id,
                &self->priv->caps_firmware_info,
                &self->priv->caps_hardware_info,
                NULL, /* executor_index */
                &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    } else {
        if (!mbim_message_device_caps_response_parse (
                response,
                NULL, /* device_type */
                &self->priv->caps_cellular_class,
                NULL, /* voice_class */
                NULL, /* sim_class */
                &self->priv->caps_data_class,
                &self->priv->caps_sms,
                NULL, /* ctrl_caps */
                &self->priv->caps_max_sessions,
                &self->priv->caps_custom_data_class,
                &self->priv->caps_device_id,
                &self->priv->caps_firmware_info,
                &self->priv->caps_hardware_info,
                &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    }

    ctx->current_mbim = mm_modem_capability_from_mbim_device_caps (self->priv->caps_cellular_class,
                                                                   self->priv->caps_data_class,
                                                                   self->priv->caps_custom_data_class);
    complete_current_capabilities (task);
}

static void
load_current_capabilities_mbim (GTask *task)
{
    g_autoptr(MbimMessage)          message = NULL;
    MMBroadbandModemMbim           *self;
    LoadCurrentCapabilitiesContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_dbg (self, "loading current capabilities...");
    if (mbim_device_check_ms_mbimex_version (ctx->device, 2, 0))
        message = mbim_message_ms_basic_connect_extensions_device_caps_query_new (NULL);
    else
        message = mbim_message_device_caps_query_new (NULL);
    mbim_device_command (ctx->device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)device_caps_query_ready,
                         task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
qmi_load_current_capabilities_ready (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    LoadCurrentCapabilitiesContext *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->current_qmi = mm_shared_qmi_load_current_capabilities_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't load currrent capabilities using QMI over MBIM: %s", error->message);
        g_clear_error (&error);
    }

    load_current_capabilities_mbim (task);
}

#endif

static void
modem_load_current_capabilities (MMIfaceModem        *self,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    MbimDevice                     *device;
    GTask                          *task;
    LoadCurrentCapabilitiesContext *ctx;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_new0 (LoadCurrentCapabilitiesContext, 1);
    ctx->device = g_object_ref (device);
    g_task_set_task_data (task, ctx, (GDestroyNotify) load_current_capabilities_context_free);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    mm_shared_qmi_load_current_capabilities (self,
                                             (GAsyncReadyCallback)qmi_load_current_capabilities_ready,
                                             task);
#else
    load_current_capabilities_mbim (task);
#endif
}

/*****************************************************************************/
/* Supported Capabilities loading (Modem interface) */

static GArray *
modem_load_supported_capabilities_finish (MMIfaceModem  *self,
                                          GAsyncResult  *res,
                                          GError       **error)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching)
        return mm_shared_qmi_load_supported_capabilities_finish (self, res, error);
#endif
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_capabilities_mbim (GTask *task)
{
    MMBroadbandModemMbim *self;
    MMModemCapability     current;
    GArray               *supported = NULL;

    self = g_task_get_source_object (task);

    /* Current capabilities should have been cached already, just assume them */
    current = mm_modem_capability_from_mbim_device_caps (self->priv->caps_cellular_class,
                                                         self->priv->caps_data_class,
                                                         self->priv->caps_custom_data_class);
    if (current != 0) {
        supported = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 1);
        g_array_append_val (supported, current);
    }

    if (!supported)
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't load supported capabilities: no previously catched current capabilities");
    else
        g_task_return_pointer (task, supported, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

static void
modem_load_supported_capabilities (MMIfaceModem        *self,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    GTask *task;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_load_supported_capabilities (self, callback, user_data);
        return;
    }
#endif

    task = g_task_new (self, NULL, callback, user_data);
    load_supported_capabilities_mbim (task);
}

/*****************************************************************************/
/* Capabilities switching (Modem interface) */

static gboolean
modem_set_current_capabilities_finish (MMIfaceModem  *self,
                                       GAsyncResult  *res,
                                       GError       **error)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching)
        return mm_shared_qmi_set_current_capabilities_finish (self, res, error);
#endif
    g_assert (error);
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_set_current_capabilities (MMIfaceModem         *self,
                                MMModemCapability     capabilities,
                                GAsyncReadyCallback   callback,
                                gpointer              user_data)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_set_current_capabilities (self, capabilities, callback, user_data);
        return;
    }
#endif

    g_task_report_new_error (self, callback, user_data,
                             modem_set_current_capabilities,
                             MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Capability switching is not supported");
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
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;
    gchar *manufacturer = NULL;
    MMPortMbim *port;

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (port) {
        manufacturer = g_strdup (mm_kernel_device_get_physdev_manufacturer (
            mm_port_peek_kernel_device (MM_PORT (port))));
    }

    if (!manufacturer)
        manufacturer = g_strdup (mm_base_modem_get_plugin (MM_BASE_MODEM (self)));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, manufacturer, g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_default (MMIfaceModem *self)
{
    return g_strdup_printf ("MBIM [%04X:%04X]",
                            (mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)) & 0xFFFF),
                            (mm_base_modem_get_product_id (MM_BASE_MODEM (self)) & 0xFFFF));
}

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
qmi_load_model_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      GTask *task)
{
    gchar  *model = NULL;
    GError *error = NULL;

    model = mm_shared_qmi_load_model_finish (self, res, &error);
    if (!model) {
        mm_obj_dbg (self, "couldn't load model using QMI over MBIM: %s", error->message);
        model = modem_load_model_default (self);
        g_clear_error (&error);
    }

     g_task_return_pointer (task, model, g_free);
     g_object_unref (task);
}

#endif

static void
at_load_model_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    gchar  *model = NULL;
    GError *error = NULL;

    model = iface_modem_parent->load_model_finish (self, res, &error);
    if (!model) {
        mm_obj_dbg (self, "couldn't load model using AT: %s", error->message);
        model = modem_load_model_default (self);
        g_clear_error (&error);
    }

    g_task_return_pointer (task, model, g_free);
    g_object_unref (task);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    gchar      *model = NULL;
    GTask      *task;
    MMPortMbim *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (port) {
        model = g_strdup (mm_kernel_device_get_physdev_product (
            mm_port_peek_kernel_device (MM_PORT (port))));
        if (model) {
            g_task_return_pointer (task, model, g_free);
            g_object_unref (task);
            return;
        }
    }

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (!model) {
        mm_shared_qmi_load_model (self, (GAsyncReadyCallback)qmi_load_model_ready, task);
        return;
    }
#endif

    iface_modem_parent->load_model (self, (GAsyncReadyCallback)at_load_model_ready, task);
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
modem_load_revision (MMIfaceModem *_self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_firmware_info)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_firmware_info),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Firmware revision information not given in device capabilities");
    g_object_unref (task);
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
modem_load_hardware_revision (MMIfaceModem *_self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_hardware_info)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_hardware_info),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Hardware revision information not given in device capabilities");
    g_object_unref (task);
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
modem_load_equipment_identifier (MMIfaceModem *_self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_device_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_device_id),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device ID not given in device capabilities");
    g_object_unref (task);
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
/* Supported modes loading (Modem interface) */

static GArray *
modem_load_supported_modes_finish (MMIfaceModem  *self,
                                   GAsyncResult  *res,
                                   GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_modes_mbim (GTask      *task,
                           MbimDevice *device)
{
    MMBroadbandModemMbim   *self;
    GArray                 *all;
    GArray                 *filtered;
    MMModemMode             mask_all;
    MMModemModeCombination  mode = {
        .allowed   = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
    };

    self = g_task_get_source_object (task);

    if (self->priv->caps_data_class == 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Data class not given in device capabilities");
        g_object_unref (task);
        return;
    }

    /* Build all */
    mask_all = mm_modem_mode_from_mbim_data_class (self->priv->caps_data_class, self->priv->caps_custom_data_class);
    mode.allowed = mask_all;
    mode.preferred = MM_MODEM_MODE_NONE;
    all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    g_array_append_val (all, mode);

    /* When using MBIMEx we can enable the mode switching operation because
     * we'll be able to know if the modes requested are the ones configured
     * as preferred after the operation. */
    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        GArray *combinations;

        combinations = g_array_new (FALSE, FALSE, sizeof (MMModemModeCombination));

#define ADD_MODE_PREFERENCE(MODE_MASK) do {                             \
            mode.allowed = MODE_MASK;                                   \
            mode.preferred = MM_MODEM_MODE_NONE;                        \
            g_array_append_val (combinations, mode);                    \
        } while (0)

        /* 2G, 3G */
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

        /* +4G */
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_4G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);

        /* +5G */
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        ADD_MODE_PREFERENCE (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);

        filtered = mm_filter_supported_modes (all, combinations, self);
        g_array_unref (combinations);
        g_array_unref (all);
#undef ADD_MODE_PREFERENCE
    } else
        filtered = all;

    g_task_return_pointer (task, filtered, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
shared_qmi_load_supported_modes_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GArray *combinations;
    GError *error = NULL;

    combinations = mm_shared_qmi_load_supported_modes_finish (self, res, &error);
    if (!combinations)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, combinations, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

#endif

static void
modem_load_supported_modes (MMIfaceModem        *self,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    GTask      *task;
    MbimDevice *device;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_load_supported_modes (self, (GAsyncReadyCallback)shared_qmi_load_supported_modes_ready, task);
        return;
    }
#endif

    load_supported_modes_mbim (task, device);
}

/*****************************************************************************/
/* Current modes loading (Modem interface) */

static gboolean
modem_load_current_modes_finish (MMIfaceModem  *self,
                                 GAsyncResult  *res,
                                 MMModemMode   *allowed,
                                 MMModemMode   *preferred,
                                 GError       **error)
{
    g_autofree MMModemModeCombination *mode = NULL;

    mode = g_task_propagate_pointer (G_TASK (res), error);
    if (!mode)
        return FALSE;

    *allowed   = mode->allowed;
    *preferred = mode->preferred;
    return TRUE;
}

static void
register_state_current_modes_query_ready (MbimDevice   *device,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    MMModemModeCombination *mode = NULL;
    GError                 *error = NULL;
    MbimDataClass           preferred_data_classes;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_v2_register_state_response_parse (
            response,
            NULL, /* nw_error */
            NULL, /* register_state */
            NULL, /* register_mode */
            NULL, /* available_data_classes */
            NULL, /* current_cellular_class */
            NULL, /* provider_id */
            NULL, /* provider_name */
            NULL, /* roaming_text */
            NULL, /* registration_flag */
            &preferred_data_classes,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mode = g_new0 (MMModemModeCombination, 1);
    mode->allowed = mm_modem_mode_from_mbim_data_class (preferred_data_classes, NULL);
    mode->preferred = MM_MODEM_MODE_NONE;
    g_task_return_pointer (task, mode, (GDestroyNotify)g_free);
    g_object_unref (task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
shared_qmi_load_current_modes_ready (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    g_autofree MMModemModeCombination *mode = NULL;
    GError                            *error = NULL;

    mode = g_new0 (MMModemModeCombination, 1);
    if (!mm_shared_qmi_load_current_modes_finish (self, res, &mode->allowed, &mode->preferred, &error))
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, g_steal_pointer (&mode), (GDestroyNotify)g_free);
    g_object_unref (task);
}

#endif

static void
modem_load_current_modes (MMIfaceModem        *self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    GTask      *task;
    MbimDevice *device;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_load_current_modes (self, (GAsyncReadyCallback)shared_qmi_load_current_modes_ready, task);
        return;
    }
#endif

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        g_autoptr(MbimMessage) message = NULL;

        message = mbim_message_register_state_query_new (NULL);
        mbim_device_command (device,
                             message,
                             60,
                             NULL,
                             (GAsyncReadyCallback)register_state_current_modes_query_ready,
                             task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Current mode loading is not supported");
    g_object_unref (task);
}

/*****************************************************************************/
/* Current modes switching (Modem interface) */

static gboolean
modem_set_current_modes_finish (MMIfaceModem  *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
complete_pending_allowed_modes_action (MMBroadbandModemMbim *self,
                                       MbimDataClass         preferred_data_classes)
{
    MbimDataClass requested_data_classes;
    MMModemMode   preferred_modes;
    MMModemMode   requested_modes;

    if (!self->priv->pending_allowed_modes_action)
        return;

    requested_data_classes = (MbimDataClass) GPOINTER_TO_UINT (g_task_get_task_data (self->priv->pending_allowed_modes_action));
    requested_modes = mm_modem_mode_from_mbim_data_class (requested_data_classes, NULL);
    preferred_modes = mm_modem_mode_from_mbim_data_class (preferred_data_classes, NULL);

    /* only early complete on success, as we don't know if they're going to be
     * intermediate indications emitted before the preference change is valid */
    if (requested_modes == preferred_modes) {
        GTask *task;

        task = g_steal_pointer (&self->priv->pending_allowed_modes_action);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
    }
}

static void
register_state_current_modes_set_ready (MbimDevice   *device,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    MMBroadbandModemMbim   *self;
    GError                 *error = NULL;
    MbimDataClass           preferred_data_classes;
    MMModemMode             preferred_modes;
    MbimDataClass           requested_data_classes;
    MMModemMode             requested_modes;

    self = g_task_get_source_object (task);
    requested_data_classes = (MbimDataClass) GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* If the task is still in the private info, it means it wasn't either
     * cancelled or completed, so we just unref that reference and go on
     * with out response processing. But if the task is no longer in the
     * private info (or if there's a different task), then it means we're
     * either cancelled (by some new incoming user request) or otherwise
     * successfully completed (if completed via a Register State indication).
     * In both those cases, just unref the incoming task and go on. */
    if (self->priv->pending_allowed_modes_action != task) {
        g_assert (g_task_get_completed (task));
        g_object_unref (task);
        return;
    }
    g_clear_object (&self->priv->pending_allowed_modes_action);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_v2_register_state_response_parse (
            response,
            NULL, /* nw_error */
            NULL, /* register_state */
            NULL, /* register_mode */
            NULL, /* available_data_classes */
            NULL, /* current_cellular_class */
            NULL, /* provider_id */
            NULL, /* provider_name */
            NULL, /* roaming_text */
            NULL, /* registration_flag */
            &preferred_data_classes,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    requested_modes = mm_modem_mode_from_mbim_data_class (requested_data_classes, NULL);
    preferred_modes = mm_modem_mode_from_mbim_data_class (preferred_data_classes, NULL);

    if (requested_modes != preferred_modes) {
        g_autofree gchar *requested_modes_str = NULL;
        g_autofree gchar *preferred_modes_str = NULL;
        g_autofree gchar *requested_data_classes_str = NULL;
        g_autofree gchar *preferred_data_classes_str = NULL;

        requested_modes_str = mm_modem_mode_build_string_from_mask (requested_modes);
        preferred_modes_str = mm_modem_mode_build_string_from_mask (preferred_modes);
        requested_data_classes_str = mbim_data_class_build_string_from_mask (requested_data_classes);
        preferred_data_classes_str = mbim_data_class_build_string_from_mask (preferred_data_classes);

        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Current mode update failed: requested %s (%s) but reported preferred is %s (%s)",
                                 requested_modes_str, requested_data_classes_str,
                                 preferred_modes_str, preferred_data_classes_str);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
shared_qmi_set_current_modes_ready (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    GError *error = NULL;

    if (!mm_shared_qmi_set_current_modes_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

#endif

static void
modem_set_current_modes (MMIfaceModem        *_self,
                         MMModemMode          allowed,
                         MMModemMode          preferred,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    MMBroadbandModemMbim    *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                   *task;
    MbimDevice              *device;
    g_autoptr(GCancellable)  cancellable = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    cancellable = g_cancellable_new ();
    task = g_task_new (self, cancellable, callback, user_data);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (self->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_set_current_modes (MM_IFACE_MODEM (self),
                                         allowed,
                                         preferred,
                                         (GAsyncReadyCallback)shared_qmi_set_current_modes_ready,
                                         task);
        return;
    }
#endif

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        g_autoptr(MbimMessage) message = NULL;

        /* Limit ANY to the currently supported modes */
        if (allowed == MM_MODEM_MODE_ANY)
            allowed = mm_modem_mode_from_mbim_data_class (self->priv->caps_data_class, self->priv->caps_custom_data_class);

        self->priv->requested_data_class = mm_mbim_data_class_from_modem_mode (allowed,
                                                                               mm_iface_modem_is_3gpp (_self),
                                                                               mm_iface_modem_is_cdma (_self));

        /* Store the ongoing allowed modes action, so that we can finish the
         * operation early via indications, instead of waiting for the modem
         * to be registered on the requested access technologies */
        g_task_set_task_data (task, GUINT_TO_POINTER (self->priv->requested_data_class), NULL);
        if (self->priv->pending_allowed_modes_action) {
            /* cancel the task and clear this reference; the _set_ready()
             * will take care of completing the task */
            g_cancellable_cancel (g_task_get_cancellable (self->priv->pending_allowed_modes_action));
            g_task_return_error_if_cancelled (self->priv->pending_allowed_modes_action);
            g_clear_object (&self->priv->pending_allowed_modes_action);
        }
        self->priv->pending_allowed_modes_action = g_object_ref (task);

        /* use the last requested operator id to determine whether the
         * registration should be manual or automatic */
        message = mbim_message_register_state_set_new (
                      self->priv->requested_operator_id ? self->priv->requested_operator_id : "",
                      self->priv->requested_operator_id ? MBIM_REGISTER_ACTION_MANUAL : MBIM_REGISTER_ACTION_AUTOMATIC,
                      self->priv->requested_data_class,
                      NULL);
        mbim_device_command (device,
                             message,
                             60,
                             NULL,
                             (GAsyncReadyCallback)register_state_current_modes_set_ready,
                             task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Current mode switching is not supported");
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
    /* Assume IPv4 + IPv6 + IPv4v6 supported */
    g_task_return_int (task,
                       MM_BEARER_IP_FAMILY_IPV4 |
                       MM_BEARER_IP_FAMILY_IPV6 |
                       MM_BEARER_IP_FAMILY_IPV4V6);
    g_object_unref (task);
}

/*****************************************************************************/
/* Unlock required loading (Modem interface) */

typedef struct {
    MbimDevice *device;
    gboolean    last_attempt;
} LoadUnlockRequiredContext;

static void
load_unlock_required_context_free (LoadUnlockRequiredContext *ctx)
{
    g_object_unref (ctx->device);
    g_slice_free (LoadUnlockRequiredContext, ctx);
}

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

static void
pin_query_ready (MbimDevice *device,
                 GAsyncResult *res,
                 GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinType pin_type;
    MbimPinState pin_state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_response_parse (
            response,
            &pin_type,
            &pin_state,
            NULL,
            &error)) {
        MMModemLock unlock_required;

        if (pin_state == MBIM_PIN_STATE_UNLOCKED)
            unlock_required = MM_MODEM_LOCK_NONE;
        else
            unlock_required = mm_modem_lock_from_mbim_pin_type (pin_type);

        g_task_return_int (task, unlock_required);
    }
    /* VZ20M reports an error when SIM-PIN is required... */
    else if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
        g_error_free (error);
        g_task_return_int (task, MBIM_PIN_TYPE_PIN1);
    }
    else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static gboolean wait_for_sim_ready (GTask *task);

static void
unlock_required_subscriber_ready_state_ready (MbimDevice   *device,
                                              GAsyncResult *res,
                                              GTask        *task)
{
    LoadUnlockRequiredContext *ctx;
    MMBroadbandModemMbim      *self;
    g_autoptr(MbimMessage)     response = NULL;
    GError                    *error = NULL;
    MbimSubscriberReadyState   ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
    MbimSubscriberReadyState   prev_ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    /* hold on to the previous ready_state value to determine if the retry logic needs to be reset. */
    prev_ready_state = self->priv->enabled_cache.last_ready_state;
    /* reset to the default if any error happens */
    self->priv->enabled_cache.last_ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_response_parse (
                response,
                &ready_state,
                NULL, /* flags */
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                NULL, /* telephone_numbers */
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v3.0 subscriber ready status response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 subscriber ready status response");
    } else {
        if (!mbim_message_subscriber_ready_status_response_parse (
                response,
                &ready_state,
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                NULL, /* telephone_numbers */
                &error))
            g_prefix_error (&error, "Failed processing subscriber ready status response: ");
        else
            mm_obj_dbg (self, "processed subscriber ready status response");
    }

    if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NOT_INITIALIZED)) {
        g_clear_error (&error);
        ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
    }

    if (!error) {
        /* Store last valid status loaded */
        self->priv->enabled_cache.last_ready_state = ready_state;

        switch (ready_state) {
        case MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED:
        case MBIM_SUBSCRIBER_READY_STATE_INITIALIZED:
        case MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED:
        case MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE:
            /* Don't set error */
            break;
        case MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED:
            /* This is an error, but we still want to retry.
             * The MC7710 may use this while the SIM is not ready yet. */
            break;
        case MBIM_SUBSCRIBER_READY_STATE_BAD_SIM:
            error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG, self);
            break;
        case MBIM_SUBSCRIBER_READY_STATE_FAILURE:
        case MBIM_SUBSCRIBER_READY_STATE_NOT_ACTIVATED:
        default:
            error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE, self);
            break;
        }
    }

    /* Fatal errors are reported right away */
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Need to retry? */
    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED ||
        ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
        /* All retries consumed? issue error */
        if (ctx->last_attempt) {
            if (ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED)
                g_task_return_error (task, mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED, self));
            else
                g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                         "Error waiting for SIM to get initialized");
        } else {
            /* Start the retry process from the top if the SIM state changes from
             * MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED to
             * MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED
             * This will address the race condition that occurs during rapid hotswap. */
            gboolean retry_reset_needed = FALSE;

            if (prev_ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
                ready_state == MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED)
                retry_reset_needed = TRUE;
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     retry_reset_needed ? MM_CORE_ERROR_RESET_AND_RETRY : MM_CORE_ERROR_RETRY,
                                     "SIM not ready yet (retry)");
        }
        g_object_unref (task);
        return;
    }

    /* Initialized */
    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED ||
        ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED) {
        MbimMessage *message;

        /* Query which lock is to unlock */
        message = mbim_message_pin_query_new (NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)pin_query_ready,
                             task);
        mbim_message_unref (message);
        return;
    }

    /* When initialized but there are not profile set, assume no lock is
     * applied. */
    mm_obj_dbg (self, "eSIM without profiles: assuming no lock is required");
    g_assert (ready_state == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE);
    g_task_return_int (task, MM_MODEM_LOCK_NONE);
    g_object_unref (task);
}

static gboolean
wait_for_sim_ready (GTask *task)
{
    LoadUnlockRequiredContext *ctx;
    MbimMessage *message;

    ctx = g_task_get_task_data (task);
    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (ctx->device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)unlock_required_subscriber_ready_state_ready,
                         task);
    mbim_message_unref (message);
    return G_SOURCE_REMOVE;
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            gboolean last_attempt,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadUnlockRequiredContext *ctx;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    ctx = g_slice_new (LoadUnlockRequiredContext);
    ctx->device = g_object_ref (device);
    ctx->last_attempt = last_attempt;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_unlock_required_context_free);

    wait_for_sim_ready (task);
}

/*****************************************************************************/
/* Unlock retries loading (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
pin_query_unlock_retries_ready (MbimDevice *device,
                                GAsyncResult *res,
                                GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinType pin_type;
    guint32 remaining_attempts;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_response_parse (
            response,
            &pin_type,
            NULL,
            &remaining_attempts,
            &error)) {
        MMBroadbandModemMbim *self;
        MMModemLock lock;

        self = MM_BROADBAND_MODEM_MBIM (g_task_get_source_object (task));
        lock = mm_modem_lock_from_mbim_pin_type (pin_type);

        mm_broadband_modem_mbim_set_unlock_retries (self, lock, remaining_attempts);
        g_task_return_pointer (task, g_object_ref (self->priv->unlock_retries), g_object_unref);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_pin_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_query_unlock_retries_ready,
                         task);
    mbim_message_unref (message);
}

void
mm_broadband_modem_mbim_set_unlock_retries (MMBroadbandModemMbim *self,
                                            MMModemLock           lock_type,
                                            guint32               remaining_attempts)
{
    g_assert (MM_IS_BROADBAND_MODEM_MBIM (self));

    if (!self->priv->unlock_retries)
        self->priv->unlock_retries = mm_unlock_retries_new ();

    /* According to the MBIM specification, RemainingAttempts is set to
     * 0xffffffff if the device does not support this information. */
    if (remaining_attempts != G_MAXUINT32)
        mm_unlock_retries_set (self->priv->unlock_retries,
                               lock_type,
                               remaining_attempts);
}

/*****************************************************************************/
/* Own numbers loading */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem  *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
own_numbers_subscriber_ready_state_ready (MbimDevice   *device,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    MMBroadbandModemMbim    *self;
    g_autoptr(MbimMessage)   response = NULL;
    GError                  *error = NULL;
    gchar                  **telephone_numbers;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_response_parse (
                response,
                NULL, /* ready_state */
                NULL, /* flags */
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v3.0 subscriber ready status response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 subscriber ready status response");
    } else {
        if (!mbim_message_subscriber_ready_status_response_parse (
                response,
                NULL, /* ready_state */
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error))
            g_prefix_error (&error, "Failed processing subscriber ready status response: ");
        else
            mm_obj_dbg (self, "processed subscriber ready status response");
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, telephone_numbers, (GDestroyNotify)g_strfreev);
    g_object_unref (task);
}

static void
modem_load_own_numbers (MMIfaceModem        *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)own_numbers_subscriber_ready_state_ready,
                         task);
}

/*****************************************************************************/
/* Initial power state loading */

static MMModemPowerState
modem_load_power_state_finish (MMIfaceModem *self,
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
radio_state_query_ready (MbimDevice *device,
                         GAsyncResult *res,
                         GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimRadioSwitchState hardware_radio_state;
    MbimRadioSwitchState software_radio_state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_radio_state_response_parse (
            response,
            &hardware_radio_state,
            &software_radio_state,
            &error)) {
        MMModemPowerState state;

        if (hardware_radio_state == MBIM_RADIO_SWITCH_STATE_OFF ||
            software_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            state = MM_MODEM_POWER_STATE_LOW;
        else
            state = MM_MODEM_POWER_STATE_ON;
        g_task_return_int (task, state);
    } else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_power_state (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_radio_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)radio_state_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Power up (Modem interface) */

static gboolean
power_up_finish (MMIfaceModem  *self,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
radio_state_set_up_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(GError)       error = NULL;
    MbimRadioSwitchState    hardware_radio_state;
    MbimRadioSwitchState    software_radio_state;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_radio_state_response_parse (
            response,
            &hardware_radio_state,
            &software_radio_state,
            &error)) {
        if (hardware_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Cannot power-up: hardware radio switch is OFF");
        else if (software_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Cannot power-up: sotware radio switch is OFF");
    }

    /* Nice! we're done, quick exit */
    if (!error) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* The SDX55 returns "Operation not allowed", but not really sure about other
     * older devices. The original logic in the MBIM implemetation triggered a retry
     * for any kind of error, so let's do the same for now. */
    mm_obj_warn (self, "%s", error->message);
    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_RETRY, "Invalid transition");
    g_object_unref (task);
}

static void
modem_power_up (MMIfaceModem        *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_radio_state_set_new (MBIM_RADIO_SWITCH_STATE_ON, NULL);
    mbim_device_command (device,
                         message,
                         20,
                         NULL,
                         (GAsyncReadyCallback)radio_state_set_up_ready,
                         task);
}

/*****************************************************************************/
/* Power down (Modem interface) */

static gboolean
power_down_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
radio_state_set_down_ready (MbimDevice *device,
                            GAsyncResult *res,
                            GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_radio_state_set_new (MBIM_RADIO_SWITCH_STATE_OFF, NULL);
    mbim_device_command (device,
                         message,
                         20,
                         NULL,
                         (GAsyncReadyCallback)radio_state_set_down_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Signal quality loading (Modem interface) */

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    gssize value;

    value = g_task_propagate_int (G_TASK (res), error);
    return value < 0 ? 0 : value;
}

static void
signal_state_query_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemMbim            *self;
    GError                          *error = NULL;
    g_autoptr(MbimMessage)           response = NULL;
    g_autoptr(MbimRsrpSnrInfoArray)  rsrp_snr = NULL;
    guint32                          rsrp_snr_count = 0;
    guint32                          rssi;
    guint32                          error_rate = 99;
    guint                            quality;
    MbimDataClass                    data_class;
    g_autoptr(MMSignal)              cdma = NULL;
    g_autoptr(MMSignal)              evdo = NULL;
    g_autoptr(MMSignal)              gsm = NULL;
    g_autoptr(MMSignal)              umts = NULL;
    g_autoptr(MMSignal)              lte = NULL;
    g_autoptr(MMSignal)              nr5g = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (!mbim_message_ms_basic_connect_v2_signal_state_response_parse (
                response,
                &rssi,
                &error_rate,
                NULL, /* signal_strength_interval */
                NULL, /* rssi_threshold */
                NULL, /* error_rate_threshold */
                &rsrp_snr_count,
                &rsrp_snr,
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v2.0 signal state response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v2.0 signal state response");
    } else {
        if (!mbim_message_signal_state_response_parse (
                response,
                &rssi,
                &error_rate,
                NULL, /* signal_strength_interval */
                NULL, /* rssi_threshold */
                NULL, /* error_rate_threshold */
                &error))
            g_prefix_error (&error, "Failed processing signal state response: ");
        else
            mm_obj_dbg (self, "processed signal state response");
    }

    if (error)
        g_task_return_error (task, error);
    else {
        /* Best guess of current data class */
        data_class = self->priv->enabled_cache.highest_available_data_class;
        if (data_class == 0)
            data_class = self->priv->enabled_cache.available_data_classes;
        if (mm_signal_from_mbim_signal_state (data_class, rssi, error_rate, rsrp_snr, rsrp_snr_count,
                                              self, &cdma, &evdo, &gsm, &umts, &lte, &nr5g))
            mm_iface_modem_signal_update (MM_IFACE_MODEM_SIGNAL (self), cdma, evdo, gsm, umts, lte, nr5g);

        quality = mm_signal_quality_from_mbim_signal_state (rssi, rsrp_snr, rsrp_snr_count, self);
        g_task_return_int (task, quality);
    }
    g_object_unref (task);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_signal_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)signal_state_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Reset */

static gboolean
modem_reset_finish (MMIfaceModem  *self,
                    GAsyncResult  *res,
                    GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
shared_qmi_reset_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError *error = NULL;

    if (!mm_shared_qmi_reset_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_reset_shared_qmi (GTask *task)
{
    mm_shared_qmi_reset (MM_IFACE_MODEM (g_task_get_source_object (task)),
                         (GAsyncReadyCallback)shared_qmi_reset_ready,
                         task);
}

#endif

static void
intel_firmware_update_modem_reboot_set_ready (MbimDevice   *device,
                                              GAsyncResult *res,
                                              GTask        *task)
{
    MbimMessage      *response;
    GError           *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
        /* We don't really expect the Intel firmware update service to be
         * available in QMI modems, but doesn't harm to fallback to the QMI
         * implementation here */
        mm_obj_dbg (g_task_get_source_object (task), "couldn't run intel reset: %s", error->message);
        g_error_free (error);
        modem_reset_shared_qmi (task);
#else
        g_task_return_error (task, error);
        g_object_unref (task);
#endif
    } else {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
    }

    if (response)
        mbim_message_unref (response);
}

static void
modem_reset (MMIfaceModem        *_self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                *task;
    MbimDevice           *device;
    MbimMessage          *message;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_intel_reset_supported) {
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
        modem_reset_shared_qmi (task);
#else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "modem reset operation is not supported");
        g_object_unref (task);
#endif
        return;
    }

    /* This message is defined in the Intel Firmware Update service, but it
     * really is just a standard modem reboot. */
    message = mbim_message_intel_firmware_update_modem_reboot_set_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)intel_firmware_update_modem_reboot_set_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Cell info retrieval */

typedef enum {
    GET_CELL_INFO_STEP_FIRST,
    GET_CELL_INFO_STEP_RFIM,
    GET_CELL_INFO_STEP_CELL_INFO,
    GET_CELL_INFO_STEP_LAST
} GetCellInfoStep;

typedef struct {
    GetCellInfoStep step;
    GList           *rfim_info_list;
    GList           *cell_info_list;
    GError          *saved_error;
} GetCellInfoContext;

static void
get_cell_info_context_free (GetCellInfoContext *ctx)
{
    mm_rfim_info_list_free (ctx->rfim_info_list);
    g_assert (!ctx->saved_error);
    g_free (ctx);
}

static void get_cell_info_step (MbimDevice *device,
                                GTask      *task);

static GList *
modem_get_cell_info_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
cell_info_list_free (GList *list)
{
    g_list_free_full (list, (GDestroyNotify)g_object_unref);
}

static void
base_stations_info_query_ready (MbimDevice   *device,
                                GAsyncResult *res,
                                GTask        *task)
{
    MMBroadbandModemMbim                          *self;
    g_autoptr(MbimMessage)                         response = NULL;
    GError                                        *error = NULL;
    GList                                         *list = NULL;
    MMCellInfo                                    *info = NULL;
    g_autoptr(MbimCellInfoServingGsm)              gsm_serving_cell = NULL;
    g_autoptr(MbimCellInfoServingUmts)             umts_serving_cell = NULL;
    g_autoptr(MbimCellInfoServingTdscdma)          tdscdma_serving_cell = NULL;
    g_autoptr(MbimCellInfoServingLte)              lte_serving_cell = NULL;
    guint32                                        gsm_neighboring_cells_count = 0;
    g_autoptr(MbimCellInfoNeighboringGsmArray)     gsm_neighboring_cells = NULL;
    guint32                                        umts_neighboring_cells_count = 0;
    g_autoptr(MbimCellInfoNeighboringUmtsArray)    umts_neighboring_cells = NULL;
    guint32                                        tdscdma_neighboring_cells_count = 0;
    g_autoptr(MbimCellInfoNeighboringTdscdmaArray) tdscdma_neighboring_cells = NULL;
    guint32                                        lte_neighboring_cells_count = 0;
    g_autoptr(MbimCellInfoNeighboringLteArray)     lte_neighboring_cells = NULL;
    guint32                                        cdma_cells_count = 0;
    g_autoptr(MbimCellInfoCdmaArray)               cdma_cells = NULL;
    guint32                                        nr_serving_cells_count = 0;
    g_autoptr(MbimCellInfoServingNrArray)          nr_serving_cells = NULL;
    guint32                                        nr_neighboring_cells_count = 0;
    g_autoptr(MbimCellInfoNeighboringNrArray)      nr_neighboring_cells = NULL;
    GetCellInfoContext                             *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        ctx->saved_error = error;
        ctx->step = GET_CELL_INFO_STEP_LAST;
        get_cell_info_step (device, task);
        return;
    }

    /* MBIMEx 3.0 support */
    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_extensions_v3_base_stations_info_response_parse (
                response,
                NULL, /* system_type_v3 */
                NULL, /* system_subtype */
                &gsm_serving_cell,
                &umts_serving_cell,
                &tdscdma_serving_cell,
                &lte_serving_cell,
                &gsm_neighboring_cells_count,
                &gsm_neighboring_cells,
                &umts_neighboring_cells_count,
                &umts_neighboring_cells,
                &tdscdma_neighboring_cells_count,
                &tdscdma_neighboring_cells,
                &lte_neighboring_cells_count,
                &lte_neighboring_cells,
                &cdma_cells_count,
                &cdma_cells,
                &nr_serving_cells_count,
                &nr_serving_cells,
                &nr_neighboring_cells_count,
                &nr_neighboring_cells,
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v3.0 base stations info response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 base stations info response");
    }
    /* MBIMEx 1.0 support */
    else {
        if (!mbim_message_ms_basic_connect_extensions_base_stations_info_response_parse (
                response,
                NULL, /* system_type */
                &gsm_serving_cell,
                &umts_serving_cell,
                &tdscdma_serving_cell,
                &lte_serving_cell,
                &gsm_neighboring_cells_count,
                &gsm_neighboring_cells,
                &umts_neighboring_cells_count,
                &umts_neighboring_cells,
                &tdscdma_neighboring_cells_count,
                &tdscdma_neighboring_cells,
                &lte_neighboring_cells_count,
                &lte_neighboring_cells,
                &cdma_cells_count,
                &cdma_cells,
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v1.0 base stations info response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v1.0 base stations info response");
    }

    if (error) {
        ctx->saved_error = error;
        ctx->step = GET_CELL_INFO_STEP_LAST;
        get_cell_info_step (device, task);
        return;
    }

#define CELL_INFO_SET_STR(VALUE, SETTER, CELL_TYPE) do {    \
        if (VALUE) {                                        \
            mm_cell_info_##SETTER (CELL_TYPE (info), VALUE); \
        }                                                   \
    } while (0)

#define CELL_INFO_SET_HEXSTR(VALUE, UNKNOWN, MODIFIER, SETTER, CELL_TYPE) do { \
        if (VALUE != UNKNOWN) {                                                \
            g_autofree gchar *str = NULL;                                      \
                                                                               \
            str = g_strdup_printf ("%" MODIFIER "X", VALUE);                   \
            mm_cell_info_##SETTER (CELL_TYPE (info), str);                     \
        }                                                                      \
    } while (0)

#define CELL_INFO_SET_UINT(VALUE, UNKNOWN, SETTER, CELL_TYPE) do { \
        if (VALUE != UNKNOWN) {                                    \
            mm_cell_info_##SETTER (CELL_TYPE (info), VALUE);       \
        }                                                          \
    } while (0)

#define CELL_INFO_SET_INT_DOUBLE(VALUE, UNKNOWN, SETTER, CELL_TYPE) do { \
        if (VALUE != (gint)UNKNOWN) {                                    \
            mm_cell_info_##SETTER (CELL_TYPE (info), (gdouble)VALUE);    \
        }                                                                \
    } while (0)

#define CELL_INFO_SET_UINT_DOUBLE_SCALED(VALUE, UNKNOWN, SCALE, SETTER, CELL_TYPE) do { \
        if (VALUE != UNKNOWN) {                                                  \
            mm_cell_info_##SETTER (CELL_TYPE (info), (gdouble)(VALUE + SCALE));        \
        }                                                                              \
    } while (0)

    if (gsm_serving_cell) {
        info = mm_cell_info_gsm_new_from_dictionary (NULL);
        mm_cell_info_set_serving (info, TRUE);

        CELL_INFO_SET_STR    (gsm_serving_cell->provider_id,                        gsm_set_operator_id,     MM_CELL_INFO_GSM);
        CELL_INFO_SET_HEXSTR (gsm_serving_cell->location_area_code, 0xFFFFFFFF, "", gsm_set_lac,             MM_CELL_INFO_GSM);
        CELL_INFO_SET_HEXSTR (gsm_serving_cell->cell_id,            0xFFFFFFFF, "", gsm_set_ci,              MM_CELL_INFO_GSM);
        CELL_INFO_SET_UINT   (gsm_serving_cell->timing_advance,     0xFFFFFFFF,     gsm_set_timing_advance,  MM_CELL_INFO_GSM);
        CELL_INFO_SET_UINT   (gsm_serving_cell->arfcn,              0xFFFFFFFF,     gsm_set_arfcn,           MM_CELL_INFO_GSM);
        CELL_INFO_SET_HEXSTR (gsm_serving_cell->base_station_id,    0xFFFFFFFF, "", gsm_set_base_station_id, MM_CELL_INFO_GSM);
        CELL_INFO_SET_UINT   (gsm_serving_cell->rx_level,           0xFFFFFFFF,     gsm_set_rx_level,        MM_CELL_INFO_GSM);

        list = g_list_append (list, g_steal_pointer (&info));
    }

    if (gsm_neighboring_cells_count && gsm_neighboring_cells) {
        guint i;

        for (i = 0; i < gsm_neighboring_cells_count; i++) {
            info = mm_cell_info_gsm_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, FALSE);

            CELL_INFO_SET_STR    (gsm_neighboring_cells[i]->provider_id,                        gsm_set_operator_id,     MM_CELL_INFO_GSM);
            CELL_INFO_SET_HEXSTR (gsm_neighboring_cells[i]->location_area_code, 0xFFFFFFFF, "", gsm_set_lac,             MM_CELL_INFO_GSM);
            CELL_INFO_SET_HEXSTR (gsm_neighboring_cells[i]->cell_id,            0xFFFFFFFF, "", gsm_set_ci,              MM_CELL_INFO_GSM);
            CELL_INFO_SET_UINT   (gsm_neighboring_cells[i]->arfcn,              0xFFFFFFFF,     gsm_set_arfcn,           MM_CELL_INFO_GSM);
            CELL_INFO_SET_HEXSTR (gsm_neighboring_cells[i]->base_station_id,    0xFFFFFFFF, "", gsm_set_base_station_id, MM_CELL_INFO_GSM);
            CELL_INFO_SET_UINT   (gsm_neighboring_cells[i]->rx_level,           0xFFFFFFFF,     gsm_set_rx_level,        MM_CELL_INFO_GSM);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (umts_serving_cell) {
        info = mm_cell_info_umts_new_from_dictionary (NULL);
        mm_cell_info_set_serving (info, TRUE);

        CELL_INFO_SET_STR        (umts_serving_cell->provider_id,                             umts_set_operator_id,      MM_CELL_INFO_UMTS);
        CELL_INFO_SET_HEXSTR     (umts_serving_cell->location_area_code,      0xFFFFFFFF, "", umts_set_lac,              MM_CELL_INFO_UMTS);
        CELL_INFO_SET_HEXSTR     (umts_serving_cell->cell_id,                 0xFFFFFFFF, "", umts_set_ci,               MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->frequency_info_ul,       0xFFFFFFFF,     umts_set_frequency_fdd_ul, MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->frequency_info_dl,       0xFFFFFFFF,     umts_set_frequency_fdd_dl, MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->frequency_info_nt,       0xFFFFFFFF,     umts_set_frequency_tdd,    MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->uarfcn,                  0xFFFFFFFF,     umts_set_uarfcn,           MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->primary_scrambling_code, 0xFFFFFFFF,     umts_set_psc,              MM_CELL_INFO_UMTS);
        CELL_INFO_SET_INT_DOUBLE (umts_serving_cell->rscp,                    0,              umts_set_rscp,             MM_CELL_INFO_UMTS);
        CELL_INFO_SET_INT_DOUBLE (umts_serving_cell->ecno,                    1,              umts_set_ecio,             MM_CELL_INFO_UMTS);
        CELL_INFO_SET_UINT       (umts_serving_cell->path_loss,               0xFFFFFFFF,     umts_set_path_loss,        MM_CELL_INFO_UMTS);

        list = g_list_append (list, g_steal_pointer (&info));
    }

    if (umts_neighboring_cells_count && umts_neighboring_cells) {
        guint i;

        for (i = 0; i < umts_neighboring_cells_count; i++) {
            info = mm_cell_info_umts_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, FALSE);

            CELL_INFO_SET_STR        (umts_neighboring_cells[i]->provider_id,                             umts_set_operator_id, MM_CELL_INFO_UMTS);
            CELL_INFO_SET_HEXSTR     (umts_neighboring_cells[i]->location_area_code,      0xFFFFFFFF, "", umts_set_lac,         MM_CELL_INFO_UMTS);
            CELL_INFO_SET_HEXSTR     (umts_neighboring_cells[i]->cell_id,                 0xFFFFFFFF, "", umts_set_ci,          MM_CELL_INFO_UMTS);
            CELL_INFO_SET_UINT       (umts_neighboring_cells[i]->uarfcn,                  0xFFFFFFFF,     umts_set_uarfcn,      MM_CELL_INFO_UMTS);
            CELL_INFO_SET_UINT       (umts_neighboring_cells[i]->primary_scrambling_code, 0xFFFFFFFF,     umts_set_psc,         MM_CELL_INFO_UMTS);
            CELL_INFO_SET_INT_DOUBLE (umts_neighboring_cells[i]->rscp,                    0,              umts_set_rscp,        MM_CELL_INFO_UMTS);
            CELL_INFO_SET_INT_DOUBLE (umts_neighboring_cells[i]->ecno,                    1,              umts_set_ecio,        MM_CELL_INFO_UMTS);
            CELL_INFO_SET_UINT       (umts_neighboring_cells[i]->path_loss,               0xFFFFFFFF,     umts_set_path_loss,   MM_CELL_INFO_UMTS);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (tdscdma_serving_cell) {
        info = mm_cell_info_tdscdma_new_from_dictionary (NULL);
        mm_cell_info_set_serving (info, TRUE);

        CELL_INFO_SET_STR        (tdscdma_serving_cell->provider_id,                        tdscdma_set_operator_id,       MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_HEXSTR     (tdscdma_serving_cell->location_area_code, 0xFFFFFFFF, "", tdscdma_set_lac,               MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_HEXSTR     (tdscdma_serving_cell->cell_id,            0xFFFFFFFF, "", tdscdma_set_ci,                MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_UINT       (tdscdma_serving_cell->uarfcn,             0xFFFFFFFF,     tdscdma_set_uarfcn,            MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_UINT       (tdscdma_serving_cell->cell_parameter_id,  0xFFFFFFFF,     tdscdma_set_cell_parameter_id, MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_UINT       (tdscdma_serving_cell->timing_advance,     0xFFFFFFFF,     tdscdma_set_timing_advance,    MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_INT_DOUBLE (tdscdma_serving_cell->rscp,               0xFFFFFFFF,     tdscdma_set_rscp,              MM_CELL_INFO_TDSCDMA);
        CELL_INFO_SET_UINT       (tdscdma_serving_cell->path_loss,          0xFFFFFFFF,     tdscdma_set_path_loss,         MM_CELL_INFO_TDSCDMA);

        list = g_list_append (list, g_steal_pointer (&info));
    }

    if (tdscdma_neighboring_cells_count && tdscdma_neighboring_cells) {
        guint i;

        for (i = 0; i < tdscdma_neighboring_cells_count; i++) {
            info = mm_cell_info_tdscdma_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, FALSE);

            CELL_INFO_SET_STR        (tdscdma_neighboring_cells[i]->provider_id,                        tdscdma_set_operator_id,       MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_HEXSTR     (tdscdma_neighboring_cells[i]->location_area_code, 0xFFFFFFFF, "", tdscdma_set_lac,               MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_HEXSTR     (tdscdma_neighboring_cells[i]->cell_id,            0xFFFFFFFF, "", tdscdma_set_ci,                MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_UINT       (tdscdma_neighboring_cells[i]->uarfcn,             0xFFFFFFFF,     tdscdma_set_uarfcn,            MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_UINT       (tdscdma_neighboring_cells[i]->cell_parameter_id,  0xFFFFFFFF,     tdscdma_set_cell_parameter_id, MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_UINT       (tdscdma_neighboring_cells[i]->timing_advance,     0xFFFFFFFF,     tdscdma_set_timing_advance,    MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_INT_DOUBLE (tdscdma_neighboring_cells[i]->rscp,               0xFFFFFFFF,     tdscdma_set_rscp,              MM_CELL_INFO_TDSCDMA);
            CELL_INFO_SET_UINT       (tdscdma_neighboring_cells[i]->path_loss,          0xFFFFFFFF,     tdscdma_set_path_loss,         MM_CELL_INFO_TDSCDMA);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (lte_serving_cell) {
        GList *l;
        GList *next;

        info = mm_cell_info_lte_new_from_dictionary (NULL);
        mm_cell_info_set_serving (info, TRUE);

        CELL_INFO_SET_STR        (lte_serving_cell->provider_id,                         lte_set_operator_id,      MM_CELL_INFO_LTE);
        CELL_INFO_SET_HEXSTR     (lte_serving_cell->tac,                 0xFFFFFFFF, "", lte_set_tac,              MM_CELL_INFO_LTE);
        CELL_INFO_SET_HEXSTR     (lte_serving_cell->cell_id,             0xFFFFFFFF, "", lte_set_ci,               MM_CELL_INFO_LTE);
        CELL_INFO_SET_HEXSTR     (lte_serving_cell->physical_cell_id,    0xFFFFFFFF, "", lte_set_physical_ci,      MM_CELL_INFO_LTE);
        CELL_INFO_SET_UINT       (lte_serving_cell->earfcn,              0xFFFFFFFF,     lte_set_earfcn,           MM_CELL_INFO_LTE);
        CELL_INFO_SET_INT_DOUBLE (lte_serving_cell->rsrp,                0xFFFFFFFF,     lte_set_rsrp,             MM_CELL_INFO_LTE);
        CELL_INFO_SET_INT_DOUBLE (lte_serving_cell->rsrq,                0xFFFFFFFF,     lte_set_rsrq,             MM_CELL_INFO_LTE);
        CELL_INFO_SET_UINT       (lte_serving_cell->timing_advance,      0xFFFFFFFF,     lte_set_timing_advance,   MM_CELL_INFO_LTE);

        /* Update cell info with the radio frequency information received previously */
        for (l = ctx->rfim_info_list, next = NULL; l; l = next) {
            MMRfInfo *data;

            next = g_list_next (l);

            data = (MMRfInfo *)(l->data);
            if (fabs ((mm_earfcn_to_frequency (lte_serving_cell->earfcn, self)) - data->center_frequency) < FREQUENCY_TOLERANCE_HZ) {
                mm_obj_dbg (self, "Merging radio frequency data with lte serving cell info");
                CELL_INFO_SET_UINT (data->serving_cell_type, MM_SERVING_CELL_TYPE_INVALID, lte_set_serving_cell_type, MM_CELL_INFO_LTE);
                CELL_INFO_SET_UINT (data->bandwidth,         0xFFFFFFFF,                   lte_set_bandwidth,         MM_CELL_INFO_LTE);
                ctx->rfim_info_list = g_list_delete_link (ctx->rfim_info_list, l);
                mm_rf_info_free (data);
            }
        }
        list = g_list_append (list, g_steal_pointer (&info));
    }

    if (lte_neighboring_cells_count && lte_neighboring_cells) {
        guint i;

        for (i = 0; i < lte_neighboring_cells_count; i++) {
            info = mm_cell_info_lte_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, FALSE);

            CELL_INFO_SET_STR        (lte_neighboring_cells[i]->provider_id,                      lte_set_operator_id, MM_CELL_INFO_LTE);
            CELL_INFO_SET_HEXSTR     (lte_neighboring_cells[i]->tac,              0xFFFFFFFF, "", lte_set_tac,         MM_CELL_INFO_LTE);
            CELL_INFO_SET_HEXSTR     (lte_neighboring_cells[i]->cell_id,          0xFFFFFFFF, "", lte_set_ci,          MM_CELL_INFO_LTE);
            CELL_INFO_SET_HEXSTR     (lte_neighboring_cells[i]->physical_cell_id, 0xFFFFFFFF, "", lte_set_physical_ci, MM_CELL_INFO_LTE);
            CELL_INFO_SET_UINT       (lte_neighboring_cells[i]->earfcn,           0xFFFFFFFF,     lte_set_earfcn,      MM_CELL_INFO_LTE);
            CELL_INFO_SET_INT_DOUBLE (lte_neighboring_cells[i]->rsrp,             0xFFFFFFFF,     lte_set_rsrp,        MM_CELL_INFO_LTE);
            CELL_INFO_SET_INT_DOUBLE (lte_neighboring_cells[i]->rsrq,             0xFFFFFFFF,     lte_set_rsrq,        MM_CELL_INFO_LTE);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (cdma_cells_count && cdma_cells) {
        guint i;

        for (i = 0; i < cdma_cells_count; i++) {
            info = mm_cell_info_cdma_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, cdma_cells[i]->serving_cell_flag);

            CELL_INFO_SET_HEXSTR     (cdma_cells[i]->nid,              0xFFFFFFFF, "", cdma_set_nid,             MM_CELL_INFO_CDMA);
            CELL_INFO_SET_HEXSTR     (cdma_cells[i]->sid,              0xFFFFFFFF, "", cdma_set_sid,             MM_CELL_INFO_CDMA);
            CELL_INFO_SET_HEXSTR     (cdma_cells[i]->base_station_id,  0xFFFFFFFF, "", cdma_set_base_station_id, MM_CELL_INFO_CDMA);
            CELL_INFO_SET_HEXSTR     (cdma_cells[i]->ref_pn,           0xFFFFFFFF, "", cdma_set_ref_pn,          MM_CELL_INFO_CDMA);
            CELL_INFO_SET_UINT       (cdma_cells[i]->pilot_strength,   0xFFFFFFFF,     cdma_set_pilot_strength,  MM_CELL_INFO_CDMA);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (nr_serving_cells_count && nr_serving_cells) {
        guint i;

        for (i = 0; i < nr_serving_cells_count; i++) {
            GList *l;
            GList *next;

            info = mm_cell_info_nr5g_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, TRUE);

            CELL_INFO_SET_STR                (nr_serving_cells[i]->provider_id,                                             nr5g_set_operator_id,      MM_CELL_INFO_NR5G);
            CELL_INFO_SET_HEXSTR             (nr_serving_cells[i]->tac,              0xFFFFFFFF,         "",                nr5g_set_tac,              MM_CELL_INFO_NR5G);
            CELL_INFO_SET_HEXSTR             (nr_serving_cells[i]->nci,              0xFFFFFFFFFFFFFFFF, G_GINT64_MODIFIER, nr5g_set_ci,               MM_CELL_INFO_NR5G);
            CELL_INFO_SET_HEXSTR             (nr_serving_cells[i]->physical_cell_id, 0xFFFFFFFF,         "",                nr5g_set_physical_ci,      MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT               (nr_serving_cells[i]->nrarfcn,          0xFFFFFFFF,                            nr5g_set_nrarfcn,          MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_serving_cells[i]->rsrp,             0xFFFFFFFF,         -156,              nr5g_set_rsrp,             MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_serving_cells[i]->rsrq,             0xFFFFFFFF,         -43,               nr5g_set_rsrq,             MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_serving_cells[i]->sinr,             0xFFFFFFFF,         -23,               nr5g_set_sinr,             MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT               (nr_serving_cells[i]->timing_advance,   0xFFFFFFFFFFFFFFFF,                    nr5g_set_timing_advance,   MM_CELL_INFO_NR5G);

            /* Update cell info with the radio frequency information received previously */
            for (l = ctx->rfim_info_list, next = NULL; l; l = next) {
                MMRfInfo *data;

                next = g_list_next (l);

                data = (MMRfInfo *)(l->data);
                /* Comparing the derived frequncy value from NRARFCN with received center frequency data to map the NR CELL */
                if (fabs (mm_nrarfcn_to_frequency (nr_serving_cells[i]->nrarfcn, self) - data->center_frequency) < FREQUENCY_TOLERANCE_HZ) {
                    mm_obj_dbg (self, "Merging radio frequency data with 5gnr serving cell info");
                    CELL_INFO_SET_UINT (data->serving_cell_type, MM_SERVING_CELL_TYPE_INVALID, nr5g_set_serving_cell_type, MM_CELL_INFO_NR5G);
                    CELL_INFO_SET_UINT (data->bandwidth,         0xFFFFFFFF,                   nr5g_set_bandwidth,         MM_CELL_INFO_NR5G);
                    ctx->rfim_info_list = g_list_delete_link (ctx->rfim_info_list, l);
                    mm_rf_info_free (data);
                }
            }
            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

    if (nr_neighboring_cells_count && nr_neighboring_cells) {
        guint i;

        for (i = 0; i < nr_neighboring_cells_count; i++) {
            info = mm_cell_info_nr5g_new_from_dictionary (NULL);
            mm_cell_info_set_serving (info, FALSE);

            CELL_INFO_SET_STR                (nr_neighboring_cells[i]->provider_id,                        nr5g_set_operator_id, MM_CELL_INFO_NR5G);
            CELL_INFO_SET_HEXSTR             (nr_neighboring_cells[i]->tac,              0xFFFFFFFF, "",   nr5g_set_tac,         MM_CELL_INFO_NR5G);
            CELL_INFO_SET_STR                (nr_neighboring_cells[i]->cell_id,                            nr5g_set_ci,          MM_CELL_INFO_NR5G);
            CELL_INFO_SET_HEXSTR             (nr_neighboring_cells[i]->physical_cell_id, 0xFFFFFFFF, "",   nr5g_set_physical_ci, MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_neighboring_cells[i]->rsrp,             0xFFFFFFFF, -156, nr5g_set_rsrp,        MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_neighboring_cells[i]->rsrq,             0xFFFFFFFF, -43,  nr5g_set_rsrq,        MM_CELL_INFO_NR5G);
            CELL_INFO_SET_UINT_DOUBLE_SCALED (nr_neighboring_cells[i]->sinr,             0xFFFFFFFF, -23,  nr5g_set_sinr,        MM_CELL_INFO_NR5G);

            list = g_list_append (list, g_steal_pointer (&info));
        }
    }

#undef CELL_INFO_SET_STR
#undef CELL_INFO_SET_HEXSTR
#undef CELL_INFO_SET_UINT
#undef CELL_INFO_SET_INT_DOUBLE
#undef CELL_INFO_SET_UINT_DOUBLE_SCALED

    ctx->cell_info_list = list;
    ctx->step++;
    get_cell_info_step (device, task);
}

static void
check_rfim_query_ready (MbimDevice   *device,
                        GAsyncResult *res,
                        GTask        *task)
{
    g_autoptr(MbimMessage)            response = NULL;
    MbimIntelRfimFrequencyValueArray  *freq_info;
    guint                             freq_count;
    GetCellInfoContext                *ctx;
    MMBroadbandModemMbim              *self;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, NULL);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, NULL) &&
        mbim_message_intel_thermal_rf_rfim_response_parse (response,
                                                           &freq_count,
                                                           &freq_info,
                                                           NULL)) {

        ctx->rfim_info_list = mm_rfim_info_list_from_mbim_intel_rfim_frequency_value_array (freq_info,
                                                                                            freq_count,
                                                                                            self);
        mbim_intel_rfim_frequency_value_array_free (freq_info);
    } else {
        mm_obj_dbg (self, "Fetching of bandwidth and serving cell type data failed");
    }
    ctx->step++;
    get_cell_info_step (device, task);
}

static void
get_cell_info_step (MbimDevice *device,
                    GTask      *task)
{
    MMBroadbandModemMbim    *self;
    GetCellInfoContext      *ctx;
    g_autoptr(MbimMessage)  message = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case GET_CELL_INFO_STEP_FIRST:
    if (!self->priv->is_base_stations_info_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "base stations info is not supported");
        g_object_unref (task);
        return;
    }
    ctx->step++;

    /* fall through */
    case GET_CELL_INFO_STEP_RFIM: {
        mm_obj_dbg (self, "Obtaining RFIM data...");
        message = mbim_message_intel_thermal_rf_rfim_query_new (NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)check_rfim_query_ready,
                             task);
        return;
    }

    case GET_CELL_INFO_STEP_CELL_INFO: {
        mm_obj_dbg (self, "Obtaining cell info...");
        /* Default capacity is 15 */
        if (mbim_device_check_ms_mbimex_version (device, 3, 0))
            message = mbim_message_ms_basic_connect_extensions_v3_base_stations_info_query_new (15, 15, 15, 15, 15, 15, NULL);
        else
            message = mbim_message_ms_basic_connect_extensions_base_stations_info_query_new (15, 15, 15, 15, 15, NULL);

        mbim_device_command (device,
                             message,
                             300,
                             NULL,
                             (GAsyncReadyCallback)base_stations_info_query_ready,
                             task);
        return;
    }

    case GET_CELL_INFO_STEP_LAST:
        if (ctx->saved_error)
            g_task_return_error (task, g_steal_pointer (&ctx->saved_error));
        else if (ctx->cell_info_list)
            g_task_return_pointer (task, ctx->cell_info_list, (GDestroyNotify)cell_info_list_free);
        else
            g_assert_not_reached ();
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}


static void
modem_get_cell_info (MMIfaceModem        *_self,
                     GAsyncReadyCallback callback,
                     gpointer            user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice           *device;
    GTask                *task;
    GetCellInfoContext   *ctx;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_new0 (GetCellInfoContext, 1);
    ctx->step = GET_CELL_INFO_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify)get_cell_info_context_free);
    get_cell_info_step (device, task);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_create_bearer (MMIfaceModem        *self,
                     MMBearerProperties  *properties,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    MMBaseBearer *bearer;
    GTask        *task;

    /* Note: the session id to be used by the bearer will always be 0
     * for non-multiplexed sessions, bound to the non-VLAN-tagged traffic
     * managed by the main network interface */

    task = g_task_new (self, NULL, callback, user_data);
    mm_obj_dbg (self, "creating MBIM bearer in MBIM modem");
    bearer = mm_bearer_mbim_new (MM_BROADBAND_MODEM_MBIM (self), properties);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Create Bearer List (Modem interface) */

static MMBearerList *
modem_create_bearer_list (MMIfaceModem *self)
{
    MMPortMbim *port;
    guint       n;
    guint       n_multiplexed;

    /* The maximum number of available/connected modems is guessed from
     * the size of the data ports list. */
    n = g_list_length (mm_base_modem_peek_data_ports (MM_BASE_MODEM (self)));
    mm_obj_dbg (self, "allowed up to %u active bearers", n);

    /* The maximum number of multiplexed links is defined by the MBIM protocol */
    n_multiplexed = (MBIM_DEVICE_SESSION_ID_MAX - MBIM_DEVICE_SESSION_ID_MIN + 1);
    mm_obj_dbg (self, "allowed up to %u active multiplexed bearers", n_multiplexed);

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (port &&
        mm_kernel_device_has_global_property (mm_port_peek_kernel_device (MM_PORT (port)),
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

    /* by default, no multiplexing support */
    return mm_bearer_list_new (n, n_multiplexed);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_mbim_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New MBIM SIM */
    mm_sim_mbim_new (MM_BASE_MODEM (self),
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* Reset data interfaces during initialization */

typedef struct {
    GList      *ports;
    MMPort     *data;
    MMPortMbim *mbim;
} ResetPortsContext;

static void
reset_ports_context_free (ResetPortsContext *ctx)
{
    g_assert (!ctx->data);
    g_assert (!ctx->mbim);
    g_list_free_full (ctx->ports, g_object_unref);
    g_slice_free (ResetPortsContext, ctx);
}

static gboolean
reset_ports_finish (MMBroadbandModemMbim  *self,
                    GAsyncResult          *res,
                    GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void reset_next_port (GTask *task);

static void
port_mbim_reset_ready (MMPortMbim   *mbim,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMBroadbandModemMbim *self;
    ResetPortsContext   *ctx;
    g_autoptr(GError)    error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_mbim_reset_finish (mbim, res, &error))
        mm_obj_warn (self, "couldn't reset MBIM port '%s' with data interface '%s': %s",
                     mm_port_get_device (MM_PORT (ctx->mbim)),
                     mm_port_get_device (ctx->data),
                     error->message);

    g_clear_object (&ctx->data);
    g_clear_object (&ctx->mbim);
    reset_next_port (task);
}

static void
reset_next_port (GTask *task)
{
    MMBroadbandModemMbim *self;
    ResetPortsContext    *ctx;

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

    ctx->mbim = mm_broadband_modem_mbim_get_port_mbim_for_data (self, ctx->data, NULL);
    if (!ctx->mbim) {
        mm_obj_dbg (self, "no MBIM port associated to data port '%s': ignoring data interface reset",
                    mm_port_get_device (ctx->data));
        g_clear_object (&ctx->data);
        reset_next_port (task);
        return;
    }

    mm_obj_dbg (self, "running MBIM port '%s' reset with data interface '%s'",
                mm_port_get_device (MM_PORT (ctx->mbim)), mm_port_get_device (ctx->data));

    mm_port_mbim_reset (ctx->mbim,
                        ctx->data,
                        (GAsyncReadyCallback) port_mbim_reset_ready,
                        task);
}

static void
reset_ports (MMBroadbandModemMbim *self,
             GAsyncReadyCallback   callback,
             gpointer              user_data)
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

    if (!MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started_finish (
            self,
            res,
            &error)) {
        /* Don't treat this as fatal. Parent enabling may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
        mm_obj_dbg (self, "couldn't start parent enabling: %s", error->message);
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
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started (
        self,
        (GAsyncReadyCallback)parent_enabling_started_ready,
        task);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMPortMbim *mbim;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    guint       qmi_service_index;
#endif
} InitializationStartedContext;

static void
initialization_started_context_free (InitializationStartedContext *ctx)
{
    if (ctx->mbim)
        g_object_unref (ctx->mbim);
    g_slice_free (InitializationStartedContext, ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem  *self,
                               GAsyncResult      *res,
                               GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult     *res,
                                     GTask            *task)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        /* Don't treat this as fatal. Parent initialization may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
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
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started (
        self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static const QmiService qmi_services[] = {
    QMI_SERVICE_DMS,
    QMI_SERVICE_NAS,
    QMI_SERVICE_PDS,
    QMI_SERVICE_LOC,
    QMI_SERVICE_PDC,
    QMI_SERVICE_UIM,
};

static void allocate_next_qmi_client (GTask *task);

static void
mbim_port_allocate_qmi_client_ready (MMPortMbim   *mbim,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    MMBroadbandModemMbim         *self;
    InitializationStartedContext *ctx;
    GError                       *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_mbim_allocate_qmi_client_finish (mbim, res, &error)) {
        mm_obj_dbg (self, "couldn't allocate QMI client for service '%s': %s",
                qmi_service_get_string (qmi_services[ctx->qmi_service_index]),
                error->message);
        g_error_free (error);
    }

    ctx->qmi_service_index++;
    allocate_next_qmi_client (task);
}

static void
allocate_next_qmi_client (GTask *task)
{
    InitializationStartedContext *ctx;

    ctx = g_task_get_task_data (task);

    if (ctx->qmi_service_index == G_N_ELEMENTS (qmi_services)) {
        parent_initialization_started (task);
        return;
    }

    /* Otherwise, allocate next client */
    mm_port_mbim_allocate_qmi_client (ctx->mbim,
                                      qmi_services[ctx->qmi_service_index],
                                      NULL,
                                      (GAsyncReadyCallback)mbim_port_allocate_qmi_client_ready,
                                      task);
}

#endif

static void
query_device_services_ready (MbimDevice   *device,
                             GAsyncResult *res,
                             GTask        *task)
{
    MMBroadbandModemMbim *self;
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceServiceElement **device_services;
    guint32 device_services_count;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_device_services_response_parse (
            response,
            &device_services_count,
            NULL, /* max_dss_sessions */
            &device_services,
            &error)) {
        guint32 i;

        for (i = 0; i < device_services_count; i++) {
            MbimService service;
            guint32     j;

            service = mbim_uuid_to_service (&device_services[i]->device_service_id);

            if (service == MBIM_SERVICE_BASIC_CONNECT) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS) {
                        mm_obj_dbg (self, "Profile management is supported");
                        self->priv->is_profile_management_supported = TRUE;
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_MS_BASIC_CONNECT_EXTENSIONS) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PCO) {
                        mm_obj_dbg (self, "PCO is supported");
                        self->priv->is_pco_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_LTE_ATTACH_INFO) {
                        mm_obj_dbg (self, "LTE attach info is supported");
                        self->priv->is_lte_attach_info_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_SLOT_INFO_STATUS) {
                        mm_obj_dbg (self, "Slot info status is supported");
                        self->priv->is_slot_info_status_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_REGISTRATION_PARAMETERS) {
                        mm_obj_dbg (self, "5GNR registration settings are supported");
                        self->priv->is_nr5g_registration_settings_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_BASE_STATIONS_INFO) {
                        mm_obj_dbg (self, "Base stations info is supported");
                        self->priv->is_base_stations_info_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PROVISIONED_CONTEXTS) {
                        mm_obj_dbg (self, "Context types extension is supported");
                        self->priv->is_context_type_ext_supported = TRUE;
                        if (mm_context_get_test_mbimex_profile_management ()) {
                            mm_obj_dbg (self, "Profile management extension is supported");
                            self->priv->is_profile_management_ext_supported = TRUE;
                        } else
                            mm_obj_dbg (self, "Profile management extension is supported but not allowed");
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_USSD) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_USSD) {
                        mm_obj_dbg (self, "USSD is supported");
                        self->priv->is_ussd_supported = TRUE;
                        break;
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_ATDS) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_ATDS_LOCATION) {
                        mm_obj_dbg (self, "ATDS location is supported");
                        self->priv->is_atds_location_supported = TRUE;
                    } else if (device_services[i]->cids[j] == MBIM_CID_ATDS_SIGNAL) {
                        mm_obj_dbg (self, "ATDS signal is supported");
                        self->priv->is_atds_signal_supported = TRUE;
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_INTEL_FIRMWARE_UPDATE) {
                if (self->priv->intel_firmware_update_unsupported) {
                    mm_obj_dbg (self, "Intel firmware update service is explicitly ignored");
                    continue;
                }
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_INTEL_FIRMWARE_UPDATE_MODEM_REBOOT) {
                        mm_obj_dbg (self, "Intel reset is supported");
                        self->priv->is_intel_reset_supported = TRUE;
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_MS_SAR) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_MS_SAR_CONFIG) {
                        mm_obj_dbg (self, "SAR is supported");
                        self->priv->is_ms_sar_supported = TRUE;
                    }
                }
                continue;
            }

            if (service == MBIM_SERVICE_GOOGLE) {
                for (j = 0; j < device_services[i]->cids_count; j++) {
                    if (device_services[i]->cids[j] == MBIM_CID_GOOGLE_CARRIER_LOCK) {
                        mm_obj_dbg (self, "Google carrier lock is supported");
                        self->priv->is_google_carrier_lock_supported = TRUE;
                    }
                }
                continue;
            }

            /* no optional features to check in remaining services */
        }
        mbim_device_service_element_array_free (device_services);
    } else {
        /* Ignore error */
        mm_obj_warn (self, "couldn't query device services: %s", error->message);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    allocate_next_qmi_client (task);
#else
    parent_initialization_started (task);
#endif
}

static void
query_device_services (GTask *task)
{
    MMBroadbandModem *self;
    InitializationStartedContext *ctx;
    MbimMessage *message;
    MbimDevice *device;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    device = mm_port_mbim_peek_device (ctx->mbim);
    g_assert (device);

    mm_obj_dbg (self, "querying device services...");

    message = mbim_message_device_services_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)query_device_services_ready,
                         task);
    mbim_message_unref (message);
}

static void
mbim_port_open_ready (MMPortMbim   *mbim,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError *error = NULL;

    if (!mm_port_mbim_open_finish (mbim, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    query_device_services (task);
}

static void
initialization_open_port (GTask *task)
{
    InitializationStartedContext *ctx;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    MMBroadbandModemMbim         *self;
    gboolean                      qmi_unsupported = FALSE;
#endif

    ctx = g_task_get_task_data (task);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    self = g_task_get_source_object (task);
    g_object_get (self,
                  MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, &qmi_unsupported,
                  NULL);
#endif

    /* Now open our MBIM port */
    mm_port_mbim_open (ctx->mbim,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                       ! qmi_unsupported, /* With QMI over MBIM support if available */
#endif
                       NULL,
                       (GAsyncReadyCallback)mbim_port_open_ready,
                       task);
}

static void
reset_ports_ready (MMBroadbandModemMbim *self,
                   GAsyncResult         *res,
                   GTask                *task)
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
    MMBroadbandModemMbim *self;

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
initialization_started (MMBroadbandModem    *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    InitializationStartedContext *ctx;
    GTask                        *task;

    ctx = g_slice_new0 (InitializationStartedContext);
    ctx->mbim = mm_broadband_modem_mbim_get_port_mbim (MM_BROADBAND_MODEM_MBIM (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_started_context_free);

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->mbim) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot initialize: MBIM port went missing");
        g_object_unref (task);
        return;
    }

    if (mm_port_mbim_is_open (ctx->mbim)) {
        /* Nothing to be done, just connect to a signal and launch parent's
         * callback */
        query_device_services (task);
        return;
    }

    initialization_reset_ports (task);
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
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_device_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_device_id),
                               g_free);
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
    MMModem3gppFacility facilities;
} LoadEnabledFacilityLocksContext;

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
pin_list_query_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;
    MbimMessage *response;
    GError *error = NULL;
    MbimPinDesc *pin_desc_pin1;
    MbimPinDesc *pin_desc_pin2;
    MbimPinDesc *pin_desc_device_sim_pin;
    MbimPinDesc *pin_desc_device_first_sim_pin;
    MbimPinDesc *pin_desc_network_pin;
    MbimPinDesc *pin_desc_network_subset_pin;
    MbimPinDesc *pin_desc_service_provider_pin;
    MbimPinDesc *pin_desc_corporate_pin;

    ctx = g_task_get_task_data (task);
    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_list_response_parse (
            response,
            &pin_desc_pin1,
            &pin_desc_pin2,
            &pin_desc_device_sim_pin,
            &pin_desc_device_first_sim_pin,
            &pin_desc_network_pin,
            &pin_desc_network_subset_pin,
            &pin_desc_service_provider_pin,
            &pin_desc_corporate_pin,
            NULL, /* pin_desc_subsidy_lock */
            NULL, /* pin_desc_custom */
            &error)) {
        MMModem3gppFacility mask = ctx->facilities;

        if (pin_desc_pin1->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_SIM;
        mbim_pin_desc_free (pin_desc_pin1);

        if (pin_desc_pin2->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_FIXED_DIALING;
        mbim_pin_desc_free (pin_desc_pin2);

        if (pin_desc_device_sim_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PH_SIM;
        mbim_pin_desc_free (pin_desc_device_sim_pin);

        if (pin_desc_device_first_sim_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PH_FSIM;
        mbim_pin_desc_free (pin_desc_device_first_sim_pin);

        if (pin_desc_network_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_NET_PERS;
        mbim_pin_desc_free (pin_desc_network_pin);

        if (pin_desc_network_subset_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_NET_SUB_PERS;
        mbim_pin_desc_free (pin_desc_network_subset_pin);

        if (pin_desc_service_provider_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PROVIDER_PERS;
        mbim_pin_desc_free (pin_desc_service_provider_pin);

        if (pin_desc_corporate_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_CORP_PERS;
        mbim_pin_desc_free (pin_desc_corporate_pin);

        g_task_return_int (task, mask);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_enabled_facility_pin_query_ready (MbimDevice *device,
                                       GAsyncResult *res,
                                       GTask *task)
{
    LoadEnabledFacilityLocksContext *ctx;
    MbimMessage *response;
    MbimMessage *message;
    GError *error = NULL;
    MbimPinType pin_type;
    MbimPinState pin_state;

    ctx = g_task_get_task_data (task);
    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        if (mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, NULL) &&
            mbim_message_pin_response_parse (response, &pin_type, &pin_state, NULL, NULL) &&
            (pin_state == MBIM_PIN_STATE_LOCKED))
            ctx->facilities |= mm_modem_3gpp_facility_from_mbim_pin_type (pin_type);

        mbim_message_unref (response);
    }

    message = mbim_message_pin_list_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_list_query_ready,
                         task);
    mbim_message_unref (message);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    LoadEnabledFacilityLocksContext *ctx;
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_new0 (LoadEnabledFacilityLocksContext, 1);
    g_task_set_task_data (task, ctx, g_free);

    /* The PIN LIST command returns status of pin locks but omits PUK locked
     * facilities. To workaround this limitation additional PIN command query
     * was added to get currently active PIN or PUK lock.
     */
    message = mbim_message_pin_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)load_enabled_facility_pin_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Facility locks disabling (3GPP interface) */

typedef struct _DisableFacilityLockContext DisableFacilityLockContext;
struct _DisableFacilityLockContext {
    MbimPinType pin_type;
};

static gboolean
modem_3gpp_disable_facility_lock_finish (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_facility_lock_ready (MbimDevice *device,
                             GAsyncResult *res,
                             GTask *task)
{
    DisableFacilityLockContext *ctx;
    MbimMessage *response = NULL;
    guint32 remaining_attempts;
    MbimPinState pin_state;
    MbimPinType pin_type;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response,
                                                        MBIM_MESSAGE_TYPE_COMMAND_DONE,
                                                        &error)) {
        g_task_return_error (task, error);
    } else if (!mbim_message_pin_response_parse (response,
                                                 &pin_type,
                                                 &pin_state,
                                                 &remaining_attempts,
                                                 &error)) {
        g_task_return_error (task, error);
    } else if (pin_type == ctx->pin_type &&
               pin_state == MBIM_PIN_STATE_LOCKED) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Disabling PIN lock %s failed, remaining attempts: %u",
                                 mbim_pin_state_get_string (pin_state),
                                 remaining_attempts);
    } else
        g_task_return_boolean (task, TRUE);

    if (response)
        mbim_message_unref (response);
    g_object_unref (task);
}

static void
modem_3gpp_disable_facility_lock (MMIfaceModem3gpp *self,
                                  MMModem3gppFacility facility,
                                  guint8 slot,
                                  const gchar *key,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    DisableFacilityLockContext *ctx;
    MbimMessage *message;
    MbimPinType pin_type;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Set type of pin lock to disable */
    pin_type = mbim_pin_type_from_mm_modem_3gpp_facility (facility);
    if (pin_type == MBIM_PIN_TYPE_UNKNOWN) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Not supported type of facility lock.");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "Trying to disable %s lock using key: %s",
                mbim_pin_type_get_string (pin_type), key);

    ctx = g_new0 (DisableFacilityLockContext, 1);
    ctx->pin_type = pin_type;
    g_task_set_task_data (task, ctx, g_free);

    message = mbim_message_pin_set_new (pin_type,
                                        MBIM_PIN_OPERATION_DISABLE,
                                        key,
                                        NULL,
                                        NULL);

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)disable_facility_lock_ready,
                         task);
    mbim_message_unref (message);
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

static MMBearerProperties *
common_process_lte_attach_info (MMBroadbandModemMbim  *self,
                                guint32                lte_attach_state,
                                guint32                ip_type,
                                const gchar           *access_string,
                                const gchar           *user_name,
                                const gchar           *password,
                                guint32                compression,
                                guint32                auth_protocol,
                                GError               **error)
{
    MMBearerProperties  *properties;
    MMBearerIpFamily     ip_family;
    MMBearerAllowedAuth  auth;

    /* Remove LTE attach bearer info */
    if (lte_attach_state == MBIM_LTE_ATTACH_STATE_DETACHED) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Not attached to LTE");
        return NULL;
    }

    properties = mm_bearer_properties_new ();
    if (access_string)
        mm_bearer_properties_set_apn (properties, access_string);
    if (user_name)
        mm_bearer_properties_set_user (properties, user_name);
    if (password)
        mm_bearer_properties_set_password (properties, password);

    ip_family = mm_bearer_ip_family_from_mbim_context_ip_type (ip_type);
    if (ip_family != MM_BEARER_IP_FAMILY_NONE)
        mm_bearer_properties_set_ip_type (properties, ip_family);

    auth = mm_bearer_allowed_auth_from_mbim_auth_protocol (auth_protocol);
    if (auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
        mm_bearer_properties_set_allowed_auth (properties, auth);

    /* note: we don't expose compression settings */

    return properties;
}

static void
lte_attach_info_query_ready (MbimDevice   *device,
                             GAsyncResult *res,
                             GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    MMBearerProperties     *properties;
    guint32                 lte_attach_state;
    guint32                 ip_type;
    g_autofree gchar       *access_string = NULL;
    g_autofree gchar       *user_name = NULL;
    g_autofree gchar       *password = NULL;
    guint32                 compression;
    guint32                 auth_protocol;
    MbimNwError             nw_error = 0;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_extensions_v3_lte_attach_info_response_parse (
                response,
                &lte_attach_state,
                &nw_error,
                &ip_type,
                &access_string,
                &user_name,
                &password,
                &compression,
                &auth_protocol,
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v3.0 LTE attach info response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 LTE attach info response");
    } else {
        if (!mbim_message_ms_basic_connect_extensions_lte_attach_info_response_parse (
                response,
                &lte_attach_state,
                &ip_type,
                &access_string,
                &user_name,
                &password,
                &compression,
                &auth_protocol,
                &error))
            g_prefix_error (&error, "Failed processing LTE attach info response: ");
        else
            mm_obj_dbg (self, "processed LTE attach info response");
    }

    properties = common_process_lte_attach_info (self,
                                                 lte_attach_state,
                                                 ip_type,
                                                 access_string,
                                                 user_name,
                                                 password,
                                                 compression,
                                                 auth_protocol,
                                                 &error);
    g_assert (properties || error);

    if (!error) {
        /* If network error is reported, then log it */
        if (nw_error) {
            const gchar *nw_error_str;

            nw_error_str = mbim_nw_error_get_string (nw_error);
            if (nw_error_str)
                mm_obj_dbg (self, "LTE attach info network error reported: %s", nw_error_str);
            else
                mm_obj_dbg (self, "LTE attach info network error reported: 0x%x", nw_error);
        }
        g_task_return_pointer (task, properties, g_object_unref);
    } else {
        g_task_return_error (task, error);
    }
    g_object_unref (task);
}

static void
modem_3gpp_load_initial_eps_bearer (MMIfaceModem3gpp    *_self,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice           *device;
    MbimMessage          *message;
    GTask                *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_lte_attach_info_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "LTE attach status is unsupported");
        g_object_unref (task);
        return;
    }

    message = mbim_message_ms_basic_connect_extensions_lte_attach_info_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)lte_attach_info_query_ready,
                         task);
    mbim_message_unref (message);
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

static MMBearerProperties *
common_process_lte_attach_configuration (MMBroadbandModemMbim        *self,
                                         MbimLteAttachConfiguration  *config,
                                         GError                     **error)
{
    MMBearerProperties  *properties;
    MMBearerIpFamily     ip_family = MM_BEARER_IP_FAMILY_NONE;
    MMBearerAllowedAuth  auth;

    properties = mm_bearer_properties_new ();
    if (config->access_string)
        mm_bearer_properties_set_apn (properties, config->access_string);
    if (config->user_name)
        mm_bearer_properties_set_user (properties, config->user_name);
    if (config->password)
        mm_bearer_properties_set_password (properties, config->password);

    ip_family = mm_bearer_ip_family_from_mbim_context_ip_type (config->ip_type);
    if (ip_family != MM_BEARER_IP_FAMILY_NONE)
        mm_bearer_properties_set_ip_type (properties, ip_family);

    auth = mm_bearer_allowed_auth_from_mbim_auth_protocol (config->auth_protocol);
    if (auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
        mm_bearer_properties_set_allowed_auth (properties, auth);

    /* note: we don't expose compression settings or the configuration source details */

    return properties;
}

static void
lte_attach_configuration_query_ready (MbimDevice   *device,
                                      GAsyncResult *res,
                                      GTask        *task)
{
    MMBroadbandModemMbim        *self;
    MbimMessage                 *response;
    GError                      *error = NULL;
    MMBearerProperties          *properties = NULL;
    guint32                      n_configurations = 0;
    MbimLteAttachConfiguration **configurations = NULL;
    guint                        i;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_lte_attach_configuration_response_parse (
            response,
            &n_configurations,
            &configurations,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* We should always receive 3 configurations but the MBIM API doesn't force
     * that so we'll just assume we don't get always the same fixed number */
    for (i = 0; i < n_configurations; i++) {
        /* We only support configuring the HOME settings */
        if (configurations[i]->roaming != MBIM_LTE_ATTACH_CONTEXT_ROAMING_CONTROL_HOME)
            continue;
        properties = common_process_lte_attach_configuration (self, configurations[i], &error);
        break;
    }
    mbim_lte_attach_configuration_array_free (configurations);

    if (!properties && !error)
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                             "Couldn't find home network LTE attach settings");

    g_assert (properties || error);
    if (properties)
        g_task_return_pointer (task, properties, g_object_unref);
    else
        g_task_return_error (task, error);
    g_object_unref (task);

 out:
    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_load_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice           *device;
    MbimMessage          *message;
    GTask                *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_lte_attach_info_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "LTE attach status info is unsupported");
        g_object_unref (task);
        return;
    }

    message = mbim_message_ms_basic_connect_extensions_lte_attach_configuration_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)lte_attach_configuration_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Set initial EPS bearer settings
 *
 * The logic to set the EPS bearer settings requires us to first load the current
 * settings from the module, because we are only going to change the settings
 * associated to the HOME slot, we will leave untouched the PARTNER and NON-PARTNER
 * slots.
 */

static gboolean
modem_3gpp_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_lte_attach_configuration_set_ready (MbimDevice   *device,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MbimMessage          *response;
    GError               *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
before_set_lte_attach_configuration_query_ready (MbimDevice   *device,
                                                 GAsyncResult *res,
                                                 GTask        *task)
{
    MMBroadbandModemMbim        *self;
    MbimMessage                 *request;
    MbimMessage                 *response;
    GError                      *error = NULL;
    MMBearerProperties          *config;
    guint32                      n_configurations = 0;
    MbimLteAttachConfiguration **configurations = NULL;
    guint                        i;

    self   = g_task_get_source_object (task);
    config = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_lte_attach_configuration_response_parse (
            response,
            &n_configurations,
            &configurations,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }

    /* We should always receive 3 configurations but the MBIM API doesn't force
     * that so we'll just assume we don't get always the same fixed number */
    for (i = 0; i < n_configurations; i++) {
        MMBearerIpFamily ip_family;
        MMBearerAllowedAuth auth;

        /* We only support configuring the HOME settings */
        if (configurations[i]->roaming != MBIM_LTE_ATTACH_CONTEXT_ROAMING_CONTROL_HOME)
            continue;

        ip_family = mm_bearer_properties_get_ip_type (config);
        if (ip_family == MM_BEARER_IP_FAMILY_NONE || ip_family == MM_BEARER_IP_FAMILY_ANY)
            configurations[i]->ip_type = MBIM_CONTEXT_IP_TYPE_DEFAULT;
        else {
            configurations[i]->ip_type = mm_bearer_ip_family_to_mbim_context_ip_type (ip_family, &error);
            if (error) {
                configurations[i]->ip_type = MBIM_CONTEXT_IP_TYPE_DEFAULT;
                mm_obj_warn (self, "unexpected IP type settings requested: %s", error->message);
                g_clear_error (&error);
            }
        }

        g_clear_pointer (&(configurations[i]->access_string), g_free);
        configurations[i]->access_string = g_strdup (mm_bearer_properties_get_apn (config));

        g_clear_pointer (&(configurations[i]->user_name), g_free);
        configurations[i]->user_name = g_strdup (mm_bearer_properties_get_user (config));

        g_clear_pointer (&(configurations[i]->password), g_free);
        configurations[i]->password = g_strdup (mm_bearer_properties_get_password (config));

        auth = mm_bearer_properties_get_allowed_auth (config);
        if ((auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN) || configurations[i]->user_name || configurations[i]->password) {
            configurations[i]->auth_protocol = mm_bearer_allowed_auth_to_mbim_auth_protocol (auth, self, &error);
            if (error) {
                configurations[i]->auth_protocol = MBIM_AUTH_PROTOCOL_NONE;
                mm_obj_warn (self, "unexpected auth settings requested: %s", error->message);
                g_clear_error (&error);
            }
        } else {
            configurations[i]->auth_protocol = MBIM_AUTH_PROTOCOL_NONE;
        }

        configurations[i]->source = MBIM_CONTEXT_SOURCE_USER;
        configurations[i]->compression = MBIM_COMPRESSION_NONE;
        break;
    }

    request = mbim_message_ms_basic_connect_extensions_lte_attach_configuration_set_new (
                  MBIM_LTE_ATTACH_CONTEXT_OPERATION_DEFAULT,
                  n_configurations,
                  (const MbimLteAttachConfiguration *const *)configurations,
                  &error);
    if (!request) {
        g_task_return_error (task, error);
        g_object_unref (task);
        goto out;
    }
    mbim_device_command (device,
                         request,
                         10,
                         NULL,
                         (GAsyncReadyCallback)set_lte_attach_configuration_set_ready,
                         task);
    mbim_message_unref (request);

 out:
    if (configurations)
        mbim_lte_attach_configuration_array_free (configurations);

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *_self,
                                            MMBearerProperties  *config,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                *task;
    MbimDevice           *device;
    MbimMessage          *message;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_lte_attach_info_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "LTE attach configuration is unsupported");
        g_object_unref (task);
        return;
    }

    g_task_set_task_data (task, g_object_ref (config), g_object_unref);

    message = mbim_message_ms_basic_connect_extensions_lte_attach_configuration_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)before_set_lte_attach_configuration_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* 5GNR registration settings loading */

static MMNr5gRegistrationSettings *
modem_3gpp_load_nr5g_registration_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
registration_parameters_query_ready (MbimDevice   *device,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    GError                     *error = NULL;
    MMNr5gRegistrationSettings *settings;
    MbimMicoMode                mico_mode = MBIM_MICO_MODE_DEFAULT;
    MbimDrxCycle                drx_cycle = MBIM_DRX_CYCLE_NOT_SPECIFIED;
    g_autoptr(MbimMessage)      response = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_v3_registration_parameters_response_parse (
            response,
            &mico_mode,
            &drx_cycle,
            NULL, /* ladn info */
            NULL, /* default pdu activation hint */
            NULL, /* reregister if needed */
            NULL, /* unnamed ies */
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    settings = mm_nr5g_registration_settings_new ();
    mm_nr5g_registration_settings_set_mico_mode (settings, mm_modem_3gpp_mico_mode_from_mbim_mico_mode (mico_mode));
    mm_nr5g_registration_settings_set_drx_cycle (settings, mm_modem_3gpp_drx_cycle_from_mbim_drx_cycle (drx_cycle));

    g_task_return_pointer (task, settings, (GDestroyNotify) g_object_unref);
    g_object_unref (task);
}

static void
modem_3gpp_load_nr5g_registration_settings (MMIfaceModem3gpp    *_self,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                  *task;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_nr5g_registration_settings_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "5GNR registration settings are unsupported");
        g_object_unref (task);
        return;
    }

    message = mbim_message_ms_basic_connect_extensions_v3_registration_parameters_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)registration_parameters_query_ready,
                         task);
}

/*****************************************************************************/
/* Set 5GNR registration settings */

static gboolean
modem_3gpp_set_nr5g_registration_settings_finish (MMIfaceModem3gpp  *self,
                                                  GAsyncResult      *res,
                                                  GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_nr5g_registration_settings_ready (MbimDevice   *device,
                                      GAsyncResult *res,
                                      GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_set_nr5g_registration_settings (MMIfaceModem3gpp           *_self,
                                           MMNr5gRegistrationSettings *settings,
                                           GAsyncReadyCallback         callback,
                                           gpointer                    user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                  *task;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;
    MbimMicoMode            mico_mode;
    MbimDrxCycle            drx_cycle;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_nr5g_registration_settings_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "5GNR registration settings are unsupported");
        g_object_unref (task);
        return;
    }

    mico_mode = mm_modem_3gpp_mico_mode_to_mbim_mico_mode (mm_nr5g_registration_settings_get_mico_mode (settings));
    drx_cycle = mm_modem_3gpp_drx_cycle_to_mbim_drx_cycle (mm_nr5g_registration_settings_get_drx_cycle (settings));

    message = mbim_message_ms_basic_connect_extensions_v3_registration_parameters_set_new (mico_mode,
                                                                                           drx_cycle,
                                                                                           MBIM_LADN_INFO_NOT_NEEDED,
                                                                                           MBIM_DEFAULT_PDU_ACTIVATION_HINT_LIKELY,
                                                                                           TRUE,
                                                                                           NULL, /* unnamed ies */
                                                                                           NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)set_nr5g_registration_settings_ready,
                         task);
}

/*****************************************************************************/
/* Signal state updates */

static void
atds_signal_query_after_indication_ready (MbimDevice           *device,
                                          GAsyncResult         *res,
                                          MMBroadbandModemMbim *_self) /* full reference! */
{
    g_autoptr(MMBroadbandModemMbim) self = _self;
    g_autoptr(MbimMessage)          response = NULL;
    g_autoptr(MMSignal)             gsm = NULL;
    g_autoptr(MMSignal)             umts = NULL;
    g_autoptr(MMSignal)             lte = NULL;
    g_autoptr(GError)               error = NULL;
    guint32                         rssi;
    guint32                         error_rate;
    guint32                         rscp;
    guint32                         ecno;
    guint32                         rsrq;
    guint32                         rsrp;
    guint32                         snr;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_atds_signal_response_parse (response, &rssi, &error_rate, &rscp, &ecno, &rsrq, &rsrp, &snr, &error)) {
        mm_obj_warn (self, "ATDS signal query failed: %s", error->message);
        return;
    }

    if (!mm_signal_from_atds_signal_response (rssi, rscp, ecno, rsrq, rsrp, snr, &gsm, &umts, &lte)) {
        mm_obj_warn (self, "No signal details given");
        return;
    }

    mm_iface_modem_signal_update (MM_IFACE_MODEM_SIGNAL (self), NULL, NULL, gsm, umts, lte, NULL);
}

static void
basic_connect_notification_signal_state (MMBroadbandModemMbim *self,
                                         MbimDevice           *device,
                                         MbimMessage          *notification)
{
    g_autoptr(GError)               error = NULL;
    g_autoptr(MbimRsrpSnrInfoArray) rsrp_snr = NULL;
    guint32                         rsrp_snr_count = 0;
    guint32                         coded_rssi;
    guint32                         coded_error_rate = 99;
    guint32                         quality;
    MbimDataClass                   data_class;
    g_autoptr(MMSignal)             cdma = NULL;
    g_autoptr(MMSignal)             evdo = NULL;
    g_autoptr(MMSignal)             gsm = NULL;
    g_autoptr(MMSignal)             umts = NULL;
    g_autoptr(MMSignal)             lte = NULL;
    g_autoptr(MMSignal)             nr5g = NULL;

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (!mbim_message_ms_basic_connect_v2_signal_state_notification_parse (
                notification,
                &coded_rssi,
                &coded_error_rate,
                NULL, /* signal_strength_interval */
                NULL, /* rssi_threshold */
                NULL, /* error_rate_threshold */
                &rsrp_snr_count,
                &rsrp_snr,
                &error)) {
            mm_obj_warn (self, "failed processing MBIMEx v2.0 signal state indication: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed MBIMEx v2.0 signal state indication");
    } else {
        if (!mbim_message_signal_state_notification_parse (
                notification,
                &coded_rssi,
                &coded_error_rate,
                NULL, /* signal_strength_interval */
                NULL, /* rssi_threshold */
                NULL, /* error_rate_threshold */
                &error)) {
            mm_obj_warn (self, "failed processing signal state indication: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed signal state indication");
        if (self->priv->is_atds_signal_supported) {
            g_autoptr(MbimMessage) message = NULL;

            mm_obj_dbg (self, "triggering ATDS signal query");
            message = mbim_message_atds_signal_query_new (NULL);
            mbim_device_command (device,
                                 message,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)atds_signal_query_after_indication_ready,
                                 g_object_ref (self));
        }
    }

    quality = mm_signal_quality_from_mbim_signal_state (coded_rssi, rsrp_snr, rsrp_snr_count, self);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);

    /* Best guess of current data class */
    data_class = self->priv->enabled_cache.highest_available_data_class;
    if (data_class == 0)
        data_class = self->priv->enabled_cache.available_data_classes;

    if (mm_signal_from_mbim_signal_state (data_class, coded_rssi, coded_error_rate, rsrp_snr, rsrp_snr_count,
                                          self, &cdma, &evdo, &gsm, &umts, &lte, &nr5g))
        mm_iface_modem_signal_update (MM_IFACE_MODEM_SIGNAL (self), cdma, evdo, gsm, umts, lte, nr5g);
}

/*****************************************************************************/
/* ATDS location update */

static void
atds_location_query_ready (MbimDevice           *device,
                           GAsyncResult         *res,
                           MMBroadbandModemMbim *self)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    guint32                 lac;
    guint32                 tac;
    guint32                 cid;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_atds_location_response_parse (response, &lac, &tac, &cid, &error)) {
        mm_obj_warn (self, "failed processing ATDS location query response: %s", error->message);
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), 0, 0, 0);
    } else {
        mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), lac, tac, cid);
    }

    g_object_unref (self);
}

static void
update_atds_location (MMBroadbandModemMbim *self)
{
    g_autoptr(MbimMessage)  message = NULL;
    MMPortMbim             *port;
    MbimDevice             *device;

    port = mm_broadband_modem_mbim_peek_port_mbim (self);
    if (!port)
        return;
    device = mm_port_mbim_peek_device (port);
    if (!device)
        return;

    message = mbim_message_atds_location_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)atds_location_query_ready,
                         g_object_ref (self));
}

/*****************************************************************************/
/* Access technology updates */

static void
update_access_technologies (MMBroadbandModemMbim *self)
{
    MMModemAccessTechnology act;

    act = mm_modem_access_technology_from_mbim_data_class (self->priv->enabled_cache.highest_available_data_class);
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
        act = mm_modem_access_technology_from_mbim_data_class (self->priv->enabled_cache.available_data_classes);

    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), act);
}

/*****************************************************************************/
/* Packet service updates */

static void update_registration_info (MMBroadbandModemMbim *self,
                                      gboolean              scheduled,
                                      MbimRegisterState     state,
                                      MbimDataClass         available_data_classes,
                                      gchar                *operator_id_take,
                                      gchar                *operator_name_take);

static void
update_packet_service_info (MMBroadbandModemMbim    *self,
                            MbimPacketServiceState   packet_service_state)
{
    MMModem3gppPacketServiceState state;

    /* Report the new value to the 3GPP interface right away, don't assume it has the same
     * cached value. */
    state = mm_modem_3gpp_packet_service_state_from_mbim_packet_service_state (packet_service_state);
    mm_iface_modem_3gpp_update_packet_service_state (MM_IFACE_MODEM_3GPP (self), state);

    if (packet_service_state == self->priv->enabled_cache.packet_service_state)
        return;

    /* PS reg state depends on the packet service state */
    self->priv->enabled_cache.packet_service_state = packet_service_state;
    update_registration_info (self,
                              FALSE,
                              self->priv->enabled_cache.reg_state,
                              self->priv->enabled_cache.available_data_classes,
                              g_strdup (self->priv->enabled_cache.current_operator_id),
                              g_strdup (self->priv->enabled_cache.current_operator_name));
}

/*****************************************************************************/
/* Registration info updates */

static void
enabling_state_changed (MMBroadbandModemMbim *self)
{
    MMModemState state;

    g_object_get (self, MM_IFACE_MODEM_STATE, &state, NULL);

    /* if we've reached a enabled state, we can trigger the update */
    if (state > MM_MODEM_STATE_ENABLING) {
        mm_obj_dbg (self, "triggering 3GPP registration info update");
        update_registration_info (self,
                                  TRUE,
                                  self->priv->enabled_cache.reg_state,
                                  self->priv->enabled_cache.available_data_classes,
                                  g_strdup (self->priv->enabled_cache.current_operator_id),
                                  g_strdup (self->priv->enabled_cache.current_operator_name));
    }
    /* if something bad happened during enabling, we can ignore any pending
     * registration info update */
    else if (state < MM_MODEM_STATE_ENABLING)
        mm_obj_dbg (self, "discarding pending 3GPP registration info update");

    /* this signal is expected to be fired just once */
    g_signal_handler_disconnect (self, self->priv->enabling_signal_id);
    self->priv->enabling_signal_id = 0;
}

static void
update_registration_info (MMBroadbandModemMbim *self,
                          gboolean              scheduled,
                          MbimRegisterState     state,
                          MbimDataClass         available_data_classes,
                          gchar                *operator_id_take,
                          gchar                *operator_name_take)
{
    MMModem3gppRegistrationState reg_state;
    MMModem3gppRegistrationState reg_state_cs;
    MMModem3gppRegistrationState reg_state_ps;
    MMModem3gppRegistrationState reg_state_eps;
    MMModem3gppRegistrationState reg_state_5gs;
    gboolean                     operator_updated = FALSE;
    gboolean                     reg_state_updated = FALSE;
    MMModemState                 modem_state;
    gboolean                     schedule_update_in_enabled = FALSE;

    /* If we're enabling, we will not attempt to update anything yet, we will
     * instead store the info and schedule the updates for when we're enabled */
    g_object_get (self, MM_IFACE_MODEM_STATE, &modem_state, NULL);
    if (modem_state == MM_MODEM_STATE_ENABLING)
        schedule_update_in_enabled = TRUE;

    if (self->priv->enabled_cache.reg_state != state)
        reg_state_updated = TRUE;
    self->priv->enabled_cache.reg_state = state;

    reg_state = mm_modem_3gpp_registration_state_from_mbim_register_state (state);

    if (reg_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        reg_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        if (self->priv->enabled_cache.current_operator_id && operator_id_take &&
            g_str_equal (self->priv->enabled_cache.current_operator_id, operator_id_take)) {
            g_free (operator_id_take);
        } else {
            operator_updated = TRUE;
            g_free (self->priv->enabled_cache.current_operator_id);
            self->priv->enabled_cache.current_operator_id = operator_id_take;
        }

        if (self->priv->enabled_cache.current_operator_name && operator_name_take &&
            g_str_equal (self->priv->enabled_cache.current_operator_name, operator_name_take)) {
            g_free (operator_name_take);
        } else {
            operator_updated = TRUE;
            g_free (self->priv->enabled_cache.current_operator_name);
            self->priv->enabled_cache.current_operator_name = operator_name_take;
        }
    } else {
        if (self->priv->enabled_cache.current_operator_id ||
            self->priv->enabled_cache.current_operator_name) {
            operator_updated = TRUE;
        }
        g_clear_pointer (&self->priv->enabled_cache.current_operator_id, g_free);
        g_clear_pointer (&self->priv->enabled_cache.current_operator_name, g_free);
        g_free (operator_id_take);
        g_free (operator_name_take);
        /* Explicitly reset packet service state if we're not registered */
        update_packet_service_info (self, MBIM_PACKET_SERVICE_STATE_UNKNOWN);
    }

    /* If we can update domain registration states right now, do it */
    if (!schedule_update_in_enabled) {
        reg_state_cs = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
        reg_state_ps = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
        reg_state_eps = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
        reg_state_5gs = MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;

        if (available_data_classes & (MBIM_DATA_CLASS_GPRS  | MBIM_DATA_CLASS_EDGE  |
                                      MBIM_DATA_CLASS_UMTS  | MBIM_DATA_CLASS_HSDPA | MBIM_DATA_CLASS_HSUPA)) {
            reg_state_cs = reg_state;
            if (self->priv->enabled_cache.packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHED)
                reg_state_ps = reg_state;
        }

        if (available_data_classes & (MBIM_DATA_CLASS_LTE))
            reg_state_eps = reg_state;

        if (available_data_classes & (MBIM_DATA_CLASS_5G_NSA | MBIM_DATA_CLASS_5G_SA))
            reg_state_5gs = reg_state;

        mm_iface_modem_3gpp_update_cs_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_cs, TRUE);
        mm_iface_modem_3gpp_update_ps_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_ps, TRUE);
        if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
            mm_iface_modem_3gpp_update_eps_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_eps, TRUE);
        if (mm_iface_modem_is_3gpp_5gnr (MM_IFACE_MODEM (self)))
            mm_iface_modem_3gpp_update_5gs_registration_state (MM_IFACE_MODEM_3GPP (self), reg_state_5gs, TRUE);
        mm_iface_modem_3gpp_apply_deferred_registration_state (MM_IFACE_MODEM_3GPP (self));

        /* request to reload operator info explicitly, so that the new
         * operator name and code is propagated to the DBus interface */
        if (operator_updated || scheduled)
            mm_iface_modem_3gpp_reload_current_registration_info (MM_IFACE_MODEM_3GPP (self), NULL, NULL);
    }

    self->priv->enabled_cache.available_data_classes = available_data_classes;
    /* If we can update access technologies right now, do it */
    if (!schedule_update_in_enabled)
        update_access_technologies (self);

    /* request to reload location info */
    if (!schedule_update_in_enabled &&
        self->priv->is_atds_location_supported &&
        (reg_state_updated || scheduled)) {
        if (self->priv->enabled_cache.reg_state < MBIM_REGISTER_STATE_HOME) {
            mm_iface_modem_3gpp_update_location (MM_IFACE_MODEM_3GPP (self), 0, 0, 0);

        } else
            update_atds_location (self);
    }

    if (schedule_update_in_enabled && !self->priv->enabling_signal_id) {
        mm_obj_dbg (self, "Scheduled registration info update once the modem is enabled");
        self->priv->enabling_signal_id = g_signal_connect (self,
                                                           "notify::" MM_IFACE_MODEM_STATE,
                                                           G_CALLBACK (enabling_state_changed),
                                                           NULL);
    }
}

static gboolean
common_process_register_state (MMBroadbandModemMbim  *self,
                               MbimDevice            *device,
                               MbimMessage           *message,
                               MbimNwError           *out_nw_error,
                               GError               **error)
{
    MbimNwError        nw_error = 0;
    MbimRegisterState  register_state = MBIM_REGISTER_STATE_UNKNOWN;
    MbimDataClass      available_data_classes = 0;
    g_autofree gchar  *provider_id = NULL;
    g_autofree gchar  *provider_name = NULL;
    MbimDataClass      preferred_data_classes = 0;
    const gchar       *nw_error_str;
    g_autofree gchar  *available_data_classes_str = NULL;
    g_autofree gchar  *preferred_data_classes_str = NULL;
    gboolean           is_notification;

    is_notification = (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_INDICATE_STATUS);
    g_assert (is_notification || (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_COMMAND_DONE));

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (is_notification) {
            if (!mbim_message_ms_basic_connect_v2_register_state_notification_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    &preferred_data_classes,
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v2.0 register state indication: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v2.0 register state indication");
        } else {
            if (!mbim_message_ms_basic_connect_v2_register_state_response_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    &preferred_data_classes,
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v2.0 register state response: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v2.0 register state indication");
        }
    } else {
        if (is_notification) {
            if (!mbim_message_register_state_notification_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    error)) {
                g_prefix_error (error, "Failed processing register state indication: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed register state indication");
        } else {
            if (!mbim_message_register_state_response_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    error)) {
                g_prefix_error (error, "Failed processing register state response: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed register state response");
        }
    }

    nw_error_str = mbim_nw_error_get_string (nw_error);
    available_data_classes_str = mbim_data_class_build_string_from_mask (available_data_classes);
    preferred_data_classes_str = mbim_data_class_build_string_from_mask (preferred_data_classes);

    mm_obj_dbg (self, "register state update:");
    if (nw_error_str)
        mm_obj_dbg (self, "              nw error: '%s'", nw_error_str);
    else
        mm_obj_dbg (self, "              nw error: '0x%x'", nw_error);
    mm_obj_dbg (self, "                 state: '%s'", mbim_register_state_get_string (register_state));
    mm_obj_dbg (self, "           provider id: '%s'", provider_id ? provider_id : "n/a");
    mm_obj_dbg (self, "         provider name: '%s'", provider_name ? provider_name : "n/a");
    mm_obj_dbg (self, "available data classes: '%s'", available_data_classes_str);
    mm_obj_dbg (self, "preferred data classes: '%s'", preferred_data_classes_str);

    update_registration_info (self,
                              FALSE,
                              register_state,
                              available_data_classes,
                              g_steal_pointer (&provider_id),
                              g_steal_pointer (&provider_name));

    if (preferred_data_classes)
        complete_pending_allowed_modes_action (self, preferred_data_classes);

    if (out_nw_error)
        *out_nw_error = nw_error;
    return TRUE;
}

static void
basic_connect_notification_register_state (MMBroadbandModemMbim *self,
                                           MbimDevice           *device,
                                           MbimMessage          *notification)
{
    g_autoptr(GError)  error = NULL;

    if (!common_process_register_state (self, device, notification, NULL, &error))
        mm_obj_warn (self, "%s", error->message);
}

typedef struct {
    MMBroadbandModemMbim *self;
    guint32               session_id;
    GError               *connection_error;
} ReportDisconnectedStatusContext;

static void
bearer_list_report_disconnected_status (MMBaseBearer *bearer,
                                        gpointer user_data)
{
    ReportDisconnectedStatusContext *ctx = user_data;

    if (MM_IS_BEARER_MBIM (bearer) &&
        mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (bearer)) == ctx->session_id) {
        mm_obj_dbg (ctx->self, "bearer '%s' was disconnected.", mm_base_bearer_get_path (bearer));
        mm_base_bearer_report_connection_status_detailed (bearer, MM_BEARER_CONNECTION_STATUS_DISCONNECTED, ctx->connection_error);
    }
}

static void
basic_connect_notification_connect (MMBroadbandModemMbim *self,
                                    MbimDevice           *device,
                                    MbimMessage          *notification)
{
    guint32                  session_id;
    MbimActivationState      activation_state;
    const MbimUuid          *context_type;
    guint32                  nw_error;
    g_autoptr(MMBearerList)  bearer_list = NULL;
    g_autoptr(GError)        error = NULL;

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_connect_notification_parse (
                notification,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                &context_type, /* context_type */
                &nw_error,
                NULL, /* media_preference */
                NULL, /* access_string */
                NULL, /* unnamed_ies */
                &error)) {
            mm_obj_warn (self, "Failed processing MBIMEx v3.0 connect notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed MBIMEx v3.0 connect notification");
    } else {
        if (!mbim_message_connect_notification_parse (
                notification,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                &context_type,
                &nw_error,
                &error)) {
            mm_obj_warn (self, "Failed processing connect notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed connect notification");
    }

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    if (!bearer_list)
        return;

    if (activation_state == MBIM_ACTIVATION_STATE_DEACTIVATED) {
        ReportDisconnectedStatusContext ctx;
        g_autoptr(GError)               connection_error = NULL;

        connection_error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error, self);

        mm_obj_dbg (self, "session ID '%u' was deactivated: %s", session_id, connection_error->message);

        ctx.self = self;
        ctx.session_id = session_id;
        ctx.connection_error = connection_error;
        mm_bearer_list_foreach (bearer_list,
                                (MMBearerListForeachFunc)bearer_list_report_disconnected_status,
                                &ctx);
    }
}

static void
basic_connect_notification_subscriber_ready_status (MMBroadbandModemMbim *self,
                                                    MbimDevice           *device,
                                                    MbimMessage          *notification)
{
    MbimSubscriberReadyState ready_state;
    g_auto(GStrv)            telephone_numbers = NULL;
    g_autoptr(GError)        error = NULL;
    gboolean                 active_sim_event = FALSE;

    if (self->priv->pending_sim_slot_switch_action) {
        mm_obj_dbg (self, "ignoring slot status change");
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_notification_parse (
                notification,
                &ready_state,
                NULL, /* flags */
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error)) {
            mm_obj_warn (self, "Failed processing MBIMEx v3.0 subscriber ready status notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed MBIMEx v3.0 subscriber ready status notification");
    } else {
        if (!mbim_message_subscriber_ready_status_notification_parse (
                notification,
                &ready_state,
                NULL, /* subscriber_id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error)) {
            mm_obj_warn (self, "Failed processing subscriber ready status notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "processed subscriber ready status notification");
    }

    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED)
        mm_iface_modem_update_own_numbers (MM_IFACE_MODEM (self), telephone_numbers);

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE)) {
        /* eSIM profiles have been added or removed, re-probe to ensure correct interfaces are exposed */
        mm_obj_dbg (self, "eSIM profile updates detected");
        active_sim_event = TRUE;
    }

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED)) {
        /* SIM has been removed or reinserted, re-probe to ensure correct interfaces are exposed */
        mm_obj_dbg (self, "SIM hot swap detected");
        active_sim_event = TRUE;
    }

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED)) {
        mm_obj_dbg (self, "Lock state change detected");
        active_sim_event = TRUE;
    }

    self->priv->enabled_cache.last_ready_state = ready_state;

    if (active_sim_event) {
        mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self));
    }
}

/*****************************************************************************/
/* Speed updates */

void
mm_broadband_modem_mbim_get_speeds (MMBroadbandModemMbim *self,
                                    guint64              *uplink_speed,
                                    guint64              *downlink_speed)
{
    if (uplink_speed)
        *uplink_speed = self->priv->enabled_cache.packet_service_uplink_speed;
    if (downlink_speed)
        *downlink_speed = self->priv->enabled_cache.packet_service_downlink_speed;
}

static void
bearer_list_report_speeds (MMBaseBearer         *bearer,
                           MMBroadbandModemMbim *self)
{
    if (!MM_IS_BEARER_MBIM (bearer))
        return;

    /* Update speeds only if connected or connecting */
    if (mm_base_bearer_get_status (bearer) >= MM_BEARER_STATUS_CONNECTING) {
        mm_obj_dbg (self, "bearer '%s' speeds updated", mm_base_bearer_get_path (bearer));
        mm_base_bearer_report_speeds (bearer,
                                      self->priv->enabled_cache.packet_service_uplink_speed,
                                      self->priv->enabled_cache.packet_service_downlink_speed);
    }
}

static void
update_bearer_speeds (MMBroadbandModemMbim *self,
                      guint64               uplink_speed,
                      guint64               downlink_speed)
{
    g_autoptr(MMBearerList) bearer_list = NULL;

    if ((self->priv->enabled_cache.packet_service_uplink_speed == uplink_speed) &&
        (self->priv->enabled_cache.packet_service_downlink_speed == downlink_speed))
        return;

    self->priv->enabled_cache.packet_service_uplink_speed = uplink_speed;
    self->priv->enabled_cache.packet_service_downlink_speed = downlink_speed;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);
    if (!bearer_list)
        return;

    mm_bearer_list_foreach (bearer_list,
                            (MMBearerListForeachFunc)bearer_list_report_speeds,
                            self);
}

/*****************************************************************************/
/* Packet service updates */

static gboolean
common_process_packet_service (MMBroadbandModemMbim     *self,
                               MbimDevice               *device,
                               MbimMessage              *message,
                               guint32                  *out_nw_error,
                               MbimPacketServiceState   *out_packet_service_state,
                               GError                  **error)
{
    guint32                 nw_error = 0;
    MbimPacketServiceState  packet_service_state = MBIM_PACKET_SERVICE_STATE_UNKNOWN;
    MbimDataClass           data_class = 0;
    MbimDataClassV3         data_class_v3 = 0;
    MbimDataSubclass        data_subclass = 0;
    guint64                 uplink_speed = 0;
    guint64                 downlink_speed = 0;
    MbimFrequencyRange      frequency_range = MBIM_FREQUENCY_RANGE_UNKNOWN;
    g_autofree gchar       *data_class_str = NULL;
    g_autofree gchar       *data_class_v3_str = NULL;
    g_autofree gchar       *data_subclass_str = NULL;
    g_autofree gchar       *frequency_range_str = NULL;
    const gchar            *nw_error_str;
    gboolean                is_notification;

    is_notification = (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_INDICATE_STATUS);
    g_assert (is_notification || (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_COMMAND_DONE));

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (is_notification) {
            if (!mbim_message_ms_basic_connect_v3_packet_service_notification_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class_v3,
                    &uplink_speed,
                    &downlink_speed,
                    &frequency_range,
                    &data_subclass,
                    NULL, /* tai */
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v3.0 packet service indication: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v3.0 packet service indication");
        } else {
            if (!mbim_message_ms_basic_connect_v3_packet_service_response_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class_v3,
                    &uplink_speed,
                    &downlink_speed,
                    &frequency_range,
                    &data_subclass,
                    NULL, /* tai */
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v3.0 packet service response: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v3.0 packet service response");
        }
        data_class_v3_str = mbim_data_class_v3_build_string_from_mask (data_class_v3);
        data_subclass_str = mbim_data_subclass_build_string_from_mask (data_subclass);
    } else if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (is_notification) {
            if (!mbim_message_ms_basic_connect_v2_packet_service_notification_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class, /* current */
                    &uplink_speed,
                    &downlink_speed,
                    &frequency_range,
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v2.0 packet service indication: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v2.0 packet service indication");
        } else {
            if (!mbim_message_ms_basic_connect_v2_packet_service_response_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class, /* current */
                    &uplink_speed,
                    &downlink_speed,
                    &frequency_range,
                    error)) {
                g_prefix_error (error, "Failed processing MBIMEx v2.0 packet service response: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed MBIMEx v2.0 packet service response");
        }
        data_class_str = mbim_data_class_build_string_from_mask (data_class);
    } else {
        if (is_notification) {
            if (!mbim_message_packet_service_notification_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class, /* highest available */
                    &uplink_speed,
                    &downlink_speed,
                    error)) {
                g_prefix_error (error, "Failed processing packet service indication: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed packet service indication");
        } else {
            if (!mbim_message_packet_service_response_parse (
                    message,
                    &nw_error,
                    &packet_service_state,
                    &data_class, /* highest available */
                    &uplink_speed,
                    &downlink_speed,
                    error)) {
                g_prefix_error (error, "Failed processing packet service response: ");
                return FALSE;
            }
            mm_obj_dbg (self, "processed packet service response");
        }
        data_class_str = mbim_data_class_build_string_from_mask (data_class);
    }

    frequency_range_str = mbim_frequency_range_build_string_from_mask (frequency_range);
    nw_error_str = mbim_nw_error_get_string (nw_error);

    mm_obj_dbg (self, "packet service update:");
    if (nw_error_str)
        mm_obj_dbg (self, "        nw error: '%s'", nw_error_str);
    else
        mm_obj_dbg (self, "        nw error: '0x%x'", nw_error);
    mm_obj_dbg (self, "           state: '%s'", mbim_packet_service_state_get_string (packet_service_state));
    if (data_class_str)
        mm_obj_dbg (self, "      data class: '%s'", data_class_str);
    else if (data_class_v3_str)
        mm_obj_dbg (self, "      data class: '%s'", data_class_v3_str);
    if (data_subclass_str)
        mm_obj_dbg (self, "   data subclass: '%s'", data_subclass_str);
    mm_obj_dbg (self, "          uplink: '%" G_GUINT64_FORMAT "' bps", uplink_speed);
    mm_obj_dbg (self, "        downlink: '%" G_GUINT64_FORMAT "' bps", downlink_speed);
    mm_obj_dbg (self, " frequency range: '%s'", frequency_range_str);

    if (packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHED) {
        if (data_class_v3)
            self->priv->enabled_cache.highest_available_data_class = mm_mbim_data_class_from_mbim_data_class_v3_and_subclass (data_class_v3, data_subclass);
        else
            self->priv->enabled_cache.highest_available_data_class = data_class;
    } else if (packet_service_state == MBIM_PACKET_SERVICE_STATE_DETACHED) {
        self->priv->enabled_cache.highest_available_data_class = 0;
    }
    update_access_technologies (self);

    update_packet_service_info (self, packet_service_state);

    update_bearer_speeds (self, uplink_speed, downlink_speed);

    if (out_nw_error)
        *out_nw_error = nw_error;
    if (out_packet_service_state)
        *out_packet_service_state = packet_service_state;
    return TRUE;
}

static void
basic_connect_notification_packet_service (MMBroadbandModemMbim *self,
                                           MbimDevice           *device,
                                           MbimMessage          *notification)
{
    g_autoptr(GError) error = NULL;

    if (!common_process_packet_service (self, device, notification, NULL, NULL, &error))
        mm_obj_warn (self, "%s", error->message);
}

static void
basic_connect_notification_provisioned_contexts (MMBroadbandModemMbim *self,
                                                 MbimDevice           *device,
                                                 MbimMessage          *notification)
{
    /* We don't even attempt to parse the indication, we just need to notify that
     * something changed to the upper layers */
    mm_iface_modem_3gpp_profile_manager_updated (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self));
}

static gboolean process_pdu_messages (MMBroadbandModemMbim       *self,
                                      MbimSmsFormat               format,
                                      guint32                     messages_count,
                                      MbimSmsPduReadRecordArray  *pdu_messages,
                                      GError                    **error);

static void
sms_notification_read_flash_sms (MMBroadbandModemMbim *self,
                                 MbimDevice           *device,
                                 MbimMessage          *notification)
{
    g_autoptr(MbimSmsPduReadRecordArray) pdu_messages = NULL;
    g_autoptr(GError)                    error = NULL;
    MbimSmsFormat                        format;
    guint32                              messages_count;

    if (!mbim_message_sms_read_notification_parse (
            notification,
            &format,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            &error) ||
        !process_pdu_messages (self,
                               format,
                               messages_count,
                               pdu_messages,
                               &error))
        mm_obj_dbg (self, "flash SMS message reading failed: %s", error->message);
}

static void
basic_connect_notification (MMBroadbandModemMbim *self,
                            MbimDevice           *device,
                            MbimMessage          *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_BASIC_CONNECT_SIGNAL_STATE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY)
            basic_connect_notification_signal_state (self, device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_REGISTER_STATE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES)
            basic_connect_notification_register_state (self, device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_CONNECT:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_CONNECT)
            basic_connect_notification_connect (self, device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO)
            basic_connect_notification_subscriber_ready_status (self, device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_PACKET_SERVICE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE)
            basic_connect_notification_packet_service (self, device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS)
            basic_connect_notification_provisioned_contexts (self, device, notification);
    default:
        /* Ignore */
        break;
    }
}

static void
alert_sms_read_query_ready (MbimDevice           *device,
                            GAsyncResult         *res,
                            MMBroadbandModemMbim *self) /* full ref */
{
    g_autoptr(MbimMessage)               response = NULL;
    g_autoptr(GError)                    error = NULL;
    g_autoptr(MbimSmsPduReadRecordArray) pdu_messages = NULL;
    MbimSmsFormat                        format;
    guint32                              messages_count;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_sms_read_response_parse (
            response,
            &format,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            &error) ||
        !process_pdu_messages (self,
                               format,
                               messages_count,
                               pdu_messages,
                               &error))
        mm_obj_dbg (self, "SMS message reading failed: %s", error->message);

    g_object_unref (self);
}

static void
sms_notification_read_stored_sms (MMBroadbandModemMbim *self,
                                  guint32               index)
{
    g_autoptr(MbimMessage)  message = NULL;
    MMPortMbim             *port;
    MbimDevice             *device;

    port = mm_broadband_modem_mbim_peek_port_mbim (self);
    if (!port)
        return;
    device = mm_port_mbim_peek_device (port);
    if (!device)
        return;

    mm_obj_dbg (self, "reading new SMS at index '%u'", index);
    message = mbim_message_sms_read_query_new (MBIM_SMS_FORMAT_PDU,
                                               MBIM_SMS_FLAG_INDEX,
                                               index,
                                               NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)alert_sms_read_query_ready,
                         g_object_ref (self));
}

static void
sms_notification (MMBroadbandModemMbim *self,
                  MbimDevice           *device,
                  MbimMessage          *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_SMS_READ:
        /* New flash/alert message? */
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ)
            sms_notification_read_flash_sms (self, device, notification);
        break;

    case MBIM_CID_SMS_MESSAGE_STORE_STATUS: {
        MbimSmsStatusFlag flags;
        guint32           index;

        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ &&
            mbim_message_sms_message_store_status_notification_parse (
                notification,
                &flags,
                &index,
                NULL)) {
            g_autofree gchar *flags_str = NULL;

            flags_str = mbim_sms_status_flag_build_string_from_mask (flags);
            mm_obj_dbg (self, "received SMS store status update: '%s'", flags_str);
            if (flags & MBIM_SMS_STATUS_FLAG_NEW_MESSAGE)
                sms_notification_read_stored_sms (self, index);
        }
        break;
    }

    default:
        /* Ignore */
        break;
    }
}

static void
ms_basic_connect_extensions_notification_pco (MMBroadbandModemMbim *self,
                                              MbimDevice           *device,
                                              MbimMessage          *notification)
{
    MbimPcoValue *pco_value;
    GError *error = NULL;
    gchar *pco_data_hex;
    MMPco *pco;

    if (!mbim_message_ms_basic_connect_extensions_pco_notification_parse (
            notification,
            &pco_value,
            &error)) {
        mm_obj_warn (self, "couldn't parse PCO notification: %s", error->message);
        g_error_free (error);
        return;
    }

    pco_data_hex = mm_utils_bin2hexstr (pco_value->pco_data_buffer,
                                        pco_value->pco_data_size);
    mm_obj_dbg (self, "received PCO: session ID=%u type=%s size=%u data=%s",
                pco_value->session_id,
                mbim_pco_type_get_string (pco_value->pco_data_type),
                pco_value->pco_data_size,
                pco_data_hex);
    g_free (pco_data_hex);

    pco = mm_pco_new ();
    mm_pco_set_session_id (pco, pco_value->session_id);
    mm_pco_set_complete (pco,
                         pco_value->pco_data_type == MBIM_PCO_TYPE_COMPLETE);
    mm_pco_set_data (pco,
                     pco_value->pco_data_buffer,
                     pco_value->pco_data_size);

    self->priv->enabled_cache.pco_list = mm_pco_list_add (self->priv->enabled_cache.pco_list, pco);
    mm_iface_modem_3gpp_update_pco_list (MM_IFACE_MODEM_3GPP (self),
                                         self->priv->enabled_cache.pco_list);
    g_object_unref (pco);
    mbim_pco_value_free (pco_value);
}

static void
ms_basic_connect_extensions_notification_lte_attach_info (MMBroadbandModemMbim *self,
                                                          MbimDevice           *device,
                                                          MbimMessage          *notification)
{
    g_autoptr(GError)              error = NULL;
    g_autoptr(MMBearerProperties)  properties = NULL;
    guint32                        lte_attach_state;
    guint32                        ip_type;
    g_autofree gchar              *access_string = NULL;
    g_autofree gchar              *user_name = NULL;
    g_autofree gchar              *password = NULL;
    guint32                        compression;
    guint32                        auth_protocol;
    MbimNwError                    nw_error = 0;

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_extensions_v3_lte_attach_info_notification_parse (
                notification,
                &lte_attach_state,
                &nw_error,
                &ip_type,
                &access_string,
                &user_name,
                &password,
                &compression,
                &auth_protocol,
                &error)) {
            mm_obj_warn (self, "Failed processing MBIMEx v3.0 LTE attach info notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "Processed MBIMEx v3.0 LTE attach info notification");
    } else {
        if (!mbim_message_ms_basic_connect_extensions_lte_attach_info_notification_parse (
            notification,
            &lte_attach_state,
            &ip_type,
            &access_string,
            &user_name,
            &password,
            &compression,
            &auth_protocol,
            &error)) {
            mm_obj_warn (self, "Failed processing LTE attach info notification: %s", error->message);
            return;
        }
        mm_obj_dbg (self, "Processed LTE attach info notification");
    }

    properties = common_process_lte_attach_info (self,
                                                 lte_attach_state,
                                                 ip_type,
                                                 access_string,
                                                 user_name,
                                                 password,
                                                 compression,
                                                 auth_protocol,
                                                 NULL);
    mm_iface_modem_3gpp_update_initial_eps_bearer (MM_IFACE_MODEM_3GPP (self), properties);

    /* If network error is reported, then log it */
    if (nw_error) {
        const gchar *nw_error_str;

        nw_error_str = mbim_nw_error_get_string (nw_error);
        if (nw_error_str)
            mm_obj_dbg (self, "LTE attach info network error reported: %s", nw_error_str);
        else
            mm_obj_dbg (self, "LTE attach info network error reported: 0x%x", nw_error);
    }
}

static MMBaseSim *
create_sim_from_slot_state (MMBroadbandModemMbim *self,
                            gboolean              active,
                            guint                 slot_index,
                            MbimUiccSlotState     slot_state)
{
    MMSimType       sim_type    = MM_SIM_TYPE_UNKNOWN;
    MMSimEsimStatus esim_status = MM_SIM_ESIM_STATUS_UNKNOWN;

    switch (slot_state) {
    case MBIM_UICC_SLOT_STATE_ACTIVE:
        sim_type = MM_SIM_TYPE_PHYSICAL;
        break;
    case MBIM_UICC_SLOT_STATE_ACTIVE_ESIM:
        sim_type = MM_SIM_TYPE_ESIM;
        esim_status = MM_SIM_ESIM_STATUS_WITH_PROFILES;
        break;
    case MBIM_UICC_SLOT_STATE_ACTIVE_ESIM_NO_PROFILES:
        sim_type = MM_SIM_TYPE_ESIM;
        esim_status = MM_SIM_ESIM_STATUS_NO_PROFILES;
        break;
    case MBIM_UICC_SLOT_STATE_NOT_READY:
    case MBIM_UICC_SLOT_STATE_ERROR:
        /* Not fully ready (NOT_READY) or unusable (ERROR) SIM cards should also be
         * reported as being available in the non-active slot. */
        break;
    case MBIM_UICC_SLOT_STATE_UNKNOWN:
    case MBIM_UICC_SLOT_SATE_OFF_EMPTY:
    case MBIM_UICC_SLOT_STATE_OFF:
    case MBIM_UICC_SLOT_STATE_EMPTY:
    default:
        return NULL;
    }

    mm_obj_dbg (self, "found %s SIM in slot %u: %s (%s)",
                active ? "active" : "inactive",
                slot_index,
                mm_sim_type_get_string (sim_type),
                (sim_type == MM_SIM_TYPE_ESIM) ? mm_sim_esim_status_get_string (esim_status) : "n/a");

    return MM_BASE_SIM (mm_sim_mbim_new_initialized (MM_BASE_MODEM (self),
                                                     slot_index,
                                                     active,
                                                     sim_type,
                                                     esim_status,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     NULL));
}

static void
ms_basic_connect_extensions_notification_slot_info_status (MMBroadbandModemMbim *self,
                                                           MbimDevice           *device,
                                                           MbimMessage          *notification)
{
    g_autoptr(GError) error = NULL;
    guint32           slot_index;
    MbimUiccSlotState slot_state;

    if (!mbim_message_ms_basic_connect_extensions_slot_info_status_notification_parse (
            notification,
            &slot_index,
            &slot_state,
            &error)) {
        mm_obj_warn (self, "Couldn't parse slot info status notification: %s", error->message);
        return;
    }

    if (self->priv->pending_sim_slot_switch_action) {
        mm_obj_dbg (self, "ignoring slot status change in SIM slot %d: %s", slot_index + 1, mbim_uicc_slot_state_get_string (slot_state));
        return;
    }

    if (self->priv->active_slot_index != (slot_index + 1)) {
        /* Modifies SIM object at the given slot based on the reported state,
         * when the slot is not the active one. */
        g_autoptr(MMBaseSim) sim = NULL;

        mm_obj_dbg (self, "processing slot status change in non-active SIM slot %d: %s", slot_index + 1, mbim_uicc_slot_state_get_string (slot_state));
        sim = create_sim_from_slot_state (self, FALSE, slot_index, slot_state);
        mm_iface_modem_modify_sim (MM_IFACE_MODEM (self), slot_index, sim);
        return;
    }

    /* Major SIM event on the active slot, will request reprobing the
     * modem from scratch. */
    mm_obj_dbg (self, "processing slot status change in active SIM slot %d: %s", slot_index + 1, mbim_uicc_slot_state_get_string (slot_state));
    mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self));
}

static void
ms_basic_connect_extensions_notification (MMBroadbandModemMbim *self,
                                          MbimDevice           *device,
                                          MbimMessage          *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PCO:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PCO)
            ms_basic_connect_extensions_notification_pco (self, device, notification);
        break;
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_LTE_ATTACH_INFO:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO)
            ms_basic_connect_extensions_notification_lte_attach_info (self, device, notification);
        break;
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_SLOT_INFO_STATUS:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS)
            ms_basic_connect_extensions_notification_slot_info_status (self, device, notification);
        break;
    default:
        /* Ignore */
        break;
    }
}

static void
process_ussd_notification (MMBroadbandModemMbim *self,
                           MbimMessage          *notification);

static void
ussd_notification (MMBroadbandModemMbim *self,
                   MbimDevice           *device,
                   MbimMessage          *notification)
{
    if (mbim_message_indicate_status_get_cid (notification) != MBIM_CID_USSD) {
        mm_obj_warn (self, "unexpected USSD notification (cid %u)", mbim_message_indicate_status_get_cid (notification));
        return;
    }

    if (!(self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_USSD))
        return;

    process_ussd_notification (self, notification);
}

static void
port_notification_cb (MMPortMbim           *port,
                      MbimMessage          *notification,
                      MMBroadbandModemMbim *self)
{
    MbimService  service;
    MbimDevice  *device;

    /* Onlyu process notifications if the device still exists */
    device = mm_port_mbim_peek_device (port);
    if (!device)
        return;

    service = mbim_message_indicate_status_get_service (notification);
    mm_obj_dbg (self, "received notification (service '%s', command '%s')",
                mbim_service_get_string (service),
                mbim_cid_get_printable (service,
                                        mbim_message_indicate_status_get_cid (notification)));

    if (service == MBIM_SERVICE_BASIC_CONNECT)
        basic_connect_notification (self, device, notification);
    else if (service == MBIM_SERVICE_MS_BASIC_CONNECT_EXTENSIONS)
        ms_basic_connect_extensions_notification (self, device, notification);
    else if (service == MBIM_SERVICE_SMS)
        sms_notification (self, device, notification);
    else if (service == MBIM_SERVICE_USSD)
        ussd_notification (self, device, notification);
}

static void
common_setup_cleanup_unsolicited_events_sync (MMBroadbandModemMbim *self,
                                              MMPortMbim           *port,
                                              gboolean              setup)
{
    if (!port)
        return;

    mm_obj_dbg (self, "supported notifications: signal (%s), registration (%s), sms (%s), "
                "connect (%s), subscriber (%s), packet (%s), pco (%s), ussd (%s), "
                "lte attach info (%s), provisioned contexts (%s), slot_info_status (%s)",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PCO ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_USSD ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS ? "yes" : "no",
                self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS ? "yes" : "no");

    if (setup) {
        /* Don't re-enable it if already there */
        if (!self->priv->notification_id)
            self->priv->notification_id =
                g_signal_connect (port,
                                  MM_PORT_MBIM_SIGNAL_NOTIFICATION,
                                  G_CALLBACK (port_notification_cb),
                                  self);
    } else {
        /* Don't remove the signal if there are still listeners interested */
        if (self->priv->setup_flags == PROCESS_NOTIFICATION_FLAG_NONE &&
            self->priv->notification_id &&
            g_signal_handler_is_connected (port, self->priv->notification_id)) {
            g_signal_handler_disconnect (port, self->priv->notification_id);
            self->priv->notification_id = 0;
        }
    }
}

static gboolean
common_setup_cleanup_unsolicited_events_finish (MMBroadbandModemMbim  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_cleanup_unsolicited_events (MMBroadbandModemMbim *self,
                                         gboolean              setup,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    GTask      *task;
    MMPortMbim *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't peek MBIM port");
        g_object_unref (task);
        return;
    }

    common_setup_cleanup_unsolicited_events_sync (self, port, setup);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup/cleanup unsolicited events (3GPP interface) */

static gboolean
common_setup_cleanup_unsolicited_events_3gpp_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
cleanup_enabled_cache (MMBroadbandModemMbim *self)
{
    g_clear_pointer (&self->priv->enabled_cache.current_operator_id, g_free);
    g_clear_pointer (&self->priv->enabled_cache.current_operator_name, g_free);
    g_list_free_full (self->priv->enabled_cache.pco_list, g_object_unref);
    self->priv->enabled_cache.pco_list = NULL;
    self->priv->enabled_cache.available_data_classes = MBIM_DATA_CLASS_NONE;
    self->priv->enabled_cache.highest_available_data_class = MBIM_DATA_CLASS_NONE;
    self->priv->enabled_cache.reg_state = MBIM_REGISTER_STATE_UNKNOWN;
    self->priv->enabled_cache.packet_service_state = MBIM_PACKET_SERVICE_STATE_UNKNOWN;
    self->priv->enabled_cache.packet_service_uplink_speed = 0;
    self->priv->enabled_cache.packet_service_downlink_speed = 0;

    /* NOTE: FLAG_SUBSCRIBER_INFO is managed both via 3GPP unsolicited
     * events and via SIM hot swap setup. We only reset the last ready state
     * if SIM hot swap context is not using it. */
    if (!self->priv->sim_hot_swap_configured)
        self->priv->enabled_cache.last_ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
}

static void
cleanup_unsolicited_events_3gpp (MMIfaceModem3gpp *_self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    /* NOTE: FLAG_SUBSCRIBER_INFO is managed both via 3GPP unsolicited
     * events and via SIM hot swap setup. We only really cleanup the
     * indication if SIM hot swap context is not using it. */
    if (!self->priv->sim_hot_swap_configured)
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    if (self->priv->is_pco_supported)
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_PCO;
    if (self->priv->is_lte_attach_info_supported)
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO;
    if (self->priv->is_slot_info_status_supported)
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);

    /* Runtime cached state while enabled, to be cleaned up once disabled */
    cleanup_enabled_cache (self);
}

static void
setup_unsolicited_events_3gpp (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    /* NOTE: FLAG_SUBSCRIBER_INFO is managed both via 3GPP unsolicited
     * events and via SIM hot swap setup. */
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    if (self->priv->is_pco_supported)
        self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_PCO;
    if (self->priv->is_lte_attach_info_supported)
        self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO;
    if (self->priv->is_slot_info_status_supported)
        self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Cleanup/Setup unsolicited registration events */

static void
cleanup_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/disable unsolicited events (common) */

static gboolean
common_enable_disable_unsolicited_events_finish (MMBroadbandModemMbim *self,
                                                 GAsyncResult *res,
                                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
subscribe_list_set_ready_cb (MbimDevice *device,
                             GAsyncResult *res,
                             GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_events (MMBroadbandModemMbim *self,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    MbimMessage *request;
    MbimDevice *device;
    MbimEventEntry **entries;
    guint n_entries = 0;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    mm_obj_dbg (self, "enabled notifications: signal (%s), registration (%s), sms (%s), "
                "connect (%s), subscriber (%s), packet (%s), pco (%s), ussd (%s), "
                "lte attach info (%s), provisioned contexts (%s), slot_info_status (%s)",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PCO ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_USSD ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS ? "yes" : "no",
                self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS ? "yes" : "no");

    entries = g_new0 (MbimEventEntry *, 5);

    /* Basic connect service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_BASIC_CONNECT, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 0;
        entries[n_entries]->cids = g_new0 (guint32, 6);
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_SIGNAL_STATE;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_REGISTER_STATE;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_CONNECT;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_PACKET_SERVICE;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS;
        n_entries++;
    }

    /* Basic connect extensions service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PCO ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_MS_BASIC_CONNECT_EXTENSIONS, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 0;
        entries[n_entries]->cids = g_new0 (guint32, 3);
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PCO)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PCO;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_LTE_ATTACH_INFO;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_SLOT_INFO_STATUS;
        n_entries++;
    }

    /* SMS service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_SMS, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 2;
        entries[n_entries]->cids = g_new0 (guint32, 2);
        entries[n_entries]->cids[0] = MBIM_CID_SMS_READ;
        entries[n_entries]->cids[1] = MBIM_CID_SMS_MESSAGE_STORE_STATUS;
        n_entries++;
    }

    /* USSD service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_USSD) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_USSD, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 1;
        entries[n_entries]->cids = g_new0 (guint32, 1);
        entries[n_entries]->cids[0] = MBIM_CID_USSD;
        n_entries++;
    }

    task = g_task_new (self, NULL, callback, user_data);

    request = (mbim_message_device_service_subscribe_list_set_new (
                   n_entries,
                   (const MbimEventEntry *const *)entries,
                   NULL));
    mbim_device_command (device,
                         request,
                         10,
                         NULL,
                         (GAsyncReadyCallback)subscribe_list_set_ready_cb,
                         task);
    mbim_message_unref (request);
    mbim_event_entry_array_free (entries);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events */

static gboolean
modem_3gpp_common_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                         GAsyncResult *res,
                                                                         GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                    gboolean cs_supported,
                                                    gboolean ps_supported,
                                                    gboolean eps_supported,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}


static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                   gboolean cs_supported,
                                                   gboolean ps_supported,
                                                   gboolean eps_supported,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Setup SIM hot swap */

typedef struct {
    MMPortMbim *port;
    GError     *subscriber_info_error;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    GError     *qmi_error;
#endif
} SetupSimHotSwapContext;

static void
setup_sim_hot_swap_context_free (SetupSimHotSwapContext *ctx)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    g_clear_error (&ctx->qmi_error);
#endif
    g_clear_error (&ctx->subscriber_info_error);
    g_clear_object (&ctx->port);
    g_slice_free (SetupSimHotSwapContext, ctx);
}

static gboolean
modem_setup_sim_hot_swap_finish (MMIfaceModem  *_self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->sim_hot_swap_configured = g_task_propagate_boolean (G_TASK (res), error);
    return self->priv->sim_hot_swap_configured;
}

static void
sim_hot_swap_complete (GTask *task)
{
    SetupSimHotSwapContext *ctx;

    ctx = g_task_get_task_data (task);

    /* If MBIM based logic worked, success */
    if (!ctx->subscriber_info_error)
        g_task_return_boolean (task, TRUE);
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    /* Otherwise, If QMI-over-MBIM based logic worked, success */
    else if (!ctx->qmi_error)
        g_task_return_boolean (task, TRUE);
#endif
    /* Otherwise, prefer MBIM specific error */
    else
        g_task_return_error (task, g_steal_pointer (&ctx->subscriber_info_error));
    g_object_unref (task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
qmi_setup_sim_hot_swap_ready (MMIfaceModem *self,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetupSimHotSwapContext *ctx;

    ctx = g_task_get_task_data (task);
    if (!mm_shared_qmi_setup_sim_hot_swap_finish (self, res, &ctx->qmi_error))
        mm_obj_dbg (self, "couldn't setup SIM hot swap using QMI over MBIM: %s", ctx->qmi_error->message);

    sim_hot_swap_complete (task);
}

#endif

static void
enable_subscriber_info_unsolicited_events_ready (MMBroadbandModemMbim *self,
                                                 GAsyncResult         *res,
                                                 GTask                *task)
{
    SetupSimHotSwapContext *ctx;

    ctx = g_task_get_task_data (task);

    if (!common_enable_disable_unsolicited_events_finish (self, res, &ctx->subscriber_info_error)) {
        mm_obj_dbg (self, "failed to enable subscriber info events: %s", ctx->subscriber_info_error->message);
        /* reset setup flags if enabling failed */
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
        common_setup_cleanup_unsolicited_events_sync (self, ctx->port, FALSE);
        /* and also reset enable flags */
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    }

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    mm_shared_qmi_setup_sim_hot_swap (MM_IFACE_MODEM (self),
                                      (GAsyncReadyCallback)qmi_setup_sim_hot_swap_ready,
                                      task);
#else
    sim_hot_swap_complete (task);
#endif
}

static void
modem_setup_sim_hot_swap (MMIfaceModem        *_self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MMPortMbim             *port;
    GTask                  *task;
    SetupSimHotSwapContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't peek MBIM port");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetupSimHotSwapContext);
    ctx->port = g_object_ref (port);
    g_task_set_task_data (task, ctx, (GDestroyNotify)setup_sim_hot_swap_context_free);

    /* Setup flags synchronously, which never fails */
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    common_setup_cleanup_unsolicited_events_sync (self, ctx->port, TRUE);

    /* Enable flags asynchronously, which may fail */
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    common_enable_disable_unsolicited_events (self,
                                              (GAsyncReadyCallback)enable_subscriber_info_unsolicited_events_ready,
                                              task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_common_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                            GAsyncResult *res,
                                                            GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    /* NOTE: FLAG_SUBSCRIBER_INFO is managed both via 3GPP unsolicited
     * events and via SIM hot swap setup. We only really disable the
     * indication if SIM hot swap context is not using it. */
    if (!self->priv->sim_hot_swap_configured)
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    if (self->priv->is_pco_supported)
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_PCO;
    if (self->priv->is_lte_attach_info_supported)
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO;
    if (self->priv->is_slot_info_status_supported)
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    /* NOTE: FLAG_SUBSCRIBER_INFO is managed both via 3GPP unsolicited
     * events and via SIM hot swap setup. */
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    if (self->priv->is_pco_supported)
        self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_PCO;
    if (self->priv->is_lte_attach_info_supported)
        self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_LTE_ATTACH_INFO;
    if (self->priv->is_slot_info_status_supported)
        self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SLOT_INFO_STATUS;
    common_enable_disable_unsolicited_events (self, callback, user_data);
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
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->enabled_cache.current_operator_name)
        g_task_return_pointer (task,
                               g_strdup (self->priv->enabled_cache.current_operator_name),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Current operator name is still unknown");
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
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->enabled_cache.current_operator_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->enabled_cache.current_operator_id),
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
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp  *self,
                                           GAsyncResult      *res,
                                           GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
packet_service_query_ready (MbimDevice   *device,
                            GAsyncResult *res,
                            GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(GError)       error = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
         g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE))) {
        g_autoptr(GError) inner_error = NULL;

        if (!common_process_packet_service (self, device, response, NULL, NULL, &inner_error))
            mm_obj_warn (self, "%s", inner_error->message);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
register_state_query_ready (MbimDevice   *device,
                            GAsyncResult *res,
                            GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(MbimMessage)  message = NULL;
    MMBroadbandModemMbim   *self;
    GError                 *error = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!common_process_register_state (self, device, response, NULL, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now queue packet service state update */
    message = mbim_message_packet_service_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)packet_service_query_ready,
                         task);
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
    g_autoptr(MbimMessage)  message = NULL;
    MbimDevice             *device;
    GTask                  *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_register_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)register_state_query_ready,
                         task);
}

/*****************************************************************************/

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->qmi_capability_and_mode_switching)
        return mm_shared_qmi_3gpp_register_in_network_finish (self, res, error);
#endif

    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
register_state_set_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;

    self = g_task_get_source_object (task);

    /* The NwError field is valid if MBIM_SET_REGISTER_STATE response status code
     * equals MBIM_STATUS_FAILURE, so we parse the message both on success and on that
     * specific failure */
    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
         g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE))) {
        g_autoptr(GError) inner_error = NULL;
        MbimNwError       nw_error = 0;

        if (!common_process_register_state (self, device, response, &nw_error, &inner_error)) {
            mm_obj_warn (self, "%s", inner_error->message);
            /* Prefer the error from the result to the parsing error */
            if (!error)
                error = g_steal_pointer (&inner_error);
        } else {
            /* Prefer the NW error if available.
             *
             * NwError "0" is defined in 3GPP TS 24.008 as "Unknown error",
             * not "No error", making it unsuitable as condition for registration check.
             * Still, there are certain modems (e.g. Fibocom NL668) that will
             * report Failure+NwError=0 even after the modem has already reported a
             * succesful registration via indications after the set operation. If
             * that is the case, log about it and ignore the error; we are anyway
             * reloading the registration info after the set, so it should not be
             * a big issue. */
            if (nw_error) {
                g_clear_error (&error);
                error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error, self);
            }
        }
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *_self,
                                const gchar *operator_id,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    /* data_class set to 0 in the MBIM register state set message ends up
     * selecting some "auto" mode that would overwrite whatever capabilities
     * and modes we had set. So, if we're using QMI-based capability and
     * mode switching, also use QMI-based network registration. */
    if (self->priv->qmi_capability_and_mode_switching) {
        mm_shared_qmi_3gpp_register_in_network (_self, operator_id, cancellable, callback, user_data);
        return;
    }
#endif

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* keep track of which operator id is selected */
    g_clear_pointer (&self->priv->requested_operator_id, g_free);
    if (operator_id && operator_id[0])
        self->priv->requested_operator_id = g_strdup (operator_id);

    message = (mbim_message_register_state_set_new (
                   self->priv->requested_operator_id ? self->priv->requested_operator_id : "",
                   self->priv->requested_operator_id ? MBIM_REGISTER_ACTION_MANUAL : MBIM_REGISTER_ACTION_AUTOMATIC,
                   self->priv->requested_data_class,
                   NULL));
    mbim_device_command (device,
                         message,
                         60,
                         NULL,
                         (GAsyncReadyCallback)register_state_set_ready,
                         task);
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

static void
visible_providers_query_ready (MbimDevice *device,
                               GAsyncResult *res,
                               GTask *task)
{
    MbimMessage *response;
    MbimProvider **providers;
    guint n_providers;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_visible_providers_response_parse (response,
                                                       &n_providers,
                                                       &providers,
                                                       &error)) {
        GList *info_list;

        info_list = mm_3gpp_network_info_list_from_mbim_providers ((const MbimProvider *const *)providers,
                                                                   n_providers);
        mbim_provider_array_free (providers);

        g_task_return_pointer (task, info_list, (GDestroyNotify)mm_3gpp_network_info_list_free);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "scanning networks...");
    message = mbim_message_visible_providers_query_new (MBIM_VISIBLE_PROVIDERS_ACTION_FULL_SCAN, NULL);
    mbim_device_command (device,
                         message,
                         300,
                         NULL,
                         (GAsyncReadyCallback)visible_providers_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Check support (Signal interface) */

static gboolean
modem_signal_check_support_finish (MMIfaceModemSignal  *self,
                                   GAsyncResult        *res,
                                   GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_signal_check_support_ready (MMIfaceModemSignal *self,
                                   GAsyncResult       *res,
                                   GTask              *task)
{
    gboolean parent_supported;

    parent_supported = iface_modem_signal_parent->check_support_finish (self, res, NULL);
    g_task_return_boolean (task, parent_supported);
    g_object_unref (task);
}

static void
modem_signal_check_support (MMIfaceModemSignal  *self,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If ATDS signal is supported, we support the Signal interface */
    if (MM_BROADBAND_MODEM_MBIM (self)->priv->is_atds_signal_supported) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Otherwise, check if the parent CESQ-based implementation works */
    g_assert (iface_modem_signal_parent->check_support && iface_modem_signal_parent->check_support_finish);
    iface_modem_signal_parent->check_support (self,
                                              (GAsyncReadyCallback)parent_signal_check_support_ready,
                                              task);
}

/*****************************************************************************/
/* Load extended signal information (Signal interface) */

typedef struct {
    MMSignal *gsm;
    MMSignal *umts;
    MMSignal *lte;
    MMSignal *nr5g;
} SignalLoadValuesResult;

static void
signal_load_values_result_free (SignalLoadValuesResult *result)
{
    g_clear_object (&result->gsm);
    g_clear_object (&result->umts);
    g_clear_object (&result->lte);
    g_clear_object (&result->nr5g);
    g_slice_free (SignalLoadValuesResult, result);
}

static gboolean
modem_signal_load_values_finish (MMIfaceModemSignal  *self,
                                 GAsyncResult        *res,
                                 MMSignal           **cdma,
                                 MMSignal           **evdo,
                                 MMSignal           **gsm,
                                 MMSignal           **umts,
                                 MMSignal           **lte,
                                 MMSignal           **nr5g,
                                 GError             **error)
{
    SignalLoadValuesResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    if (gsm)
        *gsm = g_steal_pointer (&result->gsm);
    if (umts)
        *umts = g_steal_pointer (&result->umts);
    if (lte)
        *lte = g_steal_pointer (&result->lte);
    if (nr5g)
        *nr5g = g_steal_pointer (&result->nr5g);

    signal_load_values_result_free (result);

    /* No 3GPP2 support */
    if (cdma)
        *cdma = NULL;
    if (evdo)
        *evdo = NULL;
    return TRUE;
}

static void
atds_signal_query_ready (MbimDevice   *device,
                         GAsyncResult *res,
                         GTask        *task)
{
    MbimMessage            *response;
    SignalLoadValuesResult *result;
    GError                 *error = NULL;
    guint32                 rssi;
    guint32                 error_rate;
    guint32                 rscp;
    guint32                 ecno;
    guint32                 rsrq;
    guint32                 rsrp;
    guint32                 snr;

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_atds_signal_response_parse (response, &rssi, &error_rate, &rscp, &ecno, &rsrq, &rsrp, &snr, &error)) {
        g_task_return_error (task, error);
        goto out;
    }

    result = g_slice_new0 (SignalLoadValuesResult);

    if (!mm_signal_from_atds_signal_response (rssi, rscp, ecno, rsrq, rsrp, snr, &result->gsm, &result->umts, &result->lte)) {
        signal_load_values_result_free (result);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No signal details given");
        goto out;
    }

    g_task_return_pointer (task, result, (GDestroyNotify) signal_load_values_result_free);

out:
    if (response)
        mbim_message_unref (response);
    g_object_unref (task);
}

static void
parent_signal_load_values_ready (MMIfaceModemSignal *self,
                                 GAsyncResult       *res,
                                 GTask              *task)
{
    SignalLoadValuesResult *result;
    GError                 *error = NULL;

    result = g_slice_new0 (SignalLoadValuesResult);
    if (!iface_modem_signal_parent->load_values_finish (self, res,
                                                        NULL, NULL,
                                                        &result->gsm, &result->umts, &result->lte, &result->nr5g,
                                                        &error)) {
        signal_load_values_result_free (result);
        g_task_return_error (task, error);
    } else if (!result->gsm && !result->umts && !result->lte) {
        signal_load_values_result_free (result);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No signal details given");
    } else
        g_task_return_pointer (task, result, (GDestroyNotify) signal_load_values_result_free);
    g_object_unref (task);
}

static void
mbimexv2_signal_state_query_ready (MbimDevice   *device,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    MMBroadbandModemMbim            *self;
    GError                          *error = NULL;
    SignalLoadValuesResult          *result;
    g_autoptr(MbimMessage)           response = NULL;
    g_autoptr(MbimRsrpSnrInfoArray)  rsrp_snr = NULL;
    guint32                          rsrp_snr_count = 0;
    guint32                          rssi;
    guint32                          error_rate = 99;
    MbimDataClass                    data_class;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed MBIMEx v2.0 signal state response support check");
        g_object_unref (task);
        return;
    }

    if (!mbim_message_ms_basic_connect_v2_signal_state_response_parse (
            response,
            &rssi,
            &error_rate,
            NULL, /* signal_strength_interval */
            NULL, /* rssi_threshold */
            NULL, /* error_rate_threshold */
            &rsrp_snr_count,
            &rsrp_snr,
            &error)) {
        g_prefix_error (&error, "Failed processing MBIMEx v2.0 signal state response: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    result = g_slice_new0 (SignalLoadValuesResult);

    /* Best guess of current data class */
    data_class = self->priv->enabled_cache.highest_available_data_class;
    if (data_class == 0)
        data_class = self->priv->enabled_cache.available_data_classes;
    if (!mm_signal_from_mbim_signal_state (
            data_class, rssi, error_rate, rsrp_snr, rsrp_snr_count, self,
            NULL, NULL, &result->gsm, &result->umts, &result->lte, &result->nr5g)) {
        signal_load_values_result_free (result);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No signal details given");
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, result, (GDestroyNotify) signal_load_values_result_free);
    g_object_unref (task);
}

static void
modem_signal_load_values (MMIfaceModemSignal  *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MbimDevice  *device;
    MbimMessage *message;
    GTask       *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        message = mbim_message_signal_state_query_new (NULL);
        mbim_device_command (device,
                             message,
                             5,
                             NULL,
                             (GAsyncReadyCallback)mbimexv2_signal_state_query_ready,
                             task);
        mbim_message_unref (message);
        return;
    }

    if (MM_BROADBAND_MODEM_MBIM (self)->priv->is_atds_signal_supported) {
        message = mbim_message_atds_signal_query_new (NULL);
        mbim_device_command (device,
                             message,
                             5,
                             NULL,
                             (GAsyncReadyCallback)atds_signal_query_ready,
                             task);
        mbim_message_unref (message);
        return;
    }

    /* Fallback to parent CESQ based implementation */
    g_assert (iface_modem_signal_parent->load_values && iface_modem_signal_parent->load_values_finish);
    iface_modem_signal_parent->load_values (self,
                                            NULL,
                                            (GAsyncReadyCallback)parent_signal_load_values_ready,
                                            task);
}

/*****************************************************************************/
/* Setup threshold values (Signal interface) */

static gboolean
modem_signal_setup_thresholds_finish (MMIfaceModemSignal  *self,
                                      GAsyncResult        *res,
                                      GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
signal_state_set_thresholds_ready (MbimDevice   *device,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                  *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_signal_state_response_parse (
            response,
            NULL, /* rssi */
            NULL, /* error_rate */
            NULL, /* signal_strength_interval */
            NULL, /* rssi_threshold */
            NULL, /* error_rate_threshold */
            &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
modem_signal_setup_thresholds (MMIfaceModemSignal  *self,
                               guint                rssi_threshold,
                               gboolean             error_rate_threshold,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    g_autoptr(MbimMessage)  message = NULL;
    MbimDevice             *device;
    GTask                  *task;
    guint                   coded_rssi_threshold = 0;
    guint                   coded_error_rate_threshold = 0;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* the input RSSI threshold difference is given in dBm, and in the MBIM
     * protocol we use a linear scale of coded values that correspond to 2dBm
     * per code point. */
    if (rssi_threshold) {
        coded_rssi_threshold = rssi_threshold / 2;
        if (!coded_rssi_threshold)
            coded_rssi_threshold = 1; /* minimum value when enabled */
    }

    /* the input error rate threshold is given as a boolean to enable or
     * disable, and in the MBIM protocol we have a non-linear scale of
     * coded values. We just select the minimum coded value, so that we
     * get all reports, i.e. every time it changes the coded value */
    if (error_rate_threshold)
        coded_error_rate_threshold = 1; /* minimum value when enabled */

    /* setting signal strength interval to 0 disables threshold-based
     * notifications on certain modems (FM350).
     * hence, it is being set to 5 as per FBC's recommendation.
     * typically, setting this parameter to 0 should make the modem
     * set the value to its internal default as per spec. */
    message = (mbim_message_signal_state_set_new (
                   5, /* non-zero default signal strength interval */
                   coded_rssi_threshold,
                   coded_error_rate_threshold,
                   NULL));

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)signal_state_set_thresholds_ready,
                         task);
}

/*****************************************************************************/
/* Check support (3GPP profile management interface) */

static gboolean
modem_3gpp_profile_manager_check_support_finish (MMIfaceModem3gppProfileManager  *_self,
                                                 GAsyncResult                    *res,
                                                 gchar                          **index_field,
                                                 GError                         **error)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    g_assert (g_task_propagate_boolean (G_TASK (res), NULL));

    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self))) {
        if (self->priv->is_profile_management_ext_supported) {
            *index_field = g_strdup ("apn-type");
            return TRUE;
        }
        if (self->priv->is_profile_management_supported) {
            *index_field = g_strdup ("profile-id");
            return TRUE;
        }
    }
    return FALSE;

}

static void
modem_3gpp_profile_manager_check_support (MMIfaceModem3gppProfileManager  *self,
                                          GAsyncReadyCallback              callback,
                                          gpointer                         user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (3gppProfileManager interface) */

static gboolean
common_setup_cleanup_unsolicited_events_3gpp_profile_manager_finish (MMIfaceModem3gppProfileManager  *self,
                                                                     GAsyncResult                    *res,
                                                                     GError                         **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
cleanup_unsolicited_events_3gpp_profile_manager (MMIfaceModem3gppProfileManager *_self,
                                                 GAsyncReadyCallback             callback,
                                                 gpointer                        user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_events_3gpp_profile_manager (MMIfaceModem3gppProfileManager *_self,
                                               GAsyncReadyCallback             callback,
                                               gpointer                        user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited event handlers (3gppProfileManager interface) */

static gboolean
common_enable_disable_unsolicited_events_3gpp_profile_manager_finish (MMIfaceModem3gppProfileManager  *self,
                                                                      GAsyncResult                    *res,
                                                                      GError                         **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
disable_unsolicited_events_3gpp_profile_manager (MMIfaceModem3gppProfileManager *_self,
                                                 GAsyncReadyCallback             callback,
                                                 gpointer                        user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
enable_unsolicited_events_3gpp_profile_manager (MMIfaceModem3gppProfileManager *_self,
                                                GAsyncReadyCallback             callback,
                                                gpointer                        user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_PROVISIONED_CONTEXTS;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Check format (3gppProfileManager interface) */

static gboolean
modem_3gpp_profile_manager_check_format_finish (MMIfaceModem3gppProfileManager  *_self,
                                                GAsyncResult                    *res,
                                                gboolean                        *new_id,
                                                gint                            *min_profile_id,
                                                gint                            *max_profile_id,
                                                GEqualFunc                      *apn_cmp,
                                                MM3gppProfileCmpFlags           *profile_cmp_flags,
                                                GError                         **error)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    if (!g_task_propagate_boolean (G_TASK (res), error)) {
        g_assert_not_reached ();
        return FALSE;
    }

    if (new_id)
        *new_id = TRUE;
    if (min_profile_id)
        *min_profile_id = 1;
    if (max_profile_id)
        *max_profile_id = G_MAXINT - 1;
    /* use default string comparison method */
    if (apn_cmp)
        *apn_cmp = NULL;
    if (profile_cmp_flags) {
        if (!self->priv->is_profile_management_ext_supported)
            *profile_cmp_flags = (MM_3GPP_PROFILE_CMP_FLAGS_NO_IP_TYPE |
                                  MM_3GPP_PROFILE_CMP_FLAGS_NO_ACCESS_TYPE_PREFERENCE |
                                  MM_3GPP_PROFILE_CMP_FLAGS_NO_ROAMING_ALLOWANCE |
                                  MM_3GPP_PROFILE_CMP_FLAGS_NO_PROFILE_SOURCE);
        else
            /* when using the MS extensions, we support all IP type, access type
             * preference, roaming allowance and profile source */
            *profile_cmp_flags = 0;
    }
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
/* List profiles (3GPP profile management interface) */

typedef struct {
    GList *profiles;
} ListProfilesContext;

static void
list_profiles_context_free (ListProfilesContext *ctx)
{
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

static MM3gppProfile *
provisioned_context_element_to_3gpp_profile (MbimProvisionedContextElement *element)
{
    MM3gppProfile   *profile;
    MMBearerApnType  apn_type;

    apn_type = mm_bearer_apn_type_from_mbim_context_type (mbim_uuid_to_context_type (&element->context_type));
    if (apn_type == MM_BEARER_APN_TYPE_NONE)
        return NULL;

    profile = mm_3gpp_profile_new ();
    mm_3gpp_profile_set_profile_id   (profile, element->context_id);
    mm_3gpp_profile_set_apn          (profile, element->access_string);
    mm_3gpp_profile_set_apn_type     (profile, apn_type);
    mm_3gpp_profile_set_user         (profile, element->user_name);
    mm_3gpp_profile_set_password     (profile, element->password);
    mm_3gpp_profile_set_allowed_auth (profile, (mm_bearer_allowed_auth_from_mbim_auth_protocol (element->auth_protocol)));
    /* compression unused, and ip-type not provided */
    return profile;
}

static MM3gppProfile *
provisioned_context_element_v2_to_3gpp_profile (MMBroadbandModemMbim            *self,
                                                MbimProvisionedContextElementV2 *element)
{
    MM3gppProfile                *profile;
    MMBearerApnType               apn_type;
    GError                       *error = NULL;
    gboolean                      enabled;
    MMBearerRoamingAllowance      roaming_allowance;
    MMBearerAccessTypePreference  access_type_preference;
    MMBearerProfileSource         profile_source;

    apn_type = mm_bearer_apn_type_from_mbim_context_type (mbim_uuid_to_context_type (&element->context_type));
    if (apn_type == MM_BEARER_APN_TYPE_NONE)
        return NULL;

    profile = mm_3gpp_profile_new ();
    mm_3gpp_profile_set_profile_id   (profile, element->context_id);
    mm_3gpp_profile_set_apn          (profile, element->access_string);
    mm_3gpp_profile_set_apn_type     (profile, apn_type);
    mm_3gpp_profile_set_user         (profile, element->user_name);
    mm_3gpp_profile_set_password     (profile, element->password);
    mm_3gpp_profile_set_allowed_auth (profile, (mm_bearer_allowed_auth_from_mbim_auth_protocol (element->auth_protocol)));

    if (!mm_boolean_from_mbim_context_state (element->state, &enabled, &error)) {
        mm_obj_dbg (self, "ignoring enable setting: %s", error->message);
        g_clear_error (&error);
    } else
        mm_3gpp_profile_set_enabled (profile, enabled);

    roaming_allowance = mm_bearer_roaming_allowance_from_mbim_context_roaming_control (element->roaming, &error);
    if (roaming_allowance == MM_BEARER_ROAMING_ALLOWANCE_NONE) {
        mm_obj_dbg (self, "ignoring roaming allowance: %s", error->message);
        g_clear_error (&error);
    } else
        mm_3gpp_profile_set_roaming_allowance (profile, roaming_allowance);

    if (!mm_bearer_access_type_preference_from_mbim_context_media_type (element->media_type, &access_type_preference, &error)) {
        mm_obj_dbg (self, "ignoring access type preference: %s", error->message);
        g_clear_error (&error);
    } else
        mm_3gpp_profile_set_access_type_preference (profile, access_type_preference);

    profile_source = mm_bearer_profile_source_from_mbim_context_source (element->source, &error);
    if (profile_source == MM_BEARER_PROFILE_SOURCE_UNKNOWN) {
        mm_obj_dbg (self, "ignoring profile source: %s", error->message);
        g_clear_error (&error);
    } else
        mm_3gpp_profile_set_profile_source (profile, profile_source);

    /* compression unused, and ip-type not provided */
    return profile;
}

static void
profile_manager_provisioned_contexts_query_ready (MbimDevice   *device,
                                                  GAsyncResult *res,
                                                  GTask        *task)
{
    ListProfilesContext    *ctx;
    GError                 *error = NULL;
    guint                   i;
    guint32                 provisioned_contexts_count = 0;
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(MbimProvisionedContextElementArray) provisioned_contexts = NULL;

    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_provisioned_contexts_response_parse (response,
                                                           &provisioned_contexts_count,
                                                           &provisioned_contexts,
                                                           &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < provisioned_contexts_count; i++) {
        MM3gppProfile *profile;

        profile = provisioned_context_element_to_3gpp_profile (provisioned_contexts[i]);
        if (profile)
            ctx->profiles = g_list_append (ctx->profiles, profile);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
profile_manager_provisioned_contexts_v2_query_ready (MbimDevice   *device,
                                                     GAsyncResult *res,
                                                     GTask        *task)
{
    MMBroadbandModemMbim   *self;
    ListProfilesContext    *ctx;
    GError                 *error = NULL;
    guint                   i;
    guint32                 provisioned_contexts_count = 0;
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(MbimProvisionedContextElementV2Array) provisioned_contexts_v2 = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_provisioned_contexts_response_parse (response,
                                                                                       &provisioned_contexts_count,
                                                                                       &provisioned_contexts_v2,
                                                                                       &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < provisioned_contexts_count; i++) {
        MM3gppProfile *profile;

        profile = provisioned_context_element_v2_to_3gpp_profile (self, provisioned_contexts_v2[i]);
        if (profile)
            ctx->profiles = g_list_append (ctx->profiles, profile);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_list_profiles (MMIfaceModem3gppProfileManager  *_self,
                                          GAsyncReadyCallback              callback,
                                          gpointer                         user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    ListProfilesContext    *ctx;
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (ListProfilesContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) list_profiles_context_free);

    mm_obj_dbg (self, "querying provisioned contexts...");

    if (self->priv->is_profile_management_ext_supported) {
        message = mbim_message_ms_basic_connect_extensions_provisioned_contexts_query_new (NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)profile_manager_provisioned_contexts_v2_query_ready,
                             task);
        return;
    }

    message = mbim_message_provisioned_contexts_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)profile_manager_provisioned_contexts_query_ready,
                         task);
}

/*****************************************************************************/
/* Store profile (3GPP profile management interface) */

typedef struct {
    gint            profile_id;
    MMBearerApnType apn_type;
} StoreProfileContext;

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
        *out_apn_type = ctx->apn_type;
    return TRUE;
}

static void
profile_manager_provisioned_contexts_set_ready (MbimDevice   *device,
                                                GAsyncResult *res,
                                                GTask        *task)
{
    GError                 *error = NULL;
    g_autoptr(MbimMessage)  response = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response && mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_store_profile (MMIfaceModem3gppProfileManager *_self,
                                          MM3gppProfile                  *profile,
                                          const gchar                    *index_field,
                                          GAsyncReadyCallback             callback,
                                          gpointer                        user_data)
{
    MMBroadbandModemMbim     *self = MM_BROADBAND_MODEM_MBIM (_self);
    StoreProfileContext      *ctx = NULL;
    MbimDevice               *device;
    GTask                    *task;
    GError                   *error = NULL;
    MMBearerAllowedAuth       allowed_auth;
    MbimAuthProtocol          auth_protocol = MBIM_AUTH_PROTOCOL_NONE;
    MbimContextType           context_type;
    const MbimUuid           *context_type_uuid;
    const gchar              *apn;
    const gchar              *user;
    const gchar              *password;
    g_autofree gchar         *apn_type_str = NULL;
    g_autoptr(MbimMessage)    message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new0 (StoreProfileContext, 1);
    ctx->profile_id = MM_3GPP_PROFILE_ID_UNKNOWN;
    ctx->apn_type = MM_BEARER_APN_TYPE_NONE;

    g_task_set_task_data (task, ctx, (GDestroyNotify) g_free);

    ctx->profile_id = mm_3gpp_profile_get_profile_id (profile);

    ctx->apn_type = mm_3gpp_profile_get_apn_type (profile);
    apn_type_str = mm_bearer_apn_type_build_string_from_mask (ctx->apn_type);
    context_type = mm_bearer_apn_type_to_mbim_context_type (ctx->apn_type,
                                                            self->priv->is_context_type_ext_supported,
                                                            self,
                                                            &error);
    if (error) {
        g_prefix_error (&error, "Failed to convert mbim context type from apn type: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    context_type_uuid = mbim_uuid_from_context_type (context_type);

    apn = mm_3gpp_profile_get_apn (profile);
    user = mm_3gpp_profile_get_user (profile);
    password = mm_3gpp_profile_get_password (profile);
    allowed_auth = mm_3gpp_profile_get_allowed_auth (profile);
    if ((allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN) || user || password) {
        auth_protocol = mm_bearer_allowed_auth_to_mbim_auth_protocol (allowed_auth, self, &error);
        if (error) {
            g_prefix_error (&error, "Failed to convert mbim auth protocol from allowed auth: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    }

    if (g_strcmp0 (index_field, "profile-id") == 0) {
        mm_obj_dbg (self, "storing profile '%d': apn '%s', apn type '%s'",
                    ctx->profile_id, apn, apn_type_str);
        message = mbim_message_provisioned_contexts_set_new (ctx->profile_id,
                                                             context_type_uuid,
                                                             apn ? apn : "",
                                                             user ? user : "",
                                                             password ? password : "",
                                                             MBIM_COMPRESSION_NONE,
                                                             auth_protocol,
                                                             "", /* provider id */
                                                             &error);
    } else if (g_strcmp0 (index_field, "apn-type") == 0) {
        MbimContextIpType         ip_type;
        MbimContextState          state;
        MbimContextRoamingControl roaming;
        MbimContextMediaType      media_type;
        MbimContextSource         source;

        g_assert (self->priv->is_profile_management_ext_supported);

        state = mm_boolean_to_mbim_context_state (mm_3gpp_profile_get_enabled (profile));

        ip_type = mm_bearer_ip_family_to_mbim_context_ip_type (mm_3gpp_profile_get_ip_type (profile), &error);
        if (error) {
            g_prefix_error (&error, "Failed to convert mbim context ip type from ip type: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        if (!mm_bearer_roaming_allowance_to_mbim_context_roaming_control (mm_3gpp_profile_get_roaming_allowance (profile), self, &roaming, &error)) {
            g_prefix_error (&error, "Failed to convert mbim context roaming control from roaming allowance: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        if (!mm_bearer_access_type_preference_to_mbim_context_media_type (mm_3gpp_profile_get_access_type_preference (profile), self, &media_type, &error)) {
            g_prefix_error (&error, "Failed to convert mbim context media type from access type preference: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        if (!mm_bearer_profile_source_to_mbim_context_source (mm_3gpp_profile_get_profile_source (profile), self, &source, &error)) {
            g_prefix_error (&error, "Failed to convert mbim context source from profile source: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        if (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)
            mm_obj_warn (self, "ignoring profile id '%d' when storing profile: unsupported", ctx->profile_id);

        mm_obj_dbg (self, "storing profile '%s': apn '%s'", apn_type_str, apn);
        message = mbim_message_ms_basic_connect_extensions_provisioned_contexts_set_new (MBIM_CONTEXT_OPERATION_DEFAULT,
                                                                                         context_type_uuid,
                                                                                         ip_type,
                                                                                         state,
                                                                                         roaming,
                                                                                         media_type,
                                                                                         source,
                                                                                         apn ? apn : "",
                                                                                         user ? user : "",
                                                                                         password ? password : "",
                                                                                         MBIM_COMPRESSION_NONE,
                                                                                         auth_protocol,
                                                                                         &error);
    } else
        g_assert_not_reached ();

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)profile_manager_provisioned_contexts_set_ready,
                         task);
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

static void
profile_manager_provisioned_contexts_reset_ready (MbimDevice   *device,
                                                  GAsyncResult *res,
                                                  GTask        *task)
{
    GError                 *error = NULL;
    g_autoptr(MbimMessage)  response = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response && mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_error (task, error);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_delete_profile (MMIfaceModem3gppProfileManager *_self,
                                           MM3gppProfile                  *profile,
                                           const gchar                    *index_field,
                                           GAsyncReadyCallback             callback,
                                           gpointer                        user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice             *device;
    GTask                  *task;
    GError                 *error = NULL;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (g_strcmp0 (index_field, "profile-id") == 0) {
        gint profile_id;

        profile_id = mm_3gpp_profile_get_profile_id (profile);
        g_assert (profile_id != MM_3GPP_PROFILE_ID_UNKNOWN);

        mm_obj_dbg (self, "deleting profile '%d'", profile_id);
        message = mbim_message_provisioned_contexts_set_new (profile_id,
                                                             mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_NONE),
                                                             "", /* access string */
                                                             "", /* user */
                                                             "", /* pass */
                                                             MBIM_COMPRESSION_NONE,
                                                             MBIM_AUTH_PROTOCOL_NONE,
                                                             "", /* provider id */
                                                             &error);
    } else if (g_strcmp0 (index_field, "apn-type") == 0) {
        MMBearerApnType  apn_type;
        MbimContextType  context_type;

        g_assert (self->priv->is_profile_management_ext_supported);
        g_assert (self->priv->is_context_type_ext_supported);

        apn_type = mm_3gpp_profile_get_apn_type (profile);
        g_assert (apn_type != MM_BEARER_APN_TYPE_NONE);
        context_type = mm_bearer_apn_type_to_mbim_context_type (apn_type, TRUE, self, &error);
        if (error)
            g_prefix_error (&error, "Failed to convert mbim context type from apn type: ");
        else {
            const MbimUuid  *context_type_uuid;

            context_type_uuid = mbim_uuid_from_context_type (context_type);
            message = mbim_message_ms_basic_connect_extensions_provisioned_contexts_set_new (MBIM_CONTEXT_OPERATION_DELETE,
                                                                                             context_type_uuid,
                                                                                             MBIM_CONTEXT_IP_TYPE_DEFAULT,
                                                                                             MBIM_CONTEXT_STATE_DISABLED,
                                                                                             MBIM_CONTEXT_ROAMING_CONTROL_ALLOW_ALL,
                                                                                             MBIM_CONTEXT_MEDIA_TYPE_ALL,
                                                                                             MBIM_CONTEXT_SOURCE_ADMIN,
                                                                                             "", /* access string */
                                                                                             "", /* user */
                                                                                             "", /* password */
                                                                                             MBIM_COMPRESSION_NONE,
                                                                                             MBIM_AUTH_PROTOCOL_NONE,
                                                                                             &error);
        }
    } else
        g_assert_not_reached ();

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)profile_manager_provisioned_contexts_reset_ready,
                         task);
}

/*****************************************************************************/
/* Check if USSD supported (3GPP/USSD interface) */

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
    g_task_return_boolean (task, MM_BROADBAND_MODEM_MBIM (self)->priv->is_ussd_supported);
    g_object_unref (task);
}

/*****************************************************************************/
/* USSD encoding/deconding helpers
 *
 * Note: we don't care about subclassing the ussd_encode/decode methods in the
 * interface, as we're going to use this methods just here.
 */

static GByteArray *
ussd_encode (const gchar  *command,
             guint32      *scheme,
             GError      **error)
{
    g_autoptr(GByteArray) array = NULL;

    if (mm_charset_can_convert_to (command, MM_MODEM_CHARSET_GSM)) {
        g_autoptr(GByteArray)  gsm = NULL;
        guint8                *packed;
        guint32                packed_len = 0;

        *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;
        gsm = mm_modem_charset_bytearray_from_utf8 (command, MM_MODEM_CHARSET_GSM, FALSE, error);
        if (!gsm) {
            g_prefix_error (error, "Failed to encode USSD command in GSM7 charset: ");
            return NULL;
        }
        packed = mm_charset_gsm_pack (gsm->data, gsm->len, 0, &packed_len);
        array = g_byte_array_new_take (packed, packed_len);
    } else {
        *scheme = MM_MODEM_GSM_USSD_SCHEME_UCS2;
        array = mm_modem_charset_bytearray_from_utf8 (command, MM_MODEM_CHARSET_UCS2, FALSE, error);
        if (!array) {
            g_prefix_error (error, "Failed to encode USSD command in UCS2 charset: ");
            return NULL;
        }
    }

    if (array->len > 160) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Failed to encode USSD command: encoded data too long (%u > 160)", array->len);
        return NULL;
    }

    return g_steal_pointer (&array);
}

static gchar *
ussd_decode (guint32      scheme,
             GByteArray  *data,
             GError     **error)
{
    gchar *decoded = NULL;

    if (scheme == MM_MODEM_GSM_USSD_SCHEME_7BIT) {
        g_autoptr(GByteArray)  unpacked_array = NULL;
        guint8                *unpacked = NULL;
        guint32                unpacked_len;

        unpacked = mm_charset_gsm_unpack ((const guint8 *)data->data, (data->len * 8) / 7, 0, &unpacked_len);
        unpacked_array = g_byte_array_new_take (unpacked, unpacked_len);

        decoded = mm_modem_charset_bytearray_to_utf8 (unpacked_array, MM_MODEM_CHARSET_GSM, FALSE, error);
        if (!decoded)
            g_prefix_error (error, "Error decoding USSD command in 0x%04x scheme (GSM7 charset): ", scheme);
    } else if (scheme == MM_MODEM_GSM_USSD_SCHEME_UCS2) {
        decoded = mm_modem_charset_bytearray_to_utf8 (data, MM_MODEM_CHARSET_UCS2, FALSE, error);
        if (!decoded)
            g_prefix_error (error, "Error decoding USSD command in 0x%04x scheme (UCS2 charset): ", scheme);
    } else
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Failed to decode USSD command in unsupported 0x%04x scheme", scheme);

    return decoded;
}

/*****************************************************************************/
/* USSD notifications */

static void
process_ussd_message (MMBroadbandModemMbim *self,
                      MbimUssdResponse      ussd_response,
                      MbimUssdSessionState  ussd_session_state,
                      guint32               scheme,
                      guint32               data_size,
                      const guint8         *data)
{
    GTask                       *task = NULL;
    MMModem3gppUssdSessionState  ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE;
    GByteArray                  *bytearray;
    gchar                       *converted = NULL;
    GError                      *error = NULL;

    /* Steal task and balance out received reference */
    if (self->priv->pending_ussd_action) {
        task = self->priv->pending_ussd_action;
        self->priv->pending_ussd_action = NULL;
    }

    bytearray = g_byte_array_new ();
    if (data && data_size)
        bytearray = g_byte_array_append (bytearray, data, data_size);

    switch (ussd_response) {
    case MBIM_USSD_RESPONSE_NO_ACTION_REQUIRED:
        /* no further action required */
        converted = ussd_decode (scheme, bytearray, &error);
        if (!converted)
            break;

        /* Response to the user's request? */
        if (task)
            break;

        /* Network-initiated USSD-Notify */
        mm_iface_modem_3gpp_ussd_update_network_notification (MM_IFACE_MODEM_3GPP_USSD (self), converted);
        g_clear_pointer (&converted, g_free);
        break;

    case MBIM_USSD_RESPONSE_ACTION_REQUIRED:
        /* further action required */
        ussd_state = MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE;

        converted = ussd_decode (scheme, bytearray, &error);
        if (!converted)
            break;
        /* Response to the user's request? */
        if (task)
            break;

        /* Network-initiated USSD-Request */
        mm_iface_modem_3gpp_ussd_update_network_request (MM_IFACE_MODEM_3GPP_USSD (self), converted);
        g_clear_pointer (&converted, g_free);
        break;

    case MBIM_USSD_RESPONSE_TERMINATED_BY_NETWORK:
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_ABORTED, "USSD terminated by network");
        break;

    case MBIM_USSD_RESPONSE_OTHER_LOCAL_CLIENT:
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_RETRY, "Another ongoing USSD operation is in progress");
        break;

    case MBIM_USSD_RESPONSE_OPERATION_NOT_SUPPORTED:
        error = g_error_new (MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED, "Operation not supported");
        break;

    case MBIM_USSD_RESPONSE_NETWORK_TIMEOUT:
        error = g_error_new (MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT, "Network timeout");
        break;

    default:
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Unknown USSD response (%u)", ussd_response);
        break;
    }

    mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self), ussd_state);

    g_byte_array_unref (bytearray);

    /* Complete the pending action */
    if (task) {
        if (error)
            g_task_return_error (task, error);
        else if (converted)
            g_task_return_pointer (task, converted, g_free);
        else
            g_assert_not_reached ();
        g_object_unref (task);
        return;
    }

    /* If no pending task, just report the error */
    if (error) {
        mm_obj_dbg (self, "network reported USSD message: %s", error->message);
        g_error_free (error);
    }

    g_assert (!converted);
}

static void
process_ussd_notification (MMBroadbandModemMbim *self,
                           MbimMessage          *notification)
{
    MbimUssdResponse      ussd_response;
    MbimUssdSessionState  ussd_session_state;
    guint32               scheme;
    guint32               data_size;
    const guint8         *data;

    if (mbim_message_ussd_notification_parse (notification,
                                              &ussd_response,
                                              &ussd_session_state,
                                              &scheme,
                                              &data_size,
                                              &data,
                                              NULL)) {
        mm_obj_dbg (self, "received USSD indication: %s, session state: %s, scheme: 0x%x, data size: %u bytes",
                    mbim_ussd_response_get_string (ussd_response),
                    mbim_ussd_session_state_get_string (ussd_session_state),
                    scheme,
                    data_size);
        process_ussd_message (self, ussd_response, ussd_session_state, scheme, data_size, data);
    }
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited result codes (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gppUssd  *self,
                                                         GAsyncResult          *res,
                                                         GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_flag_ussd_ready (MMBroadbandModemMbim *self,
                              GAsyncResult         *res,
                              GTask                *task)
{
    GError *error = NULL;

    if (!common_setup_cleanup_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_setup_cleanup_unsolicited_ussd_events (MMBroadbandModemMbim *self,
                                              gboolean              setup,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GINT_TO_POINTER (setup), NULL);

    if (setup)
        self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_USSD;
    else
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_USSD;
    common_setup_cleanup_unsolicited_events (self, setup, (GAsyncReadyCallback)common_setup_flag_ussd_ready, task);
}

static void
modem_3gpp_ussd_cleanup_unsolicited_events (MMIfaceModem3gppUssd *self,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data)
{
    common_setup_cleanup_unsolicited_ussd_events (MM_BROADBAND_MODEM_MBIM (self), FALSE, callback, user_data);
}

static void
modem_3gpp_ussd_setup_unsolicited_events (MMIfaceModem3gppUssd *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    common_setup_cleanup_unsolicited_ussd_events (MM_BROADBAND_MODEM_MBIM (self), TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/Disable URCs (3GPP/USSD interface) */

static gboolean
modem_3gpp_ussd_enable_disable_unsolicited_events_finish (MMIfaceModem3gppUssd  *self,
                                                          GAsyncResult          *res,
                                                          GError               **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
modem_3gpp_ussd_disable_unsolicited_events (MMIfaceModem3gppUssd *_self,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_USSD;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
modem_3gpp_ussd_enable_unsolicited_events (MMIfaceModem3gppUssd *_self,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_USSD;
    common_enable_disable_unsolicited_events (self, callback, user_data);
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
ussd_send_ready (MbimDevice           *device,
                 GAsyncResult         *res,
                 MMBroadbandModemMbim *self)
{
    MbimMessage          *response;
    GError               *error = NULL;
    MbimUssdResponse      ussd_response;
    MbimUssdSessionState  ussd_session_state;
    guint32               scheme;
    guint32               data_size;
    const guint8         *data;

    /* Note: if there is a cached task, it is ALWAYS completed here */

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_ussd_response_parse (response,
                                          &ussd_response,
                                          &ussd_session_state,
                                          &scheme,
                                          &data_size,
                                          &data,
                                          &error)) {
        mm_obj_dbg (self, "received USSD response: %s, session state: %s, scheme: 0x%x, data size: %u bytes",
                    mbim_ussd_response_get_string (ussd_response),
                    mbim_ussd_session_state_get_string (ussd_session_state),
                    scheme,
                    data_size);
        process_ussd_message (self, ussd_response, ussd_session_state, scheme, data_size, data);
    } else {
        /* Report error in the cached task, if any */
        if (self->priv->pending_ussd_action) {
            GTask *task;

            task = self->priv->pending_ussd_action;
            self->priv->pending_ussd_action = NULL;
            g_task_return_error (task, error);
            g_object_unref (task);
        } else {
            mm_obj_dbg (self, "failed to parse USSD response: %s", error->message);
            g_clear_error (&error);
        }
    }

    if (response)
        mbim_message_unref (response);

    /* Balance out received reference */
    g_object_unref (self);
}

static void
modem_3gpp_ussd_send (MMIfaceModem3gppUssd *_self,
                      const gchar          *command,
                      GAsyncReadyCallback   callback,
                      gpointer              user_data)
{
    MMBroadbandModemMbim *self;
    MbimDevice           *device;
    GTask                *task;
    MbimUssdAction        action;
    MbimMessage          *message;
    GByteArray           *encoded;
    guint32               scheme = 0;
    GError               *error = NULL;

    self = MM_BROADBAND_MODEM_MBIM (_self);
    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* Fail if there is an ongoing operation already */
    if (self->priv->pending_ussd_action) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS,
                                 "there is already an ongoing USSD operation");
        g_object_unref (task);
        return;
    }

    switch (mm_iface_modem_3gpp_ussd_get_state (MM_IFACE_MODEM_3GPP_USSD (self))) {
        case MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE:
            action = MBIM_USSD_ACTION_INITIATE;
            break;
        case MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE:
            action = MBIM_USSD_ACTION_CONTINUE;
            break;
        case MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN:
        case MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE:
        default:
            g_assert_not_reached ();
            return;
    }

    encoded = ussd_encode (command, &scheme, &error);
    if (!encoded) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    message = mbim_message_ussd_set_new (action, scheme, encoded->len, encoded->data, &error);
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Cache the action, as it may be completed via URCs */
    self->priv->pending_ussd_action = task;
    mm_iface_modem_3gpp_ussd_update_state (_self, MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE);

    mbim_device_command (device,
                         message,
                         100,
                         NULL,
                         (GAsyncReadyCallback)ussd_send_ready,
                         g_object_ref (self)); /* Full reference! */
    mbim_message_unref (message);
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
ussd_cancel_ready (MbimDevice   *device,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBroadbandModemMbim *self;
    MbimMessage          *response;
    GError               *error = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response)
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

    /* Complete the pending action, regardless of the operation result */
    if (self->priv->pending_ussd_action) {
        GTask *pending_task;

        pending_task = self->priv->pending_ussd_action;
        self->priv->pending_ussd_action = NULL;

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

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_ussd_cancel (MMIfaceModem3gppUssd *_self,
                        GAsyncReadyCallback   callback,
                        gpointer              user_data)
{
    MMBroadbandModemMbim *self;
    MbimDevice           *device;
    GTask                *task;
    MbimMessage          *message;
    GError               *error = NULL;

    self = MM_BROADBAND_MODEM_MBIM (_self);
    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_ussd_set_new (MBIM_USSD_ACTION_CANCEL, 0, 0, NULL, &error);
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)ussd_cancel_ready,
                         task);
    mbim_message_unref (message);
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
messaging_check_support (MMIfaceModemMessaging *_self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* We only handle 3GPP messaging (PDU based) currently */
    if (self->priv->caps_sms & MBIM_SMS_CAPS_PDU_RECEIVE &&
        self->priv->caps_sms & MBIM_SMS_CAPS_PDU_SEND) {
        mm_obj_dbg (self, "messaging capabilities supported");
        g_task_return_boolean (task, TRUE);
    } else {
        mm_obj_dbg (self, "messaging capabilities not supported by this modem");
        g_task_return_boolean (task, FALSE);
    }
    g_object_unref (task);
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
    MMSmsStorage supported;

    *mem1 = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), 2);
    supported = MM_SMS_STORAGE_MT;
    g_array_append_val (*mem1, supported);
    *mem2 = g_array_ref (*mem1);
    *mem3 = g_array_ref (*mem1);
    return TRUE;
}

static void
messaging_load_supported_storages (MMIfaceModemMessaging *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load initial SMS parts */

static gboolean
load_initial_sms_parts_finish (MMIfaceModemMessaging  *self,
                               GAsyncResult           *res,
                               GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
add_sms_part (MMBroadbandModemMbim       *self,
              const MbimSmsPduReadRecord *pdu)
{
    MMSmsPart         *part;
    g_autoptr(GError)  error = NULL;

    part = mm_sms_part_3gpp_new_from_binary_pdu (pdu->message_index,
                                                 pdu->pdu_data,
                                                 pdu->pdu_data_size,
                                                 self,
                                                 FALSE,
                                                 &error);
    if (part) {
        mm_obj_dbg (self, "correctly parsed PDU (%d)", pdu->message_index);
        mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                            part,
                                            mm_sms_state_from_mbim_message_status (pdu->message_status),
                                            MM_SMS_STORAGE_MT);
    } else {
        /* Don't treat the error as critical */
        mm_obj_dbg (self, "error parsing PDU (%d): %s",
                    pdu->message_index, error->message);
    }
}

static gboolean
process_pdu_messages (MMBroadbandModemMbim       *self,
                      MbimSmsFormat               format,
                      guint32                     messages_count,
                      MbimSmsPduReadRecordArray  *pdu_messages,
                      GError                    **error)
{
    guint i;

    if (format != MBIM_SMS_FORMAT_PDU) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Ignoring SMS, unsupported format: '%s'",
                     mbim_sms_format_get_string (format));
        return FALSE;
    }

    if (!pdu_messages || !messages_count) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No SMS PDUs read");
        return FALSE;
    }

    mm_obj_dbg (self, "%u SMS PDUs reported", messages_count);
    for (i = 0; i < messages_count; i++) {
        if (pdu_messages[i]) {
            mm_obj_dbg (self, "processing SMS PDU %u/%u...", i+1, messages_count);
            add_sms_part (self, pdu_messages[i]);
        } else
            mm_obj_dbg (self, "ignoring invalid SMS PDU %u/%u...", i+1, messages_count);
    }

    return TRUE;
}

static void
sms_read_query_ready (MbimDevice   *device,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemMbim                *self;
    GError                              *error = NULL;
    MbimSmsFormat                        format;
    guint32                              messages_count;
    g_autoptr(MbimMessage)               response = NULL;
    g_autoptr(MbimSmsPduReadRecordArray) pdu_messages = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_sms_read_response_parse (
            response,
            &format,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            &error) ||
        !process_pdu_messages (self,
                               format,
                               messages_count,
                               pdu_messages,
                               &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
load_initial_sms_parts (MMIfaceModemMessaging *self,
                        MMSmsStorage           storage,
                        GAsyncReadyCallback    callback,
                        gpointer               user_data)
{
    GTask                  *task;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    g_assert (storage == MM_SMS_STORAGE_MT);

    task = g_task_new (self, NULL, callback, user_data);
    message = mbim_message_sms_read_query_new (MBIM_SMS_FORMAT_PDU,
                                               MBIM_SMS_FLAG_ALL,
                                               0, /* message index, unused */
                                               NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sms_read_query_ready,
                         task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (Messaging interface) */

static gboolean
common_setup_cleanup_unsolicited_events_messaging_finish (MMIfaceModemMessaging *self,
                                                          GAsyncResult *res,
                                                          GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
cleanup_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited event handlers (Messaging interface) */

static gboolean
common_enable_disable_unsolicited_events_messaging_finish (MMIfaceModemMessaging *self,
                                                           GAsyncResult *res,
                                                           GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
disable_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
enable_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMBaseSms *
messaging_create_sms (MMIfaceModemMessaging *self)
{
    return mm_sms_mbim_new (MM_BASE_MODEM (self));
}

/*****************************************************************************/
/* Check support (SAR interface) */

static gboolean
sar_check_support_finish (MMIfaceModemSar *self,
                          GAsyncResult    *res,
                          GError         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
sar_check_support (MMIfaceModemSar    *_self,
                   GAsyncReadyCallback callback,
                   gpointer            user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask                *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "SAR capabilities %s", self->priv->is_ms_sar_supported ? "supported" : "not supported");
    g_task_return_boolean (task, self->priv->is_ms_sar_supported);
    g_object_unref (task);
}

/*****************************************************************************/

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
sar_config_query_state_ready (MbimDevice   *device,
                              GAsyncResult *res,
                              GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    MbimSarBackoffState     state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_ms_sar_config_response_parse (
            response,
            NULL,
            &state,
            NULL,
            NULL,
            NULL,
            &error))
        g_task_return_boolean (task, state);
    else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_load_state (MMIfaceModemSar    *_self,
                GAsyncReadyCallback callback,
                gpointer            user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_ms_sar_config_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sar_config_query_state_ready,
                         task);
}

/*****************************************************************************/

static gboolean
sar_load_power_level_finish (MMIfaceModemSar *self,
                             GAsyncResult    *res,
                             guint           *out_power_level,
                             GError         **error)
{
    gssize result;

    result = g_task_propagate_int (G_TASK (res), error);
    if (result < 0)
        return FALSE;

    *out_power_level = (guint) result;
    return TRUE;
}

static void
sar_config_query_power_level_ready (MbimDevice   *device,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    MMBroadbandModemMbim               *self;
    GError                             *error = NULL;
    guint32                             states_count;
    g_autoptr(MbimSarConfigStateArray)  config_states = NULL;
    g_autoptr(MbimMessage)              response = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_ms_sar_config_response_parse (
            response,
            NULL,
            NULL,
            NULL,
            &states_count,
            &config_states,
            &error)) {
        if (states_count == 0) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Couldn't load config states");
        } else {
            if (states_count > 1)
                mm_obj_dbg (self, "Device reports SAR config states for %u antennas separately, but only considering the first one", states_count);
            g_task_return_int (task, config_states[0]->backoff_index);
        }
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_load_power_level (MMIfaceModemSar    *_self,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice             *device;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_ms_sar_config_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sar_config_query_power_level_ready,
                         task);
}

/*****************************************************************************/

static gboolean
sar_enable_finish (MMIfaceModemSar *self,
                   GAsyncResult    *res,
                   guint           *out_sar_power_level,
                   GError         **error)
{
    guint level;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    level = GPOINTER_TO_UINT (g_task_get_task_data (G_TASK (res)));
    if (out_sar_power_level)
        *out_sar_power_level = level;
    return TRUE;
}

static void
sar_config_set_enable_ready (MbimDevice   *device,
                             GAsyncResult *res,
                             GTask        *task)
{
    g_autoptr(MbimMessage) response = NULL;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_boolean (task, TRUE);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_enable (MMIfaceModemSar    *_self,
            gboolean            enable,
            GAsyncReadyCallback callback,
            gpointer            user_data)
{
    MMBroadbandModemMbim          *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice                    *device;
    GTask                         *task;
    g_autoptr(MbimMessage)         message = NULL;
    g_autofree MbimSarConfigState *config_state = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /*
     * the value 0xFFFFFFFF means all antennas
     * the backoff index set to the current index of modem
     */
    config_state = g_new (MbimSarConfigState, 1);
    config_state->antenna_index = 0xFFFFFFFF;
    config_state->backoff_index = mm_iface_modem_sar_get_power_level (_self);

    g_task_set_task_data (task, GUINT_TO_POINTER (config_state->backoff_index), NULL);

    message = mbim_message_ms_sar_config_set_new (MBIM_SAR_CONTROL_MODE_OS,
                                                  enable ? MBIM_SAR_BACKOFF_STATE_ENABLED : MBIM_SAR_BACKOFF_STATE_DISABLED,
                                                  1, (const MbimSarConfigState **)&config_state, NULL);

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sar_config_set_enable_ready,
                         task);
}

/*****************************************************************************/

static gboolean
sar_set_power_level_finish (MMIfaceModemSar *self,
                            GAsyncResult    *res,
                            GError         **error)
{
     return g_task_propagate_boolean (G_TASK (res), error);
}

static void
sar_config_set_power_level_ready (MbimDevice   *device,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_boolean (task, TRUE);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
sar_set_power_level (MMIfaceModemSar    *_self,
                     guint               power_level,
                     GAsyncReadyCallback callback,
                     gpointer            user_data)
{
    MMBroadbandModemMbim          *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice                    *device;
    GTask                         *task;
    g_autoptr(MbimMessage)         message = NULL;
    g_autofree MbimSarConfigState *config_state = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    /*
     * the value 0xFFFFFFFF means all antennas
     * the backoff index set to the input power level
     */
    config_state = g_new (MbimSarConfigState, 1);
    config_state->antenna_index = 0xFFFFFFFF;
    config_state->backoff_index = power_level;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_ms_sar_config_set_new (MBIM_SAR_CONTROL_MODE_OS,
                                                  MBIM_SAR_BACKOFF_STATE_ENABLED,
                                                  1, (const MbimSarConfigState **)&config_state, NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sar_config_set_power_level_ready,
                         task);
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);

    switch (prop_id) {
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
   case PROP_QMI_UNSUPPORTED:
        self->priv->qmi_unsupported = g_value_get_boolean (value);
        break;
#endif
   case PROP_INTEL_FIRMWARE_UPDATE_UNSUPPORTED:
        self->priv->intel_firmware_update_unsupported = g_value_get_boolean (value);
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
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);

    switch (prop_id) {
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    case PROP_QMI_UNSUPPORTED:
        g_value_set_boolean (value, self->priv->qmi_unsupported);
        break;
#endif
    case PROP_INTEL_FIRMWARE_UPDATE_UNSUPPORTED:
        g_value_set_boolean (value, self->priv->intel_firmware_update_unsupported);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/*****************************************************************************/
/* Create SIMs in all SIM slots */

typedef struct {
    GPtrArray *sim_slots;
    guint number_slots;
    guint query_slot_index;  /* range [0,number_slots-1] */
    guint active_slot_index; /* range [1,number_slots]   */
} LoadSimSlotsContext;

static void
load_sim_slots_context_free (LoadSimSlotsContext *ctx)
{
    g_clear_pointer (&ctx->sim_slots, g_ptr_array_unref);
    g_slice_free (LoadSimSlotsContext, ctx);
}

static void
sim_slot_free (MMBaseSim *sim)
{
    if (sim)
        g_object_unref (sim);
}

static gboolean
load_sim_slots_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GPtrArray   **sim_slots,
                       guint        *primary_sim_slot,
                       GError      **error)
{
    LoadSimSlotsContext *ctx;

    if (!g_task_propagate_boolean (G_TASK(res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));

    if (sim_slots)
        *sim_slots = g_steal_pointer (&ctx->sim_slots);
    if (primary_sim_slot)
        *primary_sim_slot = ctx->active_slot_index;
    return TRUE;
}

static void
query_slot_information_status (MbimDevice *device,
                               guint       slot_index,
                               GTask      *task);

static void
query_slot_information_status_ready (MbimDevice   *device,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    MMBroadbandModemMbim  *self;
    g_autoptr(MbimMessage) response = NULL;
    GError                *error = NULL;
    guint32                slot_index;
    MbimUiccSlotState      slot_state;
    LoadSimSlotsContext   *ctx;
    MMBaseSim             *sim;
    gboolean               sim_active = FALSE;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_slot_info_status_response_parse (
                response,
                &slot_index,
                &slot_state,
                &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* the slot index in MM starts at 1 */
    if ((slot_index + 1) == ctx->active_slot_index)
        sim_active = TRUE;

    sim = create_sim_from_slot_state (self, sim_active, slot_index, slot_state);
    g_ptr_array_add (ctx->sim_slots, sim);

    ctx->query_slot_index++;
    if (ctx->query_slot_index < ctx->number_slots) {
        query_slot_information_status (device, ctx->query_slot_index, task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
query_slot_information_status (MbimDevice *device,
                               guint       slot_index,
                               GTask      *task)
{
    g_autoptr(MbimMessage) message = NULL;

    message = mbim_message_ms_basic_connect_extensions_slot_info_status_query_new (slot_index, NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)query_slot_information_status_ready,
                         task);
}

static void
query_device_slot_mappings_ready (MbimDevice   *device,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMBroadbandModemMbim    *self;
    g_autoptr(MbimMessage)   response = NULL;
    GError                  *error = NULL;
    guint32                  map_count = 0;
    g_autoptr(MbimSlotArray) slot_mappings = NULL;
    LoadSimSlotsContext     *ctx;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_device_slot_mappings_response_parse (
            response,
            &map_count,
            &slot_mappings,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (self->priv->executor_index >= map_count) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "The executor index doesn't have an entry in the map count");
        g_object_unref (task);
        return;
    }

    /* the slot index in MM starts at 1 */
    ctx->active_slot_index = slot_mappings[self->priv->executor_index]->slot + 1;
    self->priv->active_slot_index = ctx->active_slot_index;

    query_slot_information_status (device, ctx->query_slot_index, task);
}

static void
query_device_slot_mappings (MbimDevice *device,
                            GTask      *task)
{
    g_autoptr(MbimMessage) message = NULL;

    message = mbim_message_ms_basic_connect_extensions_device_slot_mappings_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)query_device_slot_mappings_ready,
                         task);
}

static void
query_device_caps_ready (MbimDevice   *device,
                         GAsyncResult *res,
                         GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    guint32                 executor_index;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_device_caps_response_parse (
            response,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            &executor_index,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self->priv->executor_index = executor_index;

    query_device_slot_mappings (device, task);
}

static void
query_sys_caps_ready (MbimDevice   *device,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBroadbandModemMbim  *self;
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(MbimMessage) message = NULL;
    GError                *error = NULL;
    guint32                number_executors;
    guint32                number_slots;
    guint32                concurrency;
    guint64                modem_id;
    LoadSimSlotsContext   *ctx;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_sys_caps_response_parse (
            response,
            &number_executors,
            &number_slots,
            &concurrency,
            &modem_id,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (number_slots == 1) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "Only one SIM slot is supported");
        g_object_unref (task);
        return;
    }
    ctx->number_slots = number_slots;
    ctx->sim_slots = g_ptr_array_new_full (number_slots, (GDestroyNotify) sim_slot_free);

    if (number_executors == 0) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "There is no executor");
        g_object_unref (task);
        return;
    }
    /* Given that there is one single executor supported,we assume the executor index to be always 0 */
    if (number_executors == 1) {
        self->priv->executor_index = 0;
        query_device_slot_mappings (device, task);
        return;
    }
    /* Given that more than one executors supported,we first query the current device caps to know which is the current executor index */
    message = mbim_message_ms_basic_connect_extensions_device_caps_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)query_device_caps_ready,
                         task);
}

static void
load_sim_slots_mbim (GTask *task)
{
    MMBroadbandModemMbim   *self;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;

    self = g_task_get_source_object (task);

    if (!peek_device_in_task (self, &device, task))
        return;

    message = mbim_message_ms_basic_connect_extensions_sys_caps_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)query_sys_caps_ready,
                         task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
static void
shared_qmi_load_sim_slots_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    g_autoptr(GPtrArray)   sim_slots = NULL;
    guint                  primary_sim_slot = 0;
    LoadSimSlotsContext   *ctx;

    if (!mm_shared_qmi_load_sim_slots_finish (self, res, &sim_slots, &primary_sim_slot, NULL)) {
        load_sim_slots_mbim (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    ctx->sim_slots = g_steal_pointer (&sim_slots);
    ctx->active_slot_index = primary_sim_slot;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}
#endif

static void
load_sim_slots (MMIfaceModem       *self,
                GAsyncReadyCallback callback,
                gpointer            user_data)
{
    GTask               *task;
    LoadSimSlotsContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (LoadSimSlotsContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_sim_slots_context_free);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    mm_shared_qmi_load_sim_slots (self, (GAsyncReadyCallback)shared_qmi_load_sim_slots_ready, task);
#else
    load_sim_slots_mbim (task);
#endif
}

/*****************************************************************************/
/* Set Primary SIM slot (modem interface) */

static gboolean
set_primary_sim_slot_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError      **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_device_slot_mappings_ready (MbimDevice   *device,
                                GAsyncResult *res,
                                GTask        *task)
{
    MMBroadbandModemMbim    *self;
    g_autoptr(MbimMessage)   response = NULL;
    GError                  *error = NULL;
    guint32                  map_count = 0;
    g_autoptr(MbimSlotArray) slot_mappings = NULL;
    guint                    i;
    guint                    slot_number;

    self = g_task_get_source_object (task);

    g_assert (self->priv->pending_sim_slot_switch_action);

    /* the slot index in MM starts at 1 */
    slot_number = GPOINTER_TO_UINT (g_task_get_task_data (task)) - 1;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_device_slot_mappings_response_parse (
            response,
            &map_count,
            &slot_mappings,
            &error)) {
        self->priv->pending_sim_slot_switch_action = FALSE;
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < map_count; i++) {
        if (i == self->priv->executor_index) {
            if (slot_number != slot_mappings[i]->slot) {
                self->priv->pending_sim_slot_switch_action = FALSE;
                g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                         "SIM slot switch to '%u' failed", slot_number);
            } else {
                /* Keep pending_sim_slot_switch_action flag TRUE to cleanly ignore SIM related indications
                 * during slot swithing, We don't want SIM related indications received trigger the update
                 * of SimSlots property, which may not be what we want as the modem object is being shutdown */
                self->priv->active_slot_index = slot_number + 1;
                g_task_return_boolean (task, TRUE);
            }

            g_object_unref (task);
            return;
        }
    }

    self->priv->pending_sim_slot_switch_action = FALSE;
    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                             "Can't find executor index '%u'", self->priv->executor_index);
    g_object_unref (task);
}

static void
before_set_query_device_slot_mappings_ready (MbimDevice   *device,
                                             GAsyncResult *res,
                                             GTask        *task)
{
    MMBroadbandModemMbim    *self;
    g_autoptr(MbimMessage)   response = NULL;
    GError                  *error = NULL;
    g_autoptr(MbimMessage)   message = NULL;
    guint32                  map_count = 0;
    g_autoptr(MbimSlotArray) slot_mappings = NULL;
    guint                    i;
    guint                    slot_number;

    self = g_task_get_source_object (task);

    /* the slot index in MM starts at 1 */
    slot_number = GPOINTER_TO_UINT (g_task_get_task_data (task)) - 1;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_basic_connect_extensions_device_slot_mappings_response_parse (
            response,
            &map_count,
            &slot_mappings,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < map_count; i++) {
        if (slot_number == slot_mappings[i]->slot) {
            if (i != self->priv->executor_index) {
                g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                         "The sim slot '%u' is already used by executor '%u'", slot_number, i);
            } else {
                mm_obj_dbg (self, "The slot is already the requested one");
                g_task_return_boolean (task, TRUE);
            }

            g_object_unref (task);
            return;
        }
    }

    /* Flag a pending SIM slot switch operation, so that we can ignore slot state updates
     * during the process. */
    if (self->priv->pending_sim_slot_switch_action) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS,
                                 "there is already an ongoing SIM slot switch operation");
        g_object_unref (task);
        return;
    }
    self->priv->pending_sim_slot_switch_action = TRUE;

    for (i = 0; i < map_count; i++) {
        if (i == self->priv->executor_index)
            slot_mappings[i]->slot = slot_number;
    }

    message = mbim_message_ms_basic_connect_extensions_device_slot_mappings_set_new (map_count,
                                                                                     (const MbimSlot **)slot_mappings,
                                                                                     NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)set_device_slot_mappings_ready,
                         task);
}

static void
set_primary_sim_slot_mbim (GTask *task)
{
    MMBroadbandModemMbim   *self;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;

    self = g_task_get_source_object (task);

    if (!peek_device_in_task (self, &device, task))
        return;

    message = mbim_message_ms_basic_connect_extensions_device_slot_mappings_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)before_set_query_device_slot_mappings_ready,
                         task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
static void
shared_qmi_set_primary_sim_slot_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    g_autoptr(GError) error = NULL;

    if (!mm_shared_qmi_set_primary_sim_slot_finish (self, res, &error)) {
        /* Fallback to MBIM */
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
            set_primary_sim_slot_mbim (task);
            return;
        }
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}
#endif

static void
set_primary_sim_slot (MMIfaceModem       *self,
                      guint               sim_slot,
                      GAsyncReadyCallback callback,
                      gpointer            user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    g_task_set_task_data (task, GUINT_TO_POINTER (sim_slot), NULL);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    mm_shared_qmi_set_primary_sim_slot (self, sim_slot, (GAsyncReadyCallback)shared_qmi_set_primary_sim_slot_ready, task);
#else
    set_primary_sim_slot_mbim (task);
#endif
}

/*****************************************************************************/
/* Set packet service state (3GPP interface)  */

static gboolean
set_packet_service_state_finish (MMIfaceModem3gpp  *self,
                                 GAsyncResult      *res,
                                 GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
packet_service_set_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemMbim   *self;
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(GError)       error = NULL;
    MbimPacketServiceState  requested_packet_service_state;
    MbimPacketServiceState  packet_service_state = MBIM_PACKET_SERVICE_STATE_UNKNOWN;

    self = g_task_get_source_object (task);

    /* The NwError field is valid if MBIM_SET_PACKET_SERVICE response status code
     * equals MBIM_STATUS_FAILURE, so we parse the message both on success and on that
     * specific failure */
    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
         g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE))) {
        g_autoptr(GError) inner_error = NULL;
        guint32           nw_error = 0;

        if (!common_process_packet_service (self,
                                            device,
                                            response,
                                            &nw_error,
                                            &packet_service_state,
                                            &inner_error)) {
            mm_obj_warn (self, "%s", inner_error->message);
            /* Prefer the error from the result to the parsing error */
            if (!error)
                error = g_steal_pointer (&inner_error);
        } else {
            /* Prefer the NW error if available */
            if (nw_error) {
                g_clear_error (&error);
                error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error, self);
            }
        }
    }

    if (error) {
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NO_DEVICE_SUPPORT))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "%s", error->message);
        else
            g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    requested_packet_service_state = GPOINTER_TO_UINT (g_task_get_task_data (task));

    if (((requested_packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHED) &&
         (packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHED || packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHING)) ||
        ((requested_packet_service_state == MBIM_PACKET_SERVICE_STATE_DETACHED) &&
         (packet_service_state == MBIM_PACKET_SERVICE_STATE_DETACHED || packet_service_state == MBIM_PACKET_SERVICE_STATE_DETACHING)))
        g_task_return_boolean (task, TRUE);
    else
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed to request state %s, current state: %s",
                                 mbim_packet_service_state_get_string (requested_packet_service_state),
                                 mbim_packet_service_state_get_string (packet_service_state));
    g_object_unref (task);
}

static void
set_packet_service_state (MMIfaceModem3gpp              *self,
                          MMModem3gppPacketServiceState  packet_service_state,
                          GAsyncReadyCallback            callback,
                          gpointer                       user_data)
{
    g_autoptr(MbimMessage)   message = NULL;
    MbimDevice              *device;
    GTask                   *task;
    MbimPacketServiceAction  packet_service_action;
    MbimPacketServiceState   requested_packet_service_state;

    g_assert ((packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED) ||
              (packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED));

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    switch (packet_service_state) {
    case MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED:
        packet_service_action = MBIM_PACKET_SERVICE_ACTION_ATTACH;
        requested_packet_service_state = MBIM_PACKET_SERVICE_STATE_ATTACHED;
        break;
    case MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED:
        packet_service_action = MBIM_PACKET_SERVICE_ACTION_DETACH;
        requested_packet_service_state = MBIM_PACKET_SERVICE_STATE_DETACHED;
        break;
    case MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN:
    default:
        g_assert_not_reached ();
    }

    g_task_set_task_data (task, GUINT_TO_POINTER (requested_packet_service_state), NULL);

    message = mbim_message_packet_service_set_new (packet_service_action, NULL);
    mbim_device_command (device,
                         message,
                         30,
                         NULL,
                         (GAsyncReadyCallback)packet_service_set_ready,
                         task);
}

/*****************************************************************************/
/* Set carrier lock */

static gboolean
modem_set_carrier_lock_finish (MMIfaceModem3gpp  *self,
                               GAsyncResult      *res,
                               GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_carrier_lock_ready (MbimDevice   *device,
                        GAsyncResult *res,
                        GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_task_return_boolean (task, TRUE);
    } else if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED)) {
        g_clear_error (&error);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "operation not allowed");
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
modem_set_carrier_lock (MMIfaceModem3gpp    *_self,
                        const guint8        *data,
                        gsize                data_size,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    MMBroadbandModemMbim   *self = MM_BROADBAND_MODEM_MBIM (_self);
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;
    GTask                  *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->is_google_carrier_lock_supported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Google carrier lock is not supported");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "Sending carrier lock request...");
    message = mbim_message_google_carrier_lock_set_new (data_size, data, NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)set_carrier_lock_ready,
                         task);
}

/*****************************************************************************/

MMBroadbandModemMbim *
mm_broadband_modem_mbim_new (const gchar *device,
                             const gchar *physdev,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_mbim_init (MMBroadbandModemMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_MBIM,
                                              MMBroadbandModemMbimPrivate);
    self->priv->enabled_cache.available_data_classes = MBIM_DATA_CLASS_NONE;
    self->priv->enabled_cache.highest_available_data_class = MBIM_DATA_CLASS_NONE;
    self->priv->enabled_cache.reg_state = MBIM_REGISTER_STATE_UNKNOWN;
    self->priv->enabled_cache.packet_service_state = MBIM_PACKET_SERVICE_STATE_UNKNOWN;
    self->priv->enabled_cache.last_ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;
}

static void
dispose (GObject *object)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);
    MMPortMbim *mbim;

    if (self->priv->enabling_signal_id && g_signal_handler_is_connected (self, self->priv->enabling_signal_id)) {
        g_signal_handler_disconnect (self, self->priv->enabling_signal_id);
        self->priv->enabling_signal_id = 0;
    }

    /* If any port cleanup is needed, it must be done during dispose(), as
     * the modem object will be affected by an explciit g_object_run_dispose()
     * that will remove all port references right away */
    mbim = mm_broadband_modem_mbim_peek_port_mbim (self);
    if (mbim) {
        /* Explicitly remove notification handler */
        self->priv->setup_flags = PROCESS_NOTIFICATION_FLAG_NONE;
        common_setup_cleanup_unsolicited_events_sync (self, mbim, FALSE);

        /* If we did open the MBIM port during initialization, close it now */
        if (mm_port_mbim_is_open (mbim))
            mm_port_mbim_close (mbim, NULL, NULL);
    }

    g_clear_object (&self->priv->unlock_retries);

    G_OBJECT_CLASS (mm_broadband_modem_mbim_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);

    g_free (self->priv->caps_custom_data_class);
    g_free (self->priv->caps_device_id);
    g_free (self->priv->caps_firmware_info);
    g_free (self->priv->caps_hardware_info);
    g_free (self->priv->enabled_cache.current_operator_id);
    g_free (self->priv->enabled_cache.current_operator_name);
    g_free (self->priv->requested_operator_id);
    g_list_free_full (self->priv->enabled_cache.pco_list, g_object_unref);

    G_OBJECT_CLASS (mm_broadband_modem_mbim_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);
    /* Initialization steps */
    iface->load_supported_capabilities = modem_load_supported_capabilities;
    iface->load_supported_capabilities_finish = modem_load_supported_capabilities_finish;
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->set_current_capabilities = modem_set_current_capabilities;
    iface->set_current_capabilities_finish = modem_set_current_capabilities_finish;
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
    iface->load_supported_modes = modem_load_supported_modes;
    iface->load_supported_modes_finish = modem_load_supported_modes_finish;
    iface->load_current_modes = modem_load_current_modes;
    iface->load_current_modes_finish = modem_load_current_modes_finish;
    iface->set_current_modes = modem_set_current_modes;
    iface->set_current_modes_finish = modem_set_current_modes_finish;
    iface->load_unlock_required = modem_load_unlock_required;
    iface->load_unlock_required_finish = modem_load_unlock_required_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->load_power_state = modem_load_power_state;
    iface->load_power_state_finish = modem_load_power_state_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = power_down_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->load_supported_ip_families = modem_load_supported_ip_families;
    iface->load_supported_ip_families_finish = modem_load_supported_ip_families_finish;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    iface->load_carrier_config = mm_shared_qmi_load_carrier_config;
    iface->load_carrier_config_finish = mm_shared_qmi_load_carrier_config_finish;
    iface->setup_carrier_config = mm_shared_qmi_setup_carrier_config;
    iface->setup_carrier_config_finish = mm_shared_qmi_setup_carrier_config_finish;
    iface->load_supported_bands = mm_shared_qmi_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_qmi_load_supported_bands_finish;
    iface->load_current_bands = mm_shared_qmi_load_current_bands;
    iface->load_current_bands_finish = mm_shared_qmi_load_current_bands_finish;
    iface->set_current_bands = mm_shared_qmi_set_current_bands;
    iface->set_current_bands_finish = mm_shared_qmi_set_current_bands_finish;
#endif

    /* Additional actions */
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
    iface->get_cell_info = modem_get_cell_info;
    iface->get_cell_info_finish = modem_get_cell_info_finish;

    /* Unneeded things */
    iface->modem_after_power_up = NULL;
    iface->modem_after_power_up_finish = NULL;
    iface->load_supported_charsets = NULL;
    iface->load_supported_charsets_finish = NULL;
    iface->setup_flow_control = NULL;
    iface->setup_flow_control_finish = NULL;
    iface->setup_charset = NULL;
    iface->setup_charset_finish = NULL;
    iface->load_access_technologies = NULL;
    iface->load_access_technologies_finish = NULL;

    /* Create MBIM-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;
    iface->load_sim_slots = load_sim_slots;
    iface->load_sim_slots_finish = load_sim_slots_finish;
    iface->set_primary_sim_slot = set_primary_sim_slot;
    iface->set_primary_sim_slot_finish = set_primary_sim_slot_finish;

    /* Create MBIM-specific bearer and bearer list */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->create_bearer_list = modem_create_bearer_list;

    /* SIM hot swapping */
    iface->setup_sim_hot_swap = modem_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = modem_setup_sim_hot_swap_finish;

    /* Other actions */
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    iface->factory_reset = mm_shared_qmi_factory_reset;
    iface->factory_reset_finish = mm_shared_qmi_factory_reset_finish;
#endif
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;
    iface->load_enabled_facility_locks = modem_3gpp_load_enabled_facility_locks;
    iface->load_enabled_facility_locks_finish = modem_3gpp_load_enabled_facility_locks_finish;

    iface->setup_unsolicited_events = setup_unsolicited_events_3gpp;
    iface->setup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->cleanup_unsolicited_events = cleanup_unsolicited_events_3gpp;
    iface->cleanup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_common_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_common_enable_disable_unsolicited_events_finish;
    iface->setup_unsolicited_registration_events = setup_unsolicited_registration_events;
    iface->setup_unsolicited_registration_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->cleanup_unsolicited_registration_events = cleanup_unsolicited_registration_events;
    iface->cleanup_unsolicited_registration_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->enable_unsolicited_registration_events = modem_3gpp_enable_unsolicited_registration_events;
    iface->enable_unsolicited_registration_events_finish = modem_3gpp_common_enable_disable_unsolicited_registration_events_finish;
    iface->disable_unsolicited_registration_events = modem_3gpp_disable_unsolicited_registration_events;
    iface->disable_unsolicited_registration_events_finish = modem_3gpp_common_enable_disable_unsolicited_registration_events_finish;
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
    iface->load_initial_eps_bearer = modem_3gpp_load_initial_eps_bearer;
    iface->load_initial_eps_bearer_finish = modem_3gpp_load_initial_eps_bearer_finish;
    iface->load_initial_eps_bearer_settings = modem_3gpp_load_initial_eps_bearer_settings;
    iface->load_initial_eps_bearer_settings_finish = modem_3gpp_load_initial_eps_bearer_settings_finish;
    iface->load_nr5g_registration_settings = modem_3gpp_load_nr5g_registration_settings;
    iface->load_nr5g_registration_settings_finish = modem_3gpp_load_nr5g_registration_settings_finish;
    iface->set_initial_eps_bearer_settings = modem_3gpp_set_initial_eps_bearer_settings;
    iface->set_initial_eps_bearer_settings_finish = modem_3gpp_set_initial_eps_bearer_settings_finish;
    iface->set_nr5g_registration_settings = modem_3gpp_set_nr5g_registration_settings;
    iface->set_nr5g_registration_settings_finish = modem_3gpp_set_nr5g_registration_settings_finish;
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;
    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
    iface->disable_facility_lock = modem_3gpp_disable_facility_lock;
    iface->disable_facility_lock_finish = modem_3gpp_disable_facility_lock_finish;
    iface->set_packet_service_state = set_packet_service_state;
    iface->set_packet_service_state_finish = set_packet_service_state_finish;
    /* carrier lock */
    iface->set_carrier_lock = modem_set_carrier_lock;
    iface->set_carrier_lock_finish = modem_set_carrier_lock_finish;
}

static void
iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface)
{
    /* Initialization steps */
    iface->check_support = modem_3gpp_profile_manager_check_support;
    iface->check_support_finish = modem_3gpp_profile_manager_check_support_finish;

    /* Enabling steps */
    iface->setup_unsolicited_events = setup_unsolicited_events_3gpp_profile_manager;
    iface->setup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_profile_manager_finish;
    iface->enable_unsolicited_events = enable_unsolicited_events_3gpp_profile_manager;
    iface->enable_unsolicited_events_finish = common_enable_disable_unsolicited_events_3gpp_profile_manager_finish;

    /* Disabling steps */
    iface->cleanup_unsolicited_events = cleanup_unsolicited_events_3gpp_profile_manager;
    iface->cleanup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_profile_manager_finish;
    iface->disable_unsolicited_events = disable_unsolicited_events_3gpp_profile_manager;
    iface->disable_unsolicited_events_finish = common_enable_disable_unsolicited_events_3gpp_profile_manager_finish;

    /* Additional actions */
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
    /* Initialization steps */
    iface->check_support = modem_3gpp_ussd_check_support;
    iface->check_support_finish = modem_3gpp_ussd_check_support_finish;

    /* Enabling steps */
    iface->setup_unsolicited_events = modem_3gpp_ussd_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_ussd_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_ussd_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_ussd_enable_disable_unsolicited_events_finish;

    /* Disabling steps */
    iface->cleanup_unsolicited_events_finish = modem_3gpp_ussd_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_ussd_cleanup_unsolicited_events;
    iface->disable_unsolicited_events = modem_3gpp_ussd_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_ussd_enable_disable_unsolicited_events_finish;

    /* Additional actions */
    iface->send = modem_3gpp_ussd_send;
    iface->send_finish = modem_3gpp_ussd_send_finish;
    iface->cancel = modem_3gpp_ussd_cancel;
    iface->cancel_finish = modem_3gpp_ussd_cancel_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities = mm_shared_qmi_location_load_capabilities;
    iface->load_capabilities_finish = mm_shared_qmi_location_load_capabilities_finish;
    iface->enable_location_gathering = mm_shared_qmi_enable_location_gathering;
    iface->enable_location_gathering_finish = mm_shared_qmi_enable_location_gathering_finish;
    iface->disable_location_gathering = mm_shared_qmi_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_qmi_disable_location_gathering_finish;
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
#else
    iface->load_capabilities = NULL;
    iface->load_capabilities_finish = NULL;
    iface->enable_location_gathering = NULL;
    iface->enable_location_gathering_finish = NULL;
#endif
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
    iface->set_default_storage = NULL;
    iface->set_default_storage_finish = NULL;
    iface->init_current_storages = NULL;
    iface->init_current_storages_finish = NULL;
    iface->load_initial_sms_parts = load_initial_sms_parts;
    iface->load_initial_sms_parts_finish = load_initial_sms_parts_finish;
    iface->setup_unsolicited_events = setup_unsolicited_events_messaging;
    iface->setup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_messaging_finish;
    iface->cleanup_unsolicited_events = cleanup_unsolicited_events_messaging;
    iface->cleanup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_messaging_finish;
    iface->enable_unsolicited_events = enable_unsolicited_events_messaging;
    iface->enable_unsolicited_events_finish = common_enable_disable_unsolicited_events_messaging_finish;
    iface->disable_unsolicited_events = disable_unsolicited_events_messaging;
    iface->disable_unsolicited_events_finish = common_enable_disable_unsolicited_events_messaging_finish;
    iface->create_sms = messaging_create_sms;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface_modem_signal_parent = g_type_interface_peek_parent (iface);

    iface->check_support = modem_signal_check_support;
    iface->check_support_finish = modem_signal_check_support_finish;
    iface->load_values = modem_signal_load_values;
    iface->load_values_finish = modem_signal_load_values_finish;
    iface->setup_thresholds = modem_signal_setup_thresholds;
    iface->setup_thresholds_finish = modem_signal_setup_thresholds_finish;
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

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

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

#endif

static void
mm_broadband_modem_mbim_class_init (MMBroadbandModemMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbimPrivate));

    klass->peek_port_mbim_for_data = peek_port_mbim_for_data;

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    object_class->finalize = finalize;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
    broadband_modem_class->enabling_started = enabling_started;
    broadband_modem_class->enabling_started_finish = enabling_started_finish;
    /* Do not initialize the MBIM modem through AT commands */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    g_object_class_install_property (object_class, PROP_QMI_UNSUPPORTED,
        g_param_spec_boolean (MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED,
                              "QMI over MBIM unsupported",
                              "TRUE when QMI over MBIM should not be considered.",
                              FALSE,
                              G_PARAM_READWRITE));
#endif

    g_object_class_install_property (object_class, PROP_INTEL_FIRMWARE_UPDATE_UNSUPPORTED,
        g_param_spec_boolean (MM_BROADBAND_MODEM_MBIM_INTEL_FIRMWARE_UPDATE_UNSUPPORTED,
                              "Intel Firmware Update service unsupported",
                              "TRUE when the Intel Firmware Update service should not be considered.",
                              FALSE,
                              G_PARAM_READWRITE));
}
