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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-serial-parsers.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-gobi.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemGobi, mm_broadband_modem_gobi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    const gchar *p;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    p = mm_strip_tag (response, "*CNTI:");
    p = strchr (p, ',');
    if (p) {
        /* We are reporting ALL 3GPP access technologies here */
        *access_technologies = mm_string_to_access_tech (p + 1);
        *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        return TRUE;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse access technologies result: '%s'",
                 response);
    return FALSE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*CNTI=0",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/

MMBroadbandModemGobi *
mm_broadband_modem_gobi_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_GOBI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_gobi_init (MMBroadbandModemGobi *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;

    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;
}

static void
mm_broadband_modem_gobi_class_init (MMBroadbandModemGobiClass *klass)
{
}
