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
#include "mm-serial-parsers.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-iface-modem-time.h"
#include "mm-common-helpers.h"
#include "mm-base-modem-at.h"
#include "mm-bearer-list.h"
#include "mm-broadband-bearer-icera.h"
#include "mm-broadband-modem-icera.h"
#include "mm-modem-helpers-icera.h"

static void iface_modem_init                      (MMIfaceModem                   *iface);
static void iface_modem_3gpp_init                 (MMIfaceModem3gpp               *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface);
static void iface_modem_time_init                 (MMIfaceModemTime               *iface);

static MMIfaceModem                   *iface_modem_parent;
static MMIfaceModem3gpp               *iface_modem_3gpp_parent;
static MMIfaceModem3gppProfileManager *iface_modem_3gpp_profile_manager_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemIcera, mm_broadband_modem_icera, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init))

enum {
    PROP_0,
    PROP_DEFAULT_IP_METHOD,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandModemIceraPrivate {
    MMBearerIpMethod default_ip_method;

    GRegex *nwstate_regex;
    GRegex *pacsp_regex;
    GRegex *ipdpact_regex;

    /* Cache of the most recent value seen by the unsolicited message handler */
    MMModemAccessTechnology last_act;
};

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static void
add_supported_mode (MMBroadbandModemIcera  *self,
                    GArray                **combinations,
                    guint                   mode)
{
    MMModemModeCombination combination;

    switch (mode) {
    case 0:
        mm_obj_dbg (self, "2G-only mode supported");
        combination.allowed = MM_MODEM_MODE_2G;
        combination.preferred = MM_MODEM_MODE_NONE;
        break;
    case 1:
        mm_obj_dbg (self, "3G-only mode supported");
        combination.allowed = MM_MODEM_MODE_3G;
        combination.preferred = MM_MODEM_MODE_NONE;
        break;
    case 2:
        mm_obj_dbg (self, "2G/3G mode with 2G preferred supported");
        combination.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        combination.preferred = MM_MODEM_MODE_2G;
        break;
    case 3:
        mm_obj_dbg (self, "2G/3G mode with 3G preferred supported");
        combination.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        combination.preferred = MM_MODEM_MODE_3G;
        break;
    case 5:
        /* Any, no need to add it to the list */
        return;
    default:
        mm_obj_warn (self, "unsupported mode found in %%IPSYS=?: %u", mode);
        return;
    }

    if (*combinations == NULL)
        *combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 5);

    g_array_append_val (*combinations, combination);
}

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    GArray                 *combinations = NULL;
    const gchar            *response;
    gchar                 **split = NULL;
    g_autoptr(GMatchInfo)   match_info = NULL;
    g_autoptr(GRegex)       r = NULL;
    guint                   i;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    /* Reply goes like this:
     * AT%IPSYS=?
     * %IPSYS: (0-3,5),(0-3)
     */

    r = g_regex_new ("\\%IPSYS:\\s*\\((.*)\\)\\s*,\\((.*)\\)",
                     G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, response, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        g_autofree gchar *aux = NULL;

        aux = mm_get_string_unquoted_from_match_info (match_info, 1);
        if (aux)
            split = g_strsplit (aux, ",", -1);
    }

    if (!split) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "%%IPSYS=? response didn't match");
        return NULL;
    }

    for (i = 0; split[i]; i++) {
        gchar *interval_separator;

        g_strstrip (split[i]);
        interval_separator = strstr (split[i], "-");
        if (interval_separator) {
            /* Add all in interval */
            gchar *first, *last;
            guint modefirst, modelast;

            first = g_strdup (split[i]);
            interval_separator = strstr (first, "-");
            *(interval_separator++) = '\0';
            last = interval_separator;

            if (mm_get_uint_from_str (first, &modefirst) &&
                mm_get_uint_from_str (last, &modelast) &&
                modefirst < modelast &&
                modelast <= 5) {
                guint j;

                for (j = modefirst; j <= modelast; j++)
                    add_supported_mode (MM_BROADBAND_MODEM_ICERA (self), &combinations, j);
            } else
                mm_obj_warn (self, "couldn't parse mode interval in %%IPSYS=? response: %s", split[i]);
            g_free (first);
        } else {
            guint mode;

            /* Add single */
            if (mm_get_uint_from_str (split[i], &mode))
                add_supported_mode (MM_BROADBAND_MODEM_ICERA (self), &combinations, mode);
            else
                mm_obj_warn (self, "couldn't parse mode in %%IPSYS=? response: %s", split[i]);
        }
    }

    g_strfreev (split);

    if (!combinations)
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "No mode combinations were parsed from the %%IPSYS=? response (%s)",
                     response);

    return combinations;
}

static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%IPSYS=?",
                              3,
                              TRUE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

static gboolean
modem_load_current_modes_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemMode *allowed,
                                 MMModemMode *preferred,
                                 GError **error)
{
    const gchar *response;
    const gchar *str;
    gint mode, domain;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    str = mm_strip_tag (response, "%IPSYS:");

    if (!sscanf (str, "%d,%d", &mode, &domain)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse %%IPSYS response: '%s'",
                     response);
        return FALSE;
    }

    switch (mode) {
    case 0:
        *allowed = MM_MODEM_MODE_2G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 1:
        *allowed = MM_MODEM_MODE_3G;
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    case 2:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_2G;
        return TRUE;
    case 3:
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_3G;
        return TRUE;
    case 5: /* any */
        *allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
        *preferred = MM_MODEM_MODE_NONE;
        return TRUE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse unexpected %%IPSYS response: '%s'",
                 response);
    return FALSE;
}

