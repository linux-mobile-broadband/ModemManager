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
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
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

#include "mm-iface-modem-messaging.h"
#include "mm-call-list.h"
#include "mm-base-call.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMCallList, mm_call_list, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_MODEM,
    PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

enum {
    SIGNAL_CALL_ADDED,
    SIGNAL_CALL_DELETED,
    SIGNAL_LAST
};
static guint signals[SIGNAL_LAST];

struct _MMCallListPrivate {
    /* The owner modem */
    MMBaseModem *modem;
    /* List of call objects */
    GList *list;
};

/*****************************************************************************/

void
mm_call_list_foreach (MMCallList            *self,
                      MMCallListForeachFunc  callback,
                      gpointer               user_data)
{
    GList *l;

    g_assert (callback);
    for (l = self->priv->list; l; l = g_list_next (l))
        callback (MM_BASE_CALL (l->data), user_data);
}

/*****************************************************************************/

guint
mm_call_list_get_count (MMCallList *self)
{
    return g_list_length (self->priv->list);
}

GStrv
mm_call_list_get_paths (MMCallList *self)
{
    GStrv path_list = NULL;
    GList *l;
    guint i;

    path_list = g_new0 (gchar *, 1 + g_list_length (self->priv->list));

    for (i = 0, l = self->priv->list; l; l = g_list_next (l)) {
        const gchar *path;

        /* Don't try to add NULL paths (not yet exported CALL objects) */
        path = mm_base_call_get_path (MM_BASE_CALL (l->data));
        if (path)
            path_list[i++] = g_strdup (path);
    }

    return path_list;
}

/*****************************************************************************/

MMBaseCall *
mm_call_list_get_first_incoming_call (MMCallList  *self,
                                      MMCallState  incoming_state)
{
    GList *l;

    g_assert (incoming_state == MM_CALL_STATE_RINGING_IN || incoming_state == MM_CALL_STATE_WAITING);

    for (l = self->priv->list; l; l = g_list_next (l)) {
        MMBaseCall       *call;
        MMCallState       state;
        MMCallDirection   direction;

        call = MM_BASE_CALL (l->data);

        g_object_get (call,
                      "state",     &state,
                      "direction", &direction,
                      NULL);

        if (direction == MM_CALL_DIRECTION_INCOMING &&
            state     == incoming_state) {
            return call;
        }
    }

    return NULL;
}

/*****************************************************************************/

static guint
cmp_call_by_path (MMBaseCall *call,
                 const gchar *path)
{
    return g_strcmp0 (mm_base_call_get_path (call), path);
}

MMBaseCall *
mm_call_list_get_call (MMCallList  *self,
                       const gchar *call_path)
{
    GList *l;

    l = g_list_find_custom (self->priv->list,
                            (gpointer)call_path,
                            (GCompareFunc)cmp_call_by_path);

    return (l ? MM_BASE_CALL (l->data) : NULL);
}

gboolean
mm_call_list_delete_call (MMCallList   *self,
                          const gchar  *call_path,
                          GError      **error)
{
    GList      *l;
    MMBaseCall *call;

    l = g_list_find_custom (self->priv->list,
                            (gpointer)call_path,
                            (GCompareFunc)cmp_call_by_path);
    if (!l) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "No call found with path '%s'",
                     call_path);
        return FALSE;
    }

    call = MM_BASE_CALL (l->data);
    mm_base_call_unexport (call);
    g_signal_emit (self, signals[SIGNAL_CALL_DELETED], 0, call_path);

    g_object_unref (call);
    self->priv->list = g_list_delete_link (self->priv->list, l);

    return TRUE;
}

/*****************************************************************************/

void
mm_call_list_add_call (MMCallList *self,
                     MMBaseCall *call)
{
    self->priv->list = g_list_prepend (self->priv->list, g_object_ref (call));
    g_signal_emit (self, signals[SIGNAL_CALL_ADDED], 0,
                   mm_base_call_get_path (call),
                   FALSE);
}

/*****************************************************************************/

MMCallList *
mm_call_list_new (MMBaseModem *modem)
{
    /* Create the object */
    return g_object_new (MM_TYPE_CALL_LIST,
                         MM_CALL_LIST_MODEM, modem,
                         NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMCallList *self = MM_CALL_LIST (object);

    switch (prop_id) {
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
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
    MMCallList *self = MM_CALL_LIST (object);

    switch (prop_id) {
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_call_list_init (MMCallList *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_CALL_LIST,
                                              MMCallListPrivate);
}

static void
dispose (GObject *object)
{
    MMCallList *self = MM_CALL_LIST (object);

    g_clear_object (&self->priv->modem);
    g_list_free_full (self->priv->list, g_object_unref);
    self->priv->list = NULL;

    G_OBJECT_CLASS (mm_call_list_parent_class)->dispose (object);
}

static void
mm_call_list_class_init (MMCallListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallListPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    /* Properties */
    properties[PROP_MODEM] =
        g_param_spec_object (MM_CALL_LIST_MODEM,
                             "Modem",
                             "The Modem which owns this CALL list",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    /* Signals */
    signals[SIGNAL_CALL_ADDED] =
        g_signal_new (MM_CALL_ADDED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMCallListClass, call_added),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    signals[SIGNAL_CALL_DELETED] =
        g_signal_new (MM_CALL_DELETED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMCallListClass, call_deleted),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
