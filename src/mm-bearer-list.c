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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
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

#include "mm-bearer-list.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerList, mm_bearer_list, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_NUM_BEARERS,
    PROP_MAX_ACTIVE_BEARERS,
    PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerListPrivate {
    /* List of bearers */
    GList *bearers;
    /* Max number of active bearers */
    guint max_active_bearers;
    guint max_active_multiplexed_bearers;
};

/*****************************************************************************/

guint
mm_bearer_list_get_max_active (MMBearerList *self)
{
    return self->priv->max_active_bearers;
}

guint
mm_bearer_list_get_max_active_multiplexed (MMBearerList *self)
{
    return self->priv->max_active_multiplexed_bearers;
}

gboolean
mm_bearer_list_add_bearer (MMBearerList *self,
                           MMBaseBearer *bearer,
                           GError **error)
{
    /* Keep our own reference */
    self->priv->bearers = g_list_prepend (self->priv->bearers, g_object_ref (bearer));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NUM_BEARERS]);

    return TRUE;
}

gboolean
mm_bearer_list_delete_bearer (MMBearerList *self,
                              const gchar *path,
                              GError **error)
{
    GList *l;

    for (l = self->priv->bearers; l; l = g_list_next (l)) {
        if (g_str_equal (path, mm_base_bearer_get_path (MM_BASE_BEARER (l->data)))) {
            g_object_unref (l->data);
            self->priv->bearers = g_list_delete_link (self->priv->bearers, l);
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NUM_BEARERS]);
            return TRUE;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_NOT_FOUND,
                 "Cannot delete bearer: path '%s' not found",
                 path);
    return FALSE;
}

GStrv
mm_bearer_list_get_paths (MMBearerList *self)
{
    GStrv path_list = NULL;
    GList *l;
    guint i;

    path_list = g_new0 (gchar *,
                        1 + g_list_length (self->priv->bearers));

    for (i = 0, l = self->priv->bearers; l; l = g_list_next (l))
        path_list[i++] = g_strdup (mm_base_bearer_get_path (MM_BASE_BEARER (l->data)));

    return path_list;
}

void
mm_bearer_list_foreach (MMBearerList *self,
                        MMBearerListForeachFunc func,
                        gpointer user_data)
{
    g_list_foreach (self->priv->bearers, (GFunc)func, user_data);
}

MMBaseBearer *
mm_bearer_list_find_by_properties (MMBearerList       *self,
                                   MMBearerProperties *props)
{
    GList *l;

    for (l = self->priv->bearers; l; l = g_list_next (l)) {
        /* always strict matching when comparing these bearer properties, as they're all
         * built in the same place */
        if (mm_bearer_properties_cmp (mm_base_bearer_peek_config (MM_BASE_BEARER (l->data)),
                                      props,
                                      MM_BEARER_PROPERTIES_CMP_FLAGS_NONE))
            return g_object_ref (l->data);
    }

    return NULL;
}

MMBaseBearer *
mm_bearer_list_find_by_path (MMBearerList *self,
                             const gchar *path)
{
    GList *l;

    for (l = self->priv->bearers; l; l = g_list_next (l)) {
        if (g_str_equal (path, mm_base_bearer_get_path (MM_BASE_BEARER (l->data))))
            return g_object_ref (l->data);
    }

    return NULL;
}

MMBaseBearer *
mm_bearer_list_find_by_profile_id (MMBearerList *self,
                                   gint          profile_id)
{
    GList *l;

    g_assert (profile_id != MM_3GPP_PROFILE_ID_UNKNOWN);

    for (l = self->priv->bearers; l; l = g_list_next (l)) {
        if (mm_base_bearer_get_profile_id (MM_BASE_BEARER (l->data)) == profile_id)
            return g_object_ref (l->data);
    }

    return NULL;
}

MMBaseBearer *
mm_bearer_list_find_by_apn_type (MMBearerList    *self,
                                 MMBearerApnType  apn_type)
{
    GList *l;

    g_assert (apn_type != MM_BEARER_APN_TYPE_NONE);

    for (l = self->priv->bearers; l; l = g_list_next (l)) {
        if (mm_base_bearer_get_apn_type (MM_BASE_BEARER (l->data)) == apn_type)
            return g_object_ref (l->data);
    }

    return NULL;
}

