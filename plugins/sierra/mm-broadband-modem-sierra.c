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
#include "mm-broadband-modem-sierra.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierra, mm_broadband_modem_sierra, MM_TYPE_BROADBAND_MODEM, 0,
                            G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    /* We are reporting ALL 3GPP access technologies here */
    *access_technologies = ((MMModemAccessTechnology) GPOINTER_TO_UINT (
                                g_simple_async_result_get_op_res_gpointer (
                                    G_SIMPLE_ASYNC_RESULT (res))));
    *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    return TRUE;
}

static void
cnti_set_ready (MMBaseModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response)
        g_simple_async_result_take_error (simple, error);
    else {
        MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        const gchar *p;

        p = mm_strip_tag (response, "*CNTI:");
        p = strchr (p, ',');
        if (p)
            act = mm_string_to_access_tech (p + 1);

        if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            g_simple_async_result_set_error (
                simple,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Couldn't parse access technologies result: '%s'",
                response);
        else
            g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (act), NULL);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_access_technologies);

    if (mm_iface_modem_is_3gpp (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "*CNTI=0",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)cnti_set_ready,
                                  result);
        return;
    }

    /* Cannot do this in CDMA modems */
    g_simple_async_result_set_error (result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "Cannot load access technologies in CDMA modems");
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

MMBroadbandModemSierra *
mm_broadband_modem_sierra_new (const gchar *device,
                               const gchar *driver,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_sierra_init (MMBroadbandModemSierra *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
}

static void
mm_broadband_modem_sierra_class_init (MMBroadbandModemSierraClass *klass)
{
}
