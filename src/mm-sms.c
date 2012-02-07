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
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-sms.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMSms, mm_sms, MM_GDBUS_TYPE_SMS_SKELETON);

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_IS_MULTIPART,
    PROP_MAX_PARTS,
    PROP_MULTIPART_REFERENCE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMSmsPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this SMS */
    MMBaseModem *modem;
    /* The path where the SMS object is exported */
    gchar *path;

    /* Multipart SMS specific stuff */
    gboolean is_multipart;
    guint multipart_reference;

    /* List of SMS parts */
    guint max_parts;
    GList *parts;

    /* Set to true when all needed parts were received,
     * parsed and assembled */
    gboolean is_assembled;
};

/*****************************************************************************/

void
mm_sms_export (MMSms *self)
{
    static guint id = 0;
    gchar *path;

    path = g_strdup_printf (MM_DBUS_SMS_PREFIX "/%d", id++);
    g_object_set (self,
                  MM_SMS_PATH, path,
                  NULL);
    g_free (path);
}

/*****************************************************************************/

static void
mm_sms_dbus_export (MMSms *self)
{
    GError *error = NULL;

    /* TODO: Handle method invocations */

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export SMS at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
mm_sms_dbus_unexport (MMSms *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

const gchar *
mm_sms_get_path (MMSms *self)
{
    return self->priv->path;
}

gboolean
mm_sms_is_multipart (MMSms *self)
{
    return self->priv->is_multipart;
}

guint
mm_sms_get_multipart_reference (MMSms *self)
{
    g_return_val_if_fail (self->priv->is_multipart, 0);

    return self->priv->multipart_reference;
}

gboolean
mm_sms_multipart_is_complete (MMSms *self)
{
    return (g_list_length (self->priv->parts) == self->priv->max_parts);
}

gboolean
mm_sms_multipart_is_assembled (MMSms *self)
{
    return self->priv->is_assembled;
}

/*****************************************************************************/

static guint
cmp_sms_part_index (MMSmsPart *part,
                    gpointer user_data)
{
    return (GPOINTER_TO_UINT (user_data) - mm_sms_part_get_index (part));
}

gboolean
mm_sms_has_part_index (MMSms *self,
                       guint index)
{
    return !!g_list_find_custom (self->priv->parts,
                                 GUINT_TO_POINTER (index),
                                 (GCompareFunc)cmp_sms_part_index);
}

/*****************************************************************************/

typedef struct {
    MMSms *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
    MMSmsPart *current;
    guint n_failed;
} SmsDeletePartsContext;

static void
sms_delete_parts_context_complete_and_free (SmsDeletePartsContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->current)
        mm_sms_part_free (ctx->current);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

gboolean
mm_sms_delete_parts_finish (MMSms *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void delete_next_part (SmsDeletePartsContext *ctx);

static void
delete_part_ready (MMBaseModem *modem,
                   GAsyncResult *res,
                   SmsDeletePartsContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, &error);
    if (error) {
        ctx->n_failed++;
        mm_dbg ("Couldn't delete SMS part with index %u: '%s'",
                mm_sms_part_get_index (ctx->current),
                error->message);
        g_error_free (error);
    }

    delete_next_part (ctx);
}

static void
delete_next_part (SmsDeletePartsContext *ctx)
{
    gchar *cmd;

    if (ctx->current) {
        mm_sms_part_free (ctx->current);
        ctx->current = NULL;
    }

    /* If all removed, we're done */
    if (!ctx->self->priv->parts) {
        if (ctx->n_failed > 0)
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't delete %u parts from this SMS",
                                             ctx->n_failed);
        else
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        sms_delete_parts_context_complete_and_free (ctx);
        return;
    }

    ctx->current = ctx->self->priv->parts->data;
    ctx->self->priv->parts = g_list_delete_link (ctx->self->priv->parts, ctx->self->priv->parts);

    cmd = g_strdup_printf ("+CMGD=%d",
                           mm_sms_part_get_index (ctx->current));
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              10,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)delete_part_ready,
                              ctx);
    g_free (cmd);
}

