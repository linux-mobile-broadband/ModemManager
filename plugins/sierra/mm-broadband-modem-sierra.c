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
#include "mm-iface-modem-cdma.h"
#include "mm-common-sierra.h"
#include "mm-broadband-bearer-sierra.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_cdma_init (MMIfaceModemCdma *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSierra, mm_broadband_modem_sierra, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_CDMA, iface_modem_cdma_init))

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
selrat_query_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
    const gchar *response;
    GError *error = NULL;
    GRegex *r = NULL;
    GMatchInfo *match_info = NULL;

    response = mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Example response: !SELRAT: 03, UMTS 3G Preferred */
    r = g_regex_new ("!SELRAT:\\s*(\\d+).*$", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &error)) {
        guint mode;

        if (mm_get_uint_from_match_info (match_info, 1, &mode) &&
            mode >= 0 &&
            mode <= 4) {
            switch (mode) {
            case 0:
                result.allowed = MM_MODEM_MODE_ANY;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 1:
                result.allowed = MM_MODEM_MODE_3G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 2:
                result.allowed = MM_MODEM_MODE_2G;
                result.preferred = MM_MODEM_MODE_NONE;
                break;
            case 3:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_3G;
                break;
            case 4:
                result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
                result.preferred = MM_MODEM_MODE_2G;
                break;
            default:
                g_assert_not_reached ();
                break;
            }
        } else
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse the allowed mode response: '%s'",
                                 response);
    }

    if (error)
        g_simple_async_result_take_error (simple, error);
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
    MMAtSerialPort *primary;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

    if (!mm_iface_modem_is_3gpp (self)) {
        /* Cannot do this in CDMA modems */
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Cannot load allowed modes in CDMA modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot load allowed modes while connected");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   primary,
                                   "!SELRAT?",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_query_ready,
                                   result);
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
selrat_set_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical. */
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMAtSerialPort *primary;
    gint idx = -1;
    gchar *command;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_allowed_modes);

    if (!mm_iface_modem_is_3gpp (self)) {
        /* Cannot do this in CDMA modems */
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Cannot set allowed modes in CDMA modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Sierra secondary ports don't have full AT command interpreters */
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary || mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Cannot set allowed modes while connected");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    if (allowed == MM_MODEM_MODE_3G)
        idx = 1;
    else if (allowed == MM_MODEM_MODE_2G)
        idx = 2;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (preferred == MM_MODEM_MODE_3G)
            idx = 3;
        else if (preferred == MM_MODEM_MODE_2G)
            idx = 4;
        else if (preferred == MM_MODEM_MODE_NONE)
            idx = 0;
    } else if (allowed == MM_MODEM_MODE_ANY)
        idx = 0;

    if (idx < 0) {
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

    command = g_strdup_printf ("!SELRAT=%d", idx);
    mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                   primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)selrat_set_ready,
                                   result);
    g_free (command);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New Sierra bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_sierra_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_sierra_new_finish (res, &error);
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

    mm_dbg ("Creating Sierra bearer...");
    mm_broadband_bearer_sierra_new (MM_BROADBAND_MODEM_SIERRA (self),
                                    properties,
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)broadband_bearer_sierra_new_ready,
                                    result);
}

/*****************************************************************************/
/* Modem power down (Modem interface) */

static gboolean
modem_power_down_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
pcstate_disable_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       GSimpleAsyncResult *simple)
{
    /* Ignore errors for now; we're not sure if all Sierra CDMA devices support
     * at!pcstate.
     */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_common_sierra_modem_power_up);

    /* For CDMA modems, run !pcstate */
    if (mm_iface_modem_is_cdma_only (self)) {
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "!pcstate=0",
                                  5,
                                  FALSE,
                                  (GAsyncReadyCallback)pcstate_disable_ready,
                                  result);
        return;
    }

    /* For 3GPP modems we should call parent's power down, but there is no
     * such power down command in MMBroadbandModem, so just finish here. */
    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Detailed registration state (CDMA interface) */

typedef enum {
    SYS_MODE_UNKNOWN,
    SYS_MODE_NO_SERVICE,
    SYS_MODE_CDMA_1X,
    SYS_MODE_EVDO_REV0,
    SYS_MODE_EVDO_REVA
} SysMode;

typedef struct {
    MMModemCdmaRegistrationState detailed_cdma1x_state;
    MMModemCdmaRegistrationState detailed_evdo_state;
} DetailedRegistrationStateResults;

typedef struct {
    MMBroadbandModemSierra *self;
    GSimpleAsyncResult *result;
    DetailedRegistrationStateResults state;
} DetailedRegistrationStateContext;

