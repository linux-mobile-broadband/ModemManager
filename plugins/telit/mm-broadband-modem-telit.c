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
#include "mm-telit-enums-types.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

static MMIfaceModem *iface_modem_parent;
static MMIfaceModem3gpp *iface_modem_3gpp_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemTelit, mm_broadband_modem_telit, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

#define CSIM_UNLOCK_MAX_TIMEOUT 3

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED
} FeatureSupport;

struct _MMBroadbandModemTelitPrivate {
    FeatureSupport csim_lock_support;
    MMTelitQssStatus qss_status;
    MMTelitCsimLockState csim_lock_state;
    GTask *csim_lock_task;
    guint csim_lock_timeout_id;
};

/*****************************************************************************/
/* After Sim Unlock (Modem interface) */
static gboolean
modem_after_sim_unlock_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
after_sim_unlock_ready (GTask *task)
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

    /* A short delay is necessary with some SIMs when
    they have just been unlocked. Using 1 second as secure margin. */
    g_timeout_add_seconds (1, (GSourceFunc) after_sim_unlock_ready, task);
}

/*****************************************************************************/
/* Setup SIM hot swap (Modem interface) */

typedef enum {
    QSS_SETUP_STEP_FIRST,
    QSS_SETUP_STEP_QUERY,
    QSS_SETUP_STEP_ENABLE_PRIMARY_PORT,
    QSS_SETUP_STEP_ENABLE_SECONDARY_PORT,
    QSS_SETUP_STEP_LAST
} QssSetupStep;

typedef struct {
    QssSetupStep step;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    GError *primary_error;
    GError *secondary_error;
} QssSetupContext;

static void qss_setup_step (GTask *task);
static void pending_csim_unlock_complete (MMBroadbandModemTelit *self);

static void
telit_qss_unsolicited_handler (MMPortSerialAt *port,
                               GMatchInfo *match_info,
                               MMBroadbandModemTelit *self)
{
    MMTelitQssStatus cur_qss_status;
    MMTelitQssStatus prev_qss_status;

    if (!mm_get_int_from_match_info (match_info, 1, (gint*)&cur_qss_status))
        return;

    prev_qss_status = self->priv->qss_status;
    self->priv->qss_status = cur_qss_status;

    if (self->priv->csim_lock_state >= CSIM_LOCK_STATE_LOCK_REQUESTED) {

        if (prev_qss_status > QSS_STATUS_SIM_REMOVED && cur_qss_status == QSS_STATUS_SIM_REMOVED) {
            mm_dbg ("QSS handler: #QSS=0 after +CSIM=1 -> CSIM locked!");
            self->priv->csim_lock_state = CSIM_LOCK_STATE_LOCKED;
        }

        if (prev_qss_status == QSS_STATUS_SIM_REMOVED && cur_qss_status != QSS_STATUS_SIM_REMOVED) {
            mm_dbg ("QSS handler: #QSS>=1 after +CSIM=0 -> CSIM unlocked!");
            self->priv->csim_lock_state = CSIM_LOCK_STATE_UNLOCKED;

            if (self->priv->csim_lock_timeout_id) {
                g_source_remove (self->priv->csim_lock_timeout_id);
                self->priv->csim_lock_timeout_id = 0;
            }

            pending_csim_unlock_complete (self);
        }

        return;
    }

    if (cur_qss_status != prev_qss_status)
        mm_dbg ("QSS handler: status changed '%s -> %s",
                mm_telit_qss_status_get_string (prev_qss_status),
                mm_telit_qss_status_get_string (cur_qss_status));

    if ((prev_qss_status == QSS_STATUS_SIM_REMOVED && cur_qss_status != QSS_STATUS_SIM_REMOVED) ||
        (prev_qss_status > QSS_STATUS_SIM_REMOVED && cur_qss_status == QSS_STATUS_SIM_REMOVED)) {
        mm_info ("QSS handler: SIM swap detected");
        mm_broadband_modem_update_sim_hot_swap_detected (MM_BROADBAND_MODEM (self));
    }
}

static void
qss_setup_context_free (QssSetupContext *ctx)
{
    g_clear_object (&(ctx->primary));
    g_clear_object (&(ctx->secondary));
    g_clear_error (&(ctx->primary_error));
    g_clear_error (&(ctx->secondary_error));
    g_slice_free (QssSetupContext, ctx);
}

