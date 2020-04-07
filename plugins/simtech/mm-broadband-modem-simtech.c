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
#include "mm-log-object.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-voice.h"
#include "mm-shared-simtech.h"
#include "mm-broadband-modem-simtech.h"

static void iface_modem_init          (MMIfaceModem         *iface);
static void iface_modem_3gpp_init     (MMIfaceModem3gpp     *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_voice_init    (MMIfaceModemVoice    *iface);
static void shared_simtech_init       (MMSharedSimtech      *iface);

static MMIfaceModem         *iface_modem_parent;
static MMIfaceModem3gpp     *iface_modem_3gpp_parent;
static MMIfaceModemLocation *iface_modem_location_parent;
static MMIfaceModemVoice    *iface_modem_voice_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemSimtech, mm_broadband_modem_simtech, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_VOICE, iface_modem_voice_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_SIMTECH, shared_simtech_init))

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED
} FeatureSupport;

struct _MMBroadbandModemSimtechPrivate {
    FeatureSupport  cnsmod_support;
    FeatureSupport  autocsq_support;
    GRegex         *cnsmod_regex;
    GRegex         *csq_regex;
};

/*****************************************************************************/
/* Setup/Cleanup unsolicited events (3GPP interface) */

static MMModemAccessTechnology
simtech_act_to_mm_act (guint nsmod)
{
    static const MMModemAccessTechnology simtech_act_to_mm_act_map[] = {
        [0] = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
        [1] = MM_MODEM_ACCESS_TECHNOLOGY_GSM,
        [2] = MM_MODEM_ACCESS_TECHNOLOGY_GPRS,
        [3] = MM_MODEM_ACCESS_TECHNOLOGY_EDGE,
        [4] = MM_MODEM_ACCESS_TECHNOLOGY_UMTS,
        [5] = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA,
        [6] = MM_MODEM_ACCESS_TECHNOLOGY_HSUPA,
        [7] = MM_MODEM_ACCESS_TECHNOLOGY_HSPA,
        [8] = MM_MODEM_ACCESS_TECHNOLOGY_LTE,
    };

    return (nsmod < G_N_ELEMENTS (simtech_act_to_mm_act_map) ? simtech_act_to_mm_act_map[nsmod] : MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
simtech_tech_changed (MMPortSerialAt *port,
                      GMatchInfo *match_info,
                      MMBroadbandModemSimtech *self)
{
    guint simtech_act = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &simtech_act))
        return;

    mm_iface_modem_update_access_technologies (
        MM_IFACE_MODEM (self),
        simtech_act_to_mm_act (simtech_act),
        MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

static void
simtech_signal_changed (MMPortSerialAt *port,
                        GMatchInfo *match_info,
                        MMBroadbandModemSimtech *self)
{
    guint quality = 0;

    if (!mm_get_uint_from_match_info (match_info, 1, &quality))
        return;

    if (quality != 99)
        quality = MM_CLAMP_HIGH (quality, 31) * 100 / 31;
    else
        quality = 0;

    mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
}

static void
set_unsolicited_events_handlers (MMBroadbandModemSimtech *self,
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
            self->priv->cnsmod_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)simtech_tech_changed : NULL,
            enable ? self : NULL,
            NULL);

        /* Signal quality related */
        mm_port_serial_at_add_unsolicited_msg_handler (
            ports[i],
            self->priv->csq_regex,
            enable ? (MMPortSerialAtUnsolicitedMsgFn)simtech_signal_changed : NULL,
            enable ? self : NULL,
            NULL);
    }
}

