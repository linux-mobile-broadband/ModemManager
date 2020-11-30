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

G_DEFINE_TYPE (MMBearerList, mm_bearer_list, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_NUM_BEARERS,
    PROP_MAX_BEARERS,
    PROP_MAX_ACTIVE_BEARERS,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerListPrivate {
    /* List of bearers */
    GList *bearers;
    /* Max number of bearers */
    guint max_bearers;
    /* Max number of active bearers */
    guint max_active_bearers;
};

/*****************************************************************************/

guint
mm_bearer_list_get_max (MMBearerList *self)
{
    return self->priv->max_bearers;
}

guint
mm_bearer_list_get_max_active (MMBearerList *self)
{
    return self->priv->max_active_bearers;
}

guint
mm_bearer_list_get_count (MMBearerList *self)
{
    return g_list_length (self->priv->bearers);
}

guint
mm_bearer_list_get_count_active (MMBearerList *self)
{
    return 0; /* TODO */
}

gboolean
mm_bearer_list_add_bearer (MMBearerList *self,
                           MMBaseBearer *bearer,
                           GError **error)
{
    /* Just in case, ensure we don't go off limits */
    if (g_list_length (self->priv->bearers) == self->priv->max_bearers) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_TOO_MANY,
                     "Cannot add new bearer: already reached maximum (%u)",
                     self->priv->max_bearers);
        return FALSE;
    }

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

/*****************************************************************************/

typedef struct {
    GList *pending;
    MMBaseBearer *current;
} DisconnectAllContext;

static void
disconnect_all_context_free (DisconnectAllContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    g_list_free_full (ctx->pending, g_object_unref);
    g_free (ctx);
}

gboolean
mm_bearer_list_disconnect_all_bearers_finish (MMBearerList *self,
                                              GAsyncResult *res,
                                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disconnect_next_bearer (GTask *task);

static void
disconnect_ready (MMBaseBearer *bearer,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;

    if (!mm_base_bearer_disconnect_finish (bearer, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    disconnect_next_bearer (task);
}

static void
disconnect_next_bearer (GTask *task)
{
    DisconnectAllContext *ctx;

    ctx = g_task_get_task_data (task);
    if (ctx->current)
        g_clear_object (&ctx->current);

    /* No more bearers? all done! */
    if (!ctx->pending) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    ctx->current = MM_BASE_BEARER (ctx->pending->data);
    ctx->pending = g_list_delete_link (ctx->pending, ctx->pending);

    mm_base_bearer_disconnect (ctx->current,
                               (GAsyncReadyCallback)disconnect_ready,
                               task);
}

void
mm_bearer_list_disconnect_all_bearers (MMBearerList *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    DisconnectAllContext *ctx;
    GTask *task;

    ctx = g_new0 (DisconnectAllContext, 1);
    /* Get a copy of the list */
    ctx->pending = g_list_copy_deep (self->priv->bearers,
                                     (GCopyFunc)g_object_ref,
                                     NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task,
                          ctx,
                          (GDestroyNotify)disconnect_all_context_free);

    disconnect_next_bearer (task);
}

/*****************************************************************************/

MMBearerList *
mm_bearer_list_new (guint max_bearers,
                    guint max_active_bearers)
{
    /* Create the object */
    return g_object_new  (MM_TYPE_BEARER_LIST,
                          MM_BEARER_LIST_MAX_BEARERS, max_bearers,
                          MM_BEARER_LIST_MAX_ACTIVE_BEARERS, max_active_bearers,
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
    case PROP_MAX_BEARERS:
        self->priv->max_bearers = g_value_get_uint (value);
        break;
    case PROP_MAX_ACTIVE_BEARERS:
        self->priv->max_active_bearers = g_value_get_uint (value);
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
    case PROP_MAX_BEARERS:
        g_value_set_uint (value, self->priv->max_bearers);
        break;
    case PROP_MAX_ACTIVE_BEARERS:
        g_value_set_uint (value, self->priv->max_active_bearers);
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

    properties[PROP_MAX_BEARERS] =
        g_param_spec_uint (MM_BEARER_LIST_MAX_BEARERS,
                           "Max bearers",
                           "Maximum number of bearers the list can handle",
                           1,
                           G_MAXUINT,
                           1,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MAX_BEARERS, properties[PROP_MAX_BEARERS]);

    properties[PROP_MAX_ACTIVE_BEARERS] =
        g_param_spec_uint (MM_BEARER_LIST_MAX_ACTIVE_BEARERS,
                           "Max active bearers",
                           "Maximum number of active bearers the list can handle",
                           1,
                           G_MAXUINT,
                           1,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MAX_ACTIVE_BEARERS, properties[PROP_MAX_ACTIVE_BEARERS]);

}
