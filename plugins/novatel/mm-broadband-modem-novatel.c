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
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-messaging.h"
#include "mm-broadband-modem-novatel.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);

static MMIfaceModem *iface_modem_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init));

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

    /* When a valid result is given, we never complete in idle */
    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
nwrat_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    LoadAllowedModesResult result;
    GError *error = NULL;
    const gchar *response;
    GRegex *r;
    GMatchInfo *match_info;
    gint a = -1;
    gint b = -1;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Parse response */
    r = g_regex_new ("\\$NWRAT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &error)) {
        g_prefix_error (&error,
                        "Failed to parse mode/tech response '%s': ",
                        response);
        g_regex_unref (r);
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &a) ||
        !mm_get_int_from_match_info (match_info, 2, &b) ||
        a < 0 || a > 2 ||
        b < 1 || b > 2) {
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse mode/tech response '%s': invalid modes reported",
            response);
        g_match_info_free (match_info);
        g_regex_unref (r);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    switch (a) {
    case 0:
        result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        result.preferred = MM_MODEM_MODE_NONE;
        break;
    case 1:
        if (b == 1) {
            result.allowed = MM_MODEM_MODE_2G;
            result.preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result.preferred = MM_MODEM_MODE_2G;
        }
        break;
    case 2:
        if (b == 1) {
            result.allowed = MM_MODEM_MODE_3G;
            result.preferred = MM_MODEM_MODE_NONE;
        } else /* b == 2 */ {
            result.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result.preferred = MM_MODEM_MODE_3G;
        }
        break;
    default:
        /* We only allow mode 0|1|2 */
        g_assert_not_reached ();
        break;
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    /* When a valid result is given, we never complete in idle */
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

    /* Load allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Loading allowed modes not supported in CDMA-only modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "$NWRAT?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)nwrat_query_ready,
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
allowed_mode_update_ready (MMBroadbandModemNovatel *self,
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
set_allowed_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *command;
    gint a = -1;
    gint b = -1;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        set_allowed_modes);

    /* Setting allowed modes only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Setting allowed modes not supported in CDMA-only modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    if (allowed == MM_MODEM_MODE_2G) {
        a = 1;
        b = 1;
    } else if (allowed == MM_MODEM_MODE_3G) {
        a = 2;
        b = 1;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        b = 2;
        if (preferred == MM_MODEM_MODE_NONE)
            a = 0;
        else if (preferred == MM_MODEM_MODE_2G)
            a = 1;
        else if (preferred == MM_MODEM_MODE_3G)
            a = 2;
    }

    if (a < 0 || b < 0) {
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

    command = g_strdup_printf ("AT$NWRAT=%d,%d", a, b);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        result);
    g_free (command);
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
modem_load_access_technologies_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       MMModemAccessTechnology *access_technologies,
                                       guint *mask,
                                       GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    /* Remember we're only giving 3GPP access technologies */
    *access_technologies = GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
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
    const gchar *p;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    p = mm_strip_tag (response, "$CNTI:");
    p = strchr (p, ',');
    if (p) {
        g_simple_async_result_set_op_res_gpointer (
            simple,
            GUINT_TO_POINTER (mm_string_to_access_tech (p)),
            NULL);
    } else {
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't parse $CNTI result '%s'",
            response);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_access_technologies (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_access_technologies);

    /* Load access technologies only in 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNSUPPORTED,
            "Loading access technologies not supported in CDMA-only modems");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$CNTI=0",
        3,
        FALSE,
        (GAsyncReadyCallback)cnti_set_ready,
        result);
}

/*****************************************************************************/
/* Signal quality loading (Modem interface) */

static guint
modem_load_signal_quality_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return 0;

    return GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                 G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_signal_quality_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    guint signal_quality;

    signal_quality = iface_modem_parent->load_signal_quality_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (signal_quality),
                                                   NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gint
get_one_quality (const gchar *reply,
                 const gchar *tag)
{
    gint quality = -1;
    const gchar *p;
    gint dbm;
    gboolean success = FALSE;

    p = strstr (reply, tag);
    if (!p)
        return -1;

    /* Skip the tag */
    p += strlen (tag);

    /* Skip spaces */
    while (isspace (*p))
        p++;

    if (mm_get_int_from_str (p, &dbm)) {
        if (*p == '-') {
            /* Some cards appear to use RX0/RX1 and output RSSI in negative dBm */
            if (dbm < 0)
                success = TRUE;
        } else if (isdigit (*p) && (dbm > 0) && (dbm < 115)) {
            /* S720 appears to use "1x RSSI" and print RSSI in dBm without '-' */
            dbm *= -1;
            success = TRUE;
        }
    }

    if (success) {
        dbm = CLAMP (dbm, -113, -51);
        quality = 100 - ((dbm + 51) * 100 / (-113 + 51));
    }

    return quality;
}

static void
nwrssi_ready (MMBaseModem *self,
              GAsyncResult *res,
              GSimpleAsyncResult *simple)
{
    const gchar *response;
    gint quality;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (!response) {
        /* Fallback to parent's method */
        iface_modem_parent->load_signal_quality (
            MM_IFACE_MODEM (self),
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            simple);
        return;
    }

    /* Parse the signal quality */
    quality = get_one_quality (response, "RX0=");
    if (quality < 0)
        quality = get_one_quality (response, "1x RSSI=");
    if (quality < 0)
        quality = get_one_quality (response, "RX1=");

    if (quality >= 0)
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER ((guint)quality),
                                                   NULL);
    else
        g_simple_async_result_set_error (simple,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't parse $NWRSSI response: '%s'",
                                         response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_signal_quality (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    mm_dbg ("loading signal quality...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_signal_quality);

    /* 3GPP modems can just run parent's signal quality loading */
    if (mm_iface_modem_is_3gpp (self)) {
        iface_modem_parent->load_signal_quality (
            self,
            (GAsyncReadyCallback)parent_load_signal_quality_ready,
            result);
        return;
    }

    /* CDMA modems need custom signal quality loading */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "$NWRSSI",
        3,
        FALSE,
        (GAsyncReadyCallback)nwrssi_ready,
        result);
}

/*****************************************************************************/
/* Enable unsolicited events (SMS indications) (Messaging interface) */

static gboolean
messaging_enable_unsolicited_events_finish (MMIfaceModemMessaging *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
}

static void
messaging_enable_unsolicited_events (MMIfaceModemMessaging *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Many Qualcomm chipsets don't support mode=2 */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CNMI=1,1,2,1,0",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static const MMBaseModemAtCommand nwdmat_sequence[] = {
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, NULL },
    { NULL }
};

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_novatel_parent_class)->setup_ports (self);

    /* Flip secondary ports to AT mode */
    mm_base_modem_at_sequence (MM_BASE_MODEM (self), nwdmat_sequence, NULL, NULL, NULL, NULL);
}

/*****************************************************************************/

MMBroadbandModemNovatel *
mm_broadband_modem_novatel_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Use SM storage always */
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM1_STORAGE, MM_SMS_STORAGE_SM,
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM2_STORAGE, MM_SMS_STORAGE_SM,
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM3_STORAGE, MM_SMS_STORAGE_SM,
                         NULL);
}

static void
mm_broadband_modem_novatel_init (MMBroadbandModemNovatel *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_allowed_modes = load_allowed_modes;
    iface->load_allowed_modes_finish = load_allowed_modes_finish;
    iface->set_allowed_modes = set_allowed_modes;
    iface->set_allowed_modes_finish = set_allowed_modes_finish;
    iface->load_access_technologies_finish = modem_load_access_technologies_finish;
    iface->load_access_technologies = modem_load_access_technologies;
    iface->load_signal_quality = modem_load_signal_quality;
    iface->load_signal_quality_finish = modem_load_signal_quality_finish;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->enable_unsolicited_events = messaging_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = messaging_enable_unsolicited_events_finish;
}

static void
mm_broadband_modem_novatel_class_init (MMBroadbandModemNovatelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
