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
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-mtk-legacy.h"
#include "mm-shared-mtk.h"

static void iface_modem_init      (MMIfaceModemInterface     *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gppInterface *iface);
static void shared_mtk_init       (MMSharedMtkInterface *iface);

static MMIfaceModemInterface     *iface_modem_parent;
static MMIfaceModem3gppInterface *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMtkLegacy, mm_broadband_modem_mtk_legacy, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_MTK,       shared_mtk_init));

struct _MMBroadbandModemMtkLegacyPrivate {
    /* Signal quality regex */
    GRegex *ecsqg_regex;
    GRegex *ecsqu_regex;
    GRegex *ecsqeg_regex;
    GRegex *ecsqeu_regex;
    GRegex *ecsqel_regex;
};

/*****************************************************************************/
/* Check unlock required (Modem interface) */

static MMModemLock
load_unlock_required_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCK_UNKNOWN;
    }
    return (MMModemLock)value;
}

static void
unlock_required_cimi_query_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_task_return_error (task, error);
    else {
        /* Assume unlocked if we can successfully read the IMSI */
        g_task_return_int (task, MM_MODEM_LOCK_NONE);
    }
    g_object_unref (task);
}

static void
cpin_query_ready (MMIfaceModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    MMModemLock  lock = MM_MODEM_LOCK_UNKNOWN;
    const gchar *result;
    GError      *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Some older MTK-based phones reply to +CPIN with CME ERROR 100,
         * but to even boot up they require the SIM PIN and so must be
         * unlocked. Double-check by reading the IMSI though.
         */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN)) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "+CIMI",
                                      10,
                                      FALSE,
                                      (GAsyncReadyCallback)unlock_required_cimi_query_ready,
                                      task);
            return;
        }

        /* Otherwise just return the error */
        g_task_return_error (task, error);
    } else {
        if (result)
            lock = mm_parse_cpin_response (result, TRUE);
        g_task_return_int (task, lock);
    }

    g_object_unref (task);
}

static void
load_unlock_required (MMIfaceModem *self,
                      gboolean last_attempt,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    mm_obj_dbg (self, "checking if unlock required...");
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CPIN?",
                              10,
                              FALSE,
                              (GAsyncReadyCallback)cpin_query_ready,
                              task);
}

/*****************************************************************************/

