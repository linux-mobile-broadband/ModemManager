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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-sierra.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBroadbandBearerSierra, mm_broadband_bearer_sierra, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerSierraPrivate {
    gboolean is_icera;
};

enum {
    PROP_0,
    PROP_IS_ICERA,
    PROP_LAST
};

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef enum {
    DIAL_3GPP_STEP_FIRST,
    DIAL_3GPP_STEP_PS_ATTACH,
    DIAL_3GPP_STEP_AUTHENTICATE,
    DIAL_3GPP_STEP_CONNECT,
    DIAL_3GPP_STEP_LAST
} Dial3gppStep;

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    MMPort *data;
    Dial3gppStep step;
} Dial3gppContext;

static void
dial_3gpp_context_free (Dial3gppContext *ctx)
{
    if (ctx->data)
        g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (Dial3gppContext, ctx);
}

static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void dial_3gpp_context_step (GTask *task);

static void
parent_dial_3gpp_ready (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GTask *task)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->data = MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_sierra_parent_class)->dial_3gpp_finish (self, res, &error);
    if (!ctx->data) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
scact_ready (MMBaseModem *modem,
             GAsyncResult *res,
             GTask *task)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    GTask *task)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
cgatt_ready (MMBaseModem *modem,
             GAsyncResult *res,
             GTask *task)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on */
    ctx->step++;
    dial_3gpp_context_step (task);
}

static void
dial_3gpp_context_step (GTask *task)
{
    MMBroadbandBearerSierra *self;
    Dial3gppContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case DIAL_3GPP_STEP_FIRST:
        /* Fall down */
        ctx->step++;

    case DIAL_3GPP_STEP_PS_ATTACH:
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CGATT=1",
                                       10,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgatt_ready,
                                       task);
        return;

    case DIAL_3GPP_STEP_AUTHENTICATE:
        if (!MM_IS_PORT_SERIAL_AT (ctx->data)) {
            gchar *command;
            const gchar *user;
            const gchar *password;
            MMBearerAllowedAuth allowed_auth;

            user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
            password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
            allowed_auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

            if (!user || !password || allowed_auth == MM_BEARER_ALLOWED_AUTH_NONE) {
                mm_dbg ("Not using authentication");
                if (self->priv->is_icera)
                    command = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", ctx->cid);
                else
                    command = g_strdup_printf ("$QCPDPP=%d,0", ctx->cid);
            } else {
                gchar *quoted_user;
                gchar *quoted_password;
                guint sierra_auth;

                if (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
                    mm_dbg ("Using default (PAP) authentication method");
                    sierra_auth = 1;
                } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
                    mm_dbg ("Using PAP authentication method");
                    sierra_auth = 1;
                } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
                    mm_dbg ("Using CHAP authentication method");
                    sierra_auth = 2;
                } else {
                    gchar *str;

                    str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
                    g_task_return_new_error (
                        task,
                        MM_CORE_ERROR,
                        MM_CORE_ERROR_UNSUPPORTED,
                        "Cannot use any of the specified authentication methods (%s)",
                        str);
                    g_free (str);
                    g_object_unref (task);
                    return;
                }

                quoted_user = mm_port_serial_at_quote_string (user);
                quoted_password = mm_port_serial_at_quote_string (password);
                if (self->priv->is_icera) {
                    command = g_strdup_printf ("%%IPDPCFG=%d,0,%u,%s,%s",
                                               ctx->cid,
                                               sierra_auth,
                                               quoted_user,
                                               quoted_password);
                } else {
                    /* Yes, password comes first... */
                    command = g_strdup_printf ("$QCPDPP=%d,%u,%s,%s",
                                               ctx->cid,
                                               sierra_auth,
                                               quoted_password,
                                               quoted_user);
                }
                g_free (quoted_user);
                g_free (quoted_password);
            }

            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           command,
                                           3,
                                           FALSE,
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)authenticate_ready,
                                           task);
            g_free (command);
            return;
        }

        /* Fall down */
        ctx->step++;

    case DIAL_3GPP_STEP_CONNECT:
        /* We need a net or AT data port */
        ctx->data = mm_base_modem_get_best_data_port (ctx->modem, MM_PORT_TYPE_NET);
        if (ctx->data) {
            gchar *command;

            command = g_strdup_printf ("!SCACT=1,%d", ctx->cid);
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           command,
                                           10,
                                           FALSE,
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)scact_ready,
                                           task);
            g_free (command);
            return;
        }

        /* Chain up parent's dialling if we don't have a net port */
        MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_sierra_parent_class)->dial_3gpp (
            MM_BROADBAND_BEARER (self),
            ctx->modem,
            ctx->primary,
            ctx->cid,
            g_task_get_cancellable (task),
            (GAsyncReadyCallback)parent_dial_3gpp_ready,
            task);
        return;

    case DIAL_3GPP_STEP_LAST:
        g_task_return_pointer (task,
                               g_object_ref (ctx->data),
                               g_object_unref);
        g_object_unref (task);
        return;
    }
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMPortSerialAt *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    Dial3gppContext *ctx;
    GTask *task;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->step = DIAL_3GPP_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)dial_3gpp_context_free);

    dial_3gpp_context_step (task);
}

