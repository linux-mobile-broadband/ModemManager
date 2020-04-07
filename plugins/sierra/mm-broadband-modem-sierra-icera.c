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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-broadband-modem-sierra-icera.h"
#include "mm-iface-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log-object.h"
#include "mm-common-sierra.h"
#include "mm-broadband-bearer-sierra.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierraIcera, mm_broadband_modem_sierra_icera, MM_TYPE_BROADBAND_MODEM_ICERA, 0,
                            G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_sierra_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_sierra_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "creating sierra bearer...");
    mm_broadband_bearer_sierra_new (MM_BROADBAND_MODEM (self),
                                    properties,
                                    TRUE, /* is_icera */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)broadband_bearer_sierra_new_ready,
                                    task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_sierra_icera_parent_class)->setup_ports (self);

    mm_common_sierra_setup_ports (self);
}

/*****************************************************************************/

MMBroadbandModemSierraIcera *
mm_broadband_modem_sierra_icera_new (const gchar *device,
                                     const gchar **drivers,
                                     const gchar *plugin,
                                     guint16 vendor_id,
                                     guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA_ICERA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_sierra_icera_init (MMBroadbandModemSierraIcera *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    mm_common_sierra_peek_parent_interfaces (iface);

    iface->load_power_state = mm_common_sierra_load_power_state;
    iface->load_power_state_finish = mm_common_sierra_load_power_state_finish;
    iface->modem_power_up = mm_common_sierra_modem_power_up;
    iface->modem_power_up_finish = mm_common_sierra_modem_power_up_finish;
    iface->create_sim = mm_common_sierra_create_sim;
    iface->create_sim_finish = mm_common_sierra_create_sim_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
mm_broadband_modem_sierra_icera_class_init (MMBroadbandModemSierraIceraClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
