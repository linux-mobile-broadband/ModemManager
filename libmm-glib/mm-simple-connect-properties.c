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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-simple-connect-properties.h"

/**
 * SECTION: mm-simple-connect-properties
 * @title: MMSimpleConnectProperties
 * @short_description: Helper object to handle connection properties.
 *
 * The #MMSimpleConnectProperties is an object handling the properties requested
 * to ModemManager when launching a connection with the Simple interface.
 *
 * This object is created by the user and passed to ModemManager with either
 * mm_modem_simple_connect() or mm_modem_simple_connect_sync().
 */

G_DEFINE_TYPE (MMSimpleConnectProperties, mm_simple_connect_properties, G_TYPE_OBJECT);

#define PROPERTY_PIN             "pin"
#define PROPERTY_OPERATOR_ID     "operator-id"
#define PROPERTY_BANDS           "bands"
#define PROPERTY_ALLOWED_MODES   "allowed-modes"
#define PROPERTY_PREFERRED_MODE  "preferred-mode"

struct _MMSimpleConnectPropertiesPrivate {
    /* PIN */
    gchar *pin;
    /* Operator ID */
    gchar *operator_id;
    /* Bands */
    gboolean bands_set;
    MMModemBand *bands;
    guint n_bands;
    /* Modes */
    gboolean allowed_modes_set;
    MMModemMode allowed_modes;
    MMModemMode preferred_mode;
    /* Bearer properties */
    MMBearerProperties *bearer_properties;
};

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_pin:
 * @self: a #MMSimpleConnectProperties.
 * @pin: PIN code.
 *
 * Sets the PIN code to use when unlocking the modem.
 */
void
mm_simple_connect_properties_set_pin (MMSimpleConnectProperties *self,
                                      const gchar *pin)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    g_free (self->priv->pin);
    self->priv->pin = g_strdup (pin);
}

/**
 * mm_simple_connect_properties_get_pin:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the PIN code to use when unlocking the modem.
 *
 * Returns: (transfer none): the PIN, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_pin (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return self->priv->pin;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_operator_id:
 * @self: a #MMSimpleConnectProperties.
 * @operator_id: operator ID, given as MCC/MNC.
 *
 * Sets the ID of the network to which register before connecting.
 */
void
mm_simple_connect_properties_set_operator_id (MMSimpleConnectProperties *self,
                                              const gchar *operator_id)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_simple_connect_properties_get_operator_id:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the ID of the network to which register before connecting.
 *
 * Returns: (transfer none): the operator ID, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_operator_id (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return self->priv->operator_id;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_bands:
 * @self: a #MMSimpleConnectProperties.
 * @bands: array of #MMModemBand values.
 * @n_bands: number of elements in @bands.
 *
 * Sets the frequency bands to use.
 */
void
mm_simple_connect_properties_set_bands (MMSimpleConnectProperties *self,
                                        const MMModemBand *bands,
                                        guint n_bands)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    g_free (self->priv->bands);
    self->priv->n_bands = n_bands;
    self->priv->bands = g_new (MMModemBand, self->priv->n_bands);
    memcpy (self->priv->bands,
            bands,
            sizeof (MMModemBand) * self->priv->n_bands);
    self->priv->bands_set = TRUE;
}

/**
 * mm_simple_connect_properties_get_bands:
 * @self: a #MMSimpleConnectProperties.
 * @bands: (out): location for the array of #MMModemBand values. Do not free the returned value, it is owned by @self.
 * @n_bands: (out) number of elements in @bands.
 *
 * Gets the frequency bands to use.
 *
 * Returns: %TRUE if @bands is set, %FALSE otherwise.
 */
gboolean
mm_simple_connect_properties_get_bands (MMSimpleConnectProperties *self,
                                        const MMModemBand **bands,
                                        guint *n_bands)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), FALSE);
    g_return_val_if_fail (bands != NULL, FALSE);
    g_return_val_if_fail (n_bands != NULL, FALSE);

    if (self->priv->bands_set) {
        *bands = self->priv->bands;
        *n_bands = self->priv->n_bands;
        return TRUE;
    }

    return FALSE;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_allowed_modes:
 * @self: a #MMSimpleConnectProperties.
 * @allowed: bitmask of #MMModemMode values specifying which are allowed.
 * @preferred: a #MMModemMode value, specifying which of the ones in @allowed is preferred, if any.
 *
 * Sets the modes allowed to use, and which of them is preferred.
 */
