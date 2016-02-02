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
#include "mm-bearer-list.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-mbm.h"
#include "mm-broadband-modem-mbm.h"
#include "mm-broadband-bearer-mbm.h"
#include "mm-sim-mbm.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"

/* sets the interval in seconds on how often the card emits the NMEA sentences */
#define MBM_GPS_NMEA_INTERVAL   "5"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;
static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbm, mm_broadband_modem_mbm, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init))

#define MBM_E2NAP_DISCONNECTED 0
#define MBM_E2NAP_CONNECTED    1
#define MBM_E2NAP_CONNECTING   2

struct _MMBroadbandModemMbmPrivate {
    gboolean have_emrdy;

    GRegex *e2nap_regex;
    GRegex *e2nap_ext_regex;
    GRegex *emrdy_regex;
    GRegex *pacsp_regex;
    GRegex *estksmenu_regex;
    GRegex *estksms_regex;
    GRegex *emwi_regex;
    GRegex *erinfo_regex;

    MMModemLocationSource enabled_sources;

    guint mbm_mode;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBaseBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New MBM bearer created at DBus path '%s'", mm_base_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_mbm_new_ready (GObject *source,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_mbm_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    mm_dbg ("Creating MBM bearer...");
    mm_broadband_bearer_mbm_new (MM_BROADBAND_MODEM_MBM (self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_mbm_new_ready,
                                 result);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_mbm_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New MBM SIM */
    mm_sim_mbm_new (MM_BASE_MODEM (self),
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
    return TRUE;
}

static gboolean
after_sim_unlock_wait_cb (GSimpleAsyncResult *result)
{
    g_simple_async_result_complete (result);
    g_object_unref (result);
    return G_SOURCE_REMOVE;
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
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *_self,
                             GAsyncResult *res,
                             GError **error)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    const gchar *response;
    guint32 mask =  0;
    GArray *combinations;
    MMModemModeCombination mode;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (!mm_mbm_parse_cfun_test (response, &mask, error))
        return FALSE;

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 3);

    /* 2G only */
    if (mask & (1 << MBM_NETWORK_MODE_2G)) {
        mode.allowed = MM_MODEM_MODE_2G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }

    /* 3G only */
    if (mask & (1 << MBM_NETWORK_MODE_3G)) {
        mode.allowed = MM_MODEM_MODE_3G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }

    /* 2G and 3G */
    if (mask & (1 << MBM_NETWORK_MODE_ANY)) {
        mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }

    if (combinations->len == 0) {
        g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't load any supported mode");
        g_array_unref (combinations);
        return NULL;
    }