static gboolean
modem_setup_sim_hot_swap_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
telit_qss_enable_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        GTask *task)
{
    QssSetupContext *ctx;
    MMPortSerialAt *port;
    GError **error;
    GRegex *pattern;

    ctx = g_task_get_task_data (task);

    if (ctx->step == QSS_SETUP_STEP_ENABLE_PRIMARY_PORT) {
        port = ctx->primary;
        error = &ctx->primary_error;
    } else if (ctx->step == QSS_SETUP_STEP_ENABLE_SECONDARY_PORT) {
        port = ctx->secondary;
        error = &ctx->secondary_error;
    } else
        g_assert_not_reached ();

    if (!mm_base_modem_at_command_full_finish (self, res, error)) {
        mm_warn ("QSS: error enabling unsolicited on port %s: %s", mm_port_get_device (MM_PORT (port)), (*error)->message);
        goto next_step;
    }

    pattern = g_regex_new ("#QSS:\\s*([0-3])\\r\\n", G_REGEX_RAW, 0, NULL);
    g_assert (pattern);
    mm_port_serial_at_add_unsolicited_msg_handler (
        port,
        pattern,
        (MMPortSerialAtUnsolicitedMsgFn)telit_qss_unsolicited_handler,
        self,
        NULL);
    g_regex_unref (pattern);

next_step:
    ctx->step++;
    qss_setup_step (task);
}

static void
telit_qss_query_ready (MMBaseModem *_self,
                       GAsyncResult *res,
                       GTask *task)
{
    MMBroadbandModemTelit *self;
    GError *error = NULL;
    const gchar *response;
    MMTelitQssStatus qss_status;
    QssSetupContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (error) {
        mm_warn ("Could not get \"#QSS?\" reply: %s", error->message);
        g_error_free (error);
        goto next_step;
    }

    qss_status = mm_telit_parse_qss_query (response, &error);
    if (error) {
        mm_warn ("QSS query parse error: %s", error->message);
        g_error_free (error);
        goto next_step;
    }

    mm_info ("QSS: current status is '%s'", mm_telit_qss_status_get_string (qss_status));
    self->priv->qss_status = qss_status;

next_step:
    ctx->step++;
    qss_setup_step (task);
}

static void
qss_setup_step (GTask *task)
{
    QssSetupContext *ctx;
    MMBroadbandModemTelit *self;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
        case QSS_SETUP_STEP_FIRST:
            /* Fall back on next step */
            ctx->step++;
        case QSS_SETUP_STEP_QUERY:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "#QSS?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback) telit_qss_query_ready,
                                      task);
            return;
        case QSS_SETUP_STEP_ENABLE_PRIMARY_PORT:
            mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                           ctx->primary,
                                           "#QSS=1",
                                           3,
                                           FALSE,
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback) telit_qss_enable_ready,
                                           task);
            return;
        case QSS_SETUP_STEP_ENABLE_SECONDARY_PORT:
            if (ctx->secondary) {
                mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                               ctx->secondary,
                                               "#QSS=1",
                                               3,
                                               FALSE,
                                               FALSE, /* raw */
                                               NULL, /* cancellable */
                                               (GAsyncReadyCallback) telit_qss_enable_ready,
                                               task);
                return;
            }
            /* Fall back to next step */
            ctx->step++;
        case QSS_SETUP_STEP_LAST:
            /* If all enabling actions failed (either both, or only primary if
             * there is no secondary), then we return an error */
            if (ctx->primary_error &&
                (ctx->secondary_error || !ctx->secondary))
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "QSS: couldn't enable unsolicited");
            else
                g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            break;
    }
}

static void
modem_setup_sim_hot_swap (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    QssSetupContext *ctx;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (QssSetupContext);
    ctx->step = QSS_SETUP_STEP_FIRST;
    ctx->primary = mm_base_modem_get_port_primary (MM_BASE_MODEM (self));
    ctx->secondary = mm_base_modem_get_port_secondary (MM_BASE_MODEM (self));

    g_task_set_task_data (task, ctx, (GDestroyNotify) qss_setup_context_free);
    qss_setup_step (task);
}

/*****************************************************************************/
/* Set current bands (Modem interface) */

