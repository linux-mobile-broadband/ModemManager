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

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemTelit, mm_broadband_modem_telit, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

/*****************************************************************************/
/* Set current bands (Modem interface) */

static gboolean
modem_set_current_bands_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_set_current_bands_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_set_current_bands (MMIfaceModem *self,
                         GArray *bands_array,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    gchar *cmd;
    gint flag2g;
    gint flag3g;
    gint flag4g;
    gboolean is_2g;
    gboolean is_3g;
    gboolean is_4g;
    GSimpleAsyncResult *res;

    mm_telit_get_band_flag (bands_array, &flag2g, &flag3g, &flag4g);

    is_2g = mm_iface_modem_is_2g (self);
    is_3g = mm_iface_modem_is_3g (self);
    is_4g = mm_iface_modem_is_4g (self);

    if (is_2g && flag2g == -1) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "None or invalid 2G bands combination in the provided list");
        return;
    }

    if (is_3g && flag3g == -1) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "None or invalid 3G bands combination in the provided list");
        return;
    }

    if (is_4g && flag4g == -1) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "None or invalid 4G bands combination in the provided list");
        return;
    }

    cmd = NULL;
    if (is_2g && !is_3g && !is_4g)
        cmd = g_strdup_printf ("AT#BND=%d", flag2g);
    else if (is_2g && is_3g && !is_4g)
        cmd = g_strdup_printf ("AT#BND=%d,%d", flag2g, flag3g);
    else if (is_2g && is_3g && is_4g)
        cmd = g_strdup_printf ("AT#BND=%d,%d,%d", flag2g, flag3g, flag4g);
    else if (!is_2g && !is_3g && is_4g)
        cmd = g_strdup_printf ("AT#BND=0,0,%d", flag4g);
    else if (!is_2g && is_3g && is_4g)
        cmd = g_strdup_printf ("AT#BND=0,%d,%d", flag3g, flag4g);
    else if (is_2g && !is_3g && is_4g)
        cmd = g_strdup_printf ("AT#BND=%d,0,%d", flag2g, flag4g);
    else {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Unexpectd error: could not compose AT#BND command");
        return;
    }
    res = g_simple_async_result_new (G_OBJECT (self),
                                     callback,
                                     user_data,
                                     modem_set_current_bands);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              20,
                              FALSE,
                              (GAsyncReadyCallback)modem_set_current_bands_ready,
                              res);
    g_free (cmd);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

typedef struct {
    MMIfaceModem *self;
    GSimpleAsyncResult *result;
    gboolean mm_modem_is_2g;
    gboolean mm_modem_is_3g;
    gboolean mm_modem_is_4g;
    MMTelitLoadBandsType band_type;
} LoadBandsContext;

static void
load_bands_context_complete_and_free (LoadBandsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LoadBandsContext, ctx);
}

static GArray *
modem_load_bands_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

static void
load_bands_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  LoadBandsContext *ctx)
{
    const gchar *response;
    GError *error = NULL;
    GArray *bands = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    if (!response)
        g_simple_async_result_take_error (ctx->result, error);
    else if (!mm_telit_parse_bnd_response (response,
                                           ctx->mm_modem_is_2g,
                                           ctx->mm_modem_is_3g,
                                           ctx->mm_modem_is_4g,
                                           ctx->band_type,
                                           &bands,
                                           &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gpointer (ctx->result, bands, (GDestroyNotify)g_array_unref);

    load_bands_context_complete_and_free(ctx);
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    LoadBandsContext *ctx;

    ctx = g_slice_new0 (LoadBandsContext);

    ctx->self = g_object_ref (self);
    ctx->mm_modem_is_2g = mm_iface_modem_is_2g (ctx->self);
    ctx->mm_modem_is_3g = mm_iface_modem_is_3g (ctx->self);
    ctx->mm_modem_is_4g = mm_iface_modem_is_4g (ctx->self);
    ctx->band_type = LOAD_CURRENT_BANDS;

    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_current_bands);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_bands_ready,
                              ctx);
}