/*****************************************************************************/
/* 3GPP disconnect */

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_disconnect_3gpp_ready (MMBroadbandBearer *self,
                              GAsyncResult *res,
                              GTask *task)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_sierra_parent_class)->disconnect_3gpp_finish (self, res, &error)) {
        mm_dbg ("Parent disconnection failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disconnect_scact_ready (MMBaseModem *modem,
                        GAsyncResult *res,
                        GTask *task)
{
    GError *error = NULL;

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_dbg ("Disconnection failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disconnect_3gpp (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GTask *task;

    g_assert (primary != NULL);

    task = g_task_new (self, NULL, callback, user_data);

    if (!MM_IS_PORT_SERIAL_AT (data)) {
        gchar *command;

        /* Use specific CID */
        command = g_strdup_printf ("!SCACT=0,%u", cid);
        mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                       primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)disconnect_scact_ready,
                                       task);
        g_free (command);
        return;
    }

    /* Chain up parent's disconnection if we don't have a net port */
    MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_sierra_parent_class)->disconnect_3gpp (
        self,
        modem,
        primary,
        secondary,
        data,
        cid,
        (GAsyncReadyCallback)parent_disconnect_3gpp_ready,
        task);
}

/*****************************************************************************/

#define MM_BROADBAND_BEARER_SIERRA_IS_ICERA "is-icera"

MMBaseBearer *
mm_broadband_bearer_sierra_new_finish (GAsyncResult *res,
                                       GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_sierra_new (MMBroadbandModem *modem,
                                MMBearerProperties *config,
                                gboolean is_icera,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_SIERRA,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        MM_BROADBAND_BEARER_SIERRA_IS_ICERA, is_icera,
        NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerSierra *self = MM_BROADBAND_BEARER_SIERRA (object);

    switch (prop_id) {
    case PROP_IS_ICERA:
        self->priv->is_icera = g_value_get_boolean (value);
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
    MMBroadbandBearerSierra *self = MM_BROADBAND_BEARER_SIERRA (object);

    switch (prop_id) {
    case PROP_IS_ICERA:
        g_value_set_boolean (value, self->priv->is_icera);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_sierra_init (MMBroadbandBearerSierra *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_SIERRA,
                                              MMBroadbandBearerSierraPrivate);
}

static void
mm_broadband_bearer_sierra_class_init (MMBroadbandBearerSierraClass *klass)
{
    GObjectClass           *object_class           = G_OBJECT_CLASS (klass);
    MMBaseBearerClass      *base_bearer_class      = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerSierraPrivate));

    object_class->set_property = set_property;
    object_class->get_property = get_property;

    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;

    g_object_class_install_property (object_class, PROP_IS_ICERA,
        g_param_spec_boolean (MM_BROADBAND_BEARER_SIERRA_IS_ICERA,
                              "IsIcera",
                              "Whether the modem uses Icera commands or not.",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
