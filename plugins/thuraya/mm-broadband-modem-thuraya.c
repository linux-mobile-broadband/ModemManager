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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-errors-types.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-broadband-modem-thuraya.h"
#include "mm-broadband-bearer.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-thuraya.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemThuraya, mm_broadband_modem_thuraya, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init) );

/*****************************************************************************/
/* Operator Code and Name loading (3GPP interface) */

static gchar *
load_operator_code_finish (MMIfaceModem3gpp  *self,
                           GAsyncResult      *res,
                           GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_code (MMIfaceModem3gpp    *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, g_strdup ("90106"), g_free);
    g_object_unref (task);
}

static gchar *
load_operator_name_finish (MMIfaceModem3gpp  *self,
                           GAsyncResult      *res,
                           GError           **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_name (MMIfaceModem3gpp    *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, g_strdup ("THURAYA"), g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported modes (Modem inteface) */

static GArray *
load_supported_modes_finish (MMIfaceModem  *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_modes (MMIfaceModem        *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask                  *task;
    GArray                 *combinations;
    MMModemModeCombination  mode;

    task = g_task_new (self, NULL, callback, user_data);

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);

    /* Report any, Thuraya connections are packet-switched */
    mode.allowed = MM_MODEM_MODE_ANY;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    g_task_return_pointer (task, combinations, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported SMS storages (Messaging interface) */

typedef struct {
    GArray *mem1;
    GArray *mem2;
    GArray *mem3;
} SupportedStoragesResult;

static void
supported_storages_result_free (SupportedStoragesResult *result)
{
    if (result->mem1)
        g_array_unref (result->mem1);
    if (result->mem2)
        g_array_unref (result->mem2);
    if (result->mem3)
        g_array_unref (result->mem3);
    g_free (result);
}

static gboolean
modem_messaging_load_supported_storages_finish (MMIfaceModemMessaging  *self,
                                                GAsyncResult           *res,
                                                GArray                **mem1,
                                                GArray                **mem2,
                                                GArray                **mem3,
                                                GError                **error)
{
    SupportedStoragesResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *mem1 = g_array_ref (result->mem1);
    *mem2 = g_array_ref (result->mem2);
    *mem3 = g_array_ref (result->mem3);
    supported_storages_result_free (result);
    return TRUE;
}

static void
cpms_format_check_ready (MMBaseModem  *self,
                         GAsyncResult *res,
                         GTask        *task)
{
    const gchar             *response;
    GError                  *error = NULL;
    SupportedStoragesResult *result;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    result = g_new0 (SupportedStoragesResult, 1);

    /* Parse reply */
    if (!mm_thuraya_3gpp_parse_cpms_test_response (response,
                                                   &result->mem1,
                                                   &result->mem2,
                                                   &result->mem3,
                                                   &error)) {
        supported_storages_result_free (result);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, result, (GDestroyNotify) supported_storages_result_free);
    g_object_unref (task);
}

static void
modem_messaging_load_supported_storages (MMIfaceModemMessaging *self,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
    /* Check support storages */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPMS=?",
                              3,
                              TRUE,
                              (GAsyncReadyCallback)cpms_format_check_ready,
                              g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

MMBroadbandModemThuraya *
mm_broadband_modem_thuraya_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_THURAYA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_thuraya_init (MMBroadbandModemThuraya *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* No need to power-up/power-down the modem */
    iface->load_power_state = NULL;
    iface->load_power_state_finish = NULL;
    iface->modem_power_up = NULL;
    iface->modem_power_up_finish = NULL;
    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;

    /* Supported modes cannot be queried */
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Fixed operator code and name to be reported */
    iface->load_operator_name = load_operator_name;
    iface->load_operator_name_finish = load_operator_name_finish;
    iface->load_operator_code = load_operator_code;
    iface->load_operator_code_finish = load_operator_code_finish;

    /* Don't try to scan networks with AT+COPS=?.
     * The Thuraya XT does not seem to properly support AT+COPS=?.
     * When issuing this command, it seems to get sufficiently confused
     * to drop the signal. Furthermore, it is useless anyway as there is only
     * one network supported, Thuraya.
     */
    iface->scan_networks = NULL;
    iface->scan_networks_finish = NULL;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->load_supported_storages = modem_messaging_load_supported_storages;
    iface->load_supported_storages_finish = modem_messaging_load_supported_storages_finish;
}

static void
mm_broadband_modem_thuraya_class_init (MMBroadbandModemThurayaClass *klass)
{
}