    return combinations;
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem *_self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    const gchar *response;
    guint a;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (mm_get_uint_from_str (mm_strip_tag (response, "+CFUN:"), &a)) {
        /* No settings to set preferred */
        *preferred = MM_MODEM_MODE_NONE;

        switch (a) {
        case MBM_NETWORK_MODE_OFFLINE:
        case MBM_NETWORK_MODE_LOW_POWER:
            /* Do not update internal mbm_mode */
            *allowed = MM_MODEM_MODE_NONE;
            break;
        case MBM_NETWORK_MODE_2G:
            self->priv->mbm_mode = MBM_NETWORK_MODE_2G;
            *allowed = MM_MODEM_MODE_2G;
            break;
        case MBM_NETWORK_MODE_3G:
            self->priv->mbm_mode = MBM_NETWORK_MODE_3G;
            *allowed = MM_MODEM_MODE_3G;
            break;
        default:
            /* Do not update internal mbm_mode */
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
load_current_modes (MMIfaceModem *self,
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

typedef struct {
    MMBroadbandModemMbm *self;
    GSimpleAsyncResult *result;
    gint mbm_mode;
} SetCurrentModesContext;

static void
set_current_modes_context_complete_and_free (SetCurrentModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (SetCurrentModesContext, ctx);
}

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allowed_mode_update_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           SetCurrentModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
    else {
        /* Cache current allowed mode */
        ctx->self->priv->mbm_mode = ctx->mbm_mode;
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }
    set_current_modes_context_complete_and_free (ctx);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    SetCurrentModesContext *ctx;
    gchar *command;

    ctx = g_slice_new (SetCurrentModesContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_current_modes);
    ctx->mbm_mode = -1;

    if (allowed == MM_MODEM_MODE_2G)
        ctx->mbm_mode = MBM_NETWORK_MODE_2G;
    else if (allowed == MM_MODEM_MODE_3G)
        ctx->mbm_mode = MBM_NETWORK_MODE_3G;
    else if ((allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G) ||
              allowed == MM_MODEM_MODE_ANY) &&
             preferred == MM_MODEM_MODE_NONE)
        ctx->mbm_mode = MBM_NETWORK_MODE_ANY;

    if (ctx->mbm_mode < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Requested mode (allowed: '%s', preferred: '%s') not "
                                         "supported by the modem.",
                                         allowed_str,
                                         preferred_str);
        g_free (allowed_str);
        g_free (preferred_str);

        set_current_modes_context_complete_and_free (ctx);
        return;
    }

    command = g_strdup_printf ("+CFUN=%d", ctx->mbm_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        ctx);
    g_free (command);
}

/*****************************************************************************/
/* Initializing the modem (during first enabling) */

typedef struct {
    GSimpleAsyncResult *result;
    MMBroadbandModemMbm *self;
} EnablingModemInitContext;

static void
enabling_modem_init_context_complete_and_free (EnablingModemInitContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (EnablingModemInitContext, ctx);
}

static gboolean
enabling_modem_init_finish (MMBroadbandModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enabling_init_sequence_ready (MMBaseModem *self,
                              GAsyncResult *res,
                              EnablingModemInitContext *ctx)
{
    /* Ignore errors */
    mm_base_modem_at_sequence_full_finish (self, res, NULL, NULL);
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    enabling_modem_init_context_complete_and_free (ctx);
}

static const MMBaseModemAtCommand enabling_modem_init_sequence[] = {
    /* Init command */
    { "&F", 3, FALSE, NULL },
    /* Ensure disconnected */
    { "*ENAP=0", 3, FALSE, NULL },
    { NULL }
};

static void
run_enabling_init_sequence (EnablingModemInitContext *ctx)
{
    mm_base_modem_at_sequence_full (MM_BASE_MODEM (ctx->self),
                                    mm_base_modem_peek_port_primary (MM_BASE_MODEM (ctx->self)),
                                    enabling_modem_init_sequence,
                                    NULL,  /* response_processor_context */
                                    NULL,  /* response_processor_context_free */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)enabling_init_sequence_ready,
                                    ctx);
}

static void
emrdy_ready (MMBaseModem *self,
             GAsyncResult *res,
             EnablingModemInitContext *ctx)
{
    GError *error = NULL;

    /* EMRDY unsolicited response might have happened between the command
     * submission and the response.  This was seen once:
     *
     * (ttyACM0): --> 'AT*EMRDY?<CR>'
     * (ttyACM0): <-- 'T*EMRD<CR><LF>*EMRDY: 1<CR><LF>Y?'
     *
     * So suppress the warning if the unsolicited handler handled the response
     * before we get here.
     */
    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            mm_warn ("timed out waiting for EMRDY response.");
        else
            ctx->self->priv->have_emrdy = TRUE;
        g_error_free (error);
    }

    run_enabling_init_sequence (ctx);
}

static void
enabling_modem_init (MMBroadbandModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    EnablingModemInitContext *ctx;

    ctx = g_slice_new0 (EnablingModemInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_modem_init);
    ctx->self = g_object_ref (self);

    /* Modem is ready?, no need to check EMRDY */
    if (ctx->self->priv->have_emrdy) {
        run_enabling_init_sequence (ctx);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*EMRDY?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)emrdy_ready,
                              ctx);
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
    /* Use AT+CFUN=4 for power down. It will stop the RF (IMSI detach), and
     * keeps access to the SIM */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=4",
                              3,
                              FALSE,
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

    g_assert (self->priv->mbm_mode == MBM_NETWORK_MODE_ANY ||
              self->priv->mbm_mode == MBM_NETWORK_MODE_2G  ||
              self->priv->mbm_mode == MBM_NETWORK_MODE_3G);

    command = g_strdup_printf ("+CFUN=%u", self->priv->mbm_mode);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              5,
                              FALSE,
                              callback,
                              user_data);
    g_free (command);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

