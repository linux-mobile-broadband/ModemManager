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
 * Copyright (C) 2007 - 2008 Novell, Inc.
 * Copyright (C) 2008 - 2010 Red Hat, Inc.
 */

#include <string.h>
#include <stdio.h>

#include <dbus/dbus-glib.h>
#include "mm-marshal.h"
#include "mm-properties-changed-signal.h"
#include "mm-properties-changed-glue.h"
#include "mm-log.h"

#define DBUS_TYPE_G_MAP_OF_VARIANT  (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))
#define DBUS_TYPE_G_ARRAY_OF_STRING (dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING))

#define MM_PC_SIGNAL_NAME "mm-properties-changed"
#define DBUS_PC_SIGNAL_NAME "properties-changed"
#define MM_DBUS_PROPERTY_CHANGED "MM_DBUS_PROPERTY_CHANGED"

/*****************************************************************************/

typedef struct {
    char *real_property;
    char *interface;
} ChangeInfo;

typedef struct {
    /* Whitelist of GObject property names for which changes will be emitted
     * over the bus.
     *
     * Mapping of {property-name -> ChangeInfo}
     */
    GHashTable *registered;

    /* Table of each D-Bus interface of the object for which one or more
     * properties have changed, and those properties and their new values.
     * Destroyed after the changed signal has been sent.
     * 
     * Mapping of {dbus-interface -> {property-name -> value}}
     */
    GHashTable *hash;

    guint idle_id;
} PropertiesChangedInfo;

static void
destroy_value (gpointer data)
{
    GValue *val = (GValue *) data;

    g_value_unset (val);
    g_slice_free (GValue, val);
}

static void
change_info_free (gpointer data)
{
    ChangeInfo *info = data;

    g_free (info->real_property);
    g_free (info->interface);
    memset (info, 0, sizeof (ChangeInfo));
    g_free (info);
}

static PropertiesChangedInfo *
properties_changed_info_new (void)
{
    PropertiesChangedInfo *info;

    info = g_slice_new0 (PropertiesChangedInfo);

    info->registered = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, change_info_free);
    info->hash = g_hash_table_new_full (g_str_hash, g_str_equal, 
                                        (GDestroyNotify) g_free,
                                        (GDestroyNotify) g_hash_table_destroy);
    return info;
}

static void
properties_changed_info_destroy (gpointer data)
{
    PropertiesChangedInfo *info = (PropertiesChangedInfo *) data;

    if (info->idle_id)
        g_source_remove (info->idle_id);

    g_hash_table_destroy (info->hash);
    g_hash_table_destroy (info->registered);
    g_slice_free (PropertiesChangedInfo, info);
}

#ifdef DEBUG
static void
add_to_string (gpointer key, gpointer value, gpointer user_data)
{
    char *buf = (char *) user_data;
    GValue str_val = { 0, };

    g_value_init (&str_val, G_TYPE_STRING);
    if (!g_value_transform ((GValue *) value, &str_val)) {
        if (G_VALUE_HOLDS_OBJECT (value)) {
            GObject *obj = g_value_get_object (value);

            if (g_value_get_object (value)) {
                sprintf (buf + strlen (buf), "{%s: %p (%s)}, ",
                         (const char *) key, obj, G_OBJECT_TYPE_NAME (obj));
            } else {
                sprintf (buf + strlen (buf), "{%s: %p}, ", (const char *) key, obj);
            }
        } else
            sprintf (buf + strlen (buf), "{%s: <transform error>}, ", (const char *) key);
    } else {
        sprintf (buf + strlen (buf), "{%s: %s}, ", (const char *) key, g_value_get_string (&str_val));
    }
    g_value_unset (&str_val);
}
#endif

static gboolean
properties_changed (gpointer data)
{
    GObject *object = G_OBJECT (data);
    PropertiesChangedInfo *info;
    GHashTableIter iter;
    gpointer key, value;

    info = (PropertiesChangedInfo *) g_object_get_data (object, MM_DBUS_PROPERTY_CHANGED);
    g_assert (info);

    g_hash_table_iter_init (&iter, info->hash);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        const char *interface = (const char *) key;
        GHashTable *props = (GHashTable *) value;
        GPtrArray *ignore = g_ptr_array_new ();

#ifdef DEBUG
        {
            char buf[2048] = { 0, };
            g_hash_table_foreach (props, add_to_string, &buf);
            mm_dbg ("%s: %s -> (%s) %s", __func__,
                    G_OBJECT_TYPE_NAME (object),
                    interface,
                    buf);
        }
#endif

        /* Send the PropertiesChanged signal */
        g_signal_emit_by_name (object, MM_PC_SIGNAL_NAME, interface, props);
        g_signal_emit_by_name (object, DBUS_PC_SIGNAL_NAME, interface, props, ignore);
        g_ptr_array_free (ignore, TRUE);
    }
    g_hash_table_remove_all (info->hash);

    return FALSE;
}

