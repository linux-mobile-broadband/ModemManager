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
#include "mm-modem-helpers-telit.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemTelit, mm_broadband_modem_telit, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

#define CSIM_QUERY_PIN_RETRIES_STR  "+CSIM=10,0020000100"
#define CSIM_QUERY_PUK_RETRIES_STR  "+CSIM=10,002C000100"
#define CSIM_QUERY_PIN2_RETRIES_STR "+CSIM=10,0020008100"
#define CSIM_QUERY_PUK2_RETRIES_STR "+CSIM=10,002C008100"
#define CSIM_QUERY_TIMEOUT 3

typedef enum {
    LOAD_UNLOCK_RETRIES_STEP_FIRST,
    LOAD_UNLOCK_RETRIES_STEP_PIN,
    LOAD_UNLOCK_RETRIES_STEP_PUK,
    LOAD_UNLOCK_RETRIES_STEP_PIN2,
    LOAD_UNLOCK_RETRIES_STEP_PUK2,
    LOAD_UNLOCK_RETRIES_STEP_LAST
} LoadUnlockRetriesStep;

typedef struct {
    MMBroadbandModemTelit *self;
    GSimpleAsyncResult *result;
    MMUnlockRetries *retries;
    LoadUnlockRetriesStep step;
    guint succeded_requests;
} LoadUnlockRetriesContext;

static void load_unlock_retries_step (LoadUnlockRetriesContext *ctx);

static void
load_unlock_retries_context_complete_and_free (LoadUnlockRetriesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->retries);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LoadUnlockRetriesContext, ctx);
}

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (MMUnlockRetries*) g_object_ref (g_simple_async_result_get_op_res_gpointer (
                                                G_SIMPLE_ASYNC_RESULT (res)));
}

static void
csim_query_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  LoadUnlockRetriesContext *ctx)
{
    const gchar *response;
    gint unlock_retries;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);

    if (!response) {
        mm_warn ("No respose for step %d: %s", ctx->step, error->message);
        g_error_free (error);
        goto next_step;
    }

    if ( (unlock_retries = parse_csim_response (ctx->step, response, &error)) < 0) {
        mm_warn ("Parse error in step %d: %s.", ctx->step, error->message);
        g_error_free (error);
        goto next_step;
    }

    ctx->succeded_requests++;

    switch (ctx->step) {
        case LOAD_UNLOCK_RETRIES_STEP_PIN:
            mm_dbg ("PIN unlock retries left: %d", unlock_retries);
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PIN, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK:
            mm_dbg ("PUK unlock retries left: %d", unlock_retries);
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PUK, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PIN2:
            mm_dbg ("PIN2 unlock retries left: %d", unlock_retries);
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PIN2, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK2:
            mm_dbg ("PUK2 unlock retries left: %d", unlock_retries);
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PUK2, unlock_retries);
            break;
        default:
            break;
    }

next_step:
    ctx->step++;
    load_unlock_retries_step (ctx);
}

static void
load_unlock_retries_step (LoadUnlockRetriesContext *ctx)
{
    switch (ctx->step) {
        case LOAD_UNLOCK_RETRIES_STEP_FIRST:
            /* Fall back on next step */
            ctx->step++;
        case LOAD_UNLOCK_RETRIES_STEP_PIN:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_QUERY_PIN_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      ctx);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_QUERY_PUK_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      ctx);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PIN2:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_QUERY_PIN2_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      ctx);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK2:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_QUERY_PUK2_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      ctx);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_LAST:
            if (ctx->succeded_requests == 0) {
                g_simple_async_result_set_error (ctx->result,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Could not get any of the SIM unlock retries values. Look above for warning messages");
            } else {
                g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                           g_object_ref (ctx->retries),
                                                           (GDestroyNotify)g_object_unref);
            }

            load_unlock_retries_context_complete_and_free (ctx);
            break;
        default:
            break;
    }
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    LoadUnlockRetriesContext *ctx;

    ctx = g_slice_new0 (LoadUnlockRetriesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_unlock_retries);

    ctx->retries = mm_unlock_retries_new ();
    ctx->step = 0;
    ctx->succeded_requests = 0;

    load_unlock_retries_step (ctx);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              20,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Reset (Modem interface) */

static gboolean
modem_reset_finish (MMIfaceModem *self,
                    GAsyncResult *res,
                    GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
modem_reset (MMIfaceModem *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT#REBOOT",
                              3,
                              FALSE,
                              callback,
                              user_data);
}
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
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
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
