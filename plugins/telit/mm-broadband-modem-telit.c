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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
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
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-telit.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemTelit, mm_broadband_modem_telit, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    GVariant *result;

    result = mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, error);
    if (!result) {
        if (error)
            g_assert (*error);
        return FALSE;
    }

    *access_technologies = (MMModemAccessTechnology) g_variant_get_uint32 (result);
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static gboolean
response_processor_psnt_ignore_at_errors (MMBaseModem *self,
                                          gpointer none,
                                          const gchar *command,
                                          const gchar *response,
                                          gboolean last_command,
                                          const GError *error,
                                          GVariant **result,
                                          GError **result_error)
{
    const gchar *psnt, *mode;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command)
            *result_error = g_error_copy (error);
        return FALSE;
    }

    psnt = mm_strip_tag (response, "#PSNT:");
    mode = strchr (psnt, ',');
    if (mode) {
        switch (atoi (++mode)) {
        case 0:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
            return TRUE;
        case 1:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EDGE);
            return TRUE;
        case 2:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UMTS);
            return TRUE;
        case 3:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_HSDPA);
            return TRUE;
        default:
            break;
        }
    }

    g_set_error (result_error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse #PSNT response: '%s'",
                 response);
    return FALSE;
}

static gboolean
response_processor_service_ignore_at_errors (MMBaseModem *self,
                                             gpointer none,
                                             const gchar *command,
                                             const gchar *response,
                                             gboolean last_command,
                                             const GError *error,
                                             GVariant **result,
                                             GError **result_error)
{
    const gchar *service, *mode;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command)
            *result_error = g_error_copy (error);
        return FALSE;
    }

    service = mm_strip_tag (response, "+SERVICE:");
    mode = strchr (service, ',');
    if (mode) {
        switch (atoi (++mode)) {
        case 1:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_1XRTT);
            return TRUE;
        case 2:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0);
            return TRUE;
        case 3:
            *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_EVDOA);
            return TRUE;
        default:
            break;
        }
    }

    g_set_error (result_error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse +SERVICE response: '%s'",
                 response);
    return FALSE;
}

static const MMBaseModemAtCommand access_tech_commands[] = {
    { "#PSNT?",  3, TRUE, response_processor_psnt_ignore_at_errors },
    { "+SERVICE?", 3, TRUE, response_processor_service_ignore_at_errors },
    { NULL }
};

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_dbg ("loading access technology (Telit)...");
    mm_base_modem_at_sequence (
        MM_BASE_MODEM (self),
        access_tech_commands,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        callback,
        user_data);
}

/*****************************************************************************/
/* Flow control (Modem interface) */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    /* Completely ignore errors */
    return TRUE;
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *cmd;
    guint flow_control = 1; /* Default flow control: XON/XOFF */

    switch (mm_base_modem_get_product_id (MM_BASE_MODEM (self)) & 0xFFFF) {
    case 0x0021:
        flow_control = 2; /* Telit IMC modems support only RTS/CTS mode */
        break;
    default:
        break;
    }

    cmd = g_strdup_printf ("+IFC=%u,%u", flow_control, flow_control);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              3,
                              FALSE,
                              NULL,
                              NULL);
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_flow_control);
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
    g_free (cmd);
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
   /* Ignore errors */
    mm_base_modem_at_sequence_full_finish (MM_BASE_MODEM (self),
                                           res,
                                           NULL,
                                           NULL);
    return TRUE;
}

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    /* Enable +CIEV only for: signal, service, roam */
    { "AT+CIND=0,1,1,0,0,0,1,0,0", 5, FALSE, NULL },
    /* Telit modems +CMER command supports only <ind>=2 */
    { "+CMER=3,0,0,2", 5, FALSE, NULL },
    { NULL }
};

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self)),
        unsolicited_enable_sequence,
        NULL,  /* response_processor_context */
        NULL,  /* response_processor_context_free */
        NULL,  /* cancellable */
        callback,
        user_data);
}

/*****************************************************************************/

MMBroadbandModemTelit *
mm_broadband_modem_telit_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_TELIT,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_telit_init (MMBroadbandModemTelit *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
}

static void
mm_broadband_modem_telit_class_init (MMBroadbandModemTelitClass *klass)
{
}
