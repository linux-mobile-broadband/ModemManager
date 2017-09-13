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

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-simtech.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSimtech, mm_broadband_modem_simtech, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init))

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static MMModemAccessTechnology
simtech_act_to_mm_act (int nsmod)
{
    if (nsmod == 1)
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    else if (nsmod == 2)
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    else if (nsmod == 3)
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    else if (nsmod == 4)
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (nsmod == 5)
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    else if (nsmod == 6)
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    else if (nsmod == 7)
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
simtech_tech_changed (MMPortSerialAt *port,
                      GMatchInfo *match_info,
                      MMBroadbandModemSimtech *self)
{
    gchar *str;

    str = g_match_info_fetch (match_info, 1);
    if (str && str[0])
        mm_iface_modem_update_access_technologies (
            MM_IFACE_MODEM (self),
            simtech_act_to_mm_act (atoi (str)),
            MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
    g_free (str);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemSimtech *self,
                                 gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;
    GRegex *regex;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    regex = g_regex_new ("\\r\\n\\+CNSMOD:\\s*(\\d)\\r\\n",
                         G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    /* Enable unsolicited events in given port */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)simtech_tech_changed : NULL,
            enable ? self : NULL,
            NULL);
    }

    g_regex_unref (regex);
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
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_SIMTECH (self), TRUE);
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
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
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
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
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_SIMTECH (self), FALSE);

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
    /* Autoreport access technology changes */
    { "+CNSMOD=1",    5, FALSE, NULL },
    /* Autoreport CSQ (first arg), and only report when it changes (second arg) */
    { "+AUTOCSQ=1,1", 5, FALSE, NULL },
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
        return;
    }

    /* Our own enable now */
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        unsolicited_enable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
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

static const MMBaseModemAtCommand unsolicited_disable_sequence[] = {
    { "+CNSMOD=0",  3, FALSE, NULL },
    { "+AUTOCSQ=0", 3, FALSE, NULL },
    { NULL }
};

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

    *access_technologies = (MMModemAccessTechnology) GPOINTER_TO_UINT (
        g_simple_async_result_get_op_res_gpointer (
            G_SIMPLE_ASYNC_RESULT (res)));
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
cnsmod_query_ready (MMBroadbandModemSimtech *self,
                    GAsyncResult *res,
                    GSimpleAsyncResult *operation_result)
{
    const gchar *response, *p;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (operation_result, error);
        g_simple_async_result_complete (operation_result);
        g_object_unref (operation_result);
        return;
    }

    p = mm_strip_tag (response, "+CNSMOD:");
    if (p)
        p = strchr (p, ',');

    if (!p || !isdigit (*(p + 1)))
        g_simple_async_result_set_error (
            operation_result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the +CNSMOD response: '%s'",
            response);
    else
        g_simple_async_result_set_op_res_gpointer (
            operation_result,
            GUINT_TO_POINTER (simtech_act_to_mm_act (atoi (p + 1))),
            NULL);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
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

    /* Launch query only for 3GPP modems */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_simple_async_result_set_op_res_gpointer (
            result,
            GUINT_TO_POINTER (MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN),
            NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "AT+CNSMOD?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnsmod_query_ready,
        result);
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

    /* Build list of combinations */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 5);
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
    /* 2G and 3G, 2G preferred */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_2G;
    g_array_append_val (combinations, mode);
    /* 2G and 3G, 3G preferred */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_3G;
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
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadCurrentModesResult;

typedef struct {
    MMBroadbandModemSimtech *self;
    GSimpleAsyncResult *result;
    gint acqord;
    gint modepref;
} LoadCurrentModesContext;

static void
load_current_modes_context_complete_and_free (LoadCurrentModesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
    LoadCurrentModesResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *allowed = result->allowed;
    *preferred = result->preferred;
    return TRUE;
}

