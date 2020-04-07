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
#include "mm-modem-helpers.h"
#include "mm-log-object.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-option.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemOption, mm_broadband_modem_option, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init))

struct _MMBroadbandModemOptionPrivate {
    /* Regex for access-technology related notifications */
    GRegex *_ossysi_regex;
    GRegex *_octi_regex;
    GRegex *_ouwcti_regex;

    /* Regex for signal quality related notifications */
    GRegex *_osigq_regex;

    /* Regex for other notifications to ignore */
    GRegex *ignore_regex;

    guint after_power_up_wait_id;
};

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    GError *error = NULL;
    GArray *all;
    GArray *combinations;
    GArray *filtered;
    MMModemModeCombination mode;

    all = iface_modem_parent->load_supported_modes_finish (self, res, &error);
    if (!all) {
        g_task_return_error (task, error);
        g_object_unref (task);
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
    filtered = mm_filter_supported_modes (all, combinations, self);
    g_array_unref (all);
    g_array_unref (combinations);

    g_task_return_pointer (task, filtered, (GDestroyNotify) g_array_unref);
    g_object_unref (task);
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
        g_task_new (self, NULL, callback, user_data));
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
    const gchar *response;
    const gchar *str;
    gint a, b;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    str = mm_strip_tag (response, "_OPSYS:");

    if (!sscanf (str, "%d,%d", &a, &b)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't parse OPSYS response: '%s'",
                     response);
        return FALSE;
    }

    switch (a) {
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
                 "Couldn't parse unexpected OPSYS response: '%s'",
                 response);
    return FALSE;
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "_OPSYS?",
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
allowed_mode_update_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
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
    gint option_mode = -1;

    task = g_task_new (self, NULL, callback, user_data);

    if (allowed == MM_MODEM_MODE_2G)
        option_mode = 0;
    else if (allowed == MM_MODEM_MODE_3G)
        option_mode = 1;
    else if (allowed == (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G)) {
        if (preferred == MM_MODEM_MODE_2G)
            option_mode = 2;
        else if (preferred == MM_MODEM_MODE_3G)
            option_mode = 3;
        else /* none preferred, so AUTO */
            option_mode = 5;
    } else if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE)
        option_mode = 5;

    if (option_mode < 0) {
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

    command = g_strdup_printf ("AT_OPSYS=%d,2", option_mode);
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
/* Load access technologies (Modem interface) */

typedef enum {
    ACCESS_TECHNOLOGIES_STEP_FIRST,
    ACCESS_TECHNOLOGIES_STEP_OSSYS,
    ACCESS_TECHNOLOGIES_STEP_OCTI,
    ACCESS_TECHNOLOGIES_STEP_OWCTI,
    ACCESS_TECHNOLOGIES_STEP_LAST
} AccessTechnologiesStep;

typedef struct {
    MMModemAccessTechnology access_technology;
    gboolean check_2g;
    gboolean check_3g;
    AccessTechnologiesStep step;
} AccessTechnologiesContext;

static void load_access_technologies_step (GTask *task);

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
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

    /* We are reporting ALL 3GPP access technologies here */
    *access_technologies = (MMModemAccessTechnology) value;
    *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
    return TRUE;
}