static gboolean
modem_3gpp_setup_cleanup_unsolicited_events_finish (MMIfaceModem3gpp  *self,
                                                    GAsyncResult      *res,
                                                    GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult     *res,
                                       GTask            *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->setup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* Our own setup now */
        set_unsolicited_events_handlers (MM_BROADBAND_MODEM_SIMTECH (self), TRUE);
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
                                         GAsyncResult     *res,
                                         GTask            *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->cleanup_unsolicited_events_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_cleanup_unsolicited_events (MMIfaceModem3gpp    *self,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    /* Our own cleanup first */
    set_unsolicited_events_handlers (MM_BROADBAND_MODEM_SIMTECH (self), FALSE);

    /* And now chain up parent's cleanup */
    iface_modem_3gpp_parent->cleanup_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_cleanup_unsolicited_events_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Enable unsolicited events (3GPP interface) */

typedef enum {
    ENABLE_UNSOLICITED_EVENTS_STEP_FIRST,
    ENABLE_UNSOLICITED_EVENTS_STEP_PARENT,
    ENABLE_UNSOLICITED_EVENTS_STEP_CHECK_SUPPORT_CNSMOD,
    ENABLE_UNSOLICITED_EVENTS_STEP_ENABLE_CNSMOD,
    ENABLE_UNSOLICITED_EVENTS_STEP_CHECK_SUPPORT_AUTOCSQ,
    ENABLE_UNSOLICITED_EVENTS_STEP_ENABLE_AUTOCSQ,
    ENABLE_UNSOLICITED_EVENTS_STEP_LAST,
} EnableUnsolicitedEventsStep;

typedef struct {
    EnableUnsolicitedEventsStep step;
} EnableUnsolicitedEventsContext;

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp  *self,
                                             GAsyncResult      *res,
                                             GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void enable_unsolicited_events_context_step (GTask *task);

static void
autocsq_set_enabled_ready (MMBaseModem  *self,
                           GAsyncResult *res,
                           GTask        *task)
{
    EnableUnsolicitedEventsContext *ctx;
    GError                         *error = NULL;
    gboolean                        csq_urcs_enabled = FALSE;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't enable automatic signal quality reporting: %s", error->message);
        g_error_free (error);
    } else
        csq_urcs_enabled = TRUE;

    /* Disable access technology polling if we can use the +CSQ URCs */
    g_object_set (self,
                  MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, csq_urcs_enabled,
                  NULL);

    /* go to next step */
    ctx->step++;
    enable_unsolicited_events_context_step (task);
}

static void
autocsq_test_ready (MMBaseModem  *_self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMBroadbandModemSimtech        *self;
    EnableUnsolicitedEventsContext *ctx;

    self = MM_BROADBAND_MODEM_SIMTECH (_self);
    ctx  = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (_self, res, NULL))
        self->priv->autocsq_support = FEATURE_NOT_SUPPORTED;
    else
        self->priv->autocsq_support = FEATURE_SUPPORTED;

    /* go to next step */
    ctx->step++;
    enable_unsolicited_events_context_step (task);
}

static void
cnsmod_set_enabled_ready (MMBaseModem  *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    EnableUnsolicitedEventsContext *ctx;
    GError                         *error = NULL;
    gboolean                        cnsmod_urcs_enabled = FALSE;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't enable automatic access technology reporting: %s", error->message);
        g_error_free (error);
    } else
        cnsmod_urcs_enabled = TRUE;

    /* Disable access technology polling if we can use the +CNSMOD URCs */
    g_object_set (self,
                  MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED, cnsmod_urcs_enabled,
                  NULL);

    /* go to next step */
    ctx->step++;
    enable_unsolicited_events_context_step (task);
}

static void
cnsmod_test_ready (MMBaseModem  *_self,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBroadbandModemSimtech        *self;
    EnableUnsolicitedEventsContext *ctx;

    self = MM_BROADBAND_MODEM_SIMTECH (_self);
    ctx  = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (_self, res, NULL))
        self->priv->cnsmod_support = FEATURE_NOT_SUPPORTED;
    else
        self->priv->cnsmod_support = FEATURE_SUPPORTED;

    /* go to next step */
    ctx->step++;
    enable_unsolicited_events_context_step (task);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult     *res,
                                        GTask            *task)
{
    EnableUnsolicitedEventsContext *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* go to next step */
    ctx->step++;
    enable_unsolicited_events_context_step (task);
}

