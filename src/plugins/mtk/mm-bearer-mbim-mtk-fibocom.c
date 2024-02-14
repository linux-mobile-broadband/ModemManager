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
 * Copyright (C) 2024 Google, Inc.
 */

#include <config.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-bearer-mbim-mtk-fibocom.h"

G_DEFINE_TYPE (MMBearerMbimMtkFibocom, mm_bearer_mbim_mtk_fibocom, MM_TYPE_BEARER_MBIM)

struct _MMBearerMbimMtkFibocomPrivate {
    /* Whether IP packet filters need to be removed */
    gboolean remove_ip_packet_filters;
    gboolean reload_stats_unsupported;
};

/*****************************************************************************/
/* Stats */

typedef struct {
    guint64 rx_bytes;
    guint64 tx_bytes;
} ReloadStatsResult;

static gboolean
reload_stats_finish (MMBaseBearer  *_self,
                     guint64       *rx_bytes,
                     guint64       *tx_bytes,
                     GAsyncResult  *res,
                     GError       **error)
{
    MMBearerMbimMtkFibocom       *self = MM_BEARER_MBIM_MTK_FIBOCOM (_self);
    g_autofree ReloadStatsResult *stats = NULL;
    g_autoptr(GError)             inner_error = NULL;

    stats = g_task_propagate_pointer (G_TASK (res), &inner_error);
    if (!stats) {
        /* If filters need to be removed on every stats query, we must never
         * return an error, otherwise the upper layers will stop the stats reloading
         * logic. Only return an error when filters are not being removed. */
        if (!self->priv->remove_ip_packet_filters) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        /* Flag as stats reloading being unsupported, we will avoid querying. */
        self->priv->reload_stats_unsupported = TRUE;
    }

    if (rx_bytes)
        *rx_bytes = stats ? stats->rx_bytes : 0;
    if (tx_bytes)
        *tx_bytes = stats ? stats->tx_bytes : 0;
    return TRUE;
}

static void
parent_reload_stats_ready (MMBaseBearer *self,
                           GAsyncResult *res,
                           GTask        *task)
{
    g_autofree ReloadStatsResult *stats = NULL;
    GError                       *error = NULL;

    stats = g_new0 (ReloadStatsResult, 1);

    if (!MM_BASE_BEARER_CLASS (mm_bearer_mbim_mtk_fibocom_parent_class)->reload_stats_finish (
            self,
            &stats->rx_bytes,
            &stats->tx_bytes,
            res,
            &error))
        g_task_return_error (task, g_steal_pointer (&error));
    else
        g_task_return_pointer (task, g_steal_pointer (&stats), g_free);
    g_object_unref (task);
}

static void
packet_statistics_query (GTask *task)
{
    MMBearerMbimMtkFibocom *self;

    self = g_task_get_source_object (task);
    if (self->priv->reload_stats_unsupported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Skipping stats reloading");
        g_object_unref (task);
        return;
    }

    /* Chain up parent's stats query */
    MM_BASE_BEARER_CLASS (mm_bearer_mbim_mtk_fibocom_parent_class)->reload_stats (
        MM_BASE_BEARER (self),
        (GAsyncReadyCallback) parent_reload_stats_ready,
        task);
}

static void
packet_filters_set_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBearerMbim           *self;
    g_autoptr(GError)       error = NULL;
    g_autoptr(MbimMessage)  response = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        mm_obj_dbg (self, "Couldn't reset IP packet filters: %s", error->message);

    packet_statistics_query (task);
}

static void
ensure_removed_filters (GTask *task)
{
    MMBearerMbimMtkFibocom *self;
    MMPortMbim             *port;
    g_autoptr(MbimMessage)  message = NULL;
    g_autoptr(MMBaseModem)  modem = NULL;
    guint32                 session_id;

    self = g_task_get_source_object (task);

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (modem));
    if (!port) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't peek MBIM port");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "Resetting IP packet filters...");

    session_id = mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (self));
    message = mbim_message_ip_packet_filters_set_new (session_id, 0, NULL, NULL);
    mbim_device_command (mm_port_mbim_peek_device (port),
                         message,
                         5,
                         NULL,
                         (GAsyncReadyCallback)packet_filters_set_ready,
                         task);
}

static void
reload_stats (MMBaseBearer        *_self,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    MMBearerMbimMtkFibocom *self = MM_BEARER_MBIM_MTK_FIBOCOM (_self);
    GTask                  *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->remove_ip_packet_filters)
        ensure_removed_filters (task);
    else
        packet_statistics_query (task);
}

/*****************************************************************************/

MMBaseBearer *
mm_bearer_mbim_mtk_fibocom_new (MMBroadbandModemMbim *modem,
                                gboolean              is_async_slaac_supported,
                                gboolean              remove_ip_packet_filters,
                                MMBearerProperties   *config)
{
    MMBearerMbimMtkFibocom *self;

    /* The Mbim bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    self = g_object_new (MM_TYPE_BEARER_MBIM_MTK_FIBOCOM,
                         MM_BASE_BEARER_MODEM,  modem,
                         MM_BASE_BEARER_CONFIG, config,
                         MM_BEARER_MBIM_ASYNC_SLAAC, is_async_slaac_supported,
                         NULL);

    self->priv->remove_ip_packet_filters = remove_ip_packet_filters;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (self));

    return MM_BASE_BEARER (self);
}

static void
mm_bearer_mbim_mtk_fibocom_init (MMBearerMbimMtkFibocom *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BEARER_MBIM_MTK_FIBOCOM, MMBearerMbimMtkFibocomPrivate);
}

static void
mm_bearer_mbim_mtk_fibocom_class_init (MMBearerMbimMtkFibocomClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerMbimMtkFibocomPrivate));

    base_bearer_class->reload_stats = reload_stats;
    base_bearer_class->reload_stats_finish = reload_stats_finish;
}
