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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "mm-errors-types.h"
#include "mm-enums-types.h"
#include "mm-common-helpers.h"
#include "mm-call-properties.h"

/**
 * SECTION: mm-call-properties
 * @title: MMCallProperties
 * @short_description: Helper object to handle CALL properties.
 *
 * The #MMCallProperties is an object handling the properties to be set
 * in newly created CALL objects.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_voice_create_call() or mm_modem_voice_create_call_sync().
 */

G_DEFINE_TYPE (MMCallProperties, mm_call_properties, G_TYPE_OBJECT)

#define PROPERTY_NUMBER       "number"
#define PROPERTY_DIRECTION    "direction"
#define PROPERTY_STATE_REASON "state-reason"
#define PROPERTY_STATE        "state"

struct _MMCallPropertiesPrivate {
    gchar *number;
    MMCallDirection direction;
    MMCallState state;
    MMCallStateReason state_reason;
};

/*****************************************************************************/

/**
 * mm_call_properties_set_number:
 * @self: A #MMCallProperties.
 * @text: The number to set, in UTF-8.
 *
 * Sets the call number.
 */
void
mm_call_properties_set_number (MMCallProperties *self,
                               const gchar *number)
{
    g_return_if_fail (MM_IS_CALL_PROPERTIES (self));

    g_free (self->priv->number);
    self->priv->number = g_strdup (number);
}

/**
 * mm_call_properties_get_number:
 * @self: A #MMCallProperties.
 *
 * Gets the number, in UTF-8.
 *
 * Returns: the call number, or %NULL if it doesn't contain any (anonymous caller). Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_call_properties_get_number (MMCallProperties *self)
{
    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), NULL);

    return self->priv->number;
}

/*****************************************************************************/

/**
 * mm_call_properties_set_direction:
 * @self: A #MMCallProperties.
 * @direction: the call direction
 *
 * Sets the call direction
 */
void
mm_call_properties_set_direction (MMCallProperties *self,
                                  MMCallDirection direction)
{
    g_return_if_fail (MM_IS_CALL_PROPERTIES (self));

    self->priv->direction = direction;
}

/**
 * mm_call_properties_get_direction:
 * @self: A #MMCallProperties.
 *
 * Gets the call direction.
 *
 * Returns: the call direction.
 */
MMCallDirection
mm_call_properties_get_direction (MMCallProperties *self)
{
    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), MM_CALL_DIRECTION_UNKNOWN);

    return self->priv->direction;
}

/*****************************************************************************/

/**
 * mm_call_properties_set_state:
 * @self: A #MMCallProperties.
 * @state: the call state
 *
 * Sets the call state
 */
void
mm_call_properties_set_state (MMCallProperties *self,
                              MMCallState state)
{
    g_return_if_fail (MM_IS_CALL_PROPERTIES (self));

    self->priv->state = state;
}

/**
 * mm_call_properties_get_state:
 * @self: A #MMCallProperties.
 *
 * Gets the call state.
 *
 * Returns: the call state.
 */
MMCallState
mm_call_properties_get_state (MMCallProperties *self)
{
    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), MM_CALL_STATE_UNKNOWN);

    return self->priv->state;
}

/*****************************************************************************/

/**
 * mm_call_properties_set_state_reason:
 * @self: A #MMCallProperties.
 * @state_reason: the call state_reason
 *
 * Sets the call state reason
 */
void
mm_call_properties_set_state_reason (MMCallProperties *self,
                                     MMCallStateReason state_reason)
{
    g_return_if_fail (MM_IS_CALL_PROPERTIES (self));

    self->priv->state_reason = state_reason;
}

/**
 * mm_call_properties_get_state_reason:
 * @self: A #MMCallProperties.
 *
 * Gets the call state reason.
 *
 * Returns: the call state reason.
 */
MMCallStateReason
mm_call_properties_get_state_reason (MMCallProperties *self)
{
    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), MM_CALL_STATE_UNKNOWN);

    return self->priv->state_reason;
}

/*****************************************************************************/

