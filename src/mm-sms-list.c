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

#include "mm-sms-list.h"
#include "mm-base-sms.h"
#include "mm-log-object.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void bind_iface_init (MMBindInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMSmsList, mm_sms_list, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_BIND, bind_iface_init))

enum {
    PROP_0,
    PROP_BIND_TO,
    PROP_LAST
};

enum {
    SIGNAL_ADDED,
    SIGNAL_DELETED,
    SIGNAL_LAST
};
static guint signals[SIGNAL_LAST];

struct _MMSmsListPrivate {
    /* The object this SMS list is bound to */
    GObject *bind_to;
    /* List of sms objects */
    GList *list;
};

static void _release_sms_internal (MMBaseSms *sms, MMSmsList *self);

/*****************************************************************************/

gboolean
mm_sms_list_has_local_multipart_reference (MMSmsList *self,
                                           const gchar *number,
                                           guint8 reference)
{
    GList *l;

    /* No one should look for multipart reference 0, which isn't valid */
    g_assert (reference != 0);

    for (l = self->priv->list; l; l = g_list_next (l)) {
        MMBaseSms *sms = MM_BASE_SMS (l->data);

        if (mm_base_sms_is_multipart (sms) &&
            mm_gdbus_sms_get_pdu_type (MM_GDBUS_SMS (sms)) == MM_SMS_PDU_TYPE_SUBMIT &&
            mm_base_sms_get_storage (sms) != MM_SMS_STORAGE_UNKNOWN &&
            mm_base_sms_get_multipart_reference (sms) == reference &&
            g_str_equal (mm_gdbus_sms_get_number (MM_GDBUS_SMS (sms)), number)) {
            /* Yes, the SMS list has an SMS with the same destination number
             * and multipart reference */
            return TRUE;
        }
    }

    return FALSE;
}

/*****************************************************************************/

guint
mm_sms_list_get_count (MMSmsList *self)
{
    return g_list_length (self->priv->list);
}

GStrv
mm_sms_list_get_paths (MMSmsList *self)
{
    GStrv path_list = NULL;
    GList *l;
    guint i;

    path_list = g_new0 (gchar *,
                        1 + g_list_length (self->priv->list));

    for (i = 0, l = self->priv->list; l; l = g_list_next (l)) {
        const gchar *path;

        /* Don't try to add NULL paths (not yet exported SMS objects) */
        path = mm_base_sms_get_path (MM_BASE_SMS (l->data));
        if (path)
            path_list[i++] = g_strdup (path);
    }

    return path_list;
}

/*****************************************************************************/

gboolean
mm_sms_list_delete_sms_finish (MMSmsList *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static guint
cmp_sms_by_path (MMBaseSms *sms,
                 const gchar *path)
{
    return g_strcmp0 (mm_base_sms_get_path (sms), path);
}

static void
delete_ready (MMBaseSms *sms,
              GAsyncResult *res,
              GTask *task)
{
    MMSmsList *self;
    const gchar *path;
    GError *error = NULL;
    GList *l;

    if (!mm_base_sms_delete_finish (sms, res, &error)) {
        /* We report the error */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    path = g_task_get_task_data (task);
    /* The SMS was properly deleted, we now remove it from our list */
    l = g_list_find_custom (self->priv->list,
                            path,
                            (GCompareFunc)cmp_sms_by_path);
    if (l) {
        _release_sms_internal (MM_BASE_SMS (l->data), self);
        self->priv->list = g_list_delete_link (self->priv->list, l);
    }

    /* We don't need to unref the SMS any more, but we can use the
     * reference we got in the method, which is the one kept alive
     * during the async operation. */
    mm_base_sms_unexport (sms);

    g_signal_emit (self,
                   signals[SIGNAL_DELETED], 0,
                   path);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_sms_list_delete_sms (MMSmsList *self,
                        const gchar *sms_path,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GList *l;
    GTask *task;

    l = g_list_find_custom (self->priv->list,
                            (gpointer)sms_path,
                            (GCompareFunc)cmp_sms_by_path);
    if (!l) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 mm_sms_list_delete_sms,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No SMS found with path '%s'",
                                 sms_path);
        return;
    }

    /* Delete all SMS parts */
    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, g_strdup (sms_path), g_free);

    mm_base_sms_delete (MM_BASE_SMS (l->data),
                        (GAsyncReadyCallback)delete_ready,
                        task);
}

/*****************************************************************************/