/*****************************************************************************/

typedef struct {
    gchar        *bearer_path;
    GList        *pending_to_disconnect;
    MMBaseBearer *current_to_disconnect;
} DisconnectBearersContext;

static void
disconnect_bearers_context_free (DisconnectBearersContext *ctx)
{
    g_free (ctx->bearer_path);
    if (ctx->current_to_disconnect)
        g_object_unref (ctx->current_to_disconnect);
    g_list_free_full (ctx->pending_to_disconnect, g_object_unref);
    g_slice_free (DisconnectBearersContext, ctx);
}

gboolean
mm_bearer_list_disconnect_bearers_finish (MMBearerList  *self,
                                          GAsyncResult  *res,
                                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disconnect_bearers_next (GTask *task);

static void
bearer_disconnect_ready (MMBaseBearer *bearer,
                         GAsyncResult *res,
                         GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_bearer_disconnect_finish (bearer, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }
    disconnect_bearers_next (task);
}

static void
disconnect_bearers_next (GTask *task)
{
    DisconnectBearersContext *ctx;

    ctx  = g_task_get_task_data (task);
    g_clear_object (&ctx->current_to_disconnect);

    /* No more bearers? all done! */
    if (!ctx->pending_to_disconnect) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->current_to_disconnect = MM_BASE_BEARER (ctx->pending_to_disconnect->data);
    ctx->pending_to_disconnect = g_list_delete_link (ctx->pending_to_disconnect, ctx->pending_to_disconnect);

    mm_base_bearer_disconnect (ctx->current_to_disconnect,
                               (GAsyncReadyCallback)bearer_disconnect_ready,
                               task);
}

static void
build_connected_bearer_list (MMBaseBearer             *bearer,
                             DisconnectBearersContext *ctx)
{
    if (!ctx->bearer_path ||
        g_str_equal (ctx->bearer_path, mm_base_bearer_get_path (bearer)))
        ctx->pending_to_disconnect = g_list_prepend (ctx->pending_to_disconnect, g_object_ref (bearer));
}

void
mm_bearer_list_disconnect_bearers (MMBearerList        *self,
                                   const gchar         *bearer_path,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    GTask                    *task;
    DisconnectBearersContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);
    ctx = g_slice_new0 (DisconnectBearersContext);
    ctx->bearer_path = g_strdup (bearer_path);  /* may be NULL if disconnecting all */
    g_task_set_task_data (task, ctx, (GDestroyNotify)disconnect_bearers_context_free);

    /* If a given specific bearer is being disconnected, only add that one. Otherwise,
     * disconnect all. */
    mm_bearer_list_foreach (self, (MMBearerListForeachFunc)build_connected_bearer_list, ctx);

    if (ctx->bearer_path && !ctx->pending_to_disconnect) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                 "Couldn't disconnect bearer '%s': not found",
                                 ctx->bearer_path);
        g_object_unref (task);
        return;
    }

    disconnect_bearers_next (task);
}

/*****************************************************************************/

#if defined WITH_SUSPEND_RESUME

typedef struct {
    GList        *pending_to_sync;
    MMBaseBearer *current_to_sync;
} SyncAllContext;

static void
sync_all_context_free (SyncAllContext *ctx)
{
    if (ctx->current_to_sync)
        g_object_unref (ctx->current_to_sync);
    g_list_free_full (ctx->pending_to_sync, g_object_unref);
    g_free (ctx);
}

