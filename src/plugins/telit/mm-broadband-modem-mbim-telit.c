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
 * Copyright (C) 2019 Daniele Palmas <dnlplm@gmail.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-mbim-telit.h"
#include "mm-modem-helpers-telit.h"
#include "mm-shared-telit.h"

static void iface_modem_init  (MMIfaceModem  *iface);
static void shared_telit_init (MMSharedTelit *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimTelit, mm_broadband_modem_mbim_telit, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_TELIT, shared_telit_init))

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_modes_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    MMModemModeCombination modes_combination;
    MMModemMode modes_mask = MM_MODEM_MODE_NONE;
    const gchar   *response;
    GArray        *modes;
    GArray        *all;
    GArray        *combinations;
    GArray        *filtered;
    GError        *error = NULL;
    MMSharedTelit *shared = MM_SHARED_TELIT (self);
    guint          i;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_prefix_error (&error, "generic query of supported 3GPP networks with WS46=? failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    modes = mm_3gpp_parse_ws46_test_response (response, self, &error);
    if (!modes) {
        g_prefix_error (&error, "parsing WS46=? response failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 0; i < modes->len; i++) {
        MMModemMode       mode;
        g_autofree gchar *str = NULL;

        mode = g_array_index (modes, MMModemMode, i);

        modes_mask |= mode;

        str = mm_modem_mode_build_string_from_mask (mode);
        mm_obj_dbg (self, "device allows (3GPP) mode combination: %s", str);
    }

    g_array_unref (modes);

    /* Build a mask with all supported modes */
    all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    modes_combination.allowed = modes_mask;
    modes_combination.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (all, modes_combination);

    /* Filter out those unsupported modes */
    combinations = mm_telit_build_modes_list();
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    mm_shared_telit_store_supported_modes (shared, filtered);
    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+WS46=?",
                              3,
                              TRUE, /* allow caching, it's a test command */
                              (GAsyncReadyCallback) load_supported_modes_ready,
                              task);
}

/*****************************************************************************/
/* Load revision (Modem interface) */

static gchar *
load_revision_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError      **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_revision_ready_shared (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    GError *error = NULL;
    gchar  *revision = NULL;

    revision = mm_shared_telit_modem_load_revision_finish (self, res, &error);
    if (!revision) {
        /* give up */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    mm_shared_telit_store_revision (MM_SHARED_TELIT (self), revision);
    g_task_return_pointer (task, revision, g_free);
    g_object_unref (task);
}

static void
parent_load_revision_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    gchar *revision = NULL;

    revision = iface_modem_parent->load_revision_finish (self, res, NULL);
    if (!revision || !strlen (revision)) {
        /* Some firmware versions do not properly populate the revision in the
         * MBIM response, so try using the AT ports */
        g_free (revision);
        mm_shared_telit_modem_load_revision (
            self,
            (GAsyncReadyCallback)load_revision_ready_shared,
            task);
        return;
    }
    mm_shared_telit_store_revision (MM_SHARED_TELIT (self), revision);
    g_task_return_pointer (task, revision, g_free);
    g_object_unref (task);
}

static void
load_revision (MMIfaceModem        *self,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    /* Run parent's loading */
    /* Telit's custom revision loading (in telit/mm-shared) is AT-only and the
     * MBIM modem might not have an AT port available, so we call the parent's
     * load_revision and store the revision taken from the firmware info capabilities. */
    iface_modem_parent->load_revision (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_revision_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

MMBroadbandModemMbimTelit *
mm_broadband_modem_mbim_telit_new (const gchar  *device,
                                   const gchar  *physdev,
                                   const gchar **drivers,
                                   const gchar  *plugin,
                                   guint16       vendor_id,
                                   guint16       product_id,
                                   guint16       subsystem_vendor_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_TELIT,
                         MM_BASE_MODEM_DEVICE,              device,
                         MM_BASE_MODEM_PHYSDEV,             physdev,
                         MM_BASE_MODEM_DRIVERS,             drivers,
                         MM_BASE_MODEM_PLUGIN,              plugin,
                         MM_BASE_MODEM_VENDOR_ID,           vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID,          product_id,
                         MM_BASE_MODEM_SUBSYSTEM_VENDOR_ID, subsystem_vendor_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_mbim_telit_init (MMBroadbandModemMbimTelit *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->set_current_bands = mm_shared_telit_modem_set_current_bands;
    iface->set_current_bands_finish = mm_shared_telit_modem_set_current_bands_finish;
    iface->load_current_bands = mm_shared_telit_modem_load_current_bands;
    iface->load_current_bands_finish = mm_shared_telit_modem_load_current_bands_finish;
    iface->load_supported_bands = mm_shared_telit_modem_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_telit_modem_load_supported_bands_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = mm_shared_telit_load_current_modes;
    iface->load_current_modes_finish = mm_shared_telit_load_current_modes_finish;
    iface->set_current_modes = mm_shared_telit_set_current_modes;
    iface->set_current_modes_finish = mm_shared_telit_set_current_modes_finish;
    iface->load_revision_finish = load_revision_finish;
    iface->load_revision = load_revision;
}

static MMIfaceModem *
peek_parent_modem_interface (MMSharedTelit *self)
{
    return iface_modem_parent;
}

static void
shared_telit_init (MMSharedTelit *iface)
{
    iface->peek_parent_modem_interface = peek_parent_modem_interface;
}

static void
mm_broadband_modem_mbim_telit_class_init (MMBroadbandModemMbimTelitClass *klass)
{
}