static MMModemPowerState
load_power_state_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    const gchar *response;
    guint a;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (mm_get_uint_from_str (mm_strip_tag (response, "+CFUN:"), &a)) {
        switch (a) {
        case MBM_NETWORK_MODE_OFFLINE:
            return MM_MODEM_POWER_STATE_OFF;

        case MBM_NETWORK_MODE_LOW_POWER:
            return MM_MODEM_POWER_STATE_LOW;

        case MBM_NETWORK_MODE_ANY:
        case MBM_NETWORK_MODE_2G:
        case MBM_NETWORK_MODE_3G:
            return MM_MODEM_POWER_STATE_ON;
        default:
            break;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse +CFUN response: '%s'",
                 response);
    return MM_MODEM_POWER_STATE_UNKNOWN;
}

static void
load_power_state (MMIfaceModem *self,
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
/* Reset (Modem interface) */

static gboolean
reset_finish (MMIfaceModem *self,
              GAsyncResult *res,
              GError **error)
{
    /* Ignore errors */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    return TRUE;
}

static void
reset (MMIfaceModem *self,
       GAsyncReadyCallback callback,
       gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*E2RESET",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Factory reset (Modem interface) */

static gboolean
factory_reset_finish (MMIfaceModem *self,
                      GAsyncResult *res,
                      GError **error)
{
    /* Ignore errors */
    mm_base_modem_at_sequence_finish (MM_BASE_MODEM (self), res, NULL, NULL);
    return TRUE;
}

static const MMBaseModemAtCommand factory_reset_sequence[] = {
    /* Init command */
    { "&F +CMEE=0", 3, FALSE, NULL },
    { "+COPS=0", 3, FALSE, NULL },
    { "+CR=0", 3, FALSE, NULL },
    { "+CRC=0", 3, FALSE, NULL },
    { "+CREG=0", 3, FALSE, NULL },
    { "+CMER=0", 3, FALSE, NULL },
    { "*EPEE=0", 3, FALSE, NULL },
    { "+CNMI=2, 0, 0, 0, 0", 3, FALSE, NULL },
    { "+CGREG=0", 3, FALSE, NULL },
    { "*EIAD=0", 3, FALSE, NULL },
    { "+CGSMS=3", 3, FALSE, NULL },
    { "+CSCA=\"\",129", 3, FALSE, NULL },
    { NULL }
};

static void
factory_reset (MMIfaceModem *self,
               const gchar *code,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    mm_dbg ("Ignoring factory reset code: '%s'", code);

    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               factory_reset_sequence,
                               NULL,  /* response_processor_context */
                               NULL,  /* response_processor_context_free */
                               callback,
                               user_data);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
load_unlock_retries_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMUnlockRetries *unlock_retries;
    const gchar *response;
    gint matched;
    guint a, b, c ,d;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    matched = sscanf (response, "*EPIN: %d, %d, %d, %d",
                      &a, &b, &c, &d);
    if (matched != 4) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Could not parse PIN retries results: '%s'",
                     response);
        return NULL;
    }

    if (a > 998) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Invalid PIN attempts left: '%u'",
                     a);
        return NULL;
    }

    unlock_retries = mm_unlock_retries_new ();
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PIN, a);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PUK, b);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PIN2, c);
    mm_unlock_retries_set (unlock_retries, MM_MODEM_LOCK_SIM_PUK2, d);
    return unlock_retries;
}