static void
enable_unsolicited_events_context_step (GTask *task)
{
    MMBroadbandModemSimtech        *self;
    EnableUnsolicitedEventsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLE_UNSOLICITED_EVENTS_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLE_UNSOLICITED_EVENTS_STEP_PARENT:
        iface_modem_3gpp_parent->enable_unsolicited_events (
            MM_IFACE_MODEM_3GPP (self),
            (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
            task);
        return;

    case ENABLE_UNSOLICITED_EVENTS_STEP_CHECK_SUPPORT_CNSMOD:
        if (self->priv->cnsmod_support == FEATURE_SUPPORT_UNKNOWN) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "+CNSMOD=?",
                                      3,
                                      TRUE,
                                      (GAsyncReadyCallback)cnsmod_test_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLE_UNSOLICITED_EVENTS_STEP_ENABLE_CNSMOD:
        if (self->priv->cnsmod_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      /* Autoreport +CNSMOD when it changes */
                                      "+CNSMOD=1",
                                      20,
                                      FALSE,
                                      (GAsyncReadyCallback)cnsmod_set_enabled_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLE_UNSOLICITED_EVENTS_STEP_CHECK_SUPPORT_AUTOCSQ:
        if (self->priv->autocsq_support == FEATURE_SUPPORT_UNKNOWN) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "+AUTOCSQ=?",
                                      3,
                                      TRUE,
                                      (GAsyncReadyCallback)autocsq_test_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLE_UNSOLICITED_EVENTS_STEP_ENABLE_AUTOCSQ:
        if (self->priv->autocsq_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      /* Autoreport+ CSQ (first arg), and only report when it changes (second arg) */
                                      "+AUTOCSQ=1,1",
                                      20,
                                      FALSE,
                                      (GAsyncReadyCallback)autocsq_set_enabled_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLE_UNSOLICITED_EVENTS_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp    *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    EnableUnsolicitedEventsContext *ctx;
    GTask                          *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new (EnableUnsolicitedEventsContext, 1);
    ctx->step = ENABLE_UNSOLICITED_EVENTS_STEP_FIRST;
    g_task_set_task_data (task, ctx, g_free);

    enable_unsolicited_events_context_step (task);
}

/*****************************************************************************/
/* Disable unsolicited events (3GPP interface) */

typedef enum {
    DISABLE_UNSOLICITED_EVENTS_STEP_FIRST,
    DISABLE_UNSOLICITED_EVENTS_STEP_DISABLE_AUTOCSQ,
    DISABLE_UNSOLICITED_EVENTS_STEP_DISABLE_CNSMOD,
    DISABLE_UNSOLICITED_EVENTS_STEP_PARENT,
    DISABLE_UNSOLICITED_EVENTS_STEP_LAST,
} DisableUnsolicitedEventsStep;

typedef struct {
    DisableUnsolicitedEventsStep step;
} DisableUnsolicitedEventsContext;

static gboolean
modem_3gpp_disable_unsolicited_events_finish (MMIfaceModem3gpp  *self,
                                              GAsyncResult      *res,
                                              GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disable_unsolicited_events_context_step (GTask *task);

static void
parent_disable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                         GAsyncResult     *res,
                                         GTask            *task)
{
    DisableUnsolicitedEventsContext *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!iface_modem_3gpp_parent->disable_unsolicited_events_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* go to next step */
    ctx->step++;
    disable_unsolicited_events_context_step (task);
}

static void
cnsmod_set_disabled_ready (MMBaseModem  *self,
                           GAsyncResult *res,
                           GTask        *task)
{
    DisableUnsolicitedEventsContext *ctx;
    GError                          *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't disable automatic access technology reporting: %s", error->message);
        g_error_free (error);
    }

    /* go to next step */
    ctx->step++;
    disable_unsolicited_events_context_step (task);
}

static void
autocsq_set_disabled_ready (MMBaseModem  *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    DisableUnsolicitedEventsContext *ctx;
    GError                          *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't disable automatic signal quality reporting: %s", error->message);
        g_error_free (error);
    }

    /* go to next step */
    ctx->step++;
    disable_unsolicited_events_context_step (task);
}

static void
disable_unsolicited_events_context_step (GTask *task)
{
    MMBroadbandModemSimtech         *self;
    DisableUnsolicitedEventsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLE_UNSOLICITED_EVENTS_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLE_UNSOLICITED_EVENTS_STEP_DISABLE_AUTOCSQ:
        if (self->priv->autocsq_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "+AUTOCSQ=0",
                                      20,
                                      FALSE,
                                      (GAsyncReadyCallback)autocsq_set_disabled_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLE_UNSOLICITED_EVENTS_STEP_DISABLE_CNSMOD:
        if (self->priv->cnsmod_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "+CNSMOD=0",
                                      20,
                                      FALSE,
                                      (GAsyncReadyCallback)cnsmod_set_disabled_ready,
                                      task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLE_UNSOLICITED_EVENTS_STEP_PARENT:
        iface_modem_3gpp_parent->disable_unsolicited_events (
            MM_IFACE_MODEM_3GPP (self),
            (GAsyncReadyCallback)parent_disable_unsolicited_events_ready,
            task);
        return;

    case DISABLE_UNSOLICITED_EVENTS_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp    *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    DisableUnsolicitedEventsContext *ctx;
    GTask                          *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new (DisableUnsolicitedEventsContext, 1);
    ctx->step = DISABLE_UNSOLICITED_EVENTS_STEP_FIRST;
    g_task_set_task_data (task, ctx, g_free);

    disable_unsolicited_events_context_step (task);
}

/*****************************************************************************/
/* Load access technologies (Modem interface) */

static gboolean
load_access_technologies_finish (MMIfaceModem             *self,
                                 GAsyncResult             *res,
                                 MMModemAccessTechnology  *access_technologies,
                                 guint                    *mask,
                                 GError                  **error)
{
    GError *inner_error = NULL;
    gssize  act;

    act = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *access_technologies = (MMModemAccessTechnology) act;
    *mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    return TRUE;
}

static void
cnsmod_query_ready (MMBaseModem  *self,
                    GAsyncResult *res,
                    GTask        *task)
{
    const gchar *response, *p;
    GError      *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    p = mm_strip_tag (response, "+CNSMOD:");
    if (p)
        p = strchr (p, ',');

    if (!p || !isdigit (*(p + 1)))
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the +CNSMOD response: '%s'",
            response);
    else
        g_task_return_int (task, simtech_act_to_mm_act (atoi (p + 1)));
    g_object_unref (task);
}

static void
load_access_technologies (MMIfaceModem        *_self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MMBroadbandModemSimtech *self;
    GTask                   *task;

    self = MM_BROADBAND_MODEM_SIMTECH (_self);
    task = g_task_new (self, NULL, callback, user_data);

    /* Launch query only for 3GPP modems */
    if (!mm_iface_modem_is_3gpp (_self)) {
        g_task_return_int (task, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        g_object_unref (task);
        return;
    }

    g_assert (self->priv->cnsmod_support != FEATURE_SUPPORT_UNKNOWN);
    if (self->priv->cnsmod_support == FEATURE_NOT_SUPPORTED) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Loading access technologies with +CNSMOD is not supported");
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "AT+CNSMOD?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnsmod_query_ready,
        task);
}

/*****************************************************************************/
/* Load signal quality (Modem interface) */

static guint
load_signal_quality_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    gssize value;

    value = g_task_propagate_int (G_TASK (res), error);
    return value < 0 ? 0 : value;
}

static void
csq_query_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    const gchar *response, *p;
    GError      *error = NULL;
    gint         quality;
    gint         ber;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Given that we may have enabled AUTOCSQ support, it is totally possible
     * to get an empty string at this point, because the +CSQ reply may have
     * been processed as an URC already. If we ever see this, we should not return
     * an error, because that would reset the reported signal quality to 0 :/
     * So, in this case, return the last cached signal quality value. */
    if (!response[0]) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS,
                                 "already refreshed via URCs");
        g_object_unref (task);
        return;
    }

    p = mm_strip_tag (response, "+CSQ:");
    if (sscanf (p, "%d, %d", &quality, &ber)) {
        if (quality != 99)
            quality = CLAMP (quality, 0, 31) * 100 / 31;
        else
            quality = 0;
        g_task_return_int (task, quality);
        g_object_unref (task);
        return;
    }

    g_task_return_new_error (task,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Could not parse signal quality results");
    g_object_unref (task);
}