void
mm_sms_delete_parts (MMSms *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    SmsDeletePartsContext *ctx;

    ctx = g_new0 (SmsDeletePartsContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_sms_delete_parts);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    /* Before really removing the parts, we make sure we unexport the object */
    mm_sms_dbus_unexport (self);

    /* Go on deleting parts */
    delete_next_part (ctx);
}

/*****************************************************************************/

static gboolean
assemble_sms (MMSms *self,
              GError **error)
{
    GList *l;
    gchar **textparts;
    guint idx;
    gchar *fulltext;
    MMSmsPart *first = NULL;

    /* Assemble text from all parts */
    textparts = g_malloc0 ((1 + self->priv->max_parts) * sizeof (* textparts));
    for (l = self->priv->parts; l; l = g_list_next (l)) {
        idx = mm_sms_part_get_concat_sequence ((MMSmsPart *)l->data);
        if (textparts[idx]) {
            mm_warn ("Duplicate part index (%u) found, ignoring", idx);
            continue;
        }
        /* NOTE! we don't strdup here */
        textparts[idx] = (gchar *)mm_sms_part_get_text ((MMSmsPart *)l->data);

        /* If first in multipart, keep it for later */
        if (idx == 0)
            first = (MMSmsPart *)l->data;
    }

    /* Check if we have all parts */
    for (idx = 0; idx < self->priv->max_parts; idx++) {
        if (!textparts[idx]) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Cannot assemble SMS, missing part at index %u",
                         idx);
            g_free (textparts);
            return FALSE;
        }
    }

    /* If we got all parts, we also have the first one always */
    g_assert (first != NULL);

    /* If we got everything, assemble the text! */
    fulltext = g_strjoinv (NULL, textparts);
    g_object_set (self,
                  "text",      fulltext,
                  "smsc",      mm_sms_part_get_smsc (first),
                  "class",     mm_sms_part_get_class (first),
                  "to",        mm_sms_part_get_number (first),
                  "timestamp", mm_sms_part_get_timestamp (first),
                  NULL);
    g_free (fulltext);
    g_free (textparts);

    self->priv->is_assembled = TRUE;

    return TRUE;
}

/*****************************************************************************/

static guint
cmp_sms_part_sequence (MMSmsPart *a,
                       MMSmsPart *b)
{
    return (mm_sms_part_get_concat_sequence (a) - mm_sms_part_get_concat_sequence (b));
}

gboolean
mm_sms_multipart_take_part (MMSms *self,
                            MMSmsPart *part,
                            GError **error)
{
    if (!self->priv->is_multipart) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "This SMS is not a multipart message");
        return FALSE;
    }

    if (g_list_length (self->priv->parts) >= self->priv->max_parts) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Already took %u parts, cannot take more",
                     g_list_length (self->priv->parts));
        return FALSE;
    }

    if (g_list_find_custom (self->priv->parts,
                            part,
                            (GCompareFunc)cmp_sms_part_sequence)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part, sequence %u already taken",
                     mm_sms_part_get_concat_sequence (part));
        return FALSE;
    }

    if (mm_sms_part_get_concat_sequence (part) > self->priv->max_parts) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part with sequence %u, maximum is %u",
                     mm_sms_part_get_concat_sequence (part),
                     self->priv->max_parts);
        return FALSE;
    }

    /* Insert sorted by concat sequence */
    self->priv->parts = g_list_insert_sorted (self->priv->parts,
                                              part,
                                              (GCompareFunc)cmp_sms_part_sequence);

    /* We only populate contents when the multipart SMS is complete */
    if (mm_sms_multipart_is_complete (self)) {
        GError *inner_error = NULL;

        if (!assemble_sms (self, &inner_error)) {
            /* We DO NOT propagate the error. The part was properly taken
             * so ownership passed to the MMSms object. */
            mm_warn ("Couldn't assemble SMS: '%s'",
                     inner_error->message);
            g_error_free (inner_error);
        } else
            /* Only export once properly assembled */
            mm_sms_export (self);
    }

    return TRUE;
}