void
mm_simple_connect_properties_set_allowed_modes (MMSimpleConnectProperties *self,
                                                MMModemMode allowed,
                                                MMModemMode preferred)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    self->priv->allowed_modes = allowed;
    self->priv->preferred_mode = preferred;
    self->priv->allowed_modes_set = TRUE;
}

/**
 * mm_simple_connect_properties_get_allowed_modes:
 * @self: a #MMSimpleConnectProperties.
 * @allowed: (out): location for the bitmask of #MMModemMode values specifying which are allowed.
 * @preferred: (out): loction for a #MMModemMode value, specifying which of the ones in @allowed is preferred, if any.
 *
 * Gets the modes allowed to use, and which of them is preferred.
 *
 * Returns: %TRUE if @allowed and @preferred are set, %FALSE otherwise.
 */
gboolean
mm_simple_connect_properties_get_allowed_modes (MMSimpleConnectProperties *self,
                                                MMModemMode *allowed,
                                                MMModemMode *preferred)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), FALSE);
    g_return_val_if_fail (allowed != NULL, FALSE);
    g_return_val_if_fail (preferred != NULL, FALSE);

    if (self->priv->allowed_modes_set) {
        *allowed = self->priv->allowed_modes;
        *preferred = self->priv->preferred_mode;
        return TRUE;
    }

    return FALSE;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_apn:
 * @self: a #MMSimpleConnectProperties.
 * @apn: Name of the access point.
 *
 * Sets the name of the access point to use when connecting.
 */
void
mm_simple_connect_properties_set_apn (MMSimpleConnectProperties *self,
                                      const gchar *apn)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_apn (self->priv->bearer_properties,
                                  apn);
}

/**
 * mm_simple_connect_properties_get_apn:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the name of the access point to use when connecting.
 *
 * Returns: (transfer none): the access point, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_apn (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_apn (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_user:
 * @self: a #MMSimpleConnectProperties.
 * @user: the username
 *
 * Sets the username used to authenticate with the access point.
 */
void
mm_simple_connect_properties_set_user (MMSimpleConnectProperties *self,
                                       const gchar *user)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_user (self->priv->bearer_properties,
                                   user);
}

/**
 * mm_simple_connect_properties_get_user:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the username used to authenticate with the access point.
 *
 * Returns: (transfer none): the username, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_user (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_user (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_password:
 * @self: a #MMSimpleConnectProperties.
 * @password: the password
 *
 * Sets the password used to authenticate with the access point.
 */
void
mm_simple_connect_properties_set_password (MMSimpleConnectProperties *self,
                                           const gchar *password)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_password (self->priv->bearer_properties,
                                       password);
}

/**
 * mm_simple_connect_properties_get_password:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the password used to authenticate with the access point.
 *
 * Returns: (transfer none): the password, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_password (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_password (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_ip_type:
 * @self: a #MMSimpleConnectProperties.
 * @ip_type: a #MMBearerIpFamily.
 *
 * Sets the IP type to use.
 */
void
mm_simple_connect_properties_set_ip_type (MMSimpleConnectProperties *self,
                                          MMBearerIpFamily ip_type)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_ip_type (self->priv->bearer_properties,
                                      ip_type);
}

/**
 * mm_simple_connect_properties_get_ip_type:
 * @self: a #MMSimpleConnectProperties.
 *
 * Sets the IP type to use.
 *
 * Returns: a #MMBearerIpFamily.
 */
MMBearerIpFamily
mm_simple_connect_properties_get_ip_type (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), MM_BEARER_IP_FAMILY_UNKNOWN);

    return mm_bearer_properties_get_ip_type (self->priv->bearer_properties);
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_allow_roaming:
 * @self: a #MMSimpleConnectProperties.
 * @allow_roaming: boolean value.
 *
 * Sets the flag to indicate whether roaming is allowed or not in the
 * connection.
 */
void
mm_simple_connect_properties_set_allow_roaming (MMSimpleConnectProperties *self,
                                                gboolean allow_roaming)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_allow_roaming (self->priv->bearer_properties,
                                            allow_roaming);
}

/**
 * mm_simple_connect_properties_get_allow_roaming:
 * @self: a #MMSimpleConnectProperties.
 *
 * Checks whether roaming is allowed in the connection.
 *
 * Returns: %TRUE if roaming is allowed, %FALSE otherwise..
 */
gboolean
mm_simple_connect_properties_get_allow_roaming (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), FALSE);

    return mm_bearer_properties_get_allow_roaming (self->priv->bearer_properties);
}


/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_number:
 * @self: a #MMSimpleConnectProperties.
 * @number: the number.
 *
 * Sets the number to use when performing the connection.
 */
