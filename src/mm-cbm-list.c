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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 *
 * based on mm-sms-list.c which is
 *
 * Copyright (C) 2012 Google, Inc.
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
#include "mm-cbm-list.h"
#include "mm-base-cbm.h"
#include "mm-log-object.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void bind_iface_init (MMBindInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMCbmList, mm_cbm_list, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_BIND, bind_iface_init))

enum {
    PROP_0,
    PROP_BIND_TO,
    PROP_MODEM,
    PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

enum {
    SIGNAL_ADDED,
    SIGNAL_DELETED,
    SIGNAL_LAST
};
static guint signals[SIGNAL_LAST];

struct _MMCbmListPrivate {
    /* The object this CBM list is bound to */
    GObject *bind_to;
    /* The owner modem */
    MMBaseModem *modem;
    /* List of cbm objects */
    GList *list;
};

/*****************************************************************************/

guint
mm_cbm_list_get_count (MMCbmList *self)
{
    return g_list_length (self->priv->list);
}

GStrv
mm_cbm_list_get_paths (MMCbmList *self)
{
    GStrv path_list = NULL;
    GList *l;
    guint i;

    path_list = g_new0 (gchar *, 1 + g_list_length (self->priv->list));
    for (i = 0, l = self->priv->list; l; l = g_list_next (l)) {
        const gchar *path;

        /* Don't try to add NULL paths (not yet exported CBM objects) */
        path = mm_base_cbm_get_path (MM_BASE_CBM (l->data));
        if (path)
            path_list[i++] = g_strdup (path);
    }

    return path_list;
}

/*****************************************************************************/

gboolean
mm_cbm_list_delete_cbm_finish (MMCbmList *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static guint
cmp_cbm_by_path (MMBaseCbm *cbm,
                 const gchar *path)
{
    return g_strcmp0 (mm_base_cbm_get_path (cbm), path);
}

void
mm_cbm_list_delete_cbm (MMCbmList *self,
                        const gchar *cbm_path,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_autoptr (GTask) task = NULL;
    MMBaseCbm *cbm;
    GList *l;

    l = g_list_find_custom (self->priv->list,
                            (gpointer)cbm_path,
                            (GCompareFunc)cmp_cbm_by_path);
    if (!l) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 mm_cbm_list_delete_cbm,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No CBM found with path '%s'",
                                 cbm_path);
        return;
    }
    cbm = MM_BASE_CBM (l->data);

    /* Although this could be done sync we use a task to provide the
     * async API pattern like other operations */
    task = g_task_new (self, NULL, callback, user_data);

    self->priv->list = g_list_delete_link (self->priv->list, l);

    mm_base_cbm_unexport (cbm);
    g_object_unref (cbm);

    g_signal_emit (self, signals[SIGNAL_DELETED], 0, cbm_path);
    g_task_return_boolean (task, TRUE);
}


/*****************************************************************************/

void
mm_cbm_list_add_cbm (MMCbmList *self,
                     MMBaseCbm *cbm)
{
    self->priv->list = g_list_prepend (self->priv->list, g_object_ref (cbm));
    g_signal_emit (self, signals[SIGNAL_ADDED], 0,
                   mm_base_cbm_get_path (cbm),
                   FALSE);
}

/*****************************************************************************/

typedef struct {
    guint16 serial;
    guint16 channel;
} PartIdAndSerial;

static guint
cmp_cbm_by_serial_and_id (MMBaseCbm *cbm,
                          PartIdAndSerial *ctx)
{
    return !(mm_base_cbm_get_serial (cbm) == ctx->serial &&
             mm_base_cbm_get_channel (cbm) == ctx->channel);
}

static gboolean
take_part (MMCbmList *self,
           GObject *bind_to,
           MMCbmPart *part,
           MMCbmState state,
           GError **error)
{
    GList *l;
    MMBaseCbm *cbm;
    PartIdAndSerial cmp;

    cmp = (PartIdAndSerial){
        .serial = mm_cbm_part_get_serial (part),
        .channel = mm_cbm_part_get_channel (part),
    };
    l = g_list_find_custom (self->priv->list,
                            &cmp,
                            (GCompareFunc)cmp_cbm_by_serial_and_id);
    if (l) {
        /* Try to take the part */
        mm_obj_dbg (self, "found existing multipart CBM object with serial '%u' and id '%u': adding new part",
                    cmp.serial, cmp.channel);
        return mm_base_cbm_take_part (MM_BASE_CBM (l->data), part, error);
    }

    /* Create new cbm */
    cbm = mm_base_cbm_new_with_part (bind_to,
                                     state,
                                     mm_cbm_part_get_num_parts (part),
                                     part,
                                     error);
    if (!cbm)
        return FALSE;

    mm_obj_dbg (self, "creating new multipart CBM object: need to receive %u parts with serial '%u' and id '%u'",
                mm_cbm_part_get_num_parts (part), cmp.serial, cmp.channel);

    self->priv->list = g_list_prepend (self->priv->list, cbm);
    g_signal_emit (self, signals[SIGNAL_ADDED], 0,
                   mm_base_cbm_get_path (cbm),
                   (state == MM_CBM_STATE_RECEIVED ||
                    state == MM_CBM_STATE_RECEIVING));

    return TRUE;
}