/*****************************************************************************/
/* Load supported bands (Modem interface) */

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadBandsContext *ctx;

    ctx = g_slice_new0 (LoadBandsContext);

    ctx->self = g_object_ref (self);
    ctx->mm_modem_is_2g = mm_iface_modem_is_2g (ctx->self);
    ctx->mm_modem_is_3g = mm_iface_modem_is_3g (ctx->self);
    ctx->mm_modem_is_4g = mm_iface_modem_is_4g (ctx->self);
    ctx->band_type = LOAD_SUPPORTED_BANDS;

    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_load_supported_bands);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND=?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_bands_ready,
                              ctx);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface)
 *
 * NOTE: the logic must make sure that LOAD_UNLOCK_RETRIES_STEP_UNLOCK is always
 * run if LOAD_UNLOCK_RETRIES_STEP_LOCK has been run. Currently, the logic just
 * runs all intermediate steps ignoring errors (i.e. not completing the
 * operation if something fails), so the LOAD_UNLOCK_RETRIES_STEP_UNLOCK is
 * always run.
 */

#define CSIM_LOCK_STR               "+CSIM=1"
#define CSIM_UNLOCK_STR             "+CSIM=0"
#define CSIM_QUERY_PIN_RETRIES_STR  "+CSIM=10,0020000100"
#define CSIM_QUERY_PUK_RETRIES_STR  "+CSIM=10,002C000100"
#define CSIM_QUERY_PIN2_RETRIES_STR "+CSIM=10,0020008100"
#define CSIM_QUERY_PUK2_RETRIES_STR "+CSIM=10,002C008100"
#define CSIM_QUERY_TIMEOUT 3

typedef enum {
    LOAD_UNLOCK_RETRIES_STEP_FIRST,
    LOAD_UNLOCK_RETRIES_STEP_LOCK,
    LOAD_UNLOCK_RETRIES_STEP_PIN,
    LOAD_UNLOCK_RETRIES_STEP_PUK,
    LOAD_UNLOCK_RETRIES_STEP_PIN2,
    LOAD_UNLOCK_RETRIES_STEP_PUK2,
    LOAD_UNLOCK_RETRIES_STEP_UNLOCK,
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
csim_unlock_ready (MMBaseModem              *self,
                   GAsyncResult             *res,
                   LoadUnlockRetriesContext *ctx)
{
    const gchar *response;
    GError      *error = NULL;

    /* Ignore errors */
    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_warn ("Couldn't unlock SIM card: %s", error->message);
        g_error_free (error);
    }

    ctx->step++;
    load_unlock_retries_step (ctx);
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

    if ( (unlock_retries = mm_telit_parse_csim_response (ctx->step, response, &error)) < 0) {
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
            g_assert_not_reached ();
            break;
    }

next_step:
    ctx->step++;
    load_unlock_retries_step (ctx);
}

