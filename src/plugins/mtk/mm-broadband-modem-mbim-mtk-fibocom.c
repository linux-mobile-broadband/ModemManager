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
 * Copyright (C) 2023 Google, Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-signal.h"
#include "mm-bearer-mbim-mtk-fibocom.h"
#include "mm-broadband-modem-mbim-mtk-fibocom.h"
#include "mm-shared-fibocom.h"
#include "mm-shared-mtk.h"

static void iface_modem_init        (MMIfaceModemInterface        *iface);
static void iface_modem_signal_init (MMIfaceModemSignalInterface  *iface);
static void shared_fibocom_init     (MMSharedFibocomInterface     *iface);
static void shared_mtk_init         (MMSharedMtkInterface         *iface);

static MMIfaceModemInterface        *iface_modem_parent;
static MMIfaceModemSignalInterface  *iface_modem_signal_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimMtkFibocom, mm_broadband_modem_mbim_mtk_fibocom, MM_TYPE_BROADBAND_MODEM_MBIM_MTK, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM,        iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_FIBOCOM,     shared_fibocom_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_MTK,         shared_mtk_init))

struct _MMBroadbandModemMbimMtkFibocomPrivate {
    /* Custom MTK/Fibocom bearer behavior */
    gboolean is_multiplex_supported;
    gboolean is_async_slaac_supported;
    gboolean remove_ip_packet_filters;
    gboolean normalize_nw_error;
};

/*****************************************************************************/

/* Asynchronous indications of IP configuration updates during the initial
 * modem-SLAAC operation with the network are not supported in old firmware
 * versions. */
#define ASYNC_SLAAC_SUPPORTED_VERSION 29, 23, 6

/* Multiple Multiplexed PDNs are not correctly supported in old firmware
 * versions. */
#define MULTIPLEX_SUPPORTED_VERSION 29, 23, 6

/* Explicit IP packet filter removal required in old firmware versions. */
#define IP_PACKET_FILTER_REMOVAL_UNNEEDED_VERSION 29, 23, 6

/* NW error normalization required in old firmware versions. */
#define NORMALIZE_NW_ERROR_UNNEEDED 29, 22, 13

/* Default rssi threshold in the module. */
#define DEFAULT_RSSI_THRESHOLD 6

static inline gboolean
fm350_check_version (guint A1, guint A2, guint A3,
                     guint B1, guint B2, guint B3)
{
    return ((A1 > B1) || ((A1 == B1) && ((A2 > B2) || ((A2 == B2) && (A3 >= B3)))));
}