MMSms *
mm_sms_singlepart_new (MMBaseModem *modem,
                       gboolean received,
                       MMSmsPart *part,
                       GError **error)
{
    MMSms *self;

    self = g_object_new (MM_TYPE_SMS,
                         MM_SMS_MODEM, modem,
                         "state", (received ?
                                   MM_MODEM_SMS_STATE_RECEIVED :
                                   MM_MODEM_SMS_STATE_STORED),
                         NULL);

    /* Keep the single part in the list */
    self->priv->parts = g_list_prepend (self->priv->parts, part);

    if (!assemble_sms (self, error))
        g_clear_object (&self);
    else
        /* Only export once properly created */
        mm_sms_export (self);

    return self;
}

MMSms *
mm_sms_multipart_new (MMBaseModem *modem,
                      gboolean received,
                      guint reference,
                      guint max_parts,
                      MMSmsPart *first_part,
                      GError **error)
{
    MMSms *self;

    self = g_object_new (MM_TYPE_SMS,
                         MM_SMS_MODEM,               modem,
                         MM_SMS_IS_MULTIPART,        TRUE,
                         MM_SMS_MAX_PARTS,           max_parts,
                         MM_SMS_MULTIPART_REFERENCE, reference,
                         "state", (received ?
                                   MM_MODEM_SMS_STATE_RECEIVED :
                                   MM_MODEM_SMS_STATE_STORED),
                         NULL);

    if (!mm_sms_multipart_take_part (self, first_part, error))
        g_clear_object (&self);

    return self;
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSms *self = MM_SMS (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (self->priv->path &&
            self->priv->connection)
            mm_sms_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            mm_sms_dbus_unexport (self);
        else if (self->priv->path)
            mm_sms_dbus_export (self);
        break;
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the SMS's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_SMS_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
        break;
    case PROP_IS_MULTIPART:
        self->priv->is_multipart = g_value_get_boolean (value);
        break;
    case PROP_MAX_PARTS:
        self->priv->max_parts = g_value_get_uint (value);
        break;
    case PROP_MULTIPART_REFERENCE:
        self->priv->multipart_reference = g_value_get_uint (value);
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
    MMSms *self = MM_SMS (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    case PROP_IS_MULTIPART:
        g_value_set_boolean (value, self->priv->is_multipart);
        break;
    case PROP_MAX_PARTS:
        g_value_set_uint (value, self->priv->max_parts);
        break;
    case PROP_MULTIPART_REFERENCE:
        g_value_set_uint (value, self->priv->multipart_reference);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_sms_init (MMSms *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SMS,
                                              MMSmsPrivate);
    /* Defaults */
    self->priv->max_parts = 1;
}

static void
finalize (GObject *object)
{
    MMSms *self = MM_SMS (object);

    g_list_free_full (self->priv->parts, (GDestroyNotify)mm_sms_part_free);
    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_sms_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMSms *self = MM_SMS (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        mm_sms_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_sms_parent_class)->dispose (object);
}

static void
mm_sms_class_init (MMSmsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSmsPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_SMS_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_SMS_PATH,
                             "Path",
                             "DBus path of the SMS",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_SMS_MODEM,
                             "Modem",
                             "The Modem which owns this SMS",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    properties[PROP_IS_MULTIPART] =
        g_param_spec_boolean (MM_SMS_IS_MULTIPART,
                              "Is multipart",
                              "Flag specifying if the SMS is multipart",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_IS_MULTIPART, properties[PROP_IS_MULTIPART]);

    properties[PROP_MAX_PARTS] =
        g_param_spec_uint (MM_SMS_MAX_PARTS,
                           "Max parts",
                           "Maximum number of parts composing this SMS",
                           1,255, 1,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MAX_PARTS, properties[PROP_MAX_PARTS]);

    properties[PROP_MULTIPART_REFERENCE] =
        g_param_spec_uint (MM_SMS_MULTIPART_REFERENCE,
                           "Multipart reference",
                           "Common reference for all parts in the multipart SMS",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_MULTIPART_REFERENCE, properties[PROP_MULTIPART_REFERENCE]);
}