static gboolean
modem_set_current_bands_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
modem_set_current_bands_ready (MMBaseModem *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
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
    GTask *task;

    mm_telit_get_band_flag (bands_array, &flag2g, &flag3g, &flag4g);

    is_2g = mm_iface_modem_is_2g (self);
    is_3g = mm_iface_modem_is_3g (self);
    is_4g = mm_iface_modem_is_4g (self);

    if (is_2g && flag2g == -1) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 modem_set_current_bands,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "None or invalid 2G bands combination in the provided list");
        return;
    }

    if (is_3g && flag3g == -1) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 modem_set_current_bands,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "None or invalid 3G bands combination in the provided list");
        return;
    }

    if (is_4g && flag4g == -1) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 modem_set_current_bands,
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
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 modem_set_current_bands,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Unexpected error: could not compose AT#BND command");
        return;
    }
    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              cmd,
                              20,
                              FALSE,
                              (GAsyncReadyCallback)modem_set_current_bands_ready,
                              task);
    g_free (cmd);
}

/*****************************************************************************/
/* Load current bands (Modem interface) */

typedef struct {
    gboolean mm_modem_is_2g;
    gboolean mm_modem_is_3g;
    gboolean mm_modem_is_4g;
    MMTelitLoadBandsType band_type;
} LoadBandsContext;

static void
load_bands_context_free (LoadBandsContext *ctx)
{
    g_slice_free (LoadBandsContext, ctx);
}

static GArray *
modem_load_bands_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_bands_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    GArray *bands = NULL;
    LoadBandsContext *ctx;

    ctx = g_task_get_task_data (task);
    response = mm_base_modem_at_command_finish (self, res, &error);

    if (!response)
        g_task_return_error (task, error);
    else if (!mm_telit_parse_bnd_response (response,
                                           ctx->mm_modem_is_2g,
                                           ctx->mm_modem_is_3g,
                                           ctx->mm_modem_is_4g,
                                           ctx->band_type,
                                           &bands,
                                           &error))
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bands, (GDestroyNotify)g_array_unref);

    g_object_unref (task);
}

static void
modem_load_current_bands (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GTask *task;
    LoadBandsContext *ctx;

    ctx = g_slice_new0 (LoadBandsContext);

    ctx->mm_modem_is_2g = mm_iface_modem_is_2g (self);
    ctx->mm_modem_is_3g = mm_iface_modem_is_3g (self);
    ctx->mm_modem_is_4g = mm_iface_modem_is_4g (self);
    ctx->band_type = LOAD_CURRENT_BANDS;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_bands_context_free);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_bands_ready,
                              task);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task;
    LoadBandsContext *ctx;

    ctx = g_slice_new0 (LoadBandsContext);

    ctx->mm_modem_is_2g = mm_iface_modem_is_2g (self);
    ctx->mm_modem_is_3g = mm_iface_modem_is_3g (self);
    ctx->mm_modem_is_4g = mm_iface_modem_is_4g (self);
    ctx->band_type = LOAD_SUPPORTED_BANDS;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_bands_context_free);

    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "#BND=?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) load_bands_ready,
                              task);
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

static const gchar *step_lock_names[LOAD_UNLOCK_RETRIES_STEP_LAST] = {
    [LOAD_UNLOCK_RETRIES_STEP_PIN] = "PIN",
    [LOAD_UNLOCK_RETRIES_STEP_PUK] = "PUK",
    [LOAD_UNLOCK_RETRIES_STEP_PIN2] = "PIN2",
    [LOAD_UNLOCK_RETRIES_STEP_PUK2] = "PUK2",
};

typedef struct {
    MMUnlockRetries *retries;
    LoadUnlockRetriesStep step;
    guint succeded_requests;
} LoadUnlockRetriesContext;

static void load_unlock_retries_step (GTask *task);

static void
load_unlock_retries_context_free (LoadUnlockRetriesContext *ctx)
{
    g_object_unref (ctx->retries);
    g_slice_free (LoadUnlockRetriesContext, ctx);
}

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return (MMUnlockRetries *) g_task_propagate_pointer (G_TASK (res), error);
}

static void
csim_unlock_ready (MMBaseModem  *_self,
                   GAsyncResult *res,
                   GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    /* Ignore errors */
    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response) {
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
            self->priv->csim_lock_support = FEATURE_NOT_SUPPORTED;
        }
        mm_warn ("Couldn't unlock SIM card: %s", error->message);
        g_error_free (error);
    }

    if (self->priv->csim_lock_support != FEATURE_NOT_SUPPORTED)
        self->priv->csim_lock_support = FEATURE_SUPPORTED;

    ctx->step++;
    load_unlock_retries_step (task);
}

