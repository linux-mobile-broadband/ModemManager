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

#include "mm-broadband-modem-qmi-quectel.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-time.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-log-object.h"
#include "mm-modem-helpers-quectel.h"
#include "mm-shared-quectel.h"

static void iface_modem_init                      (MMIfaceModemInterface                   *iface);
static void iface_modem_firmware_init             (MMIfaceModemFirmwareInterface           *iface);
static void iface_modem_location_init             (MMIfaceModemLocationInterface           *iface);
static void iface_modem_time_init                 (MMIfaceModemTimeInterface               *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManagerInterface *iface);
static void shared_quectel_init                   (MMSharedQuectelInterface                *iface);

static MMIfaceModemInterface                   *iface_modem_parent;
static MMIfaceModemFirmwareInterface           *iface_modem_firmware_parent;
static MMIfaceModemLocationInterface           *iface_modem_location_parent;
static MMIfaceModem3gppProfileManagerInterface *iface_modem_3gpp_profile_manager_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmiQuectel, mm_broadband_modem_qmi_quectel, MM_TYPE_BROADBAND_MODEM_QMI, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

/*****************************************************************************/

static gboolean
profile_manager_check_unsolicited_support (MMBroadbandModemQmiQuectel *self)
{
    GError      *error = NULL;
    const gchar *revision = NULL;
    guint        release_version;
    guint        minor_version;

    revision = mm_iface_modem_get_revision (MM_IFACE_MODEM (self));
    if (!mm_quectel_get_version_from_revision (revision,
                                               &release_version,
                                               &minor_version,
                                               &error)) {
        mm_obj_warn (self, "parsing revision failed: %s", error->message);
        g_error_free (error);

        /* assume profile manager supported if version not parseable */
        return TRUE;
    }

    if (!mm_quectel_is_profile_manager_supported (revision,
                                                  release_version,
                                                  minor_version)) {
        mm_obj_dbg (self, "profile management not supported by revision %s", revision);
        return FALSE;
    }

    /* profile management seems supported */
    return TRUE;
}

static gboolean
profile_manager_enable_unsolicited_events_finish (MMIfaceModem3gppProfileManager  *self,
                                                  GAsyncResult                    *res,
                                                  GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gppProfileManager *self,
                                        GAsyncResult                   *res,
                                        GTask                          *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_profile_manager_parent->enable_unsolicited_events_finish (self,
                                                                                    res,
                                                                                    &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
profile_manager_enable_unsolicited_events (MMIfaceModem3gppProfileManager *self,
                                           GAsyncReadyCallback             callback,
                                           gpointer                        user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!profile_manager_check_unsolicited_support (MM_BROADBAND_MODEM_QMI_QUECTEL (self))) {
        mm_obj_warn (self, "continuing without enabling profile manager events");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    iface_modem_3gpp_profile_manager_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        task);
}

MMBroadbandModemQmiQuectel *
mm_broadband_modem_qmi_quectel_new (const gchar  *device,
                                    const gchar  *physdev,
                                    const gchar **drivers,
                                    const gchar  *plugin,
                                    guint16       vendor_id,
                                    guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* exclude carrier information */
                         MM_IFACE_MODEM_FIRMWARE_IGNORE_CARRIER, TRUE,
                         /* QMI bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_qmi_quectel_init (MMBroadbandModemQmiQuectel *self)
{
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->setup_sim_hot_swap = mm_shared_quectel_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_quectel_setup_sim_hot_swap_finish;
    iface->cleanup_sim_hot_swap = mm_shared_quectel_cleanup_sim_hot_swap;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface)
{
    iface_modem_firmware_parent = g_type_interface_peek_parent (iface);

    iface->load_update_settings = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocationInterface *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_quectel_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_quectel_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_quectel_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_quectel_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_quectel_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_quectel_disable_location_gathering_finish;
}

static void
iface_modem_time_init (MMIfaceModemTimeInterface *iface)
{
    iface->check_support        = mm_shared_quectel_time_check_support;
    iface->check_support_finish = mm_shared_quectel_time_check_support_finish;
}

static MMBaseModemClass *
peek_parent_class (MMSharedQuectel *self)
{
    return MM_BASE_MODEM_CLASS (mm_broadband_modem_qmi_quectel_parent_class);
}

static MMIfaceModemInterface *
peek_parent_modem_interface (MMSharedQuectel *self)
{
    return iface_modem_parent;
}

static MMIfaceModemFirmwareInterface *
peek_parent_modem_firmware_interface (MMSharedQuectel *self)
{
    return iface_modem_firmware_parent;
}

static MMIfaceModemLocationInterface *
peek_parent_modem_location_interface (MMSharedQuectel *self)
{
    return iface_modem_location_parent;
}

static void
iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManagerInterface *iface)
{
    iface_modem_3gpp_profile_manager_parent = g_type_interface_peek_parent (iface);

    iface->enable_unsolicited_events        = profile_manager_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = profile_manager_enable_unsolicited_events_finish;
}

static void
shared_quectel_init (MMSharedQuectelInterface *iface)
{
    iface->peek_parent_modem_interface          = peek_parent_modem_interface;
    iface->peek_parent_modem_firmware_interface = peek_parent_modem_firmware_interface;
    iface->peek_parent_modem_location_interface = peek_parent_modem_location_interface;
    iface->peek_parent_class                    = peek_parent_class;
}

static void
mm_broadband_modem_qmi_quectel_class_init (MMBroadbandModemQmiQuectelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = mm_shared_quectel_setup_ports;
}