static void
load_unlock_retries (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_dbg ("loading unlock retries (mbm)...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*EPIN?",
                              10,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

typedef struct {
    MMBearerConnectionStatus status;
} BearerListReportStatusForeachContext;

static void
bearer_list_report_status_foreach (MMBaseBearer *bearer,
                                   BearerListReportStatusForeachContext *ctx)
{
    mm_base_bearer_report_connection_status (bearer, ctx->status);
}

static void
e2nap_received (MMPortSerialAt *port,
                GMatchInfo *info,
                MMBroadbandModemMbm *self)
{
    MMBearerList *list = NULL;
    guint state;
    BearerListReportStatusForeachContext ctx;

    if (!mm_get_uint_from_match_info (info, 1, &state))
        return;

    ctx.status = MM_BEARER_CONNECTION_STATUS_UNKNOWN;

    switch (state) {
    case MBM_E2NAP_DISCONNECTED:
        mm_dbg ("disconnected");
        ctx.status = MM_BEARER_CONNECTION_STATUS_DISCONNECTED;
        break;
    case MBM_E2NAP_CONNECTED:
        mm_dbg ("connected");
        ctx.status = MM_BEARER_CONNECTION_STATUS_CONNECTED;
        break;
    case MBM_E2NAP_CONNECTING:
        mm_dbg ("connecting");
        break;
    default:
        /* Should not happen */
        mm_dbg ("unhandled E2NAP state %d", state);
    }

    /* If unknown status, don't try to report anything */
    if (ctx.status == MM_BEARER_CONNECTION_STATUS_UNKNOWN)
        return;

    /* If empty bearer list, nothing else to do */
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list)
        return;

    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_report_status_foreach,
                            &ctx);
    g_object_unref (list);
}

static void
erinfo_received (MMPortSerialAt *port,
                 GMatchInfo *info,
                 MMBroadbandModemMbm *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    guint mode;

    if (mm_get_uint_from_match_info (info, 2, &mode)) {
        switch (mode) {
        case 1:
            act = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
            break;
        case 2:
            act = MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
            break;
        default:
            break;
        }
    }

    /* 3G modes take precedence */
    if (mm_get_uint_from_match_info (info, 3, &mode)) {
        switch (mode) {
        case 1:
            act = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
            break;
        case 2:
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
            break;
        case 3:
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
            break;
        default:
            break;
        }
    }

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                               act,
                                               MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemMbm *self,
                                 gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->erinfo_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)erinfo_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->e2nap_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)e2nap_received : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->e2nap_ext_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)e2nap_received : NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else {
        /* Our own setup now */
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MBM (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_setup_unsolicited_events);

    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        result);
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res), TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_cleanup_unsolicited_events);

    /* Our own cleanup first */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MBM (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        result);
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

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    { "*ERINFO=1", 5, FALSE, NULL },
    { "*E2NAP=1",  5, FALSE, NULL },
    { NULL }
};

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
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        unsolicited_enable_sequence,
        NULL,  /* response_processor_context */
        NULL,  /* response_processor_context_free */
        NULL, /* cancellable */
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

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
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

static const MMBaseModemAtCommand unsolicited_disable_sequence[] = {
    { "*ERINFO=0", 5, FALSE, NULL },
    { "*E2NAP=0",  5, FALSE, NULL },
    { NULL }
};

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
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        unsolicited_disable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        result);
}

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

static MMModemLocationSource
location_load_capabilities_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCATION_SOURCE_NONE;

    return (MMModemLocationSource) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMModemLocationSource sources;
    GError *error = NULL;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* not sure how to check if GPS is supported, just allow it */
    if (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

    /* So we're done, complete */
    g_simple_async_result_set_op_res_gpointer (simple,
                                               GUINT_TO_POINTER (sources),
                                               NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
location_load_capabilities (MMIfaceModemLocation *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        location_load_capabilities);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (self,
                                                    (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                    result);
}

/*****************************************************************************/
/* Enable/Disable location gathering (Location interface) */

typedef struct {
    MMBroadbandModemMbm *self;
    GSimpleAsyncResult *result;
    MMModemLocationSource source;
} LocationGatheringContext;

static void
location_gathering_context_complete_and_free (LocationGatheringContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LocationGatheringContext, ctx);
}

/******************************/
/* Disable location gathering */

static gboolean
disable_location_gathering_finish (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
gps_disabled_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    LocationGatheringContext *ctx)
{
    MMPortSerialGps *gps_port;
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        /* Even if we get an error here, we try to close the GPS port */
        gps_port = mm_base_modem_peek_port_gps (self);
        if (gps_port)
            mm_port_serial_close (MM_PORT_SERIAL (gps_port));
    }

    location_gathering_context_complete_and_free (ctx);
}

