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

#include "mm-base-cbm.h"
#include "mm-cbm-part.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void bind_iface_init (MMBindInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMBaseCbm, mm_base_cbm, MM_GDBUS_TYPE_CBM_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_BIND, bind_iface_init))

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_BIND_TO,
    PROP_MAX_PARTS,
    PROP_SERIAL,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseCbmPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;

    /* The object this CBM is bound to */
    GObject *bind_to;

    /* The path where the CBM object is exported */
    gchar *path;

    /* List of CBM parts */
    guint max_parts;
    GList *parts;

    /* channel and serial identify the CBM */
    guint16 serial;

    /* Set to true when all needed parts were received,
     * parsed and assembled */
    gboolean is_assembled;
};

void
mm_base_cbm_export (MMBaseCbm *self)
{
    g_autofree gchar *path = NULL;

    path = g_strdup_printf (MM_DBUS_CBM_PREFIX "/%d", self->priv->dbus_id);
    g_object_set (self,
                  MM_BASE_CBM_PATH, path,
                  NULL);
}

void
mm_base_cbm_unexport (MMBaseCbm *self)
{
    g_object_set (self,
                  MM_BASE_CBM_PATH, NULL,
                  NULL);
}

/*****************************************************************************/

static void
cbm_dbus_export (MMBaseCbm *self)
{
    g_autoptr (GError) error = NULL;

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_obj_warn (self, "couldn't export CBM: %s", error->message);
    }
}

static void
cbm_dbus_unexport (MMBaseCbm *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

const gchar *
mm_base_cbm_get_path (MMBaseCbm *self)
{
    return self->priv->path;
}

gboolean
mm_base_cbm_is_complete (MMBaseCbm *self)
{
    return (g_list_length (self->priv->parts) == self->priv->max_parts);
}

/*****************************************************************************/

static guint
cmp_cbm_part_has_num (MMCbmPart *part,
                      gpointer user_data)
{
    return (GPOINTER_TO_UINT (user_data) - mm_cbm_part_get_part_num (part));
}

gboolean
mm_base_cbm_has_part_num (MMBaseCbm *self,
                          guint      part_num)
{
    return !!g_list_find_custom (self->priv->parts,
                                 GUINT_TO_POINTER (part_num),
                                 (GCompareFunc)cmp_cbm_part_has_num);
}

GList *
mm_base_cbm_get_parts (MMBaseCbm *self)
{
    return self->priv->parts;
}

/*****************************************************************************/

static void
initialize_cbm (MMBaseCbm *self)
{
    MMCbmPart *part;
    guint16 serial;

    /* Some of the fields of the CBM object may be initialized as soon as we have
     * one part already available, even if it's not exactly the first one */
    g_assert (self->priv->parts);
    part = (MMCbmPart *)(self->priv->parts->data);

    serial = mm_cbm_part_get_serial (part);
    g_object_set (self,
                  MM_BASE_CBM_MAX_PARTS,  mm_cbm_part_get_num_parts (part),
                  MM_BASE_CBM_SERIAL,     serial,
                  "channel",              mm_cbm_part_get_channel (part),
                  "update",               CBM_SERIAL_MESSAGE_CODE_UPDATE (serial),
                  "message-code",         CBM_SERIAL_MESSAGE_CODE (serial),
                  "language",             mm_cbm_part_get_language (part),
                  NULL);
}

static gboolean
assemble_cbm (MMBaseCbm  *self,
              GError    **error)
{
    GList      *l;
    guint       part_num, idx;
    g_autofree MMCbmPart **sorted_parts = NULL;
    g_autoptr (GString) fulltext = NULL;

    sorted_parts = g_new0 (MMCbmPart *, self->priv->max_parts);

    /* Check if we have duplicate parts */
    for (l = self->priv->parts; l; l = g_list_next (l)) {
        part_num = mm_cbm_part_get_part_num ((MMCbmPart *)l->data);

        /* part_num starts at 1, array starts at 0 */
        idx = part_num - 1;
        if (part_num < 1 || part_num > self->priv->max_parts) {
            mm_obj_warn (self, "invalid part part nu (%u) found, ignoring", part_num);
            continue;
        }

        if (sorted_parts[idx]) {
            mm_obj_warn (self, "duplicate part index (%u) found, ignoring", part_num);
            continue;
        }

        /* Add the part to the proper array position */
        sorted_parts[idx] = (MMCbmPart *)l->data;
    }

    fulltext = g_string_new ("");

    /* Assemble text and data from all parts. */
    for (idx = 0; idx < self->priv->max_parts; idx++) {
        const gchar *parttext;

        if (!sorted_parts[idx]) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Cannot assemble CBM, missing part at index (%u)",
                         idx);
            return FALSE;
        }

        parttext = mm_cbm_part_get_text (sorted_parts[idx]);
        if (!parttext) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Cannot assemble CBM, part at index (%u) has no text",
                         idx);
            return FALSE;
        }

        if (parttext)
            g_string_append (fulltext, parttext);
    }

    /* If we got all parts, we also have the first one always */
    g_assert (sorted_parts[0] != NULL);

    /* If we got everything, assemble the text! */
    g_object_set (self,
                  "text", fulltext->str,
                  NULL);

    self->priv->is_assembled = TRUE;

    return TRUE;
}

/*****************************************************************************/

static guint
cmp_cbm_part_num (MMCbmPart *a,
                  MMCbmPart *b)
{
    return (mm_cbm_part_get_part_num (a) - mm_cbm_part_get_part_num (b));
}