static void
detailed_registration_state_context_complete_and_free (DetailedRegistrationStateContext *ctx)
{
    /* Always not in idle! we're passing a struct in stack as result */
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
get_detailed_registration_state_finish (MMIfaceModemCdma *self,
                                        GAsyncResult *res,
                                        MMModemCdmaRegistrationState *detailed_cdma1x_state,
                                        MMModemCdmaRegistrationState *detailed_evdo_state,
                                        GError **error)
{
    DetailedRegistrationStateResults *results;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *detailed_cdma1x_state = results->detailed_cdma1x_state;
    *detailed_evdo_state = results->detailed_evdo_state;
    return TRUE;
}

#define MODEM_REG_TAG "Modem has registered"
#define GENERIC_ROAM_TAG "Roaming:"
#define ROAM_1X_TAG "1xRoam:"
#define ROAM_EVDO_TAG "HDRRoam:"
#define SYS_MODE_TAG "Sys Mode:"
#define SYS_MODE_NO_SERVICE_TAG "NO SRV"
#define SYS_MODE_EVDO_TAG "HDR"
#define SYS_MODE_1X_TAG "1x"
#define SYS_MODE_CDMA_TAG "CDMA"
#define EVDO_REV_TAG "HDR Revision:"
#define SID_TAG "SID:"

static gboolean
get_roam_value (const gchar *reply,
                const gchar *tag,
                gboolean is_eri,
                gboolean *out_roaming)
{
    gchar *p;
    gboolean success;
    guint32 ind = 0;

    p = strstr (reply, tag);
    if (!p)
        return FALSE;

    p += strlen (tag);
    while (*p && isspace (*p))
        p++;

    /* Use generic ERI parsing if it's an ERI */
    if (is_eri) {
        success = mm_cdma_parse_eri (p, out_roaming, &ind, NULL);
        if (success) {
            /* Sierra redefines ERI 0, 1, and 2 */
            if (ind == 0)
                *out_roaming = FALSE;  /* home */
            else if (ind == 1 || ind == 2)
                *out_roaming = TRUE;   /* roaming */
        }
        return success;
    }

    /* If it's not an ERI, roaming is just true/false */
    if (*p == '1') {
        *out_roaming = TRUE;
        return TRUE;
    } else if (*p == '0') {
        *out_roaming = FALSE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
sys_mode_has_service (SysMode mode)
{
    return (   mode == SYS_MODE_CDMA_1X
            || mode == SYS_MODE_EVDO_REV0
            || mode == SYS_MODE_EVDO_REVA);
}

static gboolean
sys_mode_is_evdo (SysMode mode)
{
    return (mode == SYS_MODE_EVDO_REV0 || mode == SYS_MODE_EVDO_REVA);
}

static void
status_ready (MMIfaceModemCdma *self,
              GAsyncResult *res,
              DetailedRegistrationStateContext *ctx)
{
    GError *error = NULL;
    const gchar *response;
    gchar **lines;
    gchar **iter;
    gboolean registered = FALSE;
    gboolean have_sid = FALSE;
    SysMode evdo_mode = SYS_MODE_UNKNOWN;
    SysMode sys_mode = SYS_MODE_UNKNOWN;
    gboolean evdo_roam = FALSE, cdma1x_roam = FALSE;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    /* If error, leave superclass' reg state alone if AT!STATUS isn't supported. */
    if (error) {
        g_error_free (error);

        /* NOTE: always complete NOT in idle here */
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    lines = g_strsplit_set (response, "\n\r", 0);
    if (!lines) {
        /* NOTE: always complete NOT in idle here */
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
        detailed_registration_state_context_complete_and_free (ctx);
        return;
    }

    /* Sierra CDMA parts have two general formats depending on whether they
     * support EVDO or not.  EVDO parts report both 1x and EVDO roaming status
     * while of course 1x parts only report 1x status.  Some modems also do not
     * report the Roaming information (MP 555 GPS).
     *
     * AT!STATUS responses:
     *
     * Unregistered MC5725:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0  NID: 0  1xRoam: 0 HDRRoam: 0
     * Temp: 33  State: 100  Sys Mode: NO SRV
     * Pilot NOT acquired
     * Modem has NOT registered
     *
     * Registered MC5725:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 775
     * SID: 30  NID: 2  1xRoam: 0 HDRRoam: 0
     * Temp: 29  State: 200  Sys Mode: HDR
     * Pilot acquired
     * Modem has registered
     * HDR Revision: A
     *
     * Unregistered AC580:
     * -----------------------
     * Current band: PCS CDMA
     * Current channel: 350
     * SID: 0 NID: 0  Roaming: 0
     * Temp: 39  State: 100  Scan Mode: 0
     * Pilot NOT acquired
     * Modem has NOT registered
     *
     * Registered AC580:
     * -----------------------
     * Current band: Cellular Sleep
     * Current channel: 548
     * SID: 26  NID: 1  Roaming: 1
     * Temp: 39  State: 200  Scan Mode: 0
     * Pilot Acquired
     * Modem has registered
     */

    /* We have to handle the two formats slightly differently; for newer formats
     * with "Sys Mode", we consider the modem registered if the Sys Mode is not
     * "NO SRV".  The explicit registration status is just icing on the cake.
     * For older formats (no "Sys Mode") we treat the modem as registered if
     * the SID is non-zero.
     */

    for (iter = lines; iter && *iter; iter++) {
        gboolean bool_val = FALSE;
        char *p;

        if (!strncmp (*iter, MODEM_REG_TAG, strlen (MODEM_REG_TAG))) {
            registered = TRUE;
            continue;
        }

        /* Roaming */
        get_roam_value (*iter, ROAM_1X_TAG, TRUE, &cdma1x_roam);
        get_roam_value (*iter, ROAM_EVDO_TAG, TRUE, &evdo_roam);
        if (get_roam_value (*iter, GENERIC_ROAM_TAG, FALSE, &bool_val))
            cdma1x_roam = evdo_roam = bool_val;

        /* Current system mode */
        p = strstr (*iter, SYS_MODE_TAG);
        if (p) {
            p += strlen (SYS_MODE_TAG);
            while (*p && isspace (*p))
                p++;
            if (!strncmp (p, SYS_MODE_NO_SERVICE_TAG, strlen (SYS_MODE_NO_SERVICE_TAG)))
                sys_mode = SYS_MODE_NO_SERVICE;
            else if (!strncmp (p, SYS_MODE_EVDO_TAG, strlen (SYS_MODE_EVDO_TAG)))
                sys_mode = SYS_MODE_EVDO_REV0;
            else if (   !strncmp (p, SYS_MODE_1X_TAG, strlen (SYS_MODE_1X_TAG))
                     || !strncmp (p, SYS_MODE_CDMA_TAG, strlen (SYS_MODE_CDMA_TAG)))
                sys_mode = SYS_MODE_CDMA_1X;
        }

        /* Current EVDO revision if system mode is EVDO */
        p = strstr (*iter, EVDO_REV_TAG);
        if (p) {
            p += strlen (EVDO_REV_TAG);
            while (*p && isspace (*p))
                p++;
            if (*p == 'A')
                evdo_mode = SYS_MODE_EVDO_REVA;
            else if (*p == '0')
                evdo_mode = SYS_MODE_EVDO_REV0;
        }

        /* SID */
        p = strstr (*iter, SID_TAG);
        if (p) {
            p += strlen (SID_TAG);
            while (*p && isspace (*p))
                p++;
            if (isdigit (*p) && (*p != '0'))
                have_sid = TRUE;
        }
    }

    /* Update current system mode */
    if (sys_mode_is_evdo (sys_mode)) {
        /* Prefer the explicit EVDO mode from EVDO_REV_TAG */
        if (evdo_mode != SYS_MODE_UNKNOWN)
            sys_mode = evdo_mode;
    }

    /* If the modem didn't report explicit registration with "Modem has
     * registered" then get registration status by looking at either system
     * mode or (for older devices that don't report that) just the SID.
     */
    if (!registered) {
        if (sys_mode != SYS_MODE_UNKNOWN)
            registered = sys_mode_has_service (sys_mode);
        else
            registered = have_sid;
    }

    if (registered) {
        ctx->state.detailed_cdma1x_state = (cdma1x_roam ?
                                            MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                            MM_MODEM_CDMA_REGISTRATION_STATE_HOME);

        if (sys_mode_is_evdo (sys_mode)) {
            ctx->state.detailed_evdo_state = (evdo_roam ?
                                              MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING :
                                              MM_MODEM_CDMA_REGISTRATION_STATE_HOME);
        } else {
            ctx->state.detailed_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        }
    } else {
        /* Not registered */
        ctx->state.detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        ctx->state.detailed_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    }

    /* NOTE: always complete NOT in idle here */
    g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->state, NULL);
    detailed_registration_state_context_complete_and_free (ctx);
}

static void
get_detailed_registration_state (MMIfaceModemCdma *self,
                                 MMModemCdmaRegistrationState cdma1x_state,
                                 MMModemCdmaRegistrationState evdo_state,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedRegistrationStateContext *ctx;

    /* Setup context */
    ctx = g_new0 (DetailedRegistrationStateContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             get_detailed_registration_state);
    ctx->state.detailed_cdma1x_state = cdma1x_state;
    ctx->state.detailed_evdo_state = evdo_state;

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "!STATUS",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)status_ready,
                              ctx);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_sierra_parent_class)->setup_ports (self);

    mm_common_sierra_setup_ports (self);
}

/*****************************************************************************/

MMBroadbandModemSierra *
mm_broadband_modem_sierra_new (const gchar *device,
                               const gchar **drivers,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIERRA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
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
    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->modem_power_up = mm_common_sierra_modem_power_up;
    iface->modem_power_up_finish = mm_common_sierra_modem_power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->create_sim = mm_common_sierra_create_sim;
    iface->create_sim_finish = mm_common_sierra_create_sim_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

static void
iface_modem_cdma_init (MMIfaceModemCdma *iface)
{
    iface->get_detailed_registration_state = get_detailed_registration_state;
    iface->get_detailed_registration_state_finish = get_detailed_registration_state_finish;
}

static void
mm_broadband_modem_sierra_class_init (MMBroadbandModemSierraClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