static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_wait_cb (GTask *task)
{
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
modem_after_sim_unlock (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* For device, 3 second is OK for SIM get ready */
    g_timeout_add_seconds (3, (GSourceFunc)after_sim_unlock_wait_cb, task);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static void
get_supported_modes_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)

{
    g_autoptr(GMatchInfo)   match_info = NULL;
    g_autoptr(GRegex)       r = NULL;
    const gchar            *response;
    GError                 *error = NULL;
    MMModemModeCombination  mode;
    GArray                 *combinations;
    GError                 *match_error = NULL;
    gint                    device_type;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    r = g_regex_new ("\\+EGMR:\\s*\"MT([0-9]+)",
            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &match_error)) {
        if (match_error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Failed to match EGMR response: %s", response);
        g_object_unref (task);
        return;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &device_type)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed to parse the allowed mode response: '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE,
                                      FALSE,
                                      sizeof (MMModemModeCombination),
                                      8);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G, no prefer*/
    mode.allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G, 3G prefer*/
    mode.allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_3G;
    g_array_append_val (combinations, mode);

    if (device_type == 6290) {
        /* 4G only */
        mode.allowed = MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 2G and 4G, no prefer */
        mode.allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 3G and 4G, no prefer */
        mode.allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        /* 2G, 3G and 4G, no prefer */
        mode.allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
    }

    /*********************************************************************
    * No need to filter out any unsupported modes for MTK device. For
    * +GCAP, +WS64 not support completely, generic filter will filter
    * out 4G modes.
    */
    g_task_return_pointer (task, combinations, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+EGMR=0,0",
                              3,
                              FALSE,
                              (GAsyncReadyCallback)get_supported_modes_ready,
                              g_task_new (self, NULL, callback, user_data));
}

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autoptr(GRegex)      r = NULL;
    const gchar           *response;
    gint                   erat_mode = -1;
    gint                   erat_pref = -1;
    GError                *match_error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    r = g_regex_new (
                "\\+ERAT:\\s*[0-9]+,\\s*[0-9]+,\\s*([0-9]+),\\s*([0-9]+)",
                0,
                0,
                error);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &match_error)) {
        if (match_error)
            g_propagate_error (error, match_error);
        else
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse +ERAT response: '%s'",
                         response);
        return FALSE;
    }

    if (!mm_get_int_from_match_info (match_info, 1, &erat_mode) ||
        !mm_get_int_from_match_info (match_info, 2, &erat_pref)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to parse the ERAT response: m=%d p=%d",
                     erat_mode, erat_pref);
        return FALSE;
    }

    /* Correctly parsed! */
    switch (erat_mode) {
        case 0:
            *allowed = MM_MODEM_MODE_2G;
            break;
        case 1:
            *allowed = MM_MODEM_MODE_3G;
            break;
        case 2:
            *allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
            break;
        case 3:
            *allowed = MM_MODEM_MODE_4G;
            break;
        case 4:
            *allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G;
            break;
        case 5:
            *allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
            break;
        case 6:
            *allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
            break;
        default:
            mm_obj_dbg (self, "unsupported allowed mode reported in +ERAT: %d", erat_mode);
            return FALSE;
    }

    switch (erat_pref) {
        case 0:
            *preferred = MM_MODEM_MODE_NONE;
            break;
        case 1:
            *preferred = MM_MODEM_MODE_2G;
            break;
        case 2:
            *preferred = MM_MODEM_MODE_3G;
            break;
        case 3:
            *preferred = MM_MODEM_MODE_4G;
            break;
        default:
            mm_obj_dbg (self, "unsupported preferred mode %d", erat_pref);
            return FALSE;
    }

    return TRUE;
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+ERAT?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allowed_mode_update_ready (MMBroadbandModemMtkLegacy *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);

    if (error)
        /* Let the error be critical. */
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
set_current_modes (MMIfaceModem *self,
                   MMModemMode allowed,
                   MMModemMode preferred,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GTask *task;
    gchar *command;
    gint erat_mode = -1;
    gint erat_pref = -1;

    task = g_task_new (self, NULL, callback, user_data);

    if (allowed == MM_MODEM_MODE_2G) {
        erat_mode = 0;
        erat_pref = 0;
    } else if (allowed == MM_MODEM_MODE_3G) {
        erat_mode = 1;
        erat_pref = 0;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        erat_mode = 2;
        if (preferred == MM_MODEM_MODE_3G)
            erat_pref = 2;
        else if (preferred == MM_MODEM_MODE_NONE)
            erat_pref = 0;
    /* 2G prefer not supported */
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G) &&
               preferred == MM_MODEM_MODE_NONE) {
        erat_mode = 6;
        erat_pref = 0;
    } else if ((allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G)) &&
               preferred == MM_MODEM_MODE_NONE) {
        erat_mode = 4;
        erat_pref = 0;
    } else if ((allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G)) &&
               preferred == MM_MODEM_MODE_NONE) {
        erat_mode = 5;
        erat_pref = 0;
    } else if (allowed == MM_MODEM_MODE_4G) {
        erat_mode = 3;
        erat_pref = 0;
    }

    if (erat_mode < 0 || erat_pref < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Requested mode (allowed: '%s', preferred: '%s') not supported by the modem.",
            allowed_str,
            preferred_str);
        g_object_unref (task);
        g_free (allowed_str);
        g_free (preferred_str);
        return;
    }

    command = g_strdup_printf ("AT+ERAT=%d,%d", erat_mode, erat_pref);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        30,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
