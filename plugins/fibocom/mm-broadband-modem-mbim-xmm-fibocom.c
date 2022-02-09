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
 * Copyright (C) 2022 Fibocom Wireless Inc.
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
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-mbim-xmm-fibocom.h"

static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimXmmFibocom, mm_broadband_modem_mbim_xmm_fibocom, MM_TYPE_BROADBAND_MODEM_MBIM_XMM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init))

/*****************************************************************************/

static gboolean
modem_3gpp_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp  *self,
                                                   GAsyncResult      *res,
                                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
after_attach_apn_modem_power_up_ready (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to power up modem after attach APN settings update: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "success toggling modem power up after attach APN");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_set_initial_eps_bearer_settings_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult     *res,
                                              GTask            *task)
{
    GError *error = NULL;

    iface_modem_3gpp_parent->set_initial_eps_bearer_settings_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "toggle modem power up after attach APN");
    mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                    MM_MODEM_POWER_STATE_ON,
                                    (GAsyncReadyCallback) after_attach_apn_modem_power_up_ready,
                                    task);
}

static void
before_attach_apn_modem_power_down_ready (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GTask        *task)
{
    GError             *error = NULL;
    MMBearerProperties *config;

    config = g_task_get_task_data (task);

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to power down modem before attach APN settings update: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "success toggling modem power down before attach APN");
    if (iface_modem_3gpp_parent->set_initial_eps_bearer_settings &&
        iface_modem_3gpp_parent->set_initial_eps_bearer_settings_finish) {
        iface_modem_3gpp_parent->set_initial_eps_bearer_settings (MM_IFACE_MODEM_3GPP (self),
                                                                  config,
                                                                  (GAsyncReadyCallback)parent_set_initial_eps_bearer_settings_ready,
                                                                  task);
        return;
    }
}

static void
modem_3gpp_set_initial_eps_bearer_settings (MMIfaceModem3gpp    *self,
                                            MMBearerProperties  *config,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_object_ref (config), g_object_unref);

    mm_obj_dbg (self, "toggle modem power down before attach APN");
    mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                    MM_MODEM_POWER_STATE_LOW,
                                    (GAsyncReadyCallback) before_attach_apn_modem_power_down_ready,
                                    task);
}

/******************************************************************************/

MMBroadbandModemMbimXmmFibocom *
mm_broadband_modem_mbim_xmm_fibocom_new (const gchar  *device,
                                         const gchar **drivers,
                                         const gchar  *plugin,
                                         guint16       vendor_id,
                                         guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_XMM_FIBOCOM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, FALSE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                         MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, TRUE,
#endif
                         NULL);
}

static void
mm_broadband_modem_mbim_xmm_fibocom_init (MMBroadbandModemMbimXmmFibocom *self)
{
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->set_initial_eps_bearer_settings         = modem_3gpp_set_initial_eps_bearer_settings;
    iface->set_initial_eps_bearer_settings_finish  = modem_3gpp_set_initial_eps_bearer_settings_finish;
}

static void
mm_broadband_modem_mbim_xmm_fibocom_class_init (MMBroadbandModemMbimXmmFibocomClass *klass)
{
}