static void
csim_lock_ready (MMBaseModem              *self,
                 GAsyncResult             *res,
                 LoadUnlockRetriesContext *ctx)
{
    const gchar *response;
    GError      *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_prefix_error (&error, "Couldn't lock SIM card: ");
        g_simple_async_result_take_error (ctx->result, error);
        load_unlock_retries_context_complete_and_free (ctx);
        return;
    }

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
        case LOAD_UNLOCK_RETRIES_STEP_LOCK:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_LOCK_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_lock_ready,
                                      ctx);
            break;
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
        case LOAD_UNLOCK_RETRIES_STEP_UNLOCK:
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      CSIM_UNLOCK_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_unlock_ready,
                                      ctx);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_LAST:
            if (ctx->succeded_requests == 0) {
                g_simple_async_result_set_error (ctx->result,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Could not get any of the SIM unlock retries values");
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
    ctx->step = LOAD_UNLOCK_RETRIES_STEP_FIRST;
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
        case 4:
            if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self)))
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_LTE);
            else
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
            return TRUE;
        case 5:
            if (mm_iface_modem_is_3gpp_lte (MM_IFACE_MODEM (self))) {
                *result = g_variant_new_uint32 (MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
                return TRUE;
            }
            /* Fall-through since #PSNT: 5 is not supported in other than lte modems */
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
    const gchar *service;

    if (error) {
        /* Ignore AT errors (ie, ERROR or CMx ERROR) */
        if (error->domain != MM_MOBILE_EQUIPMENT_ERROR || last_command)
            *result_error = g_error_copy (error);
        return FALSE;
    }

    service = mm_strip_tag (response, "+SERVICE:");
    if (service) {
        switch (atoi (service)) {
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
/* Load current mode (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    const gchar *response;
    const gchar *str;
    gint a;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    str = mm_strip_tag (response, "+WS46: ");

    if (!sscanf (str, "%d", &a)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse +WS46 response: '%s'",
                     response);
        return FALSE;
    }

    *preferred = MM_MODEM_MODE_NONE;
    switch (a) {
    case 12:
        *allowed = MM_MODEM_MODE_2G;
        return TRUE;
    case 22:
        *allowed = MM_MODEM_MODE_3G;
        return TRUE;
    case 25:
        if (mm_iface_modem_is_3gpp_lte (self))
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        else
            *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        return TRUE;
    case 28:
        *allowed = MM_MODEM_MODE_4G;
        return TRUE;
    case 29:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        return TRUE;
    case 30:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
        return TRUE;
    case 31:
        *allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        return TRUE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse unexpected +WS46 response: '%s'",
                 response);
    return FALSE;
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+WS46?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set current modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
ws46_set_ready (MMIfaceModem *self,
                GAsyncResult *res,
                GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);
    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *command;
    gint ws46_mode = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_current_modes);

    if (allowed == MM_MODEM_MODE_2G)
        ws46_mode = 12;
    else if (allowed == MM_MODEM_MODE_3G)
        ws46_mode = 22;
    else if (allowed == MM_MODEM_MODE_4G)
        ws46_mode = 28;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (mm_iface_modem_is_3gpp_lte (self))
            ws46_mode = 29;
        else
            ws46_mode = 25;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G))
        ws46_mode = 30;
    else if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G))
        ws46_mode = 31;
    else if (allowed == (MM_MODEM_MODE_2G  | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G) ||
             allowed == MM_MODEM_MODE_ANY)
        ws46_mode = 25;

    /* Telit modems do not support preferred mode selection */
    if ((ws46_mode < 0) || (preferred != MM_MODEM_MODE_NONE)) {
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

    command = g_strdup_printf ("AT+WS46=%d", ws46_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)ws46_set_ready,
        result);
    g_free (command);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_array_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* CDMA-only modems don't support changing modes, default to parent's */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_op_res_gpointer (simple, all, (GDestroyNotify) g_array_unref);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* Build list of combinations for 3GPP devices */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 7);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 4G only */
    mode.allowed = MM_MODEM_MODE_4G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 4G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G and 4G */
    mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G, 3G and 4G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    /* Filter out those unsupported modes */
    filtered = mm_filter_supported_modes (all, combinations);
    g_array_unref (all);
    g_array_unref (combinations);

    g_simple_async_result_set_op_res_gpointer (simple, filtered, (GDestroyNotify) g_array_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   load_supported_modes));
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
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->set_current_bands = modem_set_current_bands;
    iface->set_current_bands_finish = modem_set_current_bands_finish;
    iface->load_current_bands = modem_load_current_bands;
    iface->load_current_bands_finish = modem_load_bands_finish;
    iface->load_supported_bands = modem_load_supported_bands;
    iface->load_supported_bands_finish = modem_load_bands_finish;
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
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
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