static void
load_signal_quality (MMIfaceModem        *self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CSQ",
        3,
        FALSE,
        (GAsyncReadyCallback)csq_query_ready,
        task);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem  *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_supported_modes_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    GError                 *error = NULL;
    GArray                 *all;
    GArray                 *combinations;
    GArray                 *filtered;
    MMModemModeCombination  mode;

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
load_supported_modes (MMIfaceModem        *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    /* Run parent's loading */
    iface_modem_parent->load_supported_modes (
        MM_IFACE_MODEM (self),
        (GAsyncReadyCallback)parent_load_supported_modes_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Load initial allowed/preferred modes (Modem interface) */

typedef struct {
    MMModemMode allowed;
    MMModemMode preferred;
} LoadCurrentModesResult;

typedef struct {
    gint acqord;
    gint modepref;
} LoadCurrentModesContext;

static gboolean
load_current_modes_finish (MMIfaceModem  *self,
                           GAsyncResult  *res,
                           MMModemMode   *allowed,
                           MMModemMode   *preferred,
                           GError       **error)
{
    LoadCurrentModesResult *result;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return FALSE;

    *allowed   = result->allowed;
    *preferred = result->preferred;
    g_free (result);
    return TRUE;
}

static void
cnmp_query_ready (MMBroadbandModemSimtech *self,
                  GAsyncResult            *res,
                  GTask                   *task)
{
    LoadCurrentModesContext *ctx;
    LoadCurrentModesResult  *result;
    const gchar             *response, *p;
    GError                  *error = NULL;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    p = mm_strip_tag (response, "+CNMP:");
    if (!p) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse the mode preference response: '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    result = g_new (LoadCurrentModesResult, 1);
    result->allowed   = MM_MODEM_MODE_NONE;
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
            g_task_return_new_error (
                task,
                MM_CORE_ERROR,
                MM_CORE_ERROR_FAILED,
                "Unknown acquisition order preference: '%d'",
                ctx->acqord);
            g_object_unref (task);
            g_free (result);
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
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Unknown mode preference: '%d'",
            ctx->modepref);
        g_object_unref (task);
        g_free (result);
        return;
    }

    g_task_return_pointer (task, result, g_free);
    g_object_unref (task);
}