static void
modem_load_current_modes (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "%IPSYS?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

static gboolean
modem_set_current_modes_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allowed_mode_update_ready (MMBaseModem *self,
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
modem_set_current_modes (MMIfaceModem *self,
                         MMModemMode allowed,
                         MMModemMode preferred,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;
    gchar *command;
    gint icera_mode = -1;

    task = g_task_new (self, NULL, callback, user_data);

    /*
     * The core has checked the following:
     *  - that 'allowed' are a subset of the 'supported' modes
     *  - that 'preferred' is one mode, and a subset of 'allowed'
     */
    if (allowed == MM_MODEM_MODE_2G)
        icera_mode = 0;
    else if (allowed == MM_MODEM_MODE_3G)
        icera_mode = 1;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (preferred == MM_MODEM_MODE_2G)
            icera_mode = 2;
        else if (preferred == MM_MODEM_MODE_3G)
            icera_mode = 3;
        else /* none preferred, so AUTO */
            icera_mode = 5;
    } else if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE)
        icera_mode = 5;

    if (icera_mode < 0) {
        gchar *allowed_str;
        gchar *preferred_str;

        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested mode (allowed: '%s', preferred: '%s') not "
                                 "supported by the modem.",
                                 allowed_str,
                                 preferred_str);
        g_object_unref (task);
        g_free (allowed_str);
        g_free (preferred_str);
        return;
    }

    command = g_strdup_printf ("%%IPSYS=%d", icera_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)allowed_mode_update_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Icera-specific unsolicited events handling */

typedef struct {
    guint cid;
    MMBearerConnectionStatus status;
} BearerListReportStatusForeachContext;

static void
bearer_list_report_status_foreach (MMBaseBearer *bearer,
                                   BearerListReportStatusForeachContext *ctx)
{
    gint profile_id;
    gint connecting_profile_id;

    if (!MM_IS_BROADBAND_BEARER_ICERA (bearer))
        return;

    /* The profile ID in the base bearer is set only once the modem is connected */
    profile_id = mm_base_bearer_get_profile_id (bearer);

    /* The profile ID in the icera bearer is available during the connecting phase */
    connecting_profile_id = mm_broadband_bearer_icera_get_connecting_profile_id (MM_BROADBAND_BEARER_ICERA (bearer));

    if ((profile_id != (gint)ctx->cid) && (connecting_profile_id != (gint)ctx->cid))
        return;

    mm_base_bearer_report_connection_status (bearer, ctx->status);
}

static void
ipdpact_received (MMPortSerialAt *port,
                  GMatchInfo *match_info,
                  MMBroadbandModemIcera *self)
{
    MMBearerList *list = NULL;
    BearerListReportStatusForeachContext ctx;
    guint cid;
    guint status;

    /* Ensure we got proper parsed values */
    if (!mm_get_uint_from_match_info (match_info, 1, &cid) ||
        !mm_get_uint_from_match_info (match_info, 2, &status))
        return;

    /* Setup context */
    ctx.cid = cid;
    ctx.status = MM_BEARER_CONNECTION_STATUS_UNKNOWN;

    switch (status) {
    case 0:
        ctx.status = MM_BEARER_CONNECTION_STATUS_DISCONNECTED;
        break;
    case 1:
        ctx.status = MM_BEARER_CONNECTION_STATUS_CONNECTED;
        break;
    case 2:
        /* activating */
        break;
    case 3:
        ctx.status = MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED;
        break;
    default:
        mm_obj_warn (self, "unknown %%IPDPACT connect status %d", status);
        break;
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

    /* Will report status only in the bearer with the specific CID */
    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_report_status_foreach,
                            &ctx);
    g_object_unref (list);
}

static MMModemAccessTechnology
nwstate_to_act (const gchar *str)
{
    /* small 'g' means CS, big 'G' means PS */
    if (!strcmp (str, "2g"))
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    else if (!strcmp (str, "2G-GPRS"))
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    else if (!strcmp (str, "2G-EDGE"))
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    else if (!strcmp (str, "3G"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "3g"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "R99"))
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    else if (!strcmp (str, "3G-HSDPA") || !strcmp (str, "HSDPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    else if (!strcmp (str, "3G-HSUPA") || !strcmp (str, "HSUPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA") || !strcmp (str, "HSDPA-HSUPA"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA-HSPA+") || !strcmp (str, "HSDPA-HSUPA-HSPA+"))
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
nwstate_changed (MMPortSerialAt *port,
                 GMatchInfo *info,
                 MMBroadbandModemIcera *self)
{
    gchar *str;

    /*
     * %NWSTATE: <rssi>,<mccmnc>,<tech>,<connection state>,<regulation>
     *
     * <connection state> shows the actual access technology in-use when a
     * PS connection is active.
     */

    /* Process signal quality... */
    str = g_match_info_fetch (info, 1);
    if (str) {
        gint rssi;

        rssi = atoi (str);
        rssi = CLAMP (rssi, 0, 5) * 100 / 5;
        g_free (str);

        mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self),
                                              (guint)rssi);
    }

    /* Process access technology... */
    str = g_match_info_fetch (info, 4);
    if (!str || (strcmp (str, "-") == 0)) {
        g_free (str);
        str = g_match_info_fetch (info, 3);
    }
    if (str) {
        MMModemAccessTechnology act;

        act = nwstate_to_act (str);
        g_free (str);

        /* Cache last received value, needed for explicit access technology
         * query handling */
        self->priv->last_act = act;
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   act,
                                                   MM_MODEM_ACCESS_TECHNOLOGY_ANY);
    }
}