static void
csim_query_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  GTask *task)
{
    const gchar *response;
    gint unlock_retries;
    GError *error = NULL;
    LoadUnlockRetriesContext *ctx;

    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);

    if (!response) {
        mm_warn ("load %s unlock retries got no response: %s", step_lock_names[ctx->step], error->message);
        g_error_free (error);
        goto next_step;
    }

    if ( (unlock_retries = mm_telit_parse_csim_response (response, &error)) < 0) {
        mm_warn ("load %s unlock retries parse error: %s.", step_lock_names[ctx->step], error->message);
        g_error_free (error);
        goto next_step;
    }

    ctx->succeded_requests++;

    mm_dbg ("%s unlock retries left: %d", step_lock_names[ctx->step], unlock_retries);

    switch (ctx->step) {
        case LOAD_UNLOCK_RETRIES_STEP_PIN:
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PIN, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK:
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PUK, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PIN2:
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PIN2, unlock_retries);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK2:
            mm_unlock_retries_set (ctx->retries, MM_MODEM_LOCK_SIM_PUK2, unlock_retries);
            break;
        default:
            g_assert_not_reached ();
            break;
    }

next_step:
    ctx->step++;
    load_unlock_retries_step (task);
}

static void
csim_lock_ready (MMBaseModem  *_self,
                 GAsyncResult *res,
                 GTask        *task)
{
    const gchar *response;
    GError      *error = NULL;
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (_self);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (_self, res, &error);
    if (!response) {
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
            self->priv->csim_lock_support = FEATURE_NOT_SUPPORTED;
            mm_warn ("Couldn't lock SIM card: %s. Continuing without CSIM lock.", error->message);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "Couldn't lock SIM card: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    } else {
        self->priv->csim_lock_state = CSIM_LOCK_STATE_LOCK_REQUESTED;
    }

    if (self->priv->csim_lock_support != FEATURE_NOT_SUPPORTED) {
        self->priv->csim_lock_support = FEATURE_SUPPORTED;
    }

    ctx->step++;
    load_unlock_retries_step (task);
}

static void
handle_csim_locking (GTask    *task,
                     gboolean  is_lock)
{
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);

    switch (self->priv->csim_lock_support) {
        case FEATURE_SUPPORT_UNKNOWN:
        case FEATURE_SUPPORTED:
            if (is_lock) {
                mm_base_modem_at_command (MM_BASE_MODEM (self),
                                          CSIM_LOCK_STR,
                                          CSIM_QUERY_TIMEOUT,
                                          FALSE,
                                          (GAsyncReadyCallback) csim_lock_ready,
                                          task);
            } else {
                mm_base_modem_at_command (MM_BASE_MODEM (self),
                                          CSIM_UNLOCK_STR,
                                          CSIM_QUERY_TIMEOUT,
                                          FALSE,
                                          (GAsyncReadyCallback) csim_unlock_ready,
                                          task);
            }
            break;
        case FEATURE_NOT_SUPPORTED:
            mm_dbg ("CSIM lock not supported by this modem. Skipping %s command",
                    is_lock ? "lock" : "unlock");
            ctx->step++;
            load_unlock_retries_step (task);
            break;
        default:
            g_assert_not_reached ();
            break;
    }
}

