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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
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
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-mbim-cinterion.h"
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

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimCinterion, mm_broadband_modem_mbim_cinterion, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_CINTERION, shared_cinterion_init))

/*****************************************************************************/
/* Load revision (Modem interface) */

static gchar *
load_revision_finish (MMIfaceModem  *self,
                      GAsyncResult  *res,
                      GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
at_load_revision_ready (MMBaseModem  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError                *error = NULL;
    const gchar           *output = NULL;
    g_autofree gchar      *revision = NULL;
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autoptr(GRegex)      r = NULL;

    output = mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "unable to get AT revision output");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* MV31 modem replies with: '^VERSION: xxx' */
    /* MV32 modem replies with: '+VERSION: xxx' */
    r = g_regex_new ("[+^]VERSION:\\s*(.*)", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!g_regex_match_full (r, output, strlen(output), 0, 0, &match_info, &error)) {
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't match '^VERSION:' or '+VERSION:' in reply: '%s'",
                                     output);
        g_object_unref (task);
    }

    if (!g_match_info_matches (match_info)) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse 'AT^VERSION=1' or 'AT+VERSION=1' reply: '%s'",
            output);
        g_object_unref (task);
        return;
    }

    revision = mm_get_string_unquoted_from_match_info (match_info, 1);
    if (revision == NULL) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Could not find revision in reply: '%s'",
            output);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "got AT revision '%s'", revision);
    g_task_return_pointer (task, g_strdup (revision), g_free);
    g_object_unref (task);
}

static void
mbim_load_revision_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    gchar *revision = NULL;

    revision = iface_modem_parent->load_revision_finish (self, res, NULL);

    mm_obj_dbg (self, "got mbim revision '%s'\n", revision);
    g_task_return_pointer (task, revision, g_free);
    g_object_unref (task);
}

static void
load_revision (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    GTask          *task;
    MMPortSerialAt *at_port = NULL;
    guint           product_id;

    task = g_task_new (self, NULL, callback, user_data);
    at_port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    product_id = mm_base_modem_get_product_id (MM_BASE_MODEM(self));

    if (at_port) {
        /* MV31 */
        if (product_id == 0x00b3) {
            mm_base_modem_at_command (MM_BASE_MODEM(self),
                                      "AT^VERSION=1",
                                      3,
                                      TRUE,
                                      (GAsyncReadyCallback) at_load_revision_ready,
                                      task);
            return;
        }
        /* MV32 */
        if (product_id == 0x00f1) {
            mm_base_modem_at_command (MM_BASE_MODEM(self),
                                      "AT+VERSION=1",
                                      3,
                                      TRUE,
                                      (GAsyncReadyCallback) at_load_revision_ready,
                                      task);
            return;
        }
    }

    /*
     * Only call parent function, if no AT port is available
     * or the product ID does not match
     */
    iface_modem_parent->load_revision (self, (GAsyncReadyCallback)mbim_load_revision_ready, task);
}

/*****************************************************************************/
/* Load hardware revision (Modem interface) */

static gchar *
load_hardware_revision_finish (MMIfaceModem  *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
at_load_hardware_revision_ready (MMBaseModem  *self,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    GError                *error = NULL;
    const gchar           *output = NULL;
    g_autofree gchar      *revision = NULL;
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autoptr(GRegex)      r = NULL;

    output = mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "unable to get AT hardware revision output");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    r = g_regex_new ("HW Revision:\\s*(.*)\\s*", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    if (!g_regex_match_full (r, output, strlen(output), 0, 0, &match_info, &error)) {
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't match 'HW Revision:' in reply: '%s'",
                                     output);
        g_object_unref (task);
        return;
    }

    if (!g_match_info_matches (match_info)) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse 'AT+SKUID' reply: '%s'",
            output);
        g_object_unref (task);
        return;
    }

    revision = mm_get_string_unquoted_from_match_info (match_info, 1);
    if (revision == NULL) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Could not find hardware revision in reply: '%s'",
            output);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "got AT hardware revision '%s'", revision);
    g_task_return_pointer (task, g_strdup (revision), g_free);
    g_object_unref (task);
}

static void
mbim_load_hardware_revision_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    gchar *revision = NULL;

    revision = iface_modem_parent->load_revision_finish (self, res, NULL);

    mm_obj_dbg (self, "got mbim hardware revision '%s'", revision);
    g_task_return_pointer (task, revision, g_free);
    g_object_unref (task);
}

static void
load_hardware_revision (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    GTask          *task;
    MMPortSerialAt *at_port = NULL;
    guint           product_id;

    task = g_task_new (self, NULL, callback, user_data);
    at_port = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    product_id = mm_base_modem_get_product_id (MM_BASE_MODEM(self));

    if (at_port) {
        /* MV31 or MV32 */
        if ((product_id == 0x00f1) || (product_id == 0x00b3)) {
            mm_base_modem_at_command (MM_BASE_MODEM(self),
                                      "AT+SKUID",
                                      3,
                                      TRUE,
                                      (GAsyncReadyCallback) at_load_hardware_revision_ready,
                                      task);
            return;
        }
    }

    /*
     * Only call parent function, if no AT port is available
     * or the product ID does not match
     */
    iface_modem_parent->load_revision (self, (GAsyncReadyCallback)mbim_load_hardware_revision_ready, task);
}

/*****************************************************************************/

MMBroadbandModemMbimCinterion *
mm_broadband_modem_mbim_cinterion_new (const gchar *device,
                                       const gchar *physdev,
                                      const gchar **drivers,
                                      const gchar *plugin,
                                      guint16 vendor_id,
                                      guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_CINTERION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* include carrier information */
                         MM_IFACE_MODEM_FIRMWARE_IGNORE_CARRIER, TRUE,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
                         MM_BROADBAND_MODEM_MBIM_INTEL_FIRMWARE_UPDATE_UNSUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_mbim_cinterion_init (MMBroadbandModemMbimCinterion *self)
{
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->reset                         = mm_shared_cinterion_modem_reset;
    iface->reset_finish                  = mm_shared_cinterion_modem_reset_finish;
    iface->load_revision_finish          = load_revision_finish;
    iface->load_revision                 = load_revision;
    iface->load_hardware_revision_finish = load_hardware_revision_finish;
    iface->load_hardware_revision        = load_hardware_revision;
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
mm_broadband_modem_mbim_cinterion_class_init (MMBroadbandModemMbimCinterionClass *klass)
{
}