static void
set_unsolicited_events_handlers (MMBroadbandModemIcera *self,
                                 gboolean enable)
{
    MMPortSerialAt *ports[2];
    guint i;

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Enable unsolicited events in given port */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        /* Access technology related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->nwstate_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)nwstate_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Connection status related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->ipdpact_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)ipdpact_received : NULL,
            enable ? self : NULL,
            NULL);

        /* Always to ignore */
        if (!enable) {
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                self->priv->pacsp_regex,
                NULL,
                NULL,
                NULL);
        }
    }
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
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *access_technologies = (MMModemAccessTechnology) value;
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
nwstate_query_ready (MMBroadbandModemIcera *self,
                     GAsyncResult *res,
                     GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_task_return_error (task, error);
    else {
        /*
         * The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we use the result from there.
         */
        g_task_return_int (task, self->priv->last_act);
    }

    g_object_unref (task);
}

static void
modem_load_access_technologies (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE",
        3,
        FALSE,
        (GAsyncReadyCallback)nwstate_query_ready,
        task);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

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
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), TRUE);
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
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Enable/disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
own_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
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
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Our own enable now */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=1",
        3,
        FALSE,
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
own_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Next, chain up parent's disable */
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
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%NWSTATE=0",
        3,
        FALSE,
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Create bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_icera_new_ready (GObject *source,
                                  GAsyncResult *res,
                                  GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_icera_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
broadband_bearer_new_ready (GObject *source,
                            GAsyncResult *res,
                            GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
modem_create_bearer (MMIfaceModem        *self,
                     MMBearerProperties  *props,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If we get a NET port, create Icera bearer */
    if (mm_base_modem_peek_best_data_port (MM_BASE_MODEM (self), MM_PORT_TYPE_NET)) {
        mm_broadband_bearer_icera_new (
            MM_BROADBAND_MODEM (self),
            MM_BROADBAND_MODEM_ICERA (self)->priv->default_ip_method,
            props,
            NULL, /* cancellable */
            (GAsyncReadyCallback)broadband_bearer_icera_new_ready,
            task);
        return;
    }

    /* Otherwise, plain generic broadband bearer */
    mm_broadband_bearer_new (
        MM_BROADBAND_MODEM (self),
        props,
        NULL, /* cancellable */
        (GAsyncReadyCallback)broadband_bearer_new_ready,
        task);
}

/*****************************************************************************/
/* Modem power up (Modem interface) */

static gboolean
modem_power_up_finish (MMIfaceModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cfun_enable_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        /* Ignore all errors except NOT_ALLOWED, which means Airplane Mode */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED))
            g_task_return_error (task, error);
        else {
            g_error_free (error);
            g_task_return_boolean (task, TRUE);
        }
    }

    g_object_unref (task);
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN=1",
                              10,
                              FALSE,
                              (GAsyncReadyCallback)cfun_enable_ready,
                              g_task_new (self, NULL, callback, user_data));
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
                              /* The modem usually completes +CFUN=4 within 1-2 seconds,
                               * but sometimes takes a ridiculously long time (~30-35 seconds).
                               * It's better to have a long timeout here than to have the
                               * modem not responding to subsequent AT commands until +CFUN=4
                               * completes. */
                              40,
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
                              "%IRESET",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load unlock retries (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_unlock_retries_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    int pin1, puk1, pin2, puk2;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = mm_strip_tag (response, "%PINNUM:");
    if (sscanf (response, " %d, %d, %d, %d", &pin1, &puk1, &pin2, &puk2) == 4) {
        MMUnlockRetries *retries;
        retries = mm_unlock_retries_new ();
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2);
        mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2);
        g_task_return_pointer (task, retries, g_object_unref);
    } else {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Invalid unlock retries response: '%s'",
                                 response);
    }
    g_object_unref (task);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%PINNUM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_unlock_retries_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Generic band handling utilities */

typedef struct {
    MMModemBand band;
    char *name;
    gboolean enabled;
} Band;

static void
band_free (Band *b)
{
    g_free (b->name);
    g_free (b);
}

static const Band modem_bands[] = {
    /* Sort 3G first since it's preferred */
    { MM_MODEM_BAND_UTRAN_1, (gchar *) "FDD_BAND_I",    FALSE },
    { MM_MODEM_BAND_UTRAN_2, (gchar *) "FDD_BAND_II",   FALSE },
    { MM_MODEM_BAND_UTRAN_3, (gchar *) "FDD_BAND_III",  FALSE },
    { MM_MODEM_BAND_UTRAN_4, (gchar *) "FDD_BAND_IV",   FALSE },
    { MM_MODEM_BAND_UTRAN_5, (gchar *) "FDD_BAND_V",    FALSE },
    { MM_MODEM_BAND_UTRAN_6, (gchar *) "FDD_BAND_VI",   FALSE },
    { MM_MODEM_BAND_UTRAN_8, (gchar *) "FDD_BAND_VIII", FALSE },
    /* 2G second */
    { MM_MODEM_BAND_G850,    (gchar *) "G850",          FALSE },
    { MM_MODEM_BAND_DCS,     (gchar *) "DCS",           FALSE },
    { MM_MODEM_BAND_EGSM,    (gchar *) "EGSM",          FALSE },
    { MM_MODEM_BAND_PCS,     (gchar *) "PCS",           FALSE },
    /* And ANY last since it's most inclusive */
    { MM_MODEM_BAND_ANY,     (gchar *) "ANY",           FALSE },
};

static const guint modem_band_any_bit = 1 << (G_N_ELEMENTS (modem_bands) - 1);