GVariant *
mm_call_properties_get_dictionary (MMCallProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->number)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_NUMBER,
                               g_variant_new_string (self->priv->number));

    if (self->priv->state_reason != MM_CALL_STATE_REASON_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_STATE_REASON,
                               g_variant_new_uint32 (self->priv->state_reason));

    if (self->priv->state != MM_CALL_STATE_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_STATE,
                               g_variant_new_uint32 (self->priv->state));

    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_DIRECTION,
                           g_variant_new_uint32 (self->priv->direction));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/
static gboolean
consume_string (MMCallProperties *self,
                const gchar *key,
                const gchar *value,
                GError **error)
{
    if (g_str_equal (key, PROPERTY_NUMBER)) {
        mm_call_properties_set_number (self, value);
    } else if (g_str_equal (key, PROPERTY_DIRECTION)) {
        MMCallDirection direction;
        GError *inner_error = NULL;

        direction = mm_common_get_call_direction_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_call_properties_set_direction(self, direction);
    } else if (g_str_equal (key, PROPERTY_STATE)) {
        MMCallState state;
        GError *inner_error = NULL;

        state = mm_common_get_call_state_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_call_properties_set_state(self, state);
    } else if (g_str_equal (key, PROPERTY_STATE_REASON)) {
        MMCallStateReason state_reason;
        GError *inner_error = NULL;

        state_reason = mm_common_get_call_state_reason_from_string (value, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }

        mm_call_properties_set_state_reason (self, state_reason);
    } else {
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
    MMCallProperties *properties;
    GError *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    return consume_string (ctx->properties,
                           key,
                           value,
                           &ctx->error);
}

MMCallProperties *
mm_call_properties_new_from_string (const gchar *str,
                                   GError **error)
{
    ParseKeyValueContext ctx;

    ctx.properties = mm_call_properties_new ();
    ctx.error = NULL;

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
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
consume_variant (MMCallProperties *properties,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_NUMBER))
        mm_call_properties_set_number (
            properties,
            g_variant_get_string (value, NULL));
    else if (g_str_equal (key, PROPERTY_DIRECTION))
        mm_call_properties_set_direction (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_STATE))
        mm_call_properties_set_state (
            properties,
            g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_STATE_REASON))
        mm_call_properties_set_state_reason (
            properties,
            g_variant_get_uint32 (value));
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

MMCallProperties *
mm_call_properties_new_from_dictionary (GVariant *dictionary,
                                       GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCallProperties *properties;

    properties = mm_call_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create call properties from dictionary: "
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
 * mm_call_properties_dup:
 * @orig: a #MMCallProperties
 *
 * Returns a copy of @orig.
 *
 * Returns: (transfer full): a #MMCallProperties
 */
MMCallProperties *
mm_call_properties_dup (MMCallProperties *orig)
{
    GVariant *dict;
    MMCallProperties *copy;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (orig), NULL);

    dict = mm_call_properties_get_dictionary (orig);
    copy = mm_call_properties_new_from_dictionary (dict, &error);
    g_assert_no_error (error);
    g_variant_unref (dict);

    return copy;
}

/*****************************************************************************/

/**
 * mm_call_properties_new:
 *
 * Creates a new empty #MMCallProperties.
 *
 * Returns: (transfer full): a #MMCallProperties. The returned value should be freed with g_object_unref().
 */
MMCallProperties *
mm_call_properties_new (void)
{
    return (MM_CALL_PROPERTIES (g_object_new (MM_TYPE_CALL_PROPERTIES, NULL)));
}

static void
mm_call_properties_init (MMCallProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_CALL_PROPERTIES,
                                              MMCallPropertiesPrivate);

    self->priv->number       = NULL;
    self->priv->direction    = MM_CALL_DIRECTION_UNKNOWN;
    self->priv->state        = MM_CALL_STATE_UNKNOWN;
    self->priv->state_reason = MM_CALL_STATE_REASON_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMCallProperties *self = MM_CALL_PROPERTIES (object);

    g_free (self->priv->number);

    G_OBJECT_CLASS (mm_call_properties_parent_class)->finalize (object);
}

static void
mm_call_properties_class_init (MMCallPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallPropertiesPrivate));

    object_class->finalize = finalize;
}