static void
set_local_multipart_reference (MMBaseSms   *sms,
                               const gchar *number,
                               MMSmsList   *self)
{
    guint8 reference;
    guint8 first;

    /* Start by looking for a random number */
    reference = g_random_int_range (1, 255);

    /* Then, look for the given reference in user-created messages */
    first = reference;
    do {
        if (!mm_sms_list_has_local_multipart_reference (self, number, reference)) {
            mm_base_sms_set_multipart_reference (sms, reference);
            return;
        }

        if (reference == 255)
            reference = 1;
        else
            reference++;
    } while (reference != first);
}

static void
_release_sms_internal (MMBaseSms *sms, MMSmsList *self)
{
    g_signal_handlers_disconnect_by_func (sms, set_local_multipart_reference, self);
    g_object_unref (sms);
}

static void
_add_sms_internal (MMSmsList *self,
                   MMBaseSms *sms,
                   gboolean   received)
{
    self->priv->list = g_list_prepend (self->priv->list, g_object_ref (sms));
    g_signal_connect (sms,
                      MM_BASE_SMS_SET_LOCAL_MULTIPART_REFERENCE,
                      (GCallback)set_local_multipart_reference,
                      self);

    g_signal_emit (self, signals[SIGNAL_ADDED], 0,
                   mm_base_sms_get_path (sms),
                   received);
}


void
mm_sms_list_add_sms (MMSmsList *self,
                     MMBaseSms *sms)
{
    _add_sms_internal (self, sms, FALSE);
}

/*****************************************************************************/

static gint
cmp_sms_by_number_reference (MMBaseSms *sms,
                             MMSmsPart *part)
{
    if (!mm_base_sms_is_multipart (sms))
        return -1;

    if (mm_base_sms_get_multipart_reference (sms) != mm_sms_part_get_concat_reference (part))
        return -1;

    if (g_strcmp0 (mm_gdbus_sms_get_number (MM_GDBUS_SMS (sms)), mm_sms_part_get_number (part)))
        return -1;

    if (mm_base_sms_get_max_parts (sms) != mm_sms_part_get_concat_max (part))
        return -1;

    return 0;
}

typedef struct {
    guint part_index;
    MMSmsStorage storage;
} PartIndexAndStorage;

static guint
cmp_sms_by_part_index_and_storage (MMBaseSms *sms,
                                   PartIndexAndStorage *ctx)
{
    return !(mm_base_sms_get_storage (sms) == ctx->storage &&
             mm_base_sms_has_part_index (sms, ctx->part_index));
}

static gboolean
take_singlepart (MMSmsList *self,
                 MMBaseSms *sms,
                 MMSmsPart *part,
                 MMSmsState state,
                 MMSmsStorage storage,
                 GError **error)
{
    if (!mm_base_sms_singlepart_init (sms,
                                      state,
                                      storage,
                                      part,
                                      error))
        return FALSE;

    mm_obj_dbg (sms, "creating new singlepart SMS object");
    _add_sms_internal (self, sms, state == MM_SMS_STATE_RECEIVED);
    return TRUE;
}

static gboolean
take_multipart (MMSmsList *self,
                MMBaseSms *sms,
                MMSmsPart *part,
                MMSmsState state,
                MMSmsStorage storage,
                GError **error)
{
    GList *l;
    guint concat_reference;

    concat_reference = mm_sms_part_get_concat_reference (part);
    l = g_list_find_custom (self->priv->list, part,
                            (GCompareFunc)cmp_sms_by_number_reference);
    if (l) {
        /* Try to take the part */
        mm_obj_dbg (l->data, "found existing multipart SMS object with reference '%u': adding new part", concat_reference);
        return mm_base_sms_multipart_take_part (MM_BASE_SMS (l->data), part, error);
    }

    /* Create new Multipart */
    if (!mm_base_sms_multipart_init (sms,
                                     state,
                                     storage,
                                     concat_reference,
                                     mm_sms_part_get_concat_max (part),
                                     part,
                                     error))
        return FALSE;

    mm_obj_dbg (sms, "creating new multipart SMS object: need to receive %u parts with reference '%u'",
                mm_sms_part_get_concat_max (part),
                concat_reference);
    _add_sms_internal (self, sms, (state == MM_SMS_STATE_RECEIVED || state == MM_SMS_STATE_RECEIVING));
    return TRUE;
}