mtk_80_signal_changed (MMPortSerialAt *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemMtkLegacy *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    if (quality == 99)
        quality = 0;
    else
        quality = MM_CLAMP_HIGH (quality, 31) * 100 / 31;

    mm_obj_dbg (self, "6280 signal quality URC received: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
mtk_90_2g_signal_changed (MMPortSerialAt *port,
                          GMatchInfo *match_info,
                          MMBroadbandModemMtkLegacy *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    if (quality == 99)
        quality = 0;
    else
        quality = MM_CLAMP_HIGH (quality, 63) * 100 / 63;

    mm_obj_dbg (self, "2G signal quality URC received: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
mtk_90_3g_signal_changed (MMPortSerialAt *port,
                          GMatchInfo *match_info,
                          MMBroadbandModemMtkLegacy *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = MM_CLAMP_HIGH (quality, 96) * 100 / 96;

    mm_obj_dbg (self, "3G signal quality URC received: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
mtk_90_4g_signal_changed (MMPortSerialAt *port,
                          GMatchInfo *match_info,
                          MMBroadbandModemMtkLegacy *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    quality = MM_CLAMP_HIGH (quality, 97) * 100 / 97;

    mm_obj_dbg (self, "4G signal quality URC received: %u", quality);
    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemMtkLegacy *self,
                                 gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable/disable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++){
        if(!ports[i])
            continue;

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ecsqg_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)mtk_80_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ecsqu_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)mtk_80_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ecsqeg_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)mtk_90_2g_signal_changed:NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ecsqeu_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)mtk_90_3g_signal_changed:NULL,
            enable ? self : NULL,
            NULL);

        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ecsqel_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)mtk_90_4g_signal_changed:NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MTK_LEGACY (self),
                                         TRUE);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_setup_unsolicited_events (MMIfaceModem3gpp *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    /* Chain up parent's setup */
    iface_modem_3gpp_parent->setup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_setup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

static void
parent_cleanup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    /* Our own cleanup first */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MTK_LEGACY (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    /* enable signal URC */
    { "+ECSQ=2", 5, FALSE, NULL },
    { NULL }
};

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_enable_unsolicited_events_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    MMPortSerialAt *primary;
    GError         *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't enable unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        unsolicited_enable_sequence,
        NULL,NULL,NULL,
        (GAsyncReadyCallback)own_enable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

static const MMBaseModemAtCommand unsolicited_disable_sequence[] = {
    /* disable signal URC */
    { "+ECSQ=0" , 5, FALSE, NULL },
    { NULL }
};

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult *res,
                                         GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
own_disable_unsolicited_events_ready (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_sequence_full_finish (self, res, NULL, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    iface_modem_3gpp_parent->disable_unsolicited_events (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
        task);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMPortSerialAt *primary;
    GTask          *task;

    task = g_task_new (self, NULL, callback, user_data);

    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    if (!primary) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't disable unsolicited events: no primary port");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        MM_IFACE_PORT_AT (primary),
        unsolicited_disable_sequence,
        NULL, NULL, NULL,
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mtk_legacy_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_MTK_LEGACY (self), FALSE);
}

/*****************************************************************************/
MMBroadbandModemMtkLegacy *
mm_broadband_modem_mtk_legacy_new (const gchar *device,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MTK_LEGACY,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer supports TTY only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, FALSE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_mtk_legacy_init (MMBroadbandModemMtkLegacy *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_MTK_LEGACY,
                                              MMBroadbandModemMtkLegacyPrivate);
    self->priv->ecsqg_regex = g_regex_new (
        "\\r\\n\\+ECSQ:\\s*([0-9]*),\\s*[0-9]*,\\s*-[0-9]*\\r\\n",
        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ecsqu_regex = g_regex_new (
        "\\r\\n\\+ECSQ:\\s*([0-9]*),\\s*[0-9]*,\\s*-[0-9]*,\\s*-[0-9]*,\\s*-[0-9]*\\r\\n",
        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ecsqeg_regex = g_regex_new (
        "\\r\\n\\+ECSQ:\\s*([0-9]*),\\s*[0-9]*,\\s*-[0-9]*,\\s*1,\\s*1,\\s*1,\\s*1,\\s*[0-9]*\\r\\n",
        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ecsqeu_regex = g_regex_new (
        "\\r\\n\\+ECSQ:\\s*([0-9]*),\\s*[0-9]*,\\s*1,\\s*-[0-9]*,\\s*-[0-9]*,\\s*1,\\s*1,\\s*[0-9]*\\r\\n",
        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ecsqel_regex = g_regex_new (
        "\\r\\n\\+ECSQ:\\s*[0-9]*,\\s*([0-9]*),\\s*1,\\s*1,\\s*1,\\s*-[0-9]*,\\s*-[0-9]*,\\s*[0-9]*\\r\\n",
        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemMtkLegacy *self = MM_BROADBAND_MODEM_MTK_LEGACY (object);

    g_regex_unref (self->priv->ecsqg_regex);
    g_regex_unref (self->priv->ecsqu_regex);
    g_regex_unref (self->priv->ecsqeg_regex);
    g_regex_unref (self->priv->ecsqeu_regex);
    g_regex_unref (self->priv->ecsqel_regex);

    G_OBJECT_CLASS (mm_broadband_modem_mtk_legacy_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModemInterface *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_unlock_required = load_unlock_required;
    iface->load_unlock_required_finish = load_unlock_required_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->load_unlock_retries = mm_shared_mtk_load_unlock_retries;
    iface->load_unlock_retries_finish = mm_shared_mtk_load_unlock_retries_finish;
}

static void
shared_mtk_init (MMSharedMtkInterface *iface)
{
}

static void
iface_modem_3gpp_init (MMIfaceModem3gppInterface *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->setup_unsolicited_events = modem_3gpp_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events = modem_3gpp_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = modem_3gpp_setup_cleanup_unsolicited_events_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_disable_unsolicited_events_finish;
}

static void
mm_broadband_modem_mtk_legacy_class_init (MMBroadbandModemMtkLegacyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMtkLegacyPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