gboolean
mm_base_cbm_take_part (MMBaseCbm *self,
                       MMCbmPart *part,
                       GError **error)
{
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
                            (GCompareFunc)cmp_cbm_part_num)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part, part %u already taken",
                     mm_cbm_part_get_part_num (part));
        return FALSE;
    }

    if (mm_cbm_part_get_part_num (part) > self->priv->max_parts) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot take part with number %u, maximum is %u",
                     mm_cbm_part_get_part_num (part),
                     self->priv->max_parts);
        return FALSE;
    }

    /* Insert sorted by concat sequence */
    self->priv->parts = g_list_insert_sorted (self->priv->parts,
                                              part,
                                              (GCompareFunc)cmp_cbm_part_num);

    /* If this is the first part we take, initialize common CBM fields */
    if (g_list_length (self->priv->parts) == 1)
        initialize_cbm (self);

    /* We only populate contents when the multipart CBM is complete */
    if (mm_base_cbm_is_complete (self)) {
        g_autoptr (GError) inner_error = NULL;

        if (!assemble_cbm (self, &inner_error)) {
            /* We DO NOT propagate the error. The part was properly taken
             * so ownership passed to the MMBaseCbm object. */
            mm_obj_warn (self, "couldn't assemble CBM: %s", inner_error->message);
        } else {
            /* Completed AND assembled
             * Change state RECEIVING->RECEIVED, and signal completeness */
            if (mm_gdbus_cbm_get_state (MM_GDBUS_CBM (self)) == MM_CBM_STATE_RECEIVING)
                mm_gdbus_cbm_set_state (MM_GDBUS_CBM (self), MM_CBM_STATE_RECEIVED);
        }
    }

    return TRUE;
}

MMBaseCbm *
mm_base_cbm_new (GObject *bind_to)
{
    return MM_BASE_CBM (g_object_new (MM_TYPE_BASE_CBM,
                                      MM_BIND_TO, bind_to,
                                      NULL));
}

MMBaseCbm *
mm_base_cbm_new_with_part (GObject *bind_to,
                           MMCbmState state,
                           guint max_parts,
                           MMCbmPart *first_part,
                           GError **error)
{
    MMBaseCbm *self;

    if (state == MM_CBM_STATE_RECEIVED)
        state = MM_CBM_STATE_RECEIVING;

    /* Create a CBM object as defined by the interface */
    self = mm_base_cbm_new (bind_to);
    g_object_set (self,
                  MM_BASE_CBM_MAX_PARTS,           max_parts,
                  "state",                         state,
                  NULL);

    if (!mm_base_cbm_take_part (self, first_part, error))
        g_clear_object (&self);

    /* We do export incomplete multipart messages, so clients can make use of it
     * Only the STATE of the CBM object will be valid in the exported DBus
     * interface initially.*/
    if (self)
        mm_base_cbm_export (self);

    return self;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMBaseCbm *self;

    self = MM_BASE_CBM (_self);
    return g_strdup_printf ("cbm%u", self->priv->dbus_id);
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseCbm *self = MM_BASE_CBM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->path)
            cbm_dbus_unexport (self);
        else if (self->priv->connection)
            cbm_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            cbm_dbus_unexport (self);
        else if (self->priv->path)
            cbm_dbus_export (self);
        break;
    case PROP_BIND_TO:
        g_clear_object (&self->priv->bind_to);
        self->priv->bind_to = g_value_dup_object (value);
        mm_bind_to (MM_BIND (self), MM_BASE_CBM_CONNECTION, self->priv->bind_to);
        break;
    case PROP_MAX_PARTS:
        self->priv->max_parts = g_value_get_uint (value);
        break;
    case PROP_SERIAL:
        self->priv->serial = g_value_get_uint (value);
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
    MMBaseCbm *self = MM_BASE_CBM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_BIND_TO:
        g_value_set_object (value, self->priv->bind_to);
        break;
    case PROP_MAX_PARTS:
        g_value_set_uint (value, self->priv->max_parts);
        break;
    case PROP_SERIAL:
        g_value_set_uint (value, self->priv->serial);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_cbm_init (MMBaseCbm *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_CBM, MMBaseCbmPrivate);
    self->priv->max_parts = 1;

    /* Each CBM is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;
}

static void
finalize (GObject *object)
{
    MMBaseCbm *self = MM_BASE_CBM (object);

    g_list_free_full (self->priv->parts, (GDestroyNotify)mm_cbm_part_free);
    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_cbm_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseCbm *self = MM_BASE_CBM (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        cbm_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->bind_to);

    G_OBJECT_CLASS (mm_base_cbm_parent_class)->dispose (object);
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
mm_base_cbm_class_init (MMBaseCbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseCbmPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_CBM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BASE_CBM_PATH,
                             "Path",
                             "DBus path of the CBM",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    g_object_class_override_property (object_class, PROP_BIND_TO, MM_BIND_TO);

    properties[PROP_MAX_PARTS] =
        g_param_spec_uint (MM_BASE_CBM_MAX_PARTS,
                           "Max parts",
                           "Maximum number of parts composing this CBM",
                           1, 255, 1,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MAX_PARTS, properties[PROP_MAX_PARTS]);

    properties[PROP_SERIAL] =
        g_param_spec_uint (MM_BASE_CBM_SERIAL,
                           "Serial",
                           "The serial of this CBM",
                           0, G_MAXUINT16, 0,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SERIAL, properties[PROP_SERIAL]);
}

guint16
mm_base_cbm_get_serial (MMBaseCbm *self)
{
    return self->priv->serial;
}

guint16
mm_base_cbm_get_channel (MMBaseCbm *self)
{
    return mm_gdbus_cbm_get_channel (MM_GDBUS_CBM (self));
}