static gboolean
ossys_to_mm (gchar ossys,
             MMModemAccessTechnology *access_technology)
{
    if (ossys == '0') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        return TRUE;
    }

    if (ossys == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        return TRUE;
    }

    if (ossys == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_ossys_response (const gchar *response,
                      MMModemAccessTechnology *access_technology)
{
    MMModemAccessTechnology current = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *p;
    GRegex *r;
    GMatchInfo *match_info;
    gchar *str;
    gboolean success = FALSE;

    p = mm_strip_tag (response, "_OSSYS:");
    r = g_regex_new ("(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, p, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        str = g_match_info_fetch (match_info, 2);
        if (str && ossys_to_mm (str[0], &current)) {
            *access_technology = current;
            success = TRUE;
        }
        g_free (str);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

static void
ossys_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    AccessTechnologiesContext *ctx;
    const gchar *response;

    ctx = g_task_get_task_data (task);

    /* If for some reason the OSSYS request failed, still try to check
     * explicit 2G/3G mode with OCTI and OWCTI; maybe we'll get something.
     */
    response = mm_base_modem_at_command_finish (self, res, NULL);
    /* Response is _OSSYS: <n>,<act> so we must skip the <n> */
    if (response &&
        parse_ossys_response (response, &ctx->access_technology)) {
        /* If the OSSYS response indicated a generic access tech type
         * then only check for more specific access tech of that type.
         */
        if (ctx->access_technology == MM_MODEM_ACCESS_TECHNOLOGY_GPRS)
            ctx->check_3g = FALSE;
        else if (ctx->access_technology == MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
            ctx->check_2g = FALSE;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (task);
}

static gboolean
octi_to_mm (gchar octi,
            MMModemAccessTechnology *access_technology)
{
    if (octi == '1') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GSM;
        return TRUE;
    }

    if (octi == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        return TRUE;
    }

    if (octi == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_octi_response (const gchar *response,
                     MMModemAccessTechnology *access_technology)
{
    MMModemAccessTechnology current = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *p;
    GRegex *r;
    GMatchInfo *match_info;
    gchar *str;
    gboolean success = FALSE;

    p = mm_strip_tag (response, "_OCTI:");
    r = g_regex_new ("(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, p, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        str = g_match_info_fetch (match_info, 2);
        if (str && octi_to_mm (str[0], &current)) {
            *access_technology = current;
            success = TRUE;
        }
        g_free (str);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

static void
octi_query_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    AccessTechnologiesContext *ctx;
    MMModemAccessTechnology octi = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *response;

    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        parse_octi_response (response, &octi)) {
        /* If current tech is 2G or unknown then use the more specific
         * OCTI response.
         */
        if (ctx->access_technology < MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
            ctx->access_technology = octi;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (task);
}

static gboolean
owcti_to_mm (gchar owcti, MMModemAccessTechnology *access_technology)
{
    if (owcti == '1') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        return TRUE;
    }

    if (owcti == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
        return TRUE;
    }

    if (owcti == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
        return TRUE;
    }

    if (owcti == '4') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_owcti_response (const gchar *response,
                      MMModemAccessTechnology *access_technology)
{
    response = mm_strip_tag (response, "_OWCTI:");
    return owcti_to_mm (*response, access_technology);
}

static void
owcti_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   GTask *task)
{
    AccessTechnologiesContext *ctx;
    MMModemAccessTechnology owcti = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *response;

    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        parse_owcti_response (response, &owcti)) {
        ctx->access_technology = owcti;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (task);
}

static void
load_access_technologies_step (GTask *task)
{
    MMBroadbandModemOption *self;
    AccessTechnologiesContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ACCESS_TECHNOLOGIES_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ACCESS_TECHNOLOGIES_STEP_OSSYS:
        mm_base_modem_at_command (MM_BASE_MODEM (self),
                                  "_OSSYS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)ossys_query_ready,
                                  task);
        break;

    case ACCESS_TECHNOLOGIES_STEP_OCTI:
        if (ctx->check_2g) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "_OCTI?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback)octi_query_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ACCESS_TECHNOLOGIES_STEP_OWCTI:
        if (ctx->check_3g) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "_OWCTI?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback)owcti_query_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ACCESS_TECHNOLOGIES_STEP_LAST:
        /* All done, set result and complete */
        g_task_return_int (task, ctx->access_technology);
        g_object_unref (task);
        break;

    default:
        g_assert_not_reached ();
    }
}

static void
run_access_technology_loading_sequence (MMIfaceModem *self,
                                        AccessTechnologiesStep first,
                                        gboolean check_2g,
                                        gboolean check_3g,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    AccessTechnologiesContext *ctx;
    GTask *task;

    ctx = g_new (AccessTechnologiesContext, 1);
    ctx->step = first;
    ctx->check_2g = check_2g;
    ctx->check_3g = check_3g;
    ctx->access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    load_access_technologies_step (task);
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    run_access_technology_loading_sequence (self,
                                            ACCESS_TECHNOLOGIES_STEP_FIRST,
                                            TRUE, /* check 2g */
                                            TRUE, /* check 3g */
                                            callback,
                                            user_data);
}

/*****************************************************************************/
/* After power up (Modem interface) */

static gboolean
modem_after_power_up_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_power_up_wait_cb (GTask *task)
{
    MMBroadbandModemOption *self;

    self = g_task_get_source_object (task);
    self->priv->after_power_up_wait_id = 0;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
modem_after_power_up (MMIfaceModem *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemOption *self = MM_BROADBAND_MODEM_OPTION (_self);

    /* Some Option devices return OK on +CFUN=1 right away but need some time
     * to finish initialization.
     */
    g_warn_if_fail (self->priv->after_power_up_wait_id == 0);
    self->priv->after_power_up_wait_id =
        g_timeout_add_seconds (10,
                               (GSourceFunc)after_power_up_wait_cb,
                               g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* IMSI loading (3GPP interface) */

static gchar *
modem_3gpp_load_imei_finish (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             GError **error)
{
    gchar *imei;
    gchar *comma;

    imei = g_strdup (mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error));
    if (!imei)
        return NULL;

    /* IMEI reported by Option modems contain the IMEI plus something else:
     *
     * (ttyHS4): --> 'AT+CGSN<CR>'
     * (ttyHS4): <-- '<CR><LF>357516032005989,TR19A8P11R<CR><LF><CR><LF>OK<CR><LF>'
     */
    comma = strchr (imei, ',');
    if (comma)
        *comma = '\0';

    return imei;
}

static void
modem_3gpp_load_imei (MMIfaceModem3gpp *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGSN",
                              3,
                              TRUE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static void
option_ossys_tech_changed (MMPortSerialAt *port,
                           GMatchInfo *info,
                           MMBroadbandModemOption *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        ossys_to_mm (str[0], &act);
        g_free (str);
    }

    mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                               act,
                                               MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);

    /* _OSSYSI only indicates general 2G/3G mode, so queue up some explicit
     * access technology requests.
     */
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_GPRS)
        run_access_technology_loading_sequence (MM_IFACE_MODEM (self),
                                                ACCESS_TECHNOLOGIES_STEP_OCTI,
                                                TRUE, /* check 2g */
                                                FALSE, /* check 3g */
                                                NULL,
                                                NULL);
    else if (act == MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
        run_access_technology_loading_sequence (MM_IFACE_MODEM (self),
                                                ACCESS_TECHNOLOGIES_STEP_OWCTI,
                                                FALSE, /* check 2g */
                                                TRUE, /* check 3g */
                                                NULL,
                                                NULL);
}

static void
option_2g_tech_changed (MMPortSerialAt *port,
                        GMatchInfo *match_info,
                        MMBroadbandModemOption *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;

    str = g_match_info_fetch (match_info, 1);
    if (str && octi_to_mm (str[0], &act))
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   act,
                                                   MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
    g_free (str);
}

static void
option_3g_tech_changed (MMPortSerialAt *port,
                        GMatchInfo *match_info,
                        MMBroadbandModemOption *self)
{
    MMModemAccessTechnology act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    gchar *str;

    str = g_match_info_fetch (match_info, 1);
    if (str && owcti_to_mm (str[0], &act))
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   act,
                                                   MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
    g_free (str);
}

static void
option_signal_changed (MMPortSerialAt *port,
                       GMatchInfo *match_info,
                       MMBroadbandModemOption *self)
{
    gchar *str;
    guint quality = 0;

    str = g_match_info_fetch (match_info, 1);
    if (str) {
        quality = atoi (str);
        g_free (str);
    }

    if (quality == 99) {
        /* 99 means unknown */
        quality = 0;
    } else {
        /* Normalize the quality */
        quality = MM_CLAMP_HIGH (quality, 31) * 100 / 31;
    }

    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemOption *self,
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
            self->priv->_ossysi_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)option_ossys_tech_changed : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->_octi_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)option_2g_tech_changed : NULL,
            enable ? self : NULL,
            NULL);
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->_ouwcti_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)option_3g_tech_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Signal quality related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->_osigq_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)option_signal_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Other unsolicited events to always ignore */
        if (!enable)
            mm_port_serial_at_add_unsolicited_msg_handler (
                ports[i],
                self->priv->ignore_regex,
                NULL, NULL, NULL);
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
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_OPTION (self), TRUE);
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
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_OPTION (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Enabling unsolicited events (3GPP interface) */

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

static const MMBaseModemAtCommand unsolicited_enable_sequence[] = {
    { "_OSSYS=1",  3, FALSE, NULL },
    { "_OCTI=1",   3, FALSE, NULL },
    { "_OUWCTI=1", 3, FALSE, NULL },
    { "_OSQI=1",   3, FALSE, NULL },
    { NULL }
};

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult *res,
                                        GTask *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
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
/* Disabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static const MMBaseModemAtCommand unsolicited_disable_sequence[] = {
    { "_OSSYS=0",  3, FALSE, NULL },
    { "_OCTI=0",   3, FALSE, NULL },
    { "_OUWCTI=0", 3, FALSE, NULL },
    { "_OSQI=0",   3, FALSE, NULL },
    { NULL }
};

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
    /* Our own disable first */
    mm_base_modem_at_sequence_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        unsolicited_disable_sequence,
        NULL, /* response_processor_context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)own_disable_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_option_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle when enabled */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_OPTION (self), FALSE);
}

