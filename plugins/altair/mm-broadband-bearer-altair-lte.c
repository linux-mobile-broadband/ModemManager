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
 * Copyright (C) 2013 Altair Semiconductor
 *
 * Author: Ori Inbar <ori.inbar@altair-semi.com>
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
#include "mm-broadband-bearer-altair-lte.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

#define CONNECTION_CHECK_TIMEOUT_SEC 5
#define STATCM_TAG "%STATCM:"

G_DEFINE_TYPE (MMBroadbandBearerAltairLte, mm_broadband_bearer_altair_lte, MM_TYPE_BROADBAND_BEARER);

/*****************************************************************************/
/* 3GPP Connect sequence */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
} DetailedConnectContext;

static void
detailed_connect_context_free (DetailedConnectContext *ctx)
{
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static MMBearerConnectResult *
connect_3gpp_finish (MMBroadbandBearer *self,
                     GAsyncResult *res,
                     GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
connect_3gpp_connect_ready (MMBaseModem *modem,
                            GAsyncResult *res,
                            GTask *task)
{
    DetailedConnectContext *ctx;
    const gchar *result;
    GError *error = NULL;
    MMBearerIpConfig *config;

    result = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    config = mm_bearer_ip_config_new ();

    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);

    /* Set operation result */
    g_task_return_pointer (
        task,
        mm_bearer_connect_result_new (ctx->data, config, config),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);

    g_object_unref (config);
}

static void
connect_3gpp_apnsettings_ready (MMBaseModem *modem,
                                GAsyncResult *res,
                                GTask *task)
{
    DetailedConnectContext *ctx;
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!result) {
        g_prefix_error (&error, "setting APN failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "%DPDNACT=1",
                                   MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT, /* timeout */
                                   FALSE, /* allow_cached */
                                   FALSE, /* is_raw */
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback)connect_3gpp_connect_ready,
                                   task); /* user_data */
}

static void
connect_3gpp (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMPortSerialAt *primary,
              MMPortSerialAt *secondary,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;
    gchar *command, *apn;
    MMBearerProperties *config;
    MMModem3gppRegistrationState registration_state;
    MMPort *data;
    GTask *task;

    /* There is a known firmware bug that can leave the modem unusable if a
     * connect attempt is made when out of coverage. So, fail without trying.
     */
    g_object_get (modem,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &registration_state,
                  NULL);
    if (registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 connect_3gpp,
                                 MM_MOBILE_EQUIPMENT_ERROR,
                                 MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK,
                                 "Out of coverage, can't connect.");
        return;
    }

    /* Don't allow a connect while we detach from the network to process SIM
     * refresh.
     * */
    if (mm_broadband_modem_altair_lte_is_sim_refresh_detach_in_progress (modem)) {
        mm_obj_dbg (self, "detached from network to process SIM refresh, failing connect request");
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 connect_3gpp,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_RETRY,
                                 "Detached from network to process SIM refresh, can't connect.");
        return;
    }

    data = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!data) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 connect_3gpp,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_CONNECTED,
                                 "Couldn't connect: no available net port available");
        return;
    }

    ctx = g_new0 (DetailedConnectContext, 1);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_connect_context_free);

    config = mm_base_bearer_peek_config (MM_BASE_BEARER (self));
    apn = mm_port_serial_at_quote_string (mm_bearer_properties_get_apn (config));
    command = g_strdup_printf ("%%APNN=%s", apn);
    g_free (apn);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   10, /* timeout */
                                   FALSE, /* allow_cached */
                                   FALSE, /* is_raw */
                                   cancellable,
                                   (GAsyncReadyCallback)connect_3gpp_apnsettings_ready,
                                   task); /* user_data */
    g_free (command);
}

/*****************************************************************************/
/* 3GPP Disconnect sequence */

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
} DetailedDisconnectContext;

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
detailed_disconnect_context_free (DetailedDisconnectContext *ctx)
{
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static void
disconnect_3gpp_check_status (MMBaseModem *modem,
                              GAsyncResult *res,
                              GTask *task)
{

    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!result)
        g_task_return_error (task, error);
    else
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
    DetailedDisconnectContext *ctx;
    MMModem3gppRegistrationState registration_state;
    GTask *task;

    /* There is a known firmware bug that can leave the modem unusable if a
     * disconnect attempt is made when out of coverage. So, fail without trying.
     */
    g_object_get (modem,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &registration_state,
                  NULL);
    if (registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 disconnect_3gpp,
                                 MM_MOBILE_EQUIPMENT_ERROR,
                                 MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK,
                                 "Out of coverage, can't disconnect.");
        return;
    }

    ctx = g_new0 (DetailedDisconnectContext, 1);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)detailed_disconnect_context_free);

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "%DPDNACT=0",
                                   MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT, /* timeout */
                                   FALSE, /* allow_cached */
                                   FALSE, /* is_raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)disconnect_3gpp_check_status,
                                   task); /* user_data */
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_altair_lte_new_finish (GAsyncResult *res,
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
mm_broadband_bearer_altair_lte_new (MMBroadbandModemAltairLte *modem,
                                    MMBearerProperties *config,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_altair_lte_init (MMBroadbandBearerAltairLte *self)
{

}

static void
mm_broadband_bearer_altair_lte_class_init (MMBroadbandBearerAltairLteClass *klass)
{
    MMBaseBearerClass      *base_bearer_class      = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
