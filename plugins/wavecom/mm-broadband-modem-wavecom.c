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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <libmm-common.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-wavecom.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemWavecom, mm_broadband_modem_wavecom, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

#define WAVECOM_MS_CLASS_CC_IDSTR "\"CC\""
#define WAVECOM_MS_CLASS_CG_IDSTR "\"CG\""
#define WAVECOM_MS_CLASS_B_IDSTR  "\"B\""
#define WAVECOM_MS_CLASS_A_IDSTR  "\"A\""

/*****************************************************************************/
/* Supported modes (Modem interface) */

static MMModemMode
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_MODE_NONE;

    return (MMModemMode) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                               G_SIMPLE_ASYNC_RESULT (res)));
}

static void
supported_ms_classes_query_ready (MMBaseModem *self,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;
    MMModemMode mode;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");
    mode = MM_MODEM_MODE_NONE;

    if (strstr (response, WAVECOM_MS_CLASS_A_IDSTR)) {
        mm_dbg ("Modem supports Class A mobile station");
        mode |= MM_MODEM_MODE_3G;
    }

    if (strstr (response, WAVECOM_MS_CLASS_B_IDSTR)) {
        mm_dbg ("Modem supports Class B mobile station");
        mode |= (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
    }

    if (strstr (response, WAVECOM_MS_CLASS_CG_IDSTR)) {
        mm_dbg ("Modem supports Class CG mobile station");
        mode |= MM_MODEM_MODE_2G;
    }

    if (strstr (response, WAVECOM_MS_CLASS_CC_IDSTR)) {
        mm_dbg ("Modem supports Class CC mobile station");
        mode |= MM_MODEM_MODE_CS;
    }

    /* If none received, error */
    if (mode == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get supported mobile station classes: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (mode),
                                                   NULL);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_supported_modes);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CGCLASS=?",
        3,
        FALSE,
        (GAsyncReadyCallback)supported_ms_classes_query_ready,
        result);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadAllowedModesResult;

static gboolean
load_allowed_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadAllowedModesResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
wwsm_read_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    LoadAllowedModesResult result;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    result.allowed = MM_MODEM_MODE_NONE;
    result.preferred = MM_MODEM_MODE_NONE;

    /* Possible responses:
     *   +WWSM: 0    (2G only)
     *   +WWSM: 1    (3G only)
     *   +WWSM: 2,0  (Any)
     *   +WWSM: 2,1  (2G preferred)
     *   +WWSM: 2,2  (3G preferred)
     */
    r = g_regex_new ("\\r\\n\\+WWSM: ([0-2])(,([0-2]))?.*$", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        guint allowed = 0;

        if (mm_get_uint_from_match_info (match_info, 1, &allowed)) {
            switch (allowed) {
            case 0:
                result.allowed = MM_MODEM_MODE_2G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 1:
                result.allowed = MM_MODEM_MODE_3G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 2: {
                guint preferred = 0;

                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);

                /* 3, to avoid the comma */
                if (mm_get_uint_from_match_info (match_info, 3, &preferred)) {
                    switch (preferred) {
                    case 0:
                        result.preferred = MM_MODEM_MODE_NONE;
                        break;
                    case 1:
                        result.preferred = MM_MODEM_MODE_2G;
                        break;
                    case 2:
                        result.preferred = MM_MODEM_MODE_3G;
                        break;
                    default:
                        g_warn_if_reached ();
                        break;
                    }
                }
                break;
            }
            default:
                g_warn_if_reached ();
                break;
            }
        }
    }

    if (result.allowed == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Unknown wireless data service reply: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (simple, &result, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);

    g_regex_unref (r);
    if (match_info)
        g_match_info_free (match_info);
}

static void
current_ms_class_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CGCLASS:");

    if (strncmp (response,
                 WAVECOM_MS_CLASS_A_IDSTR,
                 strlen (WAVECOM_MS_CLASS_A_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class A mobile station");
        /* For 3G devices, query WWSM status */
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "+WWSM?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)wwsm_read_ready,
                                  simple);
        return;
    }

    result.allowed = MM_MODEM_MODE_NONE;
    result.preferred = MM_MODEM_MODE_NONE;

    if (strncmp (response,
                 WAVECOM_MS_CLASS_B_IDSTR,
                 strlen (WAVECOM_MS_CLASS_B_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class B mobile station");
        result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS);
        result.preferred = MM_MODEM_MODE_2G;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CG_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CG_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CG mobile station");
        result.allowed = MM_MODEM_MODE_2G;
        result.preferred = MM_MODEM_MODE_NONE;
    } else if (strncmp (response,
                        WAVECOM_MS_CLASS_CC_IDSTR,
                        strlen (WAVECOM_MS_CLASS_CC_IDSTR)) == 0) {
        mm_dbg ("Modem configured as a Class CC mobile station");
        result.allowed = MM_MODEM_MODE_CS;
        result.preferred = MM_MODEM_MODE_NONE;
    }

    if (result.allowed == MM_MODEM_MODE_NONE)
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Unknown mobile station class: '%s'",
                                         response);
    else
        g_simple_async_result_set_op_res_gpointer (simple, &result, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_allowed_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGCLASS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)current_ms_class_ready,
                              result);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    MMBroadbandModemWavecom *self;
    GSimpleAsyncResult *result;
    MMModemMode allowed;
    MMModemMode preferred;
    gchar *cgclass_command;
    gchar *wwsm_command;
} SetAllowedModesContext;