typedef struct {
    guint16 serial;
    guint16 channel;
    guint8  part_num;
} PartIdSerialAndNum;

static guint
cmp_cbm_by_serial_id_and_part_num (MMBaseCbm          *cbm,
                                   PartIdSerialAndNum *ctx)
{
    return !(mm_base_cbm_get_serial (cbm) == ctx->serial &&
             mm_base_cbm_get_channel (cbm) == ctx->channel &&
             mm_base_cbm_has_part_num (cbm, ctx->part_num));
}

gboolean
mm_cbm_list_has_part (MMCbmList *self,
                      guint16    serial,
                      guint16    channel,
                      guint8     part_num)
{
    PartIdSerialAndNum ctx = {
        .channel = channel,
        .serial = serial,
        .part_num = part_num
    };

    return !!g_list_find_custom (self->priv->list,
                                 &ctx,
                                 (GCompareFunc)cmp_cbm_by_serial_id_and_part_num);
}

gboolean
mm_cbm_list_take_part (MMCbmList *self,
                       GObject *bind_to,
                       MMCbmPart *part,
                       MMCbmState state,
                       GError   **error)
{
    /* Ensure we don't have already taken a part with the same index */
    if (mm_cbm_list_has_part (self,
                              mm_cbm_part_get_serial (part),
                              mm_cbm_part_get_channel (part),
                              mm_cbm_part_get_part_num (part))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "A part %u with serial %u and id %u and was already taken",
                     mm_cbm_part_get_part_num (part),
                     mm_cbm_part_get_serial (part),
                     mm_cbm_part_get_channel (part));
        return FALSE;
    }

    return take_part (self, bind_to, part, state, error);
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("cbm-list");
}

/*****************************************************************************/

MMCbmList *
mm_cbm_list_new (MMBaseModem *modem, GObject *bind_to)
{
    /* Create the object */
    return g_object_new  (MM_TYPE_CBM_LIST,
                          MM_CBM_LIST_MODEM, modem,
                          MM_BIND_TO, bind_to,
                          NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMCbmList *self = MM_CBM_LIST (object);

    switch (prop_id) {
    case PROP_BIND_TO:
        g_clear_object (&self->priv->bind_to);
        self->priv->bind_to = g_value_dup_object (value);
        mm_bind_to (MM_BIND (self), NULL, self->priv->bind_to);
        break;
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
    MMCbmList *self = MM_CBM_LIST (object);

    switch (prop_id) {
    case PROP_BIND_TO:
        g_value_set_object (value, self->priv->bind_to);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_cbm_list_init (MMCbmList *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_CBM_LIST,
                                              MMCbmListPrivate);
}

static void
dispose (GObject *object)
{
    MMCbmList *self = MM_CBM_LIST (object);

    g_clear_object (&self->priv->modem);
    g_clear_object (&self->priv->bind_to);
    g_list_free_full (self->priv->list, g_object_unref);
    self->priv->list = NULL;

    G_OBJECT_CLASS (mm_cbm_list_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
bind_iface_init (MMBindInterface *iface)
{
}

static void
mm_cbm_list_class_init (MMCbmListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCbmListPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    /* Properties */
    properties[PROP_MODEM] =
        g_param_spec_object (MM_CBM_LIST_MODEM,
                             "Modem",
                             "The Modem which owns this CBM list",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    g_object_class_override_property (object_class, PROP_BIND_TO, MM_BIND_TO);

    /* Signals */
    signals[SIGNAL_ADDED] =
        g_signal_new (MM_CBM_ADDED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMCbmListClass, cbm_added),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

    signals[SIGNAL_DELETED] =
        g_signal_new (MM_CBM_DELETED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMCbmListClass, cbm_deleted),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
