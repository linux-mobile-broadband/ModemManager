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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Intel Corporation
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-signal-threshold-properties.h"

/**
 * SECTION: mm-signal-threshold-properties
 * @title: MMSignalThresholdProperties
 * @short_description: Helper object to handle signal threshold properties.
 *
 * The #MMSignalThresholdProperties is an object handling the properties requested
 * when setting up threshold based signal quality information reporting.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_signal_setup_thresholds() or mm_modem_signal_setup_thresholds_sync().
 */

G_DEFINE_TYPE (MMSignalThresholdProperties, mm_signal_threshold_properties, G_TYPE_OBJECT)

#define PROPERTY_RSSI_THRESHOLD       "rssi-threshold"
#define PROPERTY_ERROR_RATE_THRESHOLD "error-rate-threshold"

struct _MMSignalThresholdPropertiesPrivate {
    guint    rssi_threshold;
    gboolean rssi_threshold_set;
    gboolean error_rate_threshold;
    gboolean error_rate_threshold_set;
};

/*****************************************************************************/

/**
 * mm_signal_threshold_properties_set_rssi:
 * @self: a #MMSignalThresholdProperties.
 * @rssi_threshold: the RSSI threshold, or 0 to disable.
 *
 * Sets the RSSI threshold, in dBm.
 *
 * Since: 1.20
 */
void
mm_signal_threshold_properties_set_rssi (MMSignalThresholdProperties *self,
                                         guint                        rssi_threshold)
{
    g_return_if_fail (MM_IS_SIGNAL_THRESHOLD_PROPERTIES (self));

    self->priv->rssi_threshold = rssi_threshold;
    self->priv->rssi_threshold_set = TRUE;
}

/**
 * mm_signal_threshold_properties_get_rssi:
 * @self: a #MMSignalThresholdProperties.
 *
 * Gets the RSSI threshold, in dBm.
 *
 * Returns: the RSSI threshold, or 0 if disabled.
 *
 * Since: 1.20
 */
guint
mm_signal_threshold_properties_get_rssi (MMSignalThresholdProperties *self)
{
    g_return_val_if_fail (MM_IS_SIGNAL_THRESHOLD_PROPERTIES (self), 0);

    return self->priv->rssi_threshold;
}

/*****************************************************************************/

/**
 * mm_signal_threshold_properties_set_error_rate:
 * @self: a #MMSignalThresholdProperties.
 * @error_rate_threshold: %TRUE to enable, %FALSE to disable.
 *
 * Enables or disables the error rate threshold.
 *
 * Since: 1.20
 */
void
mm_signal_threshold_properties_set_error_rate (MMSignalThresholdProperties *self,
                                               gboolean                     error_rate_threshold)
{
    g_return_if_fail (MM_IS_SIGNAL_THRESHOLD_PROPERTIES (self));

    self->priv->error_rate_threshold = error_rate_threshold;
    self->priv->error_rate_threshold_set = TRUE;
}

/**
 * mm_signal_threshold_properties_get_error_rate:
 * @self: a #MMSignalThresholdProperties.
 *
 * Gets whether the error rate threshold is enabled or disabled.
 *
 * Returns: %TRUE if the error rate threshold is enabled, %FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_signal_threshold_properties_get_error_rate (MMSignalThresholdProperties *self)
{
    g_return_val_if_fail (MM_IS_SIGNAL_THRESHOLD_PROPERTIES (self), FALSE);

    return self->priv->error_rate_threshold;
}

/*****************************************************************************/

/**
 * mm_signal_threshold_properties_get_dictionary: (skip)
 */
GVariant *
mm_signal_threshold_properties_get_dictionary (MMSignalThresholdProperties *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_SIGNAL_THRESHOLD_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->rssi_threshold_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_RSSI_THRESHOLD,
                               g_variant_new_uint32 (self->priv->rssi_threshold));

    if (self->priv->error_rate_threshold_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ERROR_RATE_THRESHOLD,
                               g_variant_new_boolean (self->priv->error_rate_threshold));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

typedef struct {
    MMSignalThresholdProperties *properties;
    GError                      *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar          *key,
                   const gchar          *value,
                   ParseKeyValueContext *ctx)
{
    if (g_str_equal (key, PROPERTY_RSSI_THRESHOLD)) {
        guint rssi_threshold;

        if (!mm_get_uint_from_str (value, &rssi_threshold)) {
            g_set_error (&ctx->error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "invalid RSSI threshold value given: %s", value);
            return FALSE;
        }
        mm_signal_threshold_properties_set_rssi (ctx->properties, rssi_threshold);
        return TRUE;
    }

    if (g_str_equal (key, PROPERTY_ERROR_RATE_THRESHOLD)) {
        gboolean error_rate_threshold;

        error_rate_threshold = mm_common_get_boolean_from_string (value, &ctx->error);
        if (ctx->error) {
            g_prefix_error (&ctx->error, "invalid error rate threshold value given: ");
            return FALSE;
        }
        mm_signal_threshold_properties_set_error_rate (ctx->properties, error_rate_threshold);
        return TRUE;
    }

    g_set_error (&ctx->error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                 "Invalid properties string, unsupported key '%s'", key);
    return FALSE;
}

/**
 * mm_signal_threshold_properties_new_from_string: (skip)
 */
MMSignalThresholdProperties *
mm_signal_threshold_properties_new_from_string (const gchar  *str,
                                                GError      **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.properties = mm_signal_threshold_properties_new ();

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);
    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_clear_object (&ctx.properties);
    }

    return ctx.properties;
}

/*****************************************************************************/

static gboolean
consume_variant (MMSignalThresholdProperties  *self,
                 const gchar                  *key,
                 GVariant                     *value,
                 GError                      **error)
{
    if (g_str_equal (key, PROPERTY_RSSI_THRESHOLD))
        mm_signal_threshold_properties_set_rssi (self, g_variant_get_uint32 (value));
    else if (g_str_equal (key, PROPERTY_ERROR_RATE_THRESHOLD))
        mm_signal_threshold_properties_set_error_rate (self, g_variant_get_boolean (value));
    else {
        /* Set error */
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties dictionary, unexpected key '%s'", key);
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_signal_threshold_properties_new_from_dictionary: (skip)
 */
MMSignalThresholdProperties *
mm_signal_threshold_properties_new_from_dictionary (GVariant *dictionary,
                                                    GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMSignalThresholdProperties *properties;

    properties = mm_signal_threshold_properties_new ();
    if (!dictionary)
        return properties;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create signal threshold properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (properties);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error && g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (properties, key, value, &inner_error);
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
 * mm_signal_threshold_properties_new:
 *
 * Creates a new empty #MMSignalThresholdProperties.
 *
 * Returns: (transfer full): a #MMSignalThresholdProperties. The returned value should be freed with g_object_unref().
 *
 * Since: 1.20
 */
MMSignalThresholdProperties *
mm_signal_threshold_properties_new (void)
{
    return (MM_SIGNAL_THRESHOLD_PROPERTIES (
                g_object_new (MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES, NULL)));
}

static void
mm_signal_threshold_properties_init (MMSignalThresholdProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES,
                                              MMSignalThresholdPropertiesPrivate);
}

static void
mm_signal_threshold_properties_class_init (MMSignalThresholdPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSignalThresholdPropertiesPrivate));
}
