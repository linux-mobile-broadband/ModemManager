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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-errors-types.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-voice.h"
#include "mm-broadband-modem-qmi-simtech.h"
#include "mm-shared-simtech.h"

static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_voice_init    (MMIfaceModemVoice    *iface);
static void shared_simtech_init       (MMSharedSimtech      *iface);

static MMIfaceModemLocation *iface_modem_location_parent;
static MMIfaceModemVoice    *iface_modem_voice_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmiSimtech, mm_broadband_modem_qmi_simtech, MM_TYPE_BROADBAND_MODEM_QMI, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_SIMTECH, shared_simtech_init))

/*****************************************************************************/

MMBroadbandModemQmiSimtech *
mm_broadband_modem_qmi_simtech_new (const gchar  *device,
                                    const gchar **drivers,
                                    const gchar  *plugin,
                                    guint16       vendor_id,
                                    guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_SIMTECH,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BROADBAND_MODEM_INDICATORS_DISABLED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_qmi_simtech_init (MMBroadbandModemQmiSimtech *self)
{
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_simtech_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_simtech_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_simtech_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_simtech_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_simtech_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_simtech_disable_location_gathering_finish;
}

static MMIfaceModemLocation *
peek_parent_location_interface (MMSharedSimtech *self)
{
    return iface_modem_location_parent;
}

static void
iface_modem_voice_init (MMIfaceModemVoice *iface)
{
    iface_modem_voice_parent = g_type_interface_peek_parent (iface);

    iface->check_support                     = mm_shared_simtech_voice_check_support;
    iface->check_support_finish              = mm_shared_simtech_voice_check_support_finish;
    iface->enable_unsolicited_events         = mm_shared_simtech_voice_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish  = mm_shared_simtech_voice_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events        = mm_shared_simtech_voice_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = mm_shared_simtech_voice_disable_unsolicited_events_finish;
    iface->setup_unsolicited_events          = mm_shared_simtech_voice_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish   = mm_shared_simtech_voice_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events        = mm_shared_simtech_voice_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = mm_shared_simtech_voice_cleanup_unsolicited_events_finish;
    iface->setup_in_call_audio_channel          = mm_shared_simtech_voice_setup_in_call_audio_channel;
    iface->setup_in_call_audio_channel_finish   = mm_shared_simtech_voice_setup_in_call_audio_channel_finish;
    iface->cleanup_in_call_audio_channel        = mm_shared_simtech_voice_cleanup_in_call_audio_channel;
    iface->cleanup_in_call_audio_channel_finish = mm_shared_simtech_voice_cleanup_in_call_audio_channel_finish;
}

static MMIfaceModemVoice *
peek_parent_voice_interface (MMSharedSimtech *self)
{
    return iface_modem_voice_parent;
}

static void
shared_simtech_init (MMSharedSimtech *iface)
{
    iface->peek_parent_location_interface = peek_parent_location_interface;
    iface->peek_parent_voice_interface    = peek_parent_voice_interface;
}

static void
mm_broadband_modem_qmi_simtech_class_init (MMBroadbandModemQmiSimtechClass *klass)
{
}