/*****************************************************************************/

static gboolean
is_nozomi (const gchar **drivers)
{
    if (drivers) {
        guint i;

        for (i = 0; drivers[i]; i++) {
            if (g_str_equal (drivers[i], "nozomi"))
                return TRUE;
        }
    }

    return FALSE;
}

MMBroadbandModemOption *
mm_broadband_modem_option_new (const gchar *device,
                               const gchar **drivers,
                               const gchar *plugin,
                               guint16 vendor_id,
                               guint16 product_id)
{
    MMModem3gppFacility ignored;

    /* Ignore PH-SIM facility in 'nozomi' managed modems */
    ignored = is_nozomi (drivers) ? MM_MODEM_3GPP_FACILITY_PH_SIM : MM_MODEM_3GPP_FACILITY_NONE;

    return g_object_new (MM_TYPE_BROADBAND_MODEM_OPTION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_IFACE_MODEM_3GPP_IGNORED_FACILITY_LOCKS, ignored,
                         NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemOption *self = MM_BROADBAND_MODEM_OPTION (object);

    g_regex_unref (self->priv->_ossysi_regex);
    g_regex_unref (self->priv->_octi_regex);
    g_regex_unref (self->priv->_ouwcti_regex);
    g_regex_unref (self->priv->_osigq_regex);
    g_regex_unref (self->priv->ignore_regex);

    G_OBJECT_CLASS (mm_broadband_modem_option_parent_class)->finalize (object);
}

static void
mm_broadband_modem_option_init (MMBroadbandModemOption *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_OPTION,
                                              MMBroadbandModemOptionPrivate);
    self->priv->after_power_up_wait_id = 0;

    /* Prepare regular expressions to setup */
    self->priv->_ossysi_regex = g_regex_new ("\\r\\n_OSSYSI:\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_octi_regex = g_regex_new ("\\r\\n_OCTI:\\s*(\\d+)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_ouwcti_regex = g_regex_new ("\\r\\n_OUWCTI:\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_osigq_regex = g_regex_new ("\\r\\n_OSIGQ:\\s*(\\d+),(\\d)\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->ignore_regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->modem_after_power_up = modem_after_power_up;
    iface->modem_after_power_up_finish = modem_after_power_up_finish;
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
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;
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
mm_broadband_modem_option_class_init (MMBroadbandModemOptionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemOptionPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