static MMModemBand
icera_band_to_mm (const char *icera)
{
    guint i;

    for (i = 0 ; i < G_N_ELEMENTS (modem_bands); i++) {
        if (g_strcmp0 (icera, modem_bands[i].name) == 0)
            return modem_bands[i].band;
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static GSList *
parse_bands (const gchar *response,
             guint32     *out_len)
{
    g_autoptr(GRegex)      r = NULL;
    g_autoptr(GMatchInfo)  info = NULL;
    GSList                *bands = NULL;

    g_return_val_if_fail (out_len != NULL, NULL);

    /*
     * Response is a number of lines of the form:
     *   "EGSM": 0
     *   "FDD_BAND_I": 1
     *   ...
     * with 1 and 0 indicating whether the particular band is enabled or not.
     */
    r = g_regex_new ("^\"(\\w+)\": (\\d)",
                     G_REGEX_MULTILINE, G_REGEX_MATCH_NEWLINE_ANY,
                     NULL);
    g_assert (r != NULL);

    g_regex_match (r, response, 0, &info);
    while (g_match_info_matches (info)) {
        gchar *name, *enabled;
        Band *b;
        MMModemBand band;

        name = g_match_info_fetch (info, 1);
        enabled = g_match_info_fetch (info, 2);
        band = icera_band_to_mm (name);
        if (band != MM_MODEM_BAND_UNKNOWN) {
            b = g_malloc0 (sizeof (Band));
            b->band = band;
            b->name = g_strdup (name);
            b->enabled = (enabled[0] == '1' ? TRUE : FALSE);
            bands = g_slist_append (bands, b);
            *out_len = *out_len + 1;
        }
        g_free (name);
        g_free (enabled);
        g_match_info_next (info, NULL);
    }

    return bands;
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

typedef struct {
    MMBaseModemAtCommandAlloc *cmds;
    GSList *check_bands;
    GSList *enabled_bands;
    guint32 idx;
} SupportedBandsContext;

static void
supported_bands_context_free (SupportedBandsContext *ctx)
{
    guint i;

    for (i = 0; ctx->cmds[i].command; i++)
        mm_base_modem_at_command_alloc_clear (&ctx->cmds[i]);
    g_free (ctx->cmds);
    g_slist_free_full (ctx->check_bands, (GDestroyNotify) band_free);
    g_slist_free_full (ctx->enabled_bands, (GDestroyNotify) band_free);
    g_free (ctx);
}

static GArray *
modem_load_supported_bands_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_supported_bands_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    GError *error = NULL;
    SupportedBandsContext *ctx = NULL;
    GArray *bands;
    GSList *iter;

    mm_base_modem_at_sequence_finish (self, res, (gpointer) &ctx, &error);
    if (error)
        g_task_return_error (task, error);
    else {
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), ctx->idx);

        /* Add already enabled bands */
        for (iter = ctx->enabled_bands; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            g_array_prepend_val (bands, b->band);
        }

        /* Add any checked bands that are supported */
        for (iter = ctx->check_bands; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            /* 'enabled' here really means supported/unsupported */
            if (b->enabled)
                g_array_prepend_val (bands, b->band);
        }

        g_task_return_pointer (task, bands, (GDestroyNotify) g_array_unref);
    }
    g_object_unref (task);
}

static MMBaseModemAtResponseProcessorResult
load_supported_bands_response_processor (MMBaseModem   *self,
                                         gpointer       context,
                                         const gchar   *command,
                                         const gchar   *response,
                                         gboolean       last_command,
                                         const GError  *error,
                                         GVariant     **result,
                                         GError       **result_error)
{
    SupportedBandsContext *ctx;
    Band                  *b;

    ctx = context;
    b = g_slist_nth_data (ctx->check_bands, ctx->idx++);

    /* If there was no error setting the band, that band is supported.  We
     * abuse the 'enabled' item to mean supported/unsupported.
     */
    b->enabled = !error;

    /* Continue to next band */
    return MM_BASE_MODEM_AT_RESPONSE_PROCESSOR_RESULT_CONTINUE;
}

static void
load_supported_bands_get_current_bands_ready (MMIfaceModem *self,
                                              GAsyncResult *res,
                                              GTask *task)
{
    SupportedBandsContext *ctx;
    const gchar *response;
    GError *error = NULL;
    GSList *iter, *new;
    guint32 len = 0, i = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_new0 (SupportedBandsContext, 1);

    /* For each reported band, build up an AT command to set that band
     * to its current enabled/disabled state.
     */
    iter = ctx->check_bands = parse_bands (response, &len);
    ctx->cmds = g_new0 (MMBaseModemAtCommandAlloc, len + 1);

    while (iter) {
        Band *b = iter->data;

        if (b->enabled || b->band == MM_MODEM_BAND_ANY) {
            /* Move known-supported band to the enabled list */
            new = g_slist_next (iter);
            ctx->check_bands = g_slist_remove_link (ctx->check_bands, iter);
            ctx->enabled_bands = g_slist_prepend (ctx->enabled_bands, iter->data);
            g_slist_free (iter);
            iter = new;
        } else {
            /* Check support for disabled band */
            ctx->cmds[i].command = g_strdup_printf ("%%IPBM=\"%s\",0", b->name);
            ctx->cmds[i].timeout = 10;
            ctx->cmds[i].allow_cached = FALSE;
            ctx->cmds[i].response_processor = load_supported_bands_response_processor;
            i++;
            iter = g_slist_next (iter);
        }
    }

    mm_base_modem_at_sequence (MM_BASE_MODEM (self),
                               (const MMBaseModemAtCommand *)ctx->cmds,
                               ctx,
                               (GDestroyNotify) supported_bands_context_free,
                               (GAsyncReadyCallback) load_supported_bands_ready,
                               task);
}

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    /* The modems report some bands as disabled that they don't actually
     * support enabling.  Thanks Icera!  So we have to try setting each
     * band to it's current enabled/disabled value, and the modem will
     * return an error if it doesn't support that band at all.
     */
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_supported_bands_get_current_bands_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

static GArray *
modem_load_current_bands_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_current_bands_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask *task)
{
    GArray *bands;
    const gchar *response;
    GError *error = NULL;
    GSList *parsed, *iter;
    guint32 len = 0;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
    } else {
        /* Parse bands from Icera response into MM band numbers */
        parsed = parse_bands (response, &len);
        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), len);
        for (iter = parsed; iter; iter = g_slist_next (iter)) {
            Band *b = iter->data;

            if (b->enabled)
                g_array_append_val (bands, b->band);
        }
        g_slist_free_full (parsed, (GDestroyNotify) band_free);

        g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);
    }
    g_object_unref (task);
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPBM?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