static void
pending_csim_unlock_complete (MMBroadbandModemTelit *self)
{
    LoadUnlockRetriesContext *ctx;

    ctx = g_task_get_task_data (self->priv->csim_lock_task);

    if (ctx->succeded_requests == 0) {
        g_task_return_new_error (self->priv->csim_lock_task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Could not get any of the SIM unlock retries values");
    } else {
        g_task_return_pointer (self->priv->csim_lock_task, g_object_ref (ctx->retries), g_object_unref);
    }

    g_clear_object (&self->priv->csim_lock_task);
}

static gboolean
csim_unlock_periodic_check (MMBroadbandModemTelit *self)
{
    if (self->priv->csim_lock_state != CSIM_LOCK_STATE_UNLOCKED) {
        mm_warn ("CSIM is still locked after %d seconds. Trying to continue anyway", CSIM_UNLOCK_MAX_TIMEOUT);
    }

    self->priv->csim_lock_timeout_id = 0;
    pending_csim_unlock_complete (self);
    g_object_unref (self);

    return G_SOURCE_REMOVE;
}

static void
load_unlock_retries_step (GTask *task)
{
    MMBroadbandModemTelit *self;
    LoadUnlockRetriesContext *ctx;

    self = MM_BROADBAND_MODEM_TELIT (g_task_get_source_object (task));
    ctx = g_task_get_task_data (task);
    switch (ctx->step) {
        case LOAD_UNLOCK_RETRIES_STEP_FIRST:
            /* Fall back on next step */
            ctx->step++;
        case LOAD_UNLOCK_RETRIES_STEP_LOCK:
            handle_csim_locking (task, TRUE);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PIN:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      CSIM_QUERY_PIN_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      task);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      CSIM_QUERY_PUK_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      task);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PIN2:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      CSIM_QUERY_PIN2_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      task);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_PUK2:
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      CSIM_QUERY_PUK2_RETRIES_STR,
                                      CSIM_QUERY_TIMEOUT,
                                      FALSE,
                                      (GAsyncReadyCallback) csim_query_ready,
                                      task);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_UNLOCK:
            handle_csim_locking (task, FALSE);
            break;
        case LOAD_UNLOCK_RETRIES_STEP_LAST:
            self->priv->csim_lock_task = task;
            if (self->priv->csim_lock_state == CSIM_LOCK_STATE_LOCKED) {
                mm_dbg ("CSIM is locked. Waiting for #QSS=1");
                self->priv->csim_lock_timeout_id = g_timeout_add_seconds (CSIM_UNLOCK_MAX_TIMEOUT,
                                                                          (GSourceFunc) csim_unlock_periodic_check,
                                                                          g_object_ref(self));
            } else {
                self->priv->csim_lock_state = CSIM_LOCK_STATE_UNLOCKED;
                pending_csim_unlock_complete (self);
            }
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
    GTask *task;
    LoadUnlockRetriesContext *ctx;

    ctx = g_slice_new0 (LoadUnlockRetriesContext);
    ctx->retries = mm_unlock_retries_new ();
    ctx->step = LOAD_UNLOCK_RETRIES_STEP_FIRST;
    ctx->succeded_requests = 0;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_unlock_retries_context_free);

    load_unlock_retries_step (task);
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
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ws46_set_ready (MMBaseModem *self,
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
    gint ws46_mode = -1;

    task = g_task_new (self, NULL, callback, user_data);

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
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested mode (allowed: '%s', preferred: '%s') not "
                                 "supported by the modem.",
                                 allowed_str,
                                 preferred_str);
        g_free (allowed_str);
        g_free (preferred_str);

        g_object_unref (task);
        return;
    }

    command = g_strdup_printf ("AT+WS46=%d", ws46_mode);
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        command,
        10,
        FALSE,
        (GAsyncReadyCallback)ws46_set_ready,
        task);
    g_free (command);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return (GArray *) g_task_propagate_pointer (G_TASK (res), error);
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

    /* CDMA-only modems don't support changing modes, default to parent's */
    if (!mm_iface_modem_is_3gpp (self)) {
        g_task_return_pointer (task, all, (GDestroyNotify) g_array_unref);
        g_object_unref (task);
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
/* Enabling unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_enable_unsolicited_events_finish (MMIfaceModem3gpp  *self,
                                             GAsyncResult      *res,
                                             GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
cind_set_ready (MMBaseModem  *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        mm_warn ("Couldn't enable custom +CIND settings: %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult     *res,
                                        GTask            *task)
{
    GError *error = NULL;

    if (!iface_modem_3gpp_parent->enable_unsolicited_events_finish (self, res, &error)) {
        mm_warn ("Couldn't enable parent 3GPP unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Our own enable now */
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (self),
        mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self)),
        /* Enable +CIEV only for: signal, service, roam */
        "AT+CIND=0,1,1,0,0,0,1,0,0",
        5,
        FALSE,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cind_set_ready,
        task);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp    *self,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Chain up parent's enable */
    iface_modem_3gpp_parent->enable_unsolicited_events (
        self,
        (GAsyncReadyCallback)parent_enable_unsolicited_events_ready,
        task);
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
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, FALSE,
                         NULL);
}

static void
mm_broadband_modem_telit_init (MMBroadbandModemTelit *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_TELIT,
                                              MMBroadbandModemTelitPrivate);

    self->priv->csim_lock_support = FEATURE_SUPPORT_UNKNOWN;
    self->priv->csim_lock_state = CSIM_LOCK_STATE_UNKNOWN;
    self->priv->qss_status = QSS_STATUS_UNKNOWN;
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
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
    iface->modem_after_sim_unlock = modem_after_sim_unlock;
    iface->modem_after_sim_unlock_finish = modem_after_sim_unlock_finish;
    iface->setup_sim_hot_swap = modem_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = modem_setup_sim_hot_swap_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    iface_modem_3gpp_parent = g_type_interface_peek_parent (iface);

    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_enable_unsolicited_events_finish;
}

static void
mm_broadband_modem_telit_class_init (MMBroadbandModemTelitClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemTelitPrivate));
}