static void
process_fm350_version_features (MMBroadbandModemMbimMtkFibocom *self,
                                const gchar                    *revision)
{
    g_auto(GStrv) split = NULL;
    guint         major;
    guint         minor;
    guint         micro;

    /* Expected revision string is a multi-line value like this:
     *   81600.0000.00.MM.mm.uu_GC
     *   F09
     * For version comparison we care only about the "MM.mm.uu" part.
     */
    split = g_strsplit_set (revision, "._", -1);
    if (!split || g_strv_length (split) < 6) {
        mm_obj_warn (self, "failed to process modem firmware version string");
        return;
    }

    if (!mm_get_uint_from_str (split[3], &major) ||
        !mm_get_uint_from_str (split[4], &minor) ||
        !mm_get_uint_from_str (split[5], &micro)) {
        mm_obj_warn (self, "failed to process modem firmware version string: %s.%s.%s",
                     split[3], split[4], split[5]);
        return;
    }

    /* Check if async SLAAC is supported */
    self->priv->is_async_slaac_supported = fm350_check_version (major, minor, micro, ASYNC_SLAAC_SUPPORTED_VERSION);
    mm_obj_info (self, "modem async SLAAC result indications are %ssupported",
                 self->priv->is_async_slaac_supported ? "" : "not ");

    /* Check if multiplex is supported */
    self->priv->is_multiplex_supported = fm350_check_version (major, minor, micro, MULTIPLEX_SUPPORTED_VERSION);
    mm_obj_info (self, "modem multiplexing is %ssupported",
                 self->priv->is_multiplex_supported ? "" : "not ");

    /* Check if we need to remove IP packet filters */
    self->priv->remove_ip_packet_filters = !fm350_check_version (major, minor, micro, IP_PACKET_FILTER_REMOVAL_UNNEEDED_VERSION);
    mm_obj_info (self, "modem %s IP packet filter removal",
                 self->priv->remove_ip_packet_filters ? "requires" : "does not require");

    /* Check if we need to normalize network errors */
    self->priv->normalize_nw_error = !fm350_check_version (major, minor, micro, NORMALIZE_NW_ERROR_UNNEEDED);
    mm_obj_info (self, "modem %s network error normalization",
                 self->priv->normalize_nw_error ? "requires" : "does not require");
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
load_revision_finish (MMIfaceModem  *self,
                      GAsyncResult  *res,
                      GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_revision_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    GError *error = NULL;
    gchar  *revision;

    revision = iface_modem_parent->load_revision_finish (self, res, &error);
    if (!revision) {
        g_task_return_error (task, error);
    } else {
        process_fm350_version_features (MM_BROADBAND_MODEM_MBIM_MTK_FIBOCOM (self), revision);
        g_task_return_pointer (task, revision, g_free);
    }
    g_object_unref (task);
}

static void
load_revision (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    g_assert (iface_modem_parent->load_revision);
    g_assert (iface_modem_parent->load_revision_finish);
    iface_modem_parent->load_revision (self,
                                       (GAsyncReadyCallback)parent_load_revision_ready,
                                       g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
create_bearer_finish (MMIfaceModem  *self,
                      GAsyncResult  *res,
                      GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
create_bearer (MMIfaceModem        *_self,
               MMBearerProperties  *properties,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    MMBroadbandModemMbimMtkFibocom *self = MM_BROADBAND_MODEM_MBIM_MTK_FIBOCOM (_self);
    MMBaseBearer                   *bearer;
    GTask                          *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_obj_dbg (self, "creating MTK-based modem MBIM bearer (async SLAAC %s)",
                self->priv->is_async_slaac_supported ? "supported" : "unsupported");
    bearer = mm_bearer_mbim_mtk_fibocom_new (MM_BROADBAND_MODEM_MBIM (self),
                                             self->priv->is_async_slaac_supported,
                                             self->priv->remove_ip_packet_filters,
                                             properties);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Create Bearer List (Modem interface) */

static MMBearerList *
create_bearer_list (MMIfaceModem *self)
{
    MMBearerList *bearer_list;

    bearer_list = iface_modem_parent->create_bearer_list (self);

    if (!MM_BROADBAND_MODEM_MBIM_MTK_FIBOCOM (self)->priv->is_multiplex_supported) {
        g_object_set (bearer_list,
                      MM_BEARER_LIST_MAX_ACTIVE_MULTIPLEXED_BEARERS, 0,
                      NULL);
        mm_obj_dbg (self, "modem firmware version doesn't support multiplexed bearers");
    }

    return bearer_list;
}

/******************************************************************************/
/* Normalize network error */

static guint32
normalize_nw_error (MMBroadbandModemMbim *self,
                    guint32               nw_error)
{
    /* Work around to convert AT error to 3GPP Error */
    if (MM_BROADBAND_MODEM_MBIM_MTK_FIBOCOM (self)->priv->normalize_nw_error && nw_error > 100) {
        mm_obj_dbg (self, "network error normalization required: %u -> %u",
                    nw_error, nw_error - 100);
        nw_error -= 100;
    }
    return nw_error;
}

/*****************************************************************************/
/* Load extended signal information (Signal interface) */

static gboolean
signal_setup_thresholds_finish (MMIfaceModemSignal *self,
                                GAsyncResult       *res,
                                GError             **error)
{
    return iface_modem_signal_parent->setup_thresholds_finish (self, res, error);
}

static void
setup_default_thresholds_ready (MMIfaceModemSignal *self,
                                GAsyncResult       *res,
                                GTask              *task)
{
    GError *error  = NULL;

    if (!iface_modem_signal_parent->setup_thresholds_finish (self, res, &error)) {
        mm_obj_dbg (self, "setup rssi thresholds failed: %s", error->message);
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

static void
signal_state_query_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBroadbandModemMbimMtkFibocom   *self;
    GError                           *error   = NULL;
    g_autoptr(MbimMessage)           response = NULL;
    guint32                          rssi_threshold;
    guint32                          error_rate_threshold;

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
            NULL,            /* rssi */
            NULL,            /* error_rate */
            NULL,            /* signal_strength_interval */
            &rssi_threshold,
            &error_rate_threshold,
            NULL,            /* rsrp_snr_count */
            NULL,            /* rsrp_snr */
            &error)) {
        g_prefix_error (&error, "Failed processing MBIMEx v2.0 signal state response: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If the device doesn't have a default rssi threshold, to set one. */
    if (rssi_threshold == 0) {
        iface_modem_signal_parent->setup_thresholds (
            MM_IFACE_MODEM_SIGNAL (self),
            DEFAULT_RSSI_THRESHOLD,
            error_rate_threshold,
            (GAsyncReadyCallback)setup_default_thresholds_ready,
            task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_setup_thresholds_ready (MMIfaceModemSignal *self,
                               GAsyncResult       *res,
                               GTask              *task)
{
    MMPortMbim                *port;
    MbimDevice                *device;
    g_autoptr(MbimMessage)    message = NULL;

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't peek MBIM port");
        g_object_unref (task);
        return;
    }

    device = mm_port_mbim_peek_device (port);

    message = mbim_message_signal_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         5,
                         NULL,
                         (GAsyncReadyCallback)signal_state_query_ready,
                         task);
}

static void
signal_setup_thresholds (MMIfaceModemSignal  *self,
                         guint               rssi_threshold,
                         gboolean            error_rate_threshold,
                         GAsyncReadyCallback callback,
                         gpointer            user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* First call the parent setup_thresholds method to set the rssi threshold (0) for the module,
     * making the module to set this value to its internal default according to the specification.
     * Then use the signal state MBIM request to check the default rssi threshold for module responses.
     * If there is no valid rssi threshold, to set a default value. */
    iface_modem_signal_parent->setup_thresholds (
        self,
        rssi_threshold,
        error_rate_threshold,
        (GAsyncReadyCallback)parent_setup_thresholds_ready,
        task);
}

/******************************************************************************/

MMBroadbandModemMbimMtkFibocom *
mm_broadband_modem_mbim_mtk_fibocom_new (const gchar  *device,
                                         const gchar  *physdev,
                                         const gchar **drivers,
                                         const gchar  *plugin,
                                         guint16       vendor_id,
                                         guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_MTK_FIBOCOM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                         MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, TRUE,
#endif
                         NULL);
}

static void
mm_broadband_modem_mbim_mtk_fibocom_init (MMBroadbandModemMbimMtkFibocom *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_MBIM_MTK_FIBOCOM,
                                              MMBroadbandModemMbimMtkFibocomPrivate);

    /* By default remove, unless we have a new enough version */
    self->priv->remove_ip_packet_filters = TRUE;
    /* By default normalize, unless have a new enough version */
    self->priv->normalize_nw_error = TRUE;
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_revision = load_revision;
    iface->load_revision_finish = load_revision_finish;
    iface->create_bearer = create_bearer;
    iface->create_bearer_finish = create_bearer_finish;
    iface->create_bearer_list = create_bearer_list;
    iface->load_unlock_retries = mm_shared_mtk_load_unlock_retries;
    iface->load_unlock_retries_finish = mm_shared_mtk_load_unlock_retries_finish;
}

static void
iface_modem_signal_init (MMIfaceModemSignalInterface *iface)
{
    iface_modem_signal_parent   = g_type_interface_peek_parent (iface);

    iface->setup_thresholds        = signal_setup_thresholds;
    iface->setup_thresholds_finish = signal_setup_thresholds_finish;
}

static MMBaseModemClass *
peek_parent_class (MMSharedFibocom *self)
{
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_mbim_mtk_fibocom_parent_class);
}

/* Note: shared fibocom is only used to create usbmisc/wwan ports, so there is no need
 * to initialize the parent modem/firmware interface objects. */
static void
shared_fibocom_init (MMSharedFibocomInterface *iface)
{
    iface->peek_parent_class = peek_parent_class;
}

static void
shared_mtk_init (MMSharedMtkInterface *iface)
{
}

static void
mm_broadband_modem_mbim_mtk_fibocom_class_init (MMBroadbandModemMbimMtkFibocomClass *klass)
{
    GObjectClass              *object_class = G_OBJECT_CLASS (klass);
    MMBaseModemClass          *base_modem_class = MM_BASE_MODEM_CLASS (klass);
    MMBroadbandModemMbimClass *broadband_modem_mbim_class = MM_BROADBAND_MODEM_MBIM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbimMtkFibocomPrivate));

    base_modem_class->create_usbmisc_port = mm_shared_fibocom_create_usbmisc_port;
    base_modem_class->create_wwan_port = mm_shared_fibocom_create_wwan_port;
    broadband_modem_mbim_class->normalize_nw_error = normalize_nw_error;
}