typedef struct {
    guint bandbits;
    guint enablebits;
    guint disablebits;
} SetCurrentBandsContext;

/*
 * The modem's band-setting command (%IPBM=) enables or disables one
 * band at a time, and one band must always be enabled. Here, we first
 * get the set of enabled bands, compute the difference between that
 * set and the requested set, enable any added bands, and finally
 * disable any removed bands.
 */
static gboolean
modem_set_current_bands_finish (MMIfaceModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_one_band (MMIfaceModem *self, GTask *task);

static void
set_current_bands_next (MMIfaceModem *self,
                        GAsyncResult *res,
                        GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    set_one_band (self, task);
}

static void
set_one_band (MMIfaceModem *self,
              GTask *task)
{
    SetCurrentBandsContext *ctx;
    guint enable, band;
    gchar *command;

    ctx = g_task_get_task_data (task);

    /* Find the next band to enable or disable, always doing enables first */
    enable = 1;
    band = ffs (ctx->enablebits);
    if (band == 0) {
        enable = 0;
        band = ffs (ctx->disablebits);
    }
    if (band == 0) {
        /* Both enabling and disabling are done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Note that ffs() returning 2 corresponds to 1 << 1, not 1 << 2 */
    band--;
    mm_obj_dbg (self, "preparing %%IPBM command (1/2): enablebits %x, disablebits %x, band %d, enable %d",
                ctx->enablebits, ctx->disablebits, band, enable);

    if (enable)
        ctx->enablebits &= ~(1 << band);
    else
        ctx->disablebits &= ~(1 << band);
    mm_obj_dbg (self, "preparing %%IPBM command (2/2): enablebits %x, disablebits %x",
                ctx->enablebits, ctx->disablebits);

    command = g_strdup_printf ("%%IPBM=\"%s\",%d",
                               modem_bands[band].name,
                               enable);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)set_current_bands_next,
        task);
    g_free (command);
}

static guint
band_array_to_bandbits (GArray *bands)
{
    MMModemBand band;
    guint i, j, bandbits;

    bandbits = 0;
    for (i = 0 ; i < bands->len ; i++) {
        band = g_array_index (bands, MMModemBand, i);
        for (j = 0 ; j < G_N_ELEMENTS (modem_bands) ; j++) {
            if (modem_bands[j].band == band) {
                bandbits |= 1 << j;
                break;
            }
        }
        g_assert (j <  G_N_ELEMENTS (modem_bands));
    }

    return bandbits;
}

static void
set_current_bands_got_current_bands (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    SetCurrentBandsContext *ctx;
    GArray *bands;
    GError *error = NULL;
    guint currentbits;

    bands = modem_load_current_bands_finish (self, res, &error);
    if (!bands) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    currentbits = band_array_to_bandbits (bands);
    ctx->enablebits = ctx->bandbits & ~currentbits;
    ctx->disablebits = currentbits & ~ctx->bandbits;

    set_one_band (self, task);
}

static void
modem_set_current_bands (MMIfaceModem *self,
                         GArray *bands_array,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    SetCurrentBandsContext *ctx;
    GTask *task;

    ctx = g_new0 (SetCurrentBandsContext, 1);
    ctx->bandbits = band_array_to_bandbits (bands_array);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    /*
     * If ANY is requested, simply enable ANY to activate all bands except for
     * those forbidden. */
    if (ctx->bandbits & modem_band_any_bit) {
        ctx->enablebits = modem_band_any_bit;
        ctx->disablebits = 0;
        set_one_band (self, task);
        return;
    }

    modem_load_current_bands (self,
                              (GAsyncReadyCallback)set_current_bands_got_current_bands,
                              task);
}

/*****************************************************************************/
/* Load network timezone (Time interface) */

static gboolean
parse_tlts_query_reply (const gchar *response,
                        gchar **iso8601,
                        MMNetworkTimezone **tz,
                        GError **error)
{
    gboolean ret = TRUE;
    gint year;
    gint month;
    gint day;
    gint hour;
    gint minute;
    gint second;
    gchar sign;
    gint offset;
    GDateTime *utc, *adjusted;

    /* TLTS reports UTC time with the TZ offset to *local* time */
    response = mm_strip_tag (response, "*TLTS: ");
    if (sscanf (response,
                "\"%02d/%02d/%02d,%02d:%02d:%02d%c%02d\"",
                &year,
                &month,
                &day,
                &hour,
                &minute,
                &second,
                &sign,
                &offset) != 8) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unknown *TLTS response: %s",
                     response);
        return FALSE;
    }

    /* Icera modems only report a 2-digit year, while ISO-8601 requires
     * a 4-digit year.  Assume 2000.
     */
    if (year < 100)
        year += 2000;

    /* Offset comes in 15-min units */
    offset *= 15;
    /* Apply sign to offset;  */
    if (sign == '-')
        offset *= -1;

    utc = g_date_time_new_utc (year, month, day, hour, minute, second);
    if (!utc) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Invalid *TLTS date/time: %s",
                     response);
        return FALSE;
    }

    /* Convert UTC time to local time by adjusting by the timezone offset */
    adjusted = g_date_time_add_minutes (utc, offset);
    g_date_time_unref (utc);
    if (!adjusted) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to convert modem time to local time (offset %d)",
                     offset);
        return FALSE;
    }

    /* Convert offset from minutes-to-UTC to minutes-from-UTC */
    offset *= -1;

    if (tz) {
        *tz = mm_network_timezone_new ();
        mm_network_timezone_set_offset (*tz, offset);
    }

    if (iso8601) {
        *iso8601 = mm_new_iso8601_time (g_date_time_get_year (adjusted),
                                        g_date_time_get_month (adjusted),
                                        g_date_time_get_day_of_month (adjusted),
                                        g_date_time_get_hour (adjusted),
                                        g_date_time_get_minute (adjusted),
                                        g_date_time_get_second (adjusted),
                                        TRUE,
                                        offset,
                                        error);
        ret = (*iso8601 != NULL);
    }

    g_date_time_unref (adjusted);
    return ret;
}