void
mm_simple_connect_properties_set_number (MMSimpleConnectProperties *self,
                                         const gchar *number)
{
    g_return_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self));

    mm_bearer_properties_set_number (self->priv->bearer_properties,
                                     number);
}

/**
 * mm_simple_connect_properties_get_number:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the number to use when performing the connection.
 *
 * Returns: (transfer none): the number, or #NULL if not set. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_simple_connect_properties_get_number (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return mm_bearer_properties_get_number (self->priv->bearer_properties);
}

/*****************************************************************************/

MMBearerProperties *
mm_simple_connect_properties_get_bearer_properties (MMSimpleConnectProperties *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    return g_object_ref (self->priv->bearer_properties);
}

/*****************************************************************************/

GVariant *
mm_simple_connect_properties_get_dictionary (MMSimpleConnectProperties *self)
{
    GVariantBuilder builder;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    GVariant *bearer_properties_dictionary;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_SIMPLE_CONNECT_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->pin)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PIN,
                               g_variant_new_string (self->priv->pin));

    if (self->priv->operator_id)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_OPERATOR_ID,
                               g_variant_new_string (self->priv->operator_id));

    if (self->priv->bands)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_BANDS,
                               mm_common_bands_array_to_variant (self->priv->bands,
                                                                 self->priv->n_bands));

    if (self->priv->allowed_modes_set) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALLOWED_MODES,
                               g_variant_new_uint32 (self->priv->allowed_modes));
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_PREFERRED_MODE,
                               g_variant_new_uint32 (self->priv->preferred_mode));
    }

    /* Merge dictionaries */
    bearer_properties_dictionary = mm_bearer_properties_get_dictionary (self->priv->bearer_properties);
    g_variant_iter_init (&iter, bearer_properties_dictionary);
    while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               key,
                               value);
        g_variant_unref (value);
        g_free (key);
    }
    g_variant_unref (bearer_properties_dictionary);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

typedef struct {
    MMSimpleConnectProperties *self;
    GError *error;
    gchar *allowed_modes_str;
    gchar *preferred_mode_str;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar *key,
                   const gchar *value,
                   ParseKeyValueContext *ctx)
{
    /* First, check if we can consume this as bearer properties */
    if (mm_bearer_properties_consume_string (ctx->self->priv->bearer_properties,
                                             key, value,
                                             NULL))
        return TRUE;

    if (g_str_equal (key, PROPERTY_PIN))
        mm_simple_connect_properties_set_pin (ctx->self, value);
    else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
        mm_simple_connect_properties_set_operator_id (ctx->self, value);
    else if (g_str_equal (key, PROPERTY_BANDS)) {
        MMModemBand *bands = NULL;
        guint n_bands = 0;

        mm_common_get_bands_from_string (value, &bands, &n_bands, &ctx->error);
        if (!ctx->error) {
            mm_simple_connect_properties_set_bands (ctx->self, bands, n_bands);
            g_free (bands);
        }
    } else if (g_str_equal (key, PROPERTY_ALLOWED_MODES)) {
        ctx->allowed_modes_str = g_strdup (value);
    } else if (g_str_equal (key, PROPERTY_PREFERRED_MODE)) {
        ctx->preferred_mode_str = g_strdup (value);
    } else {
        ctx->error = g_error_new (MM_CORE_ERROR,
                                  MM_CORE_ERROR_INVALID_ARGS,
                                  "Invalid properties string, unexpected key '%s'",
                                  key);
    }

    return !ctx->error;
}

MMSimpleConnectProperties *
mm_simple_connect_properties_new_from_string (const gchar *str,
                                              GError **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.allowed_modes_str = NULL;
    ctx.preferred_mode_str = NULL;
    ctx.self = mm_simple_connect_properties_new ();

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);

    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_object_unref (ctx.self);
        ctx.self = NULL;
    }
    else if (ctx.allowed_modes_str || ctx.preferred_mode_str) {
        MMModemMode allowed_modes;
        MMModemMode preferred_mode;

        allowed_modes = (ctx.allowed_modes_str ?
                         mm_common_get_modes_from_string (ctx.allowed_modes_str,
                                                          &ctx.error) :
                         MM_MODEM_MODE_ANY);
        if (!ctx.error) {
            preferred_mode = (ctx.preferred_mode_str ?
                              mm_common_get_modes_from_string (ctx.preferred_mode_str,
                                                               &ctx.error) :
                              MM_MODEM_MODE_NONE);
        }

        if (ctx.error) {
            g_propagate_error (error, ctx.error);
            g_object_unref (ctx.self);
            ctx.self = NULL;
        } else {
            mm_simple_connect_properties_set_allowed_modes (
                ctx.self,
                allowed_modes,
                preferred_mode);
        }
    }

    g_free (ctx.allowed_modes_str);
    g_free (ctx.preferred_mode_str);

    return ctx.self;
}