static void
idle_id_reset (gpointer data)
{
    GObject *object = G_OBJECT (data);
    PropertiesChangedInfo *info = (PropertiesChangedInfo *) g_object_get_data (object, MM_DBUS_PROPERTY_CHANGED);

    /* info is unset when the object is being destroyed */
    if (info)
        info->idle_id = 0;
}

static char*
uscore_to_wincaps (const char *uscore)
{
    const char *p;
    GString *str;
    gboolean last_was_uscore;

    last_was_uscore = TRUE;
  
    str = g_string_new (NULL);
    p = uscore;
    while (p && *p) {
        if (*p == '-' || *p == '_')
            last_was_uscore = TRUE;
        else {
            if (last_was_uscore) {
                g_string_append_c (str, g_ascii_toupper (*p));
                last_was_uscore = FALSE;
            } else
                g_string_append_c (str, *p);
        }
        ++p;
    }

    return g_string_free (str, FALSE);
}

static PropertiesChangedInfo *
get_properties_changed_info (GObject *object)
{
    PropertiesChangedInfo *info = NULL;

    info = (PropertiesChangedInfo *) g_object_get_data (object, MM_DBUS_PROPERTY_CHANGED);
    if (!info) {
        info = properties_changed_info_new ();
        g_object_set_data_full (object, MM_DBUS_PROPERTY_CHANGED, info, properties_changed_info_destroy);
    }

    g_assert (info);
    return info;
}

static void
notify (GObject *object, GParamSpec *pspec)
{
    GHashTable *interfaces;
    PropertiesChangedInfo *info;
    ChangeInfo *ch_info;
    GValue *value;

    info = get_properties_changed_info (object);

    ch_info = g_hash_table_lookup (info->registered, pspec->name);
    if (!ch_info)
        return;

    /* Check if there are other changed properties for this interface already,
     * otherwise create a new hash table for all changed properties for this
     * D-Bus interface.
     */
    interfaces = g_hash_table_lookup (info->hash, ch_info->interface);
    if (!interfaces) {
        interfaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, destroy_value);
        g_hash_table_insert (info->hash, g_strdup (ch_info->interface), interfaces);
    }

    /* Now put the changed property value into the hash table of changed values
     * for its D-Bus interface.
     */
    value = g_slice_new0 (GValue);
    g_value_init (value, pspec->value_type);
    g_object_get_property (object, pspec->name, value);

    /* Use real property name, which takes shadow properties into accound */
    g_hash_table_insert (interfaces, uscore_to_wincaps (ch_info->real_property), value);

    if (!info->idle_id)
        info->idle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, properties_changed, object, idle_id_reset);
}

void
mm_properties_changed_signal_register_property (GObject *object,
                                                const char *gobject_property,
                                                const char *real_property,
                                                const char *interface)
{
    PropertiesChangedInfo *info;
    ChangeInfo *ch_info;

    /* All exported properties need to be registered explicitly for now since
     * dbus-glib doesn't expose any method to find out the properties registered
     * in the XML.
     */

    info = get_properties_changed_info (object);
    ch_info = g_hash_table_lookup (info->registered, gobject_property);
    if (ch_info) {
        g_warning ("%s: property '%s' already registerd on interface '%s'",
                   __func__, gobject_property, ch_info->interface);
    } else {
        ch_info = g_malloc0 (sizeof (ChangeInfo));
        ch_info->real_property = g_strdup (real_property ? real_property : gobject_property);
        ch_info->interface = g_strdup (interface);
        g_hash_table_insert (info->registered, g_strdup (gobject_property), ch_info);
    }
}

void
mm_properties_changed_signal_enable (GObjectClass *object_class)
{
    object_class->notify = notify;
}

/*****************************************************************************/

static void
mm_properties_changed_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    g_signal_new (MM_PC_SIGNAL_NAME,
                  G_TYPE_FROM_INTERFACE (g_iface),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  mm_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, DBUS_TYPE_G_MAP_OF_VARIANT);

    g_signal_new (DBUS_PC_SIGNAL_NAME,
                  G_TYPE_FROM_INTERFACE (g_iface),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  mm_marshal_VOID__STRING_BOXED_BOXED,
                  G_TYPE_NONE, 3,
                  G_TYPE_STRING,
                  DBUS_TYPE_G_MAP_OF_VARIANT,
                  DBUS_TYPE_G_ARRAY_OF_STRING);

    initialized = TRUE;
}

GType
mm_properties_changed_get_type (void)
{
    static GType pc_type = 0;

    if (!G_UNLIKELY (pc_type)) {
        const GTypeInfo pc_info = {
            sizeof (MMPropertiesChanged), /* class_size */
            mm_properties_changed_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        pc_type = g_type_register_static (G_TYPE_INTERFACE,
                                          "MMPropertiesChanged",
                                          &pc_info, 0);

        g_type_interface_add_prerequisite (pc_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (pc_type, &dbus_glib_mm_properties_changed_object_info);
    }

    return pc_type;
}
