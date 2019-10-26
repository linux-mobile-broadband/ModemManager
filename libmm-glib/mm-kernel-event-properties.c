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
 * Copyright (C) 2016 Velocloud, Inc.
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "mm-errors-types.h"
#include "mm-enums-types.h"
#include "mm-common-helpers.h"
#include "mm-kernel-event-properties.h"

/**
 * SECTION: mm-kernel-event-properties
 * @title: MMKernelEventProperties
 * @short_description: Helper object to handle kernel event properties.
 *
 * The #MMKernelEventProperties is an object handling the properties to be set
 * in reported kernel events.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_manager_report_kernel_event() or mm_manager_report_kernel_event_sync().
 */

G_DEFINE_TYPE (MMKernelEventProperties, mm_kernel_event_properties, G_TYPE_OBJECT)

#define PROPERTY_ACTION    "action"
#define PROPERTY_SUBSYSTEM "subsystem"
#define PROPERTY_NAME      "name"
#define PROPERTY_UID       "uid"

struct _MMKernelEventPropertiesPrivate {
    gchar   *action;
    gchar   *subsystem;
    gchar   *name;
    gchar   *uid;
};

/*****************************************************************************/

/**
 * mm_kernel_event_properties_set_action:
 * @self: A #MMKernelEventProperties.
 * @action: The action to set.
 *
 * Sets the action.
 *
 * Since: 1.8
 */
void
mm_kernel_event_properties_set_action (MMKernelEventProperties *self,
                                       const gchar             *action)
{
    g_return_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self));

    g_free (self->priv->action);
    self->priv->action = g_strdup (action);
}

/**
 * mm_kernel_event_properties_get_action:
 * @self: A #MMKernelEventProperties.
 *
 * Gets the action.
 *
 * Returns: (transfer none): The action. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.8
 */
const gchar *
mm_kernel_event_properties_get_action (MMKernelEventProperties *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self), NULL);

    return self->priv->action;
}

/*****************************************************************************/

/**
 * mm_kernel_event_properties_set_subsystem:
 * @self: A #MMKernelEventProperties.
 * @subsystem: The subsystem to set.
 *
 * Sets the subsystem.
 *
 * Since: 1.8
 */
void
mm_kernel_event_properties_set_subsystem (MMKernelEventProperties *self,
                                          const gchar             *subsystem)
{
    g_return_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self));

    g_free (self->priv->subsystem);
    self->priv->subsystem = g_strdup (subsystem);
}

/**
 * mm_kernel_event_properties_get_subsystem:
 * @self: A #MMKernelEventProperties.
 *
 * Gets the subsystem.
 *
 * Returns: (transfer none): The subsystem. Do not free the returned value, it
 * is owned by @self.
 *
 * Since: 1.8
 */
const gchar *
mm_kernel_event_properties_get_subsystem (MMKernelEventProperties *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self), NULL);

    return self->priv->subsystem;
}

/*****************************************************************************/

/**
 * mm_kernel_event_properties_set_name:
 * @self: A #MMKernelEventProperties.
 * @name: The name to set.
 *
 * Sets the name.
 *
 * Since: 1.8
 */
void
mm_kernel_event_properties_set_name (MMKernelEventProperties *self,
                                     const gchar             *name)
{
    g_return_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self));

    g_free (self->priv->name);
    self->priv->name = g_strdup (name);
}

/**
 * mm_kernel_event_properties_get_name:
 * @self: A #MMKernelEventProperties.
 *
 * Gets the name.
 *
 * Returns: (transfer none): The name. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.8
 */
const gchar *
mm_kernel_event_properties_get_name (MMKernelEventProperties *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self), NULL);

    return self->priv->name;
}

/*****************************************************************************/

/**
 * mm_kernel_event_properties_set_uid:
 * @self: A #MMKernelEventProperties.
 * @uid: The uid to set.
 *
 * Sets the unique ID of the physical device.
 *
 * Since: 1.8
 */
void
mm_kernel_event_properties_set_uid (MMKernelEventProperties *self,
                                    const gchar             *uid)
{
    g_return_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self));

    g_free (self->priv->uid);
    self->priv->uid = g_strdup (uid);
}

/**
 * mm_kernel_event_properties_get_uid:
 * @self: A #MMKernelEventProperties.
 *
 * Gets the unique ID of the physical device.
 *
 * Returns: (transfer none): The uid. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.8
 */
