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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-voice.h"
#include "mm-broadband-modem-qmi-cinterion.h"
#include "mm-shared-cinterion.h"

static void iface_modem_init          (MMIfaceModemInterface         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface);
static void iface_modem_location_init (MMIfaceModemLocationInterface *iface);
static void iface_modem_voice_init    (MMIfaceModemVoiceInterface    *iface);
static void iface_modem_time_init     (MMIfaceModemTimeInterface     *iface);
static void shared_cinterion_init     (MMSharedCinterionInterface    *iface);

static MMIfaceModemInterface         *iface_modem_parent;
static MMIfaceModemFirmwareInterface *iface_modem_firmware_parent;
static MMIfaceModemLocationInterface *iface_modem_location_parent;
static MMIfaceModemVoiceInterface    *iface_modem_voice_parent;
static MMIfaceModemTimeInterface     *iface_modem_time_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmiCinterion, mm_broadband_modem_qmi_cinterion, MM_TYPE_BROADBAND_MODEM_QMI, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_CINTERION, shared_cinterion_init))

/*****************************************************************************/

MMBroadbandModemQmiCinterion *
mm_broadband_modem_qmi_cinterion_new (const gchar *device,
                                      const gchar *physdev,
                                      const gchar **drivers,
                                      const gchar *plugin,
                                      guint16 vendor_id,
                                      guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_CINTERION,
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
mm_broadband_modem_qmi_cinterion_init (MMBroadbandModemQmiCinterion *self)
{
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->reset        = mm_shared_cinterion_modem_reset;
    iface->reset_finish = mm_shared_cinterion_modem_reset_finish;
}

static MMIfaceModemInterface *
peek_parent_interface (MMSharedCinterion *self)
{
    return iface_modem_parent;
}

static void
iface_modem_firmware_init (MMIfaceModemFirmwareInterface *iface)
{
    iface_modem_firmware_parent = g_type_interface_peek_parent (iface);

    iface->load_update_settings = mm_shared_cinterion_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_cinterion_firmware_load_update_settings_finish;
}

static MMIfaceModemFirmwareInterface *
peek_parent_firmware_interface (MMSharedCinterion *self)
{
    return iface_modem_firmware_parent;
}

static void
iface_modem_location_init (MMIfaceModemLocationInterface *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_cinterion_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_cinterion_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_cinterion_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_cinterion_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_cinterion_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_cinterion_disable_location_gathering_finish;
}

static MMIfaceModemLocationInterface *
peek_parent_location_interface (MMSharedCinterion *self)
{
    return iface_modem_location_parent;
}

static void
iface_modem_voice_init (MMIfaceModemVoiceInterface *iface)
{
    iface_modem_voice_parent = g_type_interface_peek_parent (iface);

    iface->create_call = mm_shared_cinterion_create_call;

    iface->check_support                     = mm_shared_cinterion_voice_check_support;
    iface->check_support_finish              = mm_shared_cinterion_voice_check_support_finish;
    iface->enable_unsolicited_events         = mm_shared_cinterion_voice_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish  = mm_shared_cinterion_voice_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events        = mm_shared_cinterion_voice_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = mm_shared_cinterion_voice_disable_unsolicited_events_finish;
    iface->setup_unsolicited_events          = mm_shared_cinterion_voice_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish   = mm_shared_cinterion_voice_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events        = mm_shared_cinterion_voice_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = mm_shared_cinterion_voice_cleanup_unsolicited_events_finish;
}

static MMIfaceModemVoiceInterface *
peek_parent_voice_interface (MMSharedCinterion *self)
{
    return iface_modem_voice_parent;
}

static void
iface_modem_time_init (MMIfaceModemTimeInterface *iface)
{
    iface_modem_time_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events          = mm_shared_cinterion_time_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish   = mm_shared_cinterion_time_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events        = mm_shared_cinterion_time_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = mm_shared_cinterion_time_cleanup_unsolicited_events_finish;
}

static MMIfaceModemTimeInterface *
peek_parent_time_interface (MMSharedCinterion *self)
{
    return iface_modem_time_parent;
}

static void
shared_cinterion_init (MMSharedCinterionInterface *iface)
{
    iface->peek_parent_interface          = peek_parent_interface;
    iface->peek_parent_firmware_interface = peek_parent_firmware_interface;
    iface->peek_parent_location_interface = peek_parent_location_interface;
    iface->peek_parent_voice_interface    = peek_parent_voice_interface;
    iface->peek_parent_time_interface     = peek_parent_time_interface;
}

static void
mm_broadband_modem_qmi_cinterion_class_init (MMBroadbandModemQmiCinterionClass *klass)
{
}
