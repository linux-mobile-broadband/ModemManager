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
 * GNU General Public License for more details.
 *
 * Copyright (C) 2008 - 2010 Ericsson AB
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *         Dan Williams <dcbw@redhat.com>
 *         Aleksander Morgado <aleksander@lanedo.com>
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
#include "mm-modem-helpers.h"
#include "mm-broadband-modem-mbm.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbm, mm_broadband_modem_mbm, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init))

#define MBM_NETWORK_MODE_ANY  1
#define MBM_NETWORK_MODE_2G   5
#define MBM_NETWORK_MODE_3G   6

struct _MMBroadbandModemMbmPrivate {
    guint network_mode;
};

/*****************************************************************************/
/* After SIM unlock (Modem interface) */

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return TRUE;
}

static gboolean
after_sim_unlock_wait_cb (GSimpleAsyncResult *result)
{
    g_simple_async_result_complete (result);
    g_object_unref (result);
    return FALSE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_after_sim_unlock);

    /* wait so sim pin is done */
    g_timeout_add (500, (GSourceFunc)after_sim_unlock_wait_cb, result);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    const gchar *response;
    guint a;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (mm_get_uint_from_str (mm_strip_tag (response, "CFUN:"), &a)) {
        /* No settings to set preferred */
        *preferred = MM_MODEM_MODE_NONE;

        switch (a) {
        case MBM_NETWORK_MODE_2G:
            *allowed = MM_MODEM_MODE_2G;
            break;
        case MBM_NETWORK_MODE_3G:
            *allowed = MM_MODEM_MODE_3G;
            break;
        default:
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            break;
        }

        return TRUE;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse +CFUN response: '%s'",
                 response);
    return FALSE;
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_mode_update_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_allowed_modes (MMIfaceModem *_self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    GSimpleAsyncResult *result;
    gchar *command;
    gint mbm_mode = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    if (allowed == MM_MODEM_MODE_2G)
        mbm_mode = MBM_NETWORK_MODE_2G;
    else if (allowed == MM_MODEM_MODE_3G)
        mbm_mode = MBM_NETWORK_MODE_3G;
    else if ((allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G) ||
              allowed == MM_MODEM_MODE_ANY) &&
             preferred == MM_MODEM_MODE_NONE)
        mbm_mode = MBM_NETWORK_MODE_ANY;

    if (mbm_mode < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Requested mode (allowed: '%s', preferred: '%s') not "
                                         "supported by the modem.",
                                         allowed_str,
                                         preferred_str);
        g_free (allowed_str);
        g_free (preferred_str);

        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Cache the value for next power-ups */
    self->priv->network_mode = mbm_mode;

    command = g_strdup_printf ("+CFUN=%d", mbm_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        result);
    g_free (command);
}

/*****************************************************************************/
/* Initializing the modem (Modem interface) */

static gboolean
modem_init_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    /* Ignore errors */
    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, NULL);
    return TRUE;
}

static const MMBaseModemAtCommand modem_init_sequence[] = {
    /* Init command */
    { "&F E0 V1 X4 &C1 +CMEE=1", 3, FALSE, NULL },
    /* Ensure disconnected */
    { "*ENAP=0", 3, FALSE, NULL },
    { NULL }
};

static void
modem_init (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               modem_init_sequence,
                               NULL,  /* response_processor_context */
                               NULL,  /* response_processor_context_free */
                               callback,
                               user_data);
}

/*****************************************************************************/
/* Powering up the modem (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    /* By default, errors in the power up command are ignored. */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    return TRUE;
}

static void
modem_power_up (MMIfaceModem *_self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    gchar *command;

    command = g_strdup_printf ("+CFUN=%u", self->priv->network_mode);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              5,
                              FALSE,
                              callback,
                              user_data);
    g_free (command);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
    }

    /* Our own enable now */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*ERINFO=1",
                              5,
                              FALSE,
                              (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
                              simple);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_enable_unsolicited_events);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Disabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Next, chain up parent's disable */
    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        simple);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_disable_unsolicited_events);

    /* Our own disable first */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*ERINFO=0",
                              5,
                              FALSE,
                              (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
                              result);
}

/*****************************************************************************/

MMBroadbandModemMbm *
mm_broadband_modem_mbm_new (const gchar *device,
                            const gchar *driver,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_mbm_init (MMBroadbandModemMbm *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_MBM,
                                              MMBroadbandModemMbmPrivate);

    self->priv->network_mode = MBM_NETWORK_MODE_ANY;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->modem_init = modem_init;
    iface->modem_init_finish = modem_init_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;
}

static void
mm_broadband_modem_mbm_class_init (MMBroadbandModemMbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbmPrivate));
}