static void
cnmp_query_ready (MMBroadbandModemSimtech *self,
                  GAsyncResult *res,
                  LoadCurrentModesContext *ctx)
{
    LoadCurrentModesResult *result;
    const gchar *response, *p;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
        load_current_modes_context_complete_and_free (ctx);
        return;
    }

    p = mm_strip_tag (response, "+CNMP:");
    if (!p) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the mode preference response: '%s'",
            response);
        load_current_modes_context_complete_and_free (ctx);
        return;
    }

    result = g_new (LoadCurrentModesResult, 1);
    result->allowed = MM_MODEM_MODE_NONE;
    result->preferred = MM_MODEM_MODE_NONE;

    ctx->modepref = atoi (p);
    switch (ctx->modepref) {
    case 2:
        /* Automatic */
        switch (ctx->acqord) {
        case 0:
            result->allowed = MM_MODEM_MODE_ANY;
            result->preferred = MM_MODEM_MODE_NONE;
            break;
        case 1:
            result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result->preferred = MM_MODEM_MODE_2G;
            break;
        case 2:
            result->allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
            result->preferred = MM_MODEM_MODE_3G;
            break;
        default:
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Unknown acquisition order preference: '%d'",
                ctx->acqord);
            load_current_modes_context_complete_and_free (ctx);
            return;
        }
        break;

    case 13:
        /* GSM only */
        result->allowed = MM_MODEM_MODE_2G;
        result->preferred = MM_MODEM_MODE_NONE;
        break;

    case 14:
        /* WCDMA only */
        result->allowed = MM_MODEM_MODE_3G;
        result->preferred = MM_MODEM_MODE_NONE;
        break;

    default:
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Unknown mode preference: '%d'",
            ctx->modepref);
        load_current_modes_context_complete_and_free (ctx);
        return;
    }

    /* Set final result and complete */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               result,
                                               g_free);
    load_current_modes_context_complete_and_free (ctx);
}

static void
cnaop_query_ready (MMBroadbandModemSimtech *self,
                   GAsyncResult *res,
                   LoadCurrentModesContext *ctx)
{
    const gchar *response, *p;
    GError *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
        load_current_modes_context_complete_and_free (ctx);
        return;
    }

    p = mm_strip_tag (response, "+CNAOP:");
    if (p)
        ctx->acqord = atoi (p);

    if (ctx->acqord < 0 || ctx->acqord > 2) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the acquisition order response: '%s'",
            response);
        load_current_modes_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CNMP?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnmp_query_ready,
        ctx);
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    LoadCurrentModesContext *ctx;

    ctx = g_new (LoadCurrentModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_current_modes);
    ctx->acqord = -1;
    ctx->modepref = -1;

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CNAOP?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnaop_query_ready,
        ctx);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    MMBroadbandModemSimtech *self;
    GSimpleAsyncResult *result;
    guint nmp;   /* mode preference */
    guint naop;  /* acquisition order */
} SetCurrentModesContext;

static void
set_current_modes_context_complete_and_free (SetCurrentModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
set_current_modes_finish (MMIfaceModem *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cnaop_set_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 SetCurrentModesContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    set_current_modes_context_complete_and_free (ctx);
}

static void
cnmp_set_ready (MMBaseModem *self,
                GAsyncResult *res,
                SetCurrentModesContext *ctx)
{
    GError *error = NULL;
    gchar *command;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Let the error be critical. */
        g_simple_async_result_take_error (ctx->result, error);
        set_current_modes_context_complete_and_free (ctx);
        return;
    }

    command = g_strdup_printf ("+CNAOP=%u", ctx->naop);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)cnaop_set_ready,
        ctx);
    g_free (command);
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

    ctx = g_new (SetCurrentModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             set_current_modes);

    /* Defaults: automatic search */
    ctx->nmp = 2;
    ctx->naop = 0;

    if (allowed == MM_MODEM_MODE_ANY &&
        preferred == MM_MODEM_MODE_NONE) {
        /* defaults nmp and naop */
    } else if (allowed == MM_MODEM_MODE_2G) {
        ctx->nmp = 13;
        ctx->naop = 0;
    } else if (allowed == MM_MODEM_MODE_3G) {
        ctx->nmp = 14;
        ctx->naop = 0;
    } else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        /* default nmp */
        if (preferred == MM_MODEM_MODE_2G)
            ctx->naop = 3;
        else if (preferred == MM_MODEM_MODE_3G)
            ctx->naop = 2;
        else
            /* default naop */
            ctx->naop = 0;
    } else {
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

    command = g_strdup_printf ("+CNMP=%u", ctx->nmp);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)cnmp_set_ready,
        ctx);
    g_free (command);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_simtech_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_SIMTECH (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemSimtech *
mm_broadband_modem_simtech_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SIMTECH,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_simtech_init (MMBroadbandModemSimtech *self)
{
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
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
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
}

static void
mm_broadband_modem_simtech_class_init (MMBroadbandModemSimtechClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