gboolean
mm_sms_list_has_part (MMSmsList *self,
                      MMSmsStorage storage,
                      guint index)
{
    PartIndexAndStorage ctx;

    if (storage == MM_SMS_STORAGE_UNKNOWN ||
        index == SMS_PART_INVALID_INDEX)
        return FALSE;

    ctx.part_index = index;
    ctx.storage = storage;

    return !!g_list_find_custom (self->priv->list,
                                 &ctx,
                                 (GCompareFunc)cmp_sms_by_part_index_and_storage);
}

gboolean
mm_sms_list_take_part (MMSmsList *self,
                       MMBaseSms *uninitialized_sms,
                       MMSmsPart *part,
                       MMSmsState state,
                       MMSmsStorage storage,
                       GError **error)
{
    /* Ensure we don't have already taken a part with the same index */
    if (mm_sms_list_has_part (self,
                              storage,
                              mm_sms_part_get_index (part))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "A part with index %u was already taken",
                     mm_sms_part_get_index (part));
        return FALSE;
    }

    /* Did we just get a part of a multi-part SMS? */
    if (mm_sms_part_should_concat (part)) {
        if (mm_sms_part_get_index (part) != SMS_PART_INVALID_INDEX)
            mm_obj_dbg (self, "SMS part at '%s/%u' is from a multipart SMS (reference: '%u', sequence: '%u/%u')",
                        mm_sms_storage_get_string (storage),
                        mm_sms_part_get_index (part),
                        mm_sms_part_get_concat_reference (part),
                        mm_sms_part_get_concat_sequence (part),
                        mm_sms_part_get_concat_max (part));
        else
            mm_obj_dbg (self, "SMS part (not stored) is from a multipart SMS (reference: '%u', sequence: '%u/%u')",
                        mm_sms_part_get_concat_reference (part),
                        mm_sms_part_get_concat_sequence (part),
                        mm_sms_part_get_concat_max (part));

        return take_multipart (self, uninitialized_sms, part, state, storage, error);
    }

    /* Otherwise, we build a whole new single-part MMSms just from this part */
    if (mm_sms_part_get_index (part) != SMS_PART_INVALID_INDEX)
        mm_obj_dbg (self, "SMS part at '%s/%u' is from a singlepart SMS",
                    mm_sms_storage_get_string (storage),
                    mm_sms_part_get_index (part));
    else
        mm_obj_dbg (self, "SMS part (not stored) is from a singlepart SMS");
    return take_singlepart (self, uninitialized_sms, part, state, storage, error);
}

/*****************************************************************************/

void
mm_sms_list_set_default_storage (MMSmsList    *self,
                                 MMSmsStorage  default_storage)
{
    GList *l;

    for (l = self->priv->list; l; l = g_list_next (l)) {
        g_object_set (MM_BASE_SMS (l->data),
                      MM_BASE_SMS_DEFAULT_STORAGE, default_storage,
                      NULL);
    }
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("sms-list");
}

/*****************************************************************************/

MMSmsList *
mm_sms_list_new (GObject *bind_to)
{
    /* Create the object */
    return g_object_new  (MM_TYPE_SMS_LIST,
                          MM_BIND_TO, bind_to,
                          NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSmsList *self = MM_SMS_LIST (object);

    switch (prop_id) {
    case PROP_BIND_TO:
        g_clear_object (&self->priv->bind_to);
        self->priv->bind_to = g_value_dup_object (value);
        mm_bind_to (MM_BIND (self), NULL, self->priv->bind_to);
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
    MMSmsList *self = MM_SMS_LIST (object);

    switch (prop_id) {
    case PROP_BIND_TO:
        g_value_set_object (value, self->priv->bind_to);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_sms_list_init (MMSmsList *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_SMS_LIST,
                                              MMSmsListPrivate);
}

static void
dispose (GObject *object)
{
    MMSmsList *self = MM_SMS_LIST (object);

    g_clear_object (&self->priv->bind_to);
    g_list_foreach (self->priv->list, (GFunc)_release_sms_internal, self);
    g_clear_pointer (&self->priv->list, (GDestroyNotify)g_list_free);

    G_OBJECT_CLASS (mm_sms_list_parent_class)->dispose (object);
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
mm_sms_list_class_init (MMSmsListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSmsListPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    /* Properties */
    g_object_class_override_property (object_class, PROP_BIND_TO, MM_BIND_TO);

    /* Signals */
    signals[SIGNAL_ADDED] =
        g_signal_new (MM_SMS_ADDED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMSmsListClass, sms_added),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

    signals[SIGNAL_DELETED] =
        g_signal_new (MM_SMS_DELETED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMSmsListClass, sms_deleted),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