gboolean
mm_bearer_list_sync_all_bearers_finish (MMBearerList  *self,
                                        GAsyncResult  *res,
                                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void sync_next_bearer (GTask *task);

static void
sync_ready (MMBaseBearer *bearer,
            GAsyncResult *res,
            GTask        *task)
{
    g_autoptr(GError) error = NULL;

    if (!mm_base_bearer_sync_finish (bearer, res, &error))
        mm_obj_warn (bearer, "failed synchronizing state: %s", error->message);

    sync_next_bearer (task);
}

static void
sync_next_bearer (GTask *task)
{
    SyncAllContext *ctx;

    ctx = g_task_get_task_data (task);
    if (ctx->current_to_sync)
        g_clear_object (&ctx->current_to_sync);

    /* No more bearers? all done! */
    if (!ctx->pending_to_sync) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->current_to_sync = MM_BASE_BEARER (ctx->pending_to_sync->data);
    ctx->pending_to_sync = g_list_delete_link (ctx->pending_to_sync, ctx->pending_to_sync);

    mm_base_bearer_sync (ctx->current_to_sync, (GAsyncReadyCallback)sync_ready, task);
}

void
mm_bearer_list_sync_all_bearers (MMBearerList        *self,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
    SyncAllContext *ctx;
    GTask          *task;

    ctx = g_new0 (SyncAllContext, 1);

    /* Get a copy of the list */
    ctx->pending_to_sync = g_list_copy_deep (self->priv->bearers,
                                             (GCopyFunc)g_object_ref,
                                             NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sync_all_context_free);

    sync_next_bearer (task);
}

#endif

/*****************************************************************************/

MMBearerList *
mm_bearer_list_new (guint max_active_bearers,
                    guint max_active_multiplexed_bearers)
{
    /* Create the object */
    return g_object_new  (MM_TYPE_BEARER_LIST,
                          MM_BEARER_LIST_MAX_ACTIVE_BEARERS, max_active_bearers,
                          MM_BEARER_LIST_MAX_ACTIVE_MULTIPLEXED_BEARERS, max_active_multiplexed_bearers,
                          NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearerList *self = MM_BEARER_LIST (object);

    switch (prop_id) {
    case PROP_NUM_BEARERS:
        g_assert_not_reached ();
        break;
    case PROP_MAX_ACTIVE_BEARERS:
        self->priv->max_active_bearers = g_value_get_uint (value);
        break;
    case PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS:
        self->priv->max_active_multiplexed_bearers = g_value_get_uint (value);
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
    MMBearerList *self = MM_BEARER_LIST (object);

    switch (prop_id) {
    case PROP_NUM_BEARERS:
        g_value_set_uint (value, g_list_length (self->priv->bearers));
        break;
    case PROP_MAX_ACTIVE_BEARERS:
        g_value_set_uint (value, self->priv->max_active_bearers);
        break;
    case PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS:
        g_value_set_uint (value, self->priv->max_active_multiplexed_bearers);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_bearer_list_init (MMBearerList *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BEARER_LIST,
                                              MMBearerListPrivate);
}

static void
dispose (GObject *object)
{
    MMBearerList *self = MM_BEARER_LIST (object);

    if (self->priv->bearers) {
        g_list_free_full (self->priv->bearers, g_object_unref);
        self->priv->bearers = NULL;
    }

    G_OBJECT_CLASS (mm_bearer_list_parent_class)->dispose (object);
}

static void
mm_bearer_list_class_init (MMBearerListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerListPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    properties[PROP_NUM_BEARERS] =
        g_param_spec_uint (MM_BEARER_LIST_NUM_BEARERS,
                           "Number of bearers",
                           "Current number of bearers in the list",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READABLE);
    g_object_class_install_property (object_class, PROP_NUM_BEARERS, properties[PROP_NUM_BEARERS]);

    properties[PROP_MAX_ACTIVE_BEARERS] =
        g_param_spec_uint (MM_BEARER_LIST_MAX_ACTIVE_BEARERS,
                           "Max active bearers",
                           "Maximum number of active bearers the list can handle",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MAX_ACTIVE_BEARERS, properties[PROP_MAX_ACTIVE_BEARERS]);

    properties[PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS] =
        g_param_spec_uint (MM_BEARER_LIST_MAX_ACTIVE_MULTIPLEXED_BEARERS,
                           "Max active multiplexed bearers",
                           "Maximum number of active multiplexed bearers the list can handle",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS, properties[PROP_MAX_ACTIVE_MULTIPLEXED_BEARERS]);
}