static void
set_allowed_modes_context_complete_and_free (SetAllowedModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx->cgclass_command);
    g_free (ctx->wwsm_command);
    g_free (ctx);
}

static gboolean
set_allowed_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
wwsm_update_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   SetAllowedModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    set_allowed_modes_context_complete_and_free (ctx);
}

static void
cgclass_update_ready (MMBaseModem *self,
                      GAsyncResult *res,
                      SetAllowedModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    if (!ctx->wwsm_command) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->wwsm_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)wwsm_update_ready,
                              ctx);
}

static void
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    SetAllowedModesContext *ctx;

    ctx = g_new (SetAllowedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_allowed_modes);
    ctx->allowed = allowed;
    ctx->preferred = preferred;

    if (allowed == MM_MODEM_MODE_CS)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CC_IDSTR);
    else if (allowed == MM_MODEM_MODE_2G)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_CG_IDSTR);
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_CS) &&
             preferred == MM_MODEM_MODE_NONE)
        ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_B_IDSTR);
    else if (allowed & MM_MODEM_MODE_3G) {
        if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G) ||
            allowed == (MM_MODEM_MODE_CS | MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
            if (preferred == MM_MODEM_MODE_2G)
                ctx->wwsm_command = g_strdup ("+WWSM=2,1");
            else if (preferred == MM_MODEM_MODE_3G)
                ctx->wwsm_command = g_strdup ("+WWSM=2,2");
            else if (preferred == MM_MODEM_MODE_NONE)
                ctx->wwsm_command = g_strdup ("+WWSM=2,0");
        } else if (allowed == MM_MODEM_MODE_3G)
            ctx->wwsm_command = g_strdup ("+WWSM=1");

        if (ctx->wwsm_command)
            ctx->cgclass_command = g_strdup ("+CGCLASS=" WAVECOM_MS_CLASS_A_IDSTR);
    }

    if (!ctx->cgclass_command) {
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

        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->cgclass_command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)cgclass_update_ready,
                              ctx);
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
    MMModemAccessTechnology act;
    const gchar *p;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    p = mm_strip_tag (response, "+WGPRSIND:");
    if (p) {
        switch (*p) {
        case '1':
            /* GPRS only */
            act = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
            break;
        case '2':
            /* EGPRS/EDGE supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
            break;
        case '3':
            /* 3G R99 supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
            break;
        case '4':
            /* HSDPA supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
            break;
        case '5':
            /* HSUPA supported */
            act = MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
            break;
        default:
            break;
        }
    }

    if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse access technologies result: '%s'",
                     response);
        return FALSE;
    }

    /* We are reporting ALL 3GPP access technologies here */
    *access_technologies = act;
    *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    return TRUE;
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+WGPRS=9,2",
                              3,
                              FALSE,
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
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    /* Wavecom doesn't have XOFF/XON flow control, so we enable RTS/CTS */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+IFC=2,2",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power up (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
full_functionality_status_ready (MMBaseModem *self,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
get_current_functionality_status_ready (MMBaseModem *self,
                                        GAsyncResult *res,
                                        GSimpleAsyncResult *simple)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        mm_warn ("Failed checking if power-up command is needed: '%s'. "
                 "Will assume it isn't.",
                 error->message);
        g_error_free (error);
        /* On error, just assume we don't need the power-up command */
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    response = mm_strip_tag (response, "+CFUN:");
    if (response && *response == '1') {
        /* If reported functionality status is '1', then we do not need to
         * issue the power-up command. Otherwise, do it. */
        mm_dbg ("Already in full functionality status, skipping power-up command");
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    mm_warn ("Not in full functionality status, power-up command is needed. "
             "Note that it may reboot the modem.");

    /* Try to go to full functionality mode without rebooting the system.
     * Works well if we previously switched off the power with CFUN=4
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1,0",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)full_functionality_status_ready,
                              simple);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_power_up);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)get_current_functionality_status_ready,
                              result);
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

MMBroadbandModemWavecom *
mm_broadband_modem_wavecom_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_WAVECOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_wavecom_init (MMBroadbandModemWavecom *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
}

static void
mm_broadband_modem_wavecom_class_init (MMBroadbandModemWavecomClass *klass)
{
}