const gchar *
mm_kernel_event_properties_get_uid (MMKernelEventProperties *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self), NULL);

    return self->priv->uid;
}

/*****************************************************************************/

/**
 * mm_kernel_event_properties_get_dictionary: (skip)
 */
GVariant *
mm_kernel_event_properties_get_dictionary (MMKernelEventProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->action)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ACTION,
                               g_variant_new_string (self->priv->action));

    if (self->priv->subsystem)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_SUBSYSTEM,
                               g_variant_new_string (self->priv->subsystem));

    if (self->priv->name)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NAME,
                               g_variant_new_string (self->priv->name));

    if (self->priv->uid)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_UID,
                               g_variant_new_string (self->priv->uid));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_string (MMKernelEventProperties  *self,
                const gchar              *key,
                const gchar              *value,
                GError                  **error)
{
    if (g_str_equal (key, PROPERTY_ACTION))
        mm_kernel_event_properties_set_action (self, value);
    else if (g_str_equal (key, PROPERTY_SUBSYSTEM))
        mm_kernel_event_properties_set_subsystem (self, value);
    else if (g_str_equal (key, PROPERTY_NAME))
        mm_kernel_event_properties_set_name (self, value);
    else if (g_str_equal (key, PROPERTY_UID))
        mm_kernel_event_properties_set_uid (self, value);
    else {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties string, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

typedef struct {
    MMKernelEventProperties *properties;
    GError                  *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar          *key,
                   const gchar          *value,
                   ParseKeyValueContext *ctx)
{
    return consume_string (ctx->properties,
                           key,
                           value,
                           &ctx->error);
}

/**
 * mm_kernel_event_properties_new_from_string: (skip)
 */
MMKernelEventProperties *
mm_kernel_event_properties_new_from_string (const gchar  *str,
                                            GError      **error)
{
    ParseKeyValueContext ctx;

    ctx.properties = mm_kernel_event_properties_new ();
    ctx.error = NULL;

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn) key_value_foreach,
                                      &ctx);

    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_object_unref (ctx.properties);
        ctx.properties = NULL;
    }

    return ctx.properties;
}

/*****************************************************************************/

static gboolean
consume_variant (MMKernelEventProperties  *properties,
                 const gchar              *key,
                 GVariant                 *value,
                 GError                  **error)
{
    if (g_str_equal (key, PROPERTY_ACTION))
        mm_kernel_event_properties_set_action (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_SUBSYSTEM))
        mm_kernel_event_properties_set_subsystem (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_NAME))
        mm_kernel_event_properties_set_name (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_UID))
        mm_kernel_event_properties_set_uid (
            properties,
            g_variant_get_string (value, NULL));
    else {
        /* Set error */
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties dictionary, unexpected key '%s'",
                     key);
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_kernel_event_properties_new_from_dictionary: (skip)
 */
MMKernelEventProperties *
mm_kernel_event_properties_new_from_dictionary (GVariant  *dictionary,
                                                GError   **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMKernelEventProperties *properties;

    properties = mm_kernel_event_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create kernel event properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (properties);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (properties,
                         key,
                         value,
                         &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (properties);
        properties = NULL;
    }

    return properties;
}

/*****************************************************************************/

/**
 * mm_kernel_event_properties_new:
 *
 * Creates a new empty #MMKernelEventProperties.
 *
 * Returns: (transfer full): a #MMKernelEventProperties. The returned value
 * should be freed with g_object_unref().
 *
 * Since: 1.8
 */
MMKernelEventProperties *
mm_kernel_event_properties_new (void)
{
    return (MM_KERNEL_EVENT_PROPERTIES (g_object_new (MM_TYPE_KERNEL_EVENT_PROPERTIES, NULL)));
}

static void
mm_kernel_event_properties_init (MMKernelEventProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_KERNEL_EVENT_PROPERTIES,
                                              MMKernelEventPropertiesPrivate);
}

static void
finalize (GObject *object)
{
    MMKernelEventProperties *self = MM_KERNEL_EVENT_PROPERTIES (object);

    g_free (self->priv->action);
    g_free (self->priv->subsystem);
    g_free (self->priv->name);
    g_free (self->priv->uid);

    G_OBJECT_CLASS (mm_kernel_event_properties_parent_class)->finalize (object);
}

static void
mm_kernel_event_properties_class_init (MMKernelEventPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMKernelEventPropertiesPrivate));

    object_class->finalize = finalize;
}