static void
disable_location_gathering (MMIfaceModemLocation *_self,
                            MMModemLocationSource source,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    gboolean stop_gps = FALSE;
    LocationGatheringContext *ctx;

    ctx = g_slice_new (LocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disable_location_gathering);
    ctx->source = source;

    /* Only stop GPS engine if no GPS-related sources enabled */
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                  MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                  MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        self->priv->enabled_sources &= ~source;

        if (!(self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                             MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            stop_gps = TRUE;
    }

    if (stop_gps) {
        mm_base_modem_at_command_full (MM_BASE_MODEM (_self),
                                       mm_base_modem_peek_port_primary (MM_BASE_MODEM (_self)),
                                       "AT*E2GPSCTL=0",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_disabled_ready,
                                       ctx);
        return;
    }

    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    location_gathering_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

static gboolean
enable_location_gathering_finish (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
gps_enabled_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   LocationGatheringContext *ctx)
{
    GError *error = NULL;
    MMPortSerialGps *gps_port;

    if (!mm_base_modem_at_command_full_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        location_gathering_context_complete_and_free (ctx);
        return;
    }

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        gps_port = mm_base_modem_peek_port_gps (self);
        if (!gps_port ||
            !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
            if (error)
                g_simple_async_result_take_error (ctx->result, error);
            else
                g_simple_async_result_set_error (ctx->result,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Couldn't open raw GPS serial port");
        } else {
            GByteArray *buf;
            const gchar *command = "ATE0*E2GPSNPD\r\n";

            /* We need to send an AT command to the GPS data port to
             * toggle it into this data mode. This is a particularity of
             * mbm cards where the GPS data port is not hard wired. So
             * we need to use the MMPortSerial API here.
             */
            buf = g_byte_array_new ();
            g_byte_array_append (buf, (const guint8 *) command, strlen (command));
            mm_port_serial_command (MM_PORT_SERIAL (gps_port),
                                    buf,
                                    3,
                                    FALSE,
                                    NULL,
                                    NULL,
                                    NULL);
            g_byte_array_unref (buf);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        }

    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    location_gathering_context_complete_and_free (ctx);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        LocationGatheringContext *ctx)
{
    gboolean start_gps = FALSE;
    GError *error = NULL;

    if (!iface_modem_location_parent->enable_location_gathering_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        location_gathering_context_complete_and_free (ctx);
        return;
    }

    /* Now our own enabling */

    /* NMEA and RAW are both enabled in the same way */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                       MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        /* Only start GPS engine if not done already */
        if (!(ctx->self->priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                                  MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            start_gps = TRUE;
        ctx->self->priv->enabled_sources |= ctx->source;
    }

    if (start_gps) {
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                                       "AT*E2GPSCTL=1," MBM_GPS_NMEA_INTERVAL ",0",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_enabled_ready,
                                       ctx);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    location_gathering_context_complete_and_free (ctx);
}

static void
enable_location_gathering (MMIfaceModemLocation *self,
                           MMModemLocationSource source,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    LocationGatheringContext *ctx;

    ctx = g_slice_new (LocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enable_location_gathering);
    ctx->source = source;

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (self,
                                                            source,
                                                            (GAsyncReadyCallback)parent_enable_location_gathering_ready,
                                                            ctx);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
emrdy_received (MMPortSerialAt *port,
                GMatchInfo *info,
                MMBroadbandModemMbm *self)
{
    self->priv->have_emrdy = TRUE;
}

static void
gps_trace_received (MMPortSerialGps *port,
                    const gchar *trace,
                    MMIfaceModemLocation *self)
{
    mm_iface_modem_location_gps_update (self, trace);
}

static void
setup_ports (MMBroadbandModem *_self)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (_self);
    MMPortSerialAt *ports[2];
    MMPortSerialGps *gps_data_port;
    guint i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbm_parent_class)->setup_ports (_self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Setup unsolicited handlers which should be always on */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* The Ericsson modems always have a free AT command port, so we
         * don't need to flash the ports when disconnecting to get back to
         * command mode.  F5521gw R2A07 resets port properties like echo when
         * flashed, leading to confusion.  bgo #650740
         */
        g_object_set (G_OBJECT (ports[i]),
                      MM_PORT_SERIAL_FLASH_OK, FALSE,
                      NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->emrdy_regex,
            (MMPortSerialAtUnsolicitedMsgFn)emrdy_received,
            self,
            NULL);

        /* Several unsolicited messages to always ignore... */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->pacsp_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->estksmenu_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->estksms_regex,
            NULL, NULL, NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->emwi_regex,
            NULL, NULL, NULL);
    }

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MBM (self), FALSE);

    /* NMEA GPS monitoring */
    gps_data_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
    if (gps_data_port) {
        /* make sure GPS is stopped incase it was left enabled */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                                       "AT*E2GPSCTL=0",
                                       3, FALSE, FALSE, NULL, NULL, NULL);
        /* Add handler for the NMEA traces */
        mm_port_serial_gps_add_trace_handler (gps_data_port,
                                              (MMPortSerialGpsTraceFn)gps_trace_received,
                                              self, NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemMbm *
mm_broadband_modem_mbm_new (const gchar *device,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_mbm_init (MMBroadbandModemMbm *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_MBM,
                                              MMBroadbandModemMbmPrivate);

    /* Prepare regular expressions to setup */
    self->priv->e2nap_regex = g_regex_new ("\\r\\n\\*E2NAP: (\\d)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->e2nap_ext_regex = g_regex_new ("\\r\\n\\*E2NAP: (\\d),.*\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->emrdy_regex = g_regex_new ("\\r\\n\\*EMRDY: \\d\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->pacsp_regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->estksmenu_regex = g_regex_new ("\\R\\*ESTKSMENU:.*\\R",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF, G_REGEX_MATCH_NEWLINE_CRLF, NULL);
    self->priv->estksms_regex = g_regex_new ("\\r\\n\\*ESTKSMS:.*\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->emwi_regex = g_regex_new ("\\r\\n\\*EMWI: (\\d),(\\d).*\\r\\n",
                                          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->erinfo_regex = g_regex_new ("\\r\\n\\*ERINFO:\\s*(\\d),(\\d),(\\d).*\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->mbm_mode = MBM_NETWORK_MODE_ANY;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemMbm *self = MM_BROADBAND_MODEM_MBM (object);

    g_regex_unref (self->priv->e2nap_regex);
    g_regex_unref (self->priv->e2nap_ext_regex);
    g_regex_unref (self->priv->emrdy_regex);
    g_regex_unref (self->priv->pacsp_regex);
    g_regex_unref (self->priv->estksmenu_regex);
    g_regex_unref (self->priv->estksms_regex);
    g_regex_unref (self->priv->emwi_regex);
    g_regex_unref (self->priv->erinfo_regex);

    G_OBJECT_CLASS (mm_broadband_modem_mbm_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->reset = reset;
    iface->reset_finish = reset_finish;
    iface->factory_reset = factory_reset;
    iface->factory_reset_finish = factory_reset_finish;
    iface->load_unlock_retries = load_unlock_retries;
    iface->load_unlock_retries_finish = load_unlock_retries_finish;
    iface->load_power_state = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities = location_load_capabilities;
    iface->load_capabilities_finish = location_load_capabilities_finish;
    iface->enable_location_gathering = enable_location_gathering;
    iface->enable_location_gathering_finish = enable_location_gathering_finish;
    iface->disable_location_gathering = disable_location_gathering;
    iface->disable_location_gathering_finish = disable_location_gathering_finish;
}

static void
mm_broadband_modem_mbm_class_init (MMBroadbandModemMbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbmPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
    broadband_modem_class->enabling_modem_init = enabling_modem_init;
    broadband_modem_class->enabling_modem_init_finish = enabling_modem_init_finish;
}
