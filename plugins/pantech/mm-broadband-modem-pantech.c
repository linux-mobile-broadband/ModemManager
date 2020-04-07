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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-messaging.h"
#include "mm-errors-types.h"
#include "mm-broadband-modem-pantech.h"
#include "mm-sim-pantech.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

static MMIfaceModemMessaging *iface_modem_messaging_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemPantech, mm_broadband_modem_pantech, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init))

/*****************************************************************************/
/* Load supported SMS storages (Messaging interface) */

static void
skip_sm_sr_storage (GArray *mem)
{
    guint i = mem->len;

    if (!mem)
        return;

    /* Remove SM and SR from the list of supported storages */
    while (i-- > 0) {
        if (g_array_index (mem, MMSmsStorage, i) == MM_SMS_STORAGE_SR ||
            g_array_index (mem, MMSmsStorage, i) == MM_SMS_STORAGE_SM)
            g_array_remove_index (mem, i);
    }
}

static gboolean
load_supported_storages_finish (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                GArray **mem1,
                                GArray **mem2,
                                GArray **mem3,
                                GError **error)
{
    if (!iface_modem_messaging_parent->load_supported_storages_finish (self, res, mem1, mem2, mem3, error))
        return FALSE;

    skip_sm_sr_storage (*mem1);
    skip_sm_sr_storage (*mem2);
    skip_sm_sr_storage (*mem3);
    return TRUE;
}

static void
load_supported_storages (MMIfaceModemMessaging *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    /* Chain up parent's loading */
    iface_modem_messaging_parent->load_supported_storages (self, callback, user_data);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_pantech_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New Pantech SIM */
    mm_sim_pantech_new (MM_BASE_MODEM (self),
                        NULL, /* cancellable */
                        callback,
                        user_data);
}

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_wait_cb (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    /* wait so sim pin is done */
    g_timeout_add_seconds (5,
                           (GSourceFunc)after_sim_unlock_wait_cb,
                           g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

MMBroadbandModemPantech *
mm_broadband_modem_pantech_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_PANTECH,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_pantech_init (MMBroadbandModemPantech *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Create Pantech-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface_modem_messaging_parent = g_type_interface_peek_parent (iface);

    iface->load_supported_storages = load_supported_storages;
    iface->load_supported_storages_finish = load_supported_storages_finish;
}

static void
mm_broadband_modem_pantech_class_init (MMBroadbandModemPantechClass *klass)
{
}