static void
cnaop_query_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    LoadCurrentModesContext *ctx;
    const gchar             *response, *p;
    GError                  *error = NULL;

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    p = mm_strip_tag (response, "+CNAOP:");
    if (p)
        ctx->acqord = atoi (p);

    if (ctx->acqord < 0 || ctx->acqord > 2) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Failed to parse the acquisition order response: '%s'",
            response);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CNMP?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnmp_query_ready,
        task);
}

static void
load_current_modes (MMIfaceModem        *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask                   *task;
    LoadCurrentModesContext *ctx;

    ctx = g_new (LoadCurrentModesContext, 1);
    ctx->acqord   = -1;
    ctx->modepref = -1;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+CNAOP?",
        3,
        FALSE,
        (GAsyncReadyCallback)cnaop_query_ready,
        task);
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef struct {
    guint nmp;   /* mode preference */
    guint naop;  /* acquisition order */
} SetCurrentModesContext;

static gboolean
set_current_modes_finish (MMIfaceModem  *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cnaop_set_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
cnmp_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    SetCurrentModesContext *ctx;
    GError                 *error = NULL;
    gchar                  *command;

    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (error) {
        /* Let the error be critical. */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    command = g_strdup_printf ("+CNAOP=%u", ctx->naop);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)cnaop_set_ready,
        task);
    g_free (command);
}