static MMNetworkTimezone *
modem_time_load_network_timezone_finish (MMIfaceModemTime *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    const gchar *response;
    MMNetworkTimezone *tz;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, NULL);
    if (!response) {
        /* We'll assume we can retry a bit later */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_RETRY,
                     "Retry");
        return NULL;
    }

    return (parse_tlts_query_reply (response, NULL, &tz, error) ? tz : NULL);
}

static void
modem_time_load_network_timezone (MMIfaceModemTime *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*TLTS",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* List profiles (3GPP profile management interface) */

typedef struct {
    GList *profiles;
} ListProfilesContext;

static void
list_profiles_context_free (ListProfilesContext *ctx)
{
    mm_3gpp_profile_list_free (ctx->profiles);
    g_slice_free (ListProfilesContext, ctx);
}

static gboolean
modem_3gpp_profile_manager_list_profiles_finish (MMIfaceModem3gppProfileManager  *self,
                                                 GAsyncResult                    *res,
                                                 GList                          **out_profiles,
                                                 GError                         **error)
{
    ListProfilesContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (out_profiles)
        *out_profiles = g_steal_pointer (&ctx->profiles);
    return TRUE;
}

static void
profile_manager_ipdpcfg_query_ready (MMBaseModem  *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    ListProfilesContext *ctx;
    const gchar         *response;
    g_autoptr(GError)    error = NULL;

    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response)
        mm_obj_warn (self, "couldn't load PDP context auth settings: %s", error->message);
    else if (!mm_icera_parse_ipdpcfg_query_response (response, ctx->profiles, self, &error))
        mm_obj_warn (self, "couldn't update profile list with PDP context auth settings: %s", error->message);

    /* complete successfully anyway */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
profile_manager_parent_list_profiles_ready (MMIfaceModem3gppProfileManager *self,
                                            GAsyncResult                   *res,
                                            GTask                          *task)
{
    ListProfilesContext *ctx;
    GError              *error = NULL;

    ctx = g_slice_new0 (ListProfilesContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) list_profiles_context_free);

    if (!iface_modem_3gpp_profile_manager_parent->list_profiles_finish (self, res, &ctx->profiles, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!ctx->profiles) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "%IPDPCFG?",
        3,
        FALSE,
        (GAsyncReadyCallback)profile_manager_ipdpcfg_query_ready,
        task);
}

static void
modem_3gpp_profile_manager_list_profiles (MMIfaceModem3gppProfileManager *self,
                                          GAsyncReadyCallback             callback,
                                          gpointer                        user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    iface_modem_3gpp_profile_manager_parent->list_profiles (
        self,
        (GAsyncReadyCallback)profile_manager_parent_list_profiles_ready,
        task);
}

typedef struct {
    gboolean              new_id;
    gint                  min_profile_id;
    gint                  max_profile_id;
    GEqualFunc            apn_cmp;
    MM3gppProfileCmpFlags profile_cmp_flags;
} CheckFormatContext;

static void
check_format_context_free (CheckFormatContext *ctx)
{
    g_slice_free (CheckFormatContext, ctx);
}

static gboolean
modem_3gpp_profile_manager_check_format_finish (MMIfaceModem3gppProfileManager  *self,
                                                GAsyncResult                    *res,
                                                gboolean                        *new_id,
                                                gint                            *min_profile_id,
                                                gint                            *max_profile_id,
                                                GEqualFunc                      *apn_cmp,
                                                MM3gppProfileCmpFlags           *profile_cmp_flags,
                                                GError                         **error)
{
    CheckFormatContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (new_id)
        *new_id = ctx->new_id;
    if (min_profile_id)
        *min_profile_id = (gint) ctx->min_profile_id;
    if (max_profile_id)
        *max_profile_id = (gint) ctx->max_profile_id;
    if (apn_cmp)
        *apn_cmp = ctx->apn_cmp;
    if (profile_cmp_flags)
        *profile_cmp_flags = ctx->profile_cmp_flags;
    return TRUE;
}

