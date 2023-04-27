/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "mm-errors-types.h"
#include "mm-enums-types.h"
#include "mm-flags-types.h"
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

#define PROPERTY_NUMBER "number"

struct _MMCallPropertiesPrivate {
    gchar *number;
};

/*****************************************************************************/

/**
 * mm_call_properties_set_number:
 * @self: A #MMCallProperties.
 * @text: The number to set, in UTF-8.
 *
 * Sets the call number.
 *
 * Since: 1.6
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
 * Returns: the call number, or %NULL if it doesn't contain any (anonymous
 * caller). Do not free the returned value, it is owned by @self.
 *
 * Since: 1.6
 */
const gchar *
mm_call_properties_get_number (MMCallProperties *self)
{
    g_return_val_if_fail (MM_IS_CALL_PROPERTIES (self), NULL);

    return self->priv->number;
}

/*****************************************************************************/

/*
 * mm_call_properties_get_dictionary: (skip)
 */
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

/*
 * mm_call_properties_new_from_string: (skip)
 */
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

/*
 * mm_call_properties_new_from_dictionary: (skip)
 */
MMCallProperties *
mm_call_properties_new_from_dictionary (GVariant  *dictionary,
                                        GError   **error)
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
 * mm_call_properties_new:
 *
 * Creates a new empty #MMCallProperties.
 *
 * Returns: (transfer full): a #MMCallProperties. The returned value should be
 * freed with g_object_unref().
 *
 * Since: 1.6
 */
MMCallProperties *
mm_call_properties_new (void)
{
    return (MM_CALL_PROPERTIES (g_object_new (MM_TYPE_CALL_PROPERTIES, NULL)));
}

static void
mm_call_properties_init (MMCallProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_CALL_PROPERTIES,
                                              MMCallPropertiesPrivate);
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
