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

G_DEFINE_TYPE (MMCallList, mm_call_list, G_TYPE_OBJECT);

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

    path_list = g_new0 (gchar *,
                        1 + g_list_length (self->priv->list));

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

MMBaseCall* mm_call_list_get_new_incoming(MMCallList *self)
{
    MMBaseCall *call = NULL;
    GList *l;
    guint i;

    for (i = 0, l = self->priv->list; l && !call; l = g_list_next (l)) {

        MMCallState         state;
        MMCallStateReason   reason;
        MMCallDirection     direct;

        g_object_get (MM_BASE_CALL (l->data),
                      "state"       , &state,
                      "state-reason", &reason,
                      "direction"   , &direct,
                      NULL);

        if( direct  == MM_CALL_DIRECTION_INCOMING           &&
            state   == MM_CALL_STATE_RINGING_IN             &&
            reason  == MM_CALL_STATE_REASON_INCOMING_NEW    ) {

            call = MM_BASE_CALL (l->data);
            break;
        }
    }

    return call;
}

MMBaseCall* mm_call_list_get_first_ringing_call(MMCallList *self)
{
    MMBaseCall *call = NULL;
    GList *l;
    guint i;

    for (i = 0, l = self->priv->list; l && !call; l = g_list_next (l)) {

        MMCallState         state;

        g_object_get (MM_BASE_CALL (l->data),
                      "state"       , &state,
                      NULL);

        if( state == MM_CALL_STATE_RINGING_IN   ||
            state == MM_CALL_STATE_RINGING_OUT  ) {

            call = MM_BASE_CALL (l->data);
            break;
        }
    }

    return call;
}

MMBaseCall* mm_call_list_get_first_outgoing_dialing_call(MMCallList *self)
{
    MMBaseCall *call = NULL;
    GList *l;
    guint i;

    for (i = 0, l = self->priv->list; l && !call; l = g_list_next (l)) {

        MMCallState         state;
        MMCallDirection     direct;

        g_object_get (MM_BASE_CALL (l->data),
                      "state"       , &state,
                      "direction"   , &direct,
                      NULL);

        if( direct == MM_CALL_DIRECTION_OUTGOING    &&
            state  == MM_CALL_STATE_DIALING         ) {

            call = MM_BASE_CALL (l->data);
            break;
        }
    }

    return call;
}

MMBaseCall* mm_call_list_get_first_non_terminated_call(MMCallList *self)
{
    MMBaseCall *call = NULL;
    GList *l;
    guint i;

    for (i = 0, l = self->priv->list; l && !call; l = g_list_next (l)) {

        MMCallState         state;

        g_object_get (MM_BASE_CALL (l->data),
                      "state"       , &state,
                      NULL);

        if( state != MM_CALL_STATE_TERMINATED ) {
            call = MM_BASE_CALL (l->data);
            break;
        }
    }

    return call;
}

gboolean mm_call_list_send_dtmf_to_active_calls(MMCallList *self, gchar *dtmf)
{
    gboolean signaled = FALSE;
    GList *l;
    guint i;

    for (i = 0, l = self->priv->list; l; l = g_list_next (l)) {

        MMCallState         state;

        g_object_get (MM_BASE_CALL (l->data),
                      "state"       , &state,
                      NULL);

        if( state == MM_CALL_STATE_ACTIVE ) {
            signaled = TRUE;
            mm_base_call_received_dtmf(MM_BASE_CALL (l->data), dtmf);
        }
    }

    return signaled;
}

/*****************************************************************************/

typedef struct {
    MMCallList *self;
    GSimpleAsyncResult *result;
    gchar *path;
} DeleteCallContext;

static void
delete_call_context_complete_and_free (DeleteCallContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx->path);
    g_free (ctx);
}

gboolean
mm_call_list_delete_call_finish (MMCallList *self,
                               GAsyncResult *res,
                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static guint
cmp_call_by_path (MMBaseCall *call,
                 const gchar *path)
{
    return g_strcmp0 (mm_base_call_get_path (call), path);
}

static void
delete_ready (MMBaseCall *call,
              GAsyncResult *res,
              DeleteCallContext *ctx)
{
    GError *error = NULL;
    GList *l;

    if (!mm_base_call_delete_finish (call, res, &error)) {
        /* We report the error */
        g_simple_async_result_take_error (ctx->result, error);
        delete_call_context_complete_and_free (ctx);
        return;
    }

    /* The CALL was properly deleted, we now remove it from our list */
    l = g_list_find_custom (ctx->self->priv->list,
                            ctx->path,
                            (GCompareFunc)cmp_call_by_path);
    if (l) {
        g_object_unref (MM_BASE_CALL (l->data));
        ctx->self->priv->list = g_list_delete_link (ctx->self->priv->list, l);
    }

    /* We don't need to unref the CALL any more, but we can use the
     * reference we got in the method, which is the one kept alive
     * during the async operation. */
    mm_base_call_unexport (call);

    g_signal_emit (ctx->self,
                   signals[SIGNAL_CALL_DELETED], 0,
                   ctx->path);

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    delete_call_context_complete_and_free (ctx);
}

void
mm_call_list_delete_call (MMCallList *self,
                        const gchar *call_path,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    DeleteCallContext *ctx;
    GList *l;

    l = g_list_find_custom (self->priv->list,
                            (gpointer)call_path,
                            (GCompareFunc)cmp_call_by_path);
    if (!l) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "No CALL found with path '%s'",
                                             call_path);
        return;
    }

    /* Delete all CALL parts */
    ctx = g_new0 (DeleteCallContext, 1);
    ctx->self = g_object_ref (self);
    ctx->path = g_strdup (call_path);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_call_list_delete_call);

    mm_base_call_delete (MM_BASE_CALL (l->data),
                        (GAsyncReadyCallback)delete_ready,
                        ctx);
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
    return g_object_new  (MM_TYPE_CALL_LIST,
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
    g_list_free_full (self->priv->list, (GDestroyNotify)g_object_unref);
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