static void
profile_manager_parent_check_format_ready (MMIfaceModem3gppProfileManager *self,
                                           GAsyncResult                   *res,
                                           GTask                          *task)
{
    GError             *error = NULL;
    CheckFormatContext *ctx;

    ctx = g_task_get_task_data (task);

    if (!iface_modem_3gpp_profile_manager_parent->check_format_finish (self,
                                                                       res,
                                                                       &ctx->new_id,
                                                                       &ctx->min_profile_id,
                                                                       &ctx->max_profile_id,
                                                                       &ctx->apn_cmp,
                                                                       &ctx->profile_cmp_flags,
                                                                       &error)) {
        g_task_return_error (task, error);
    } else {
        /* the icera implementation supports AUTH, so unset that cmp flag */
        ctx->profile_cmp_flags &= ~MM_3GPP_PROFILE_CMP_FLAGS_NO_AUTH;
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_check_format (MMIfaceModem3gppProfileManager *self,
                                         MMBearerIpFamily                ip_type,
                                         GAsyncReadyCallback             callback,
                                         gpointer                        user_data)
{
    GTask              *task;
    CheckFormatContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (CheckFormatContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify)check_format_context_free);

    iface_modem_3gpp_profile_manager_parent->check_format (
        self,
        ip_type,
        (GAsyncReadyCallback)profile_manager_parent_check_format_ready,
        task);
}

/*****************************************************************************/
/* Deactivate profile (3GPP profile management interface) */

static gboolean
modem_3gpp_profile_manager_deactivate_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                      GAsyncResult                    *res,
                                                      GError                         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
deactivate_profile_ipdpact_set_ready (MMBaseModem  *self,
                                      GAsyncResult *res,
                                      GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_profile_manager_deactivate_profile (MMIfaceModem3gppProfileManager *self,
                                               MM3gppProfile                  *profile,
                                               GAsyncReadyCallback             callback,
                                               gpointer                        user_data)
{
    GTask            *task;
    g_autofree gchar *cmd = NULL;
    gint              profile_id;

    task = g_task_new (self, NULL, callback, user_data);

    profile_id = mm_3gpp_profile_get_profile_id (profile);
    mm_obj_dbg (self, "deactivating profile '%d'...", profile_id);

    cmd = g_strdup_printf ("%%IPDPACT=%d,0", profile_id);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        cmd,
        MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
        FALSE,
        (GAsyncReadyCallback)deactivate_profile_ipdpact_set_ready,
        task);
}

/*****************************************************************************/
/* Set profile (3GPP profile management interface) */

#define IPDPCFG_SET_MAX_ATTEMPTS       3
#define IPDPCFG_SET_RETRY_TIMEOUT_SECS 1

typedef struct {
    MM3gppProfile       *profile;
    gchar               *cmd;
    gint                 profile_id;
    guint                n_attempts;
} StoreProfileContext;

static void
store_profile_context_free (StoreProfileContext *ctx)
{
    g_free (ctx->cmd);
    g_clear_object (&ctx->profile);
    g_slice_free (StoreProfileContext, ctx);
}

static gint
modem_3gpp_profile_manager_store_profile_finish (MMIfaceModem3gppProfileManager  *self,
                                                 GAsyncResult                    *res,
                                                 gint                            *out_profile_id,
                                                 MMBearerApnType                 *out_apn_type,
                                                 GError                         **error)
{
    StoreProfileContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (out_profile_id)
        *out_profile_id = ctx->profile_id;
    if (out_apn_type)
        *out_apn_type = MM_BEARER_APN_TYPE_NONE;
    return TRUE;
}

static void profile_manager_store_profile_auth_settings (GTask *task);

static gboolean
profile_manager_ipdpcfg_set_retry (GTask *task)
{
    profile_manager_store_profile_auth_settings (task);
    return G_SOURCE_REMOVE;
}

static void
profile_manager_ipdpcfg_set_ready (MMBaseModem  *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    StoreProfileContext *ctx;
    g_autoptr(GError)    error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        /* Retry configuring the context. It sometimes fails with a 583
         * error ["a profile (CID) is currently active"] if a connect
         * is attempted too soon after a disconnect. */
        if (ctx->n_attempts < IPDPCFG_SET_MAX_ATTEMPTS) {
            mm_obj_dbg (self, "couldn't store auth settings in profile '%d': %s; retrying...",
                        ctx->profile_id, error->message);
            g_timeout_add_seconds (IPDPCFG_SET_RETRY_TIMEOUT_SECS, (GSourceFunc)profile_manager_ipdpcfg_set_retry, task);
            return;
        }
        g_task_return_error (task, g_steal_pointer (&error));
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
profile_manager_store_profile_auth_settings (GTask *task)
{
    MMIfaceModem3gppProfileManager *self;
    StoreProfileContext            *ctx;
    g_autofree gchar               *cmd = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->cmd) {
        const gchar         *user;
        const gchar         *password;
        MMBearerAllowedAuth  allowed_auth;

        user = mm_3gpp_profile_get_user (ctx->profile);
        password = mm_3gpp_profile_get_password (ctx->profile);
        allowed_auth = mm_3gpp_profile_get_allowed_auth (ctx->profile);

        /* Both user and password are required; otherwise firmware returns an error */
        if (!user || !password || allowed_auth == MM_BEARER_ALLOWED_AUTH_NONE) {
            mm_obj_dbg (self, "not using authentication");
            ctx->cmd = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", ctx->profile_id);
        } else {
            g_autofree gchar *quoted_user = NULL;
            g_autofree gchar *quoted_password = NULL;
            guint             icera_auth;

            if (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
                mm_obj_dbg (self, "using default (CHAP) authentication method");
                icera_auth = 2;
            } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
                mm_obj_dbg (self, "using CHAP authentication method");
                icera_auth = 2;
            } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
                mm_obj_dbg (self, "using PAP authentication method");
                icera_auth = 1;
            } else {
                g_autofree gchar *str = NULL;

                str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Cannot use any of the specified authentication methods (%s)",
                                         str);
                g_object_unref (task);
                return;
            }

            quoted_user     = mm_port_serial_at_quote_string (user);
            quoted_password = mm_port_serial_at_quote_string (password);
            ctx->cmd = g_strdup_printf ("%%IPDPCFG=%d,0,%u,%s,%s",
                                        ctx->profile_id,
                                        icera_auth,
                                        quoted_user,
                                        quoted_password);
        }
    }

    ctx->n_attempts++;
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              ctx->cmd,
                              6,
                              FALSE,
                              (GAsyncReadyCallback)profile_manager_ipdpcfg_set_ready,
                              task);
}