static void
set_current_modes (MMIfaceModem        *self,
                   MMModemMode          allowed,
                   MMModemMode          preferred,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask                  *task;
    SetCurrentModesContext *ctx;
    gchar                  *command;

    /* Defaults: automatic search */
    ctx = g_new (SetCurrentModesContext, 1);
    ctx->nmp  = 2;
    ctx->naop = 0;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE) {
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

    command = g_strdup_printf ("+CNMP=%u", ctx->nmp);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        3,
        FALSE,
        (GAsyncReadyCallback)cnmp_set_ready,
        task);
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
                         MM_BROADBAND_MODEM_INDICATORS_DISABLED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_simtech_init (MMBroadbandModemSimtech *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_SIMTECH,
                                              MMBroadbandModemSimtechPrivate);

    self->priv->cnsmod_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->autocsq_support = FEATURE_SUPPORT_UNKNOWN;

    self->priv->cnsmod_regex = g_regex_new ("\\r\\n\\+CNSMOD:\\s*(\\d+)\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->csq_regex    = g_regex_new ("\\r\\n\\+CSQ:\\s*(\\d+),(\\d+)\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemSimtech *self = MM_BROADBAND_MODEM_SIMTECH (object);

    g_regex_unref (self->priv->cnsmod_regex);
    g_regex_unref (self->priv->csq_regex);

    G_OBJECT_CLASS (mm_broadband_modem_simtech_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
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
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_simtech_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_simtech_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_simtech_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_simtech_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_simtech_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_simtech_disable_location_gathering_finish;
}

static MMIfaceModemLocation *
peek_parent_location_interface (MMSharedSimtech *self)
{
    return iface_modem_location_parent;
}

static void
iface_modem_voice_init (MMIfaceModemVoice *iface)
{
    iface_modem_voice_parent = g_type_interface_peek_parent (iface);

    iface->check_support                     = mm_shared_simtech_voice_check_support;
    iface->check_support_finish              = mm_shared_simtech_voice_check_support_finish;
    iface->enable_unsolicited_events         = mm_shared_simtech_voice_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish  = mm_shared_simtech_voice_enable_unsolicited_events_finish;
    iface->disable_unsolicited_events        = mm_shared_simtech_voice_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = mm_shared_simtech_voice_disable_unsolicited_events_finish;
    iface->setup_unsolicited_events          = mm_shared_simtech_voice_setup_unsolicited_events;
    iface->setup_unsolicited_events_finish   = mm_shared_simtech_voice_setup_unsolicited_events_finish;
    iface->cleanup_unsolicited_events        = mm_shared_simtech_voice_cleanup_unsolicited_events;
    iface->cleanup_unsolicited_events_finish = mm_shared_simtech_voice_cleanup_unsolicited_events_finish;
    iface->setup_in_call_audio_channel          = mm_shared_simtech_voice_setup_in_call_audio_channel;
    iface->setup_in_call_audio_channel_finish   = mm_shared_simtech_voice_setup_in_call_audio_channel_finish;
    iface->cleanup_in_call_audio_channel        = mm_shared_simtech_voice_cleanup_in_call_audio_channel;
    iface->cleanup_in_call_audio_channel_finish = mm_shared_simtech_voice_cleanup_in_call_audio_channel_finish;

}

static MMIfaceModemVoice *
peek_parent_voice_interface (MMSharedSimtech *self)
{
    return iface_modem_voice_parent;
}

static void
shared_simtech_init (MMSharedSimtech *iface)
{
    iface->peek_parent_location_interface = peek_parent_location_interface;
    iface->peek_parent_voice_interface    = peek_parent_voice_interface;
}

static void
mm_broadband_modem_simtech_class_init (MMBroadbandModemSimtechClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemSimtechPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->setup_ports = setup_ports;
}