/*****************************************************************************/

MMSimpleConnectProperties *
mm_simple_connect_properties_new_from_dictionary (GVariant *dictionary,
                                                  GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMSimpleConnectProperties *self;
    GVariant *allowed_modes_variant = NULL;
    GVariant *preferred_mode_variant = NULL;

    self = mm_simple_connect_properties_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Simple Connect properties from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {

        /* First, check if we can consume this as bearer properties */
        if (!mm_bearer_properties_consume_variant (self->priv->bearer_properties,
                                                   key, value,
                                                   NULL)) {
            if (g_str_equal (key, PROPERTY_PIN))
                mm_simple_connect_properties_set_pin (
                    self,
                    g_variant_get_string (value, NULL));
            else if (g_str_equal (key, PROPERTY_OPERATOR_ID))
                mm_simple_connect_properties_set_operator_id (
                    self,
                    g_variant_get_string (value, NULL));
            else if (g_str_equal (key, PROPERTY_BANDS)) {
                GArray *array;

                array = mm_common_bands_variant_to_garray (value);
                mm_simple_connect_properties_set_bands (
                    self,
                    (MMModemBand *)array->data,
                    array->len);
                g_array_unref (array);
            } else if (g_str_equal (key, PROPERTY_ALLOWED_MODES))
                allowed_modes_variant = g_variant_ref (value);
            else if (g_str_equal (key, PROPERTY_PREFERRED_MODE))
                preferred_mode_variant = g_variant_ref (value);
            else {
                /* Set inner error, will stop the loop */
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Invalid properties dictionary, unexpected key '%s'",
                                           key);
            }
        }

        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        self = NULL;
    }
    /* If we got allowed modes variant, check if we got preferred mode */
    else if (allowed_modes_variant) {
        mm_simple_connect_properties_set_allowed_modes (
            self,
            g_variant_get_uint32 (allowed_modes_variant),
            (preferred_mode_variant ?
             g_variant_get_uint32 (preferred_mode_variant) :
             MM_MODEM_MODE_NONE));
    }
    /* If we only got preferred mode, assume allowed is ANY */
    else if (preferred_mode_variant) {
        mm_simple_connect_properties_set_allowed_modes (
            self,
            MM_MODEM_MODE_ANY,
            g_variant_get_uint32 (preferred_mode_variant));
    }

    /* Cleanup last things before exiting */
    if (allowed_modes_variant)
        g_variant_unref (allowed_modes_variant);
    if (preferred_mode_variant)
        g_variant_unref (preferred_mode_variant);

    return self;
}

/*****************************************************************************/

/**
 * mm_simple_connect_properties_new:
 *
 * Creates a new empty #MMSimpleConnectProperties.
 *
 * Returns: (transfer full): a #MMSimpleConnectProperties. The returned value should be freed with g_object_unref().
 */
MMSimpleConnectProperties *
mm_simple_connect_properties_new (void)
{
    return (MM_SIMPLE_CONNECT_PROPERTIES (
                g_object_new (MM_TYPE_SIMPLE_CONNECT_PROPERTIES, NULL)));
}

static void
mm_simple_connect_properties_init (MMSimpleConnectProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SIMPLE_CONNECT_PROPERTIES,
                                              MMSimpleConnectPropertiesPrivate);

    /* Some defaults */
    self->priv->bearer_properties = mm_bearer_properties_new ();
    self->priv->allowed_modes = MM_MODEM_MODE_ANY;
    self->priv->preferred_mode = MM_MODEM_MODE_NONE;
    self->priv->bands = g_new (MMModemBand, 1);
    self->priv->bands[0] = MM_MODEM_BAND_UNKNOWN;
    self->priv->n_bands = 1;
}

static void
finalize (GObject *object)
{
    MMSimpleConnectProperties *self = MM_SIMPLE_CONNECT_PROPERTIES (object);

    g_free (self->priv->pin);
    g_free (self->priv->operator_id);
    g_free (self->priv->bands);
    g_object_unref (self->priv->bearer_properties);

    G_OBJECT_CLASS (mm_simple_connect_properties_parent_class)->finalize (object);
}

static void
mm_simple_connect_properties_class_init (MMSimpleConnectPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimpleConnectPropertiesPrivate));

    object_class->finalize = finalize;
}