static void
profile_manager_parent_store_profile_ready (MMIfaceModem3gppProfileManager *self,
                                            GAsyncResult                   *res,
                                            GTask                          *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_profile_manager_parent->store_profile_finish (self, res, NULL, NULL, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    profile_manager_store_profile_auth_settings (task);
}

static void
modem_3gpp_profile_manager_store_profile (MMIfaceModem3gppProfileManager  *self,
                                          MM3gppProfile                   *profile,
                                          const gchar                     *index_field,
                                          GAsyncReadyCallback              callback,
                                          gpointer                         user_data)
{
    StoreProfileContext *ctx;
    GTask               *task;

    g_assert (g_strcmp0 (index_field, "profile-id") == 0);

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (StoreProfileContext);
    ctx->profile = g_object_ref (profile);
    ctx->profile_id = mm_3gpp_profile_get_profile_id (ctx->profile);
    g_assert (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN);
    g_task_set_task_data (task, ctx, (GDestroyNotify) store_profile_context_free);

    iface_modem_3gpp_profile_manager_parent->store_profile (
        self,
        profile,
        index_field,
        (GAsyncReadyCallback)profile_manager_parent_store_profile_ready,
        task);
}

/*****************************************************************************/
/* Load network time (Time interface) */

static gchar *
modem_time_load_network_time_finish (MMIfaceModemTime *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    const gchar *response;
    gchar *iso8601;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return NULL;

    return (parse_tlts_query_reply (response, &iso8601, NULL, error) ? iso8601 : NULL);
}

static void
modem_time_load_network_time (MMIfaceModemTime *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "*TLTS",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Check support (Time interface) */

static gboolean
modem_time_check_support_finish (MMIfaceModemTime *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_time_check_support (MMIfaceModemTime *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* We assume Icera devices always support *TLTS, since they appear
     * to return ERROR if the modem is not powered up, and thus we cannot
     * check for *TLTS support during modem initialization.
     */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_icera_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_ICERA (self), FALSE);
}

/*****************************************************************************/

MMBroadbandModemIcera *
mm_broadband_modem_icera_new (const gchar *device,
                              const gchar *physdev,
                              const gchar **drivers,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_ICERA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* Generic bearer (AT) or Icera bearer (NET) supported */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, TRUE,
                         NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        self->priv->default_ip_method = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        g_value_set_enum (value, self->priv->default_ip_method);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_modem_icera_init (MMBroadbandModemIcera *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_ICERA,
                                              MMBroadbandModemIceraPrivate);

    self->priv->nwstate_regex = g_regex_new ("%NWSTATE:\\s*(-?\\d+),(\\d+),([^,]*),([^,]*),(\\d+)",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->pacsp_regex = g_regex_new ("\\r\\n\\+PACSP(\\d)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ipdpact_regex = g_regex_new ("\\r\\n%IPDPACT:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    self->priv->default_ip_method = MM_BEARER_IP_METHOD_STATIC;
    self->priv->last_act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemIcera *self = MM_BROADBAND_MODEM_ICERA (object);

    g_regex_unref (self->priv->nwstate_regex);
    g_regex_unref (self->priv->pacsp_regex);
    g_regex_unref (self->priv->ipdpact_regex);

    G_OBJECT_CLASS (mm_broadband_modem_icera_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = modem_load_current_modes;
    iface->load_current_modes_finish = modem_load_current_modes_finish;
    iface->set_current_modes = modem_set_current_modes;
    iface->set_current_modes_finish = modem_set_current_modes_finish;
    iface->load_access_technologies = modem_load_access_technologies;
    iface->load_access_technologies_finish = modem_load_access_technologies_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_supported_bands = modem_load_supported_bands;
    iface->load_supported_bands_finish = modem_load_supported_bands_finish;
    iface->load_current_bands = modem_load_current_bands;
    iface->load_current_bands_finish = modem_load_current_bands_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = modem_power_up_finish;
    /* Note: don't implement modem_init_power_down, as CFUN=4 here may take
     * looong to reply */
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = modem_power_down_finish;
    iface->reset = modem_reset;
    iface->reset_finish = modem_reset_finish;
    iface->set_current_bands = modem_set_current_bands;
    iface->set_current_bands_finish = modem_set_current_bands_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
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
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_enable_disable_unsolicited_events_finish;
}

static void
iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface)
{
    iface_modem_3gpp_profile_manager_parent = g_type_interface_peek_parent (iface);

    iface->list_profiles = modem_3gpp_profile_manager_list_profiles;
    iface->list_profiles_finish = modem_3gpp_profile_manager_list_profiles_finish;
    iface->check_format = modem_3gpp_profile_manager_check_format;
    iface->check_format_finish = modem_3gpp_profile_manager_check_format_finish;
    /* note: the parent check_activated_profile() implementation using +CGACT? seems to
     * be perfectly valid. */
    iface->deactivate_profile = modem_3gpp_profile_manager_deactivate_profile;
    iface->deactivate_profile_finish = modem_3gpp_profile_manager_deactivate_profile_finish;
    iface->store_profile = modem_3gpp_profile_manager_store_profile;
    iface->store_profile_finish = modem_3gpp_profile_manager_store_profile_finish;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
    iface->check_support = modem_time_check_support;
    iface->check_support_finish = modem_time_check_support_finish;
    iface->load_network_time = modem_time_load_network_time;
    iface->load_network_time_finish = modem_time_load_network_time_finish;
    iface->load_network_timezone = modem_time_load_network_timezone;
    iface->load_network_timezone_finish = modem_time_load_network_timezone_finish;
}

static void
mm_broadband_modem_icera_class_init (MMBroadbandModemIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemIceraPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;

    properties[PROP_DEFAULT_IP_METHOD] =
        g_param_spec_enum (MM_BROADBAND_MODEM_ICERA_DEFAULT_IP_METHOD,
                           "Default IP method",
                           "Default IP Method (static or DHCP) to use.",
                           MM_TYPE_BEARER_IP_METHOD,
                           MM_BEARER_IP_METHOD_STATIC,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEFAULT_IP_METHOD, properties[PROP_DEFAULT_IP_METHOD]);
}
