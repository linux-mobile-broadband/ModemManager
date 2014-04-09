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
 * Copyright (C) 2012 Google Inc.
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-cdma-manual-activation-properties.h"

/**
 * SECTION: mm-cdma-manual-activation-properties
 * @title: MMCdmaManualActivationProperties
 * @short_description: Helper object to handle manual CDMA activation properties.
 *
 * The #MMCdmaManualActivationProperties is an object handling the properties
 * required during a manual CDMA activation request.
 */

G_DEFINE_TYPE (MMCdmaManualActivationProperties, mm_cdma_manual_activation_properties, G_TYPE_OBJECT)

#define PROPERTY_SPC        "spc"
#define PROPERTY_SID        "sid"
#define PROPERTY_MDN        "mdn"
#define PROPERTY_MIN        "min"
#define PROPERTY_MN_HA_KEY  "mn-ha-key"
#define PROPERTY_MN_AAA_KEY "mn-aaa-key"
#define PROPERTY_PRL        "prl"

struct _MMCdmaManualActivationPropertiesPrivate {
    /* Mandatory parameters */
    gchar *spc;
    guint16 sid;
    gboolean sid_set;
    gchar *mdn;
    gchar *min;
    /* Optional */
    gchar *mn_ha_key;
    gchar *mn_aaa_key;
    GByteArray *prl;
};

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_spc:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the Service Programming Code.
 *
 * Returns: (transfer none): The SPC. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.2
 */
const gchar *
mm_cdma_manual_activation_properties_get_spc (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->spc;
}

/* SPC is a 6-digit string */
static gboolean
validate_spc (const gchar *spc,
              GError **error)
{
    guint i;

    if (strlen (spc) != 6) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "SPC must be exactly 6-digit long");
        return FALSE;
    }

    for (i = 0; i < 6; i ++) {
        if (!g_ascii_isdigit (spc[i])) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "SPC must not contain non-digit characters");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_spc:
 * @self: A #MMCdmaManualActivationProperties.
 * @spc: The SPC string, exactly 6 digits.
 * @error: Return location for error or %NULL.
 *
 * Sets the Service Programming Code.
 *
 * Returns: %TRUE if the SPC was successfully set, or %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_spc (MMCdmaManualActivationProperties *self,
                                              const gchar *spc,
                                              GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_spc (spc, error))
        return FALSE;

    g_free (self->priv->spc);
    self->priv->spc = g_strdup (spc);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_sid:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the System Identification Number.
 *
 * Returns: The SID.
 *
 * Since: 1.2
 */
guint16
mm_cdma_manual_activation_properties_get_sid (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), 0);

    return self->priv->sid;
}

/**
 * mm_cdma_manual_activation_properties_set_sid:
 * @self: A #MMCdmaManualActivationProperties.
 * @sid: The SID.
 *
 * Sets the Service Identification Number.
 *
 * Since: 1.2
 */
void
mm_cdma_manual_activation_properties_set_sid (MMCdmaManualActivationProperties *self,
                                              guint16 sid)
{
    g_return_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self));

    self->priv->sid_set = TRUE;
    self->priv->sid = sid;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_mdn:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the Mobile Directory Number.
 *
 * Returns: (transfer none): The MDN. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.2
 */
const gchar *
mm_cdma_manual_activation_properties_get_mdn (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->mdn;
}

/* MDN is max 15 characters */
static gboolean
validate_mdn (const gchar *mdn,
              GError **error)
{
    if (strlen (mdn) > 15) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "MDN must be maximum 15 characters long");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_mdn:
 * @self: A #MMCdmaManualActivationProperties.
 * @mdn: The MDN string, maximum 15 characters.
 * @error: Return location for error or %NULL.
 *
 * Sets the Mobile Directory Number.
 *
 * Returns: %TRUE if the MDN was successfully set, or %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_mdn (MMCdmaManualActivationProperties *self,
                                              const gchar *mdn,
                                              GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_mdn (mdn, error))
        return FALSE;

    g_free (self->priv->mdn);
    self->priv->mdn = g_strdup (mdn);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_min:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the Mobile Indentification Number.
 *
 * Returns: (transfer none): The MIN. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.2
 */
const gchar *
mm_cdma_manual_activation_properties_get_min (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->min;
}

/* MIN is max 15 characters */
static gboolean
validate_min (const gchar *min,
              GError **error)
{
    if (strlen (min) > 15) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "MIN must be maximum 15 characters long");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_min:
 * @self: A #MMCdmaManualActivationProperties.
 * @min: The MIN string, maximum 15 characters.
 * @error: Return location for error or %NULL.
 *
 * Sets the Mobile Identification Number.
 *
 * Returns: %TRUE if the MIN was successfully set, or %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_min (MMCdmaManualActivationProperties *self,
                                              const gchar *min,
                                              GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_min (min, error))
        return FALSE;

    g_free (self->priv->min);
    self->priv->min = g_strdup (min);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_mn_ha_key:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the MN-HA key.
 *
 * Returns: (transfer none): The MN-HA key. Do not free the returned value, it
 * is owned by @self.
 *
 * Since: 1.2
 */
const gchar *
mm_cdma_manual_activation_properties_get_mn_ha_key (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->mn_ha_key;
}

/* MN-HA key is max 16 characters */
static gboolean
validate_mn_ha_key (const gchar *mn_ha_key,
                    GError **error)
{
    if (strlen (mn_ha_key) > 16) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "MN-HA key must be maximum 16 characters long");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_mn_ha_key:
 * @self: A #MMCdmaManualActivationProperties.
 * @mn_ha_key: The MN-HA key string, maximum 16 characters.
 * @error: Return location for error or %NULL.
 *
 * Sets the Mobile Identification Number.
 *
 * Returns: %TRUE if the MN-HA key was successfully set, or %FALSE if @error
 * is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_mn_ha_key (MMCdmaManualActivationProperties *self,
                                                    const gchar *mn_ha_key,
                                                    GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_mn_ha_key (mn_ha_key, error))
        return FALSE;

    g_free (self->priv->mn_ha_key);
    self->priv->mn_ha_key = g_strdup (mn_ha_key);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_mn_aaa_key:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the MN-AAA key.
 *
 * Returns: (transfer none): The MN-AAA key. Do not free the returned value, it
 * is owned by @self.
 *
 * Since: 1.2
 */
const gchar *
mm_cdma_manual_activation_properties_get_mn_aaa_key (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->mn_aaa_key;
}

/* MN-AAA key is max 16 characters */
static gboolean
validate_mn_aaa_key (const gchar *mn_aaa_key,
                    GError **error)
{
    if (strlen (mn_aaa_key) > 16) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "MN-AAA key must be maximum 16 characters long");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_mn_aaa_key:
 * @self: A #MMCdmaManualActivationProperties.
 * @mn_aaa_key: The MN-AAA key string, maximum 16 characters.
 * @error: Return location for error or %NULL.
 *
 * Sets the Mobile Identification Number.
 *
 * Returns: %TRUE if the MN-AAA key was successfully set, or %FALSE if @error is
 * set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_mn_aaa_key (MMCdmaManualActivationProperties *self,
                                                     const gchar *mn_aaa_key,
                                                     GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_mn_aaa_key (mn_aaa_key, error))
        return FALSE;

    g_free (self->priv->mn_aaa_key);
    self->priv->mn_aaa_key = g_strdup (mn_aaa_key);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_prl:
 * @self: A #MMCdmaManualActivationProperties.
 * @prl_len: (out): Size of the returned PRL.
 *
 * Gets the Preferred Roaming List.
 *
 * Returns: (transfer none): The PRL. Do not free the returned value, it is
 * owned by @self.
 *
 * Since: 1.2
 */
const guint8 *
mm_cdma_manual_activation_properties_get_prl (MMCdmaManualActivationProperties *self,
                                              gsize *prl_len)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    if (prl_len)
        *prl_len = (self->priv->prl ? self->priv->prl->len : 0);

    return (self->priv->prl ? self->priv->prl->data : NULL);
}

/**
 * mm_cdma_manual_activation_properties_peek_prl_bytearray:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the Preferred Roaming List.
 *
 * Returns: (transfer none): A #GByteArray with the PRL, or %NULL if it doesn't
 * contain any. Do not free the returned value, it is owned by @self.
 *
 * Since: 1.2
 */
GByteArray *
mm_cdma_manual_activation_properties_peek_prl_bytearray (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return self->priv->prl;
}

/**
 * mm_cdma_manual_activation_properties_get_prl_bytearray:
 * @self: A #MMCdmaManualActivationProperties.
 *
 * Gets the Preferred Roaming List.
 *
 * Returns: (transfer full): A #GByteArray with the PRL, or %NULL if it doesn't
 * contain any. The returned value should be freed with g_byte_array_unref().
 *
 * Since: 1.2
 */
GByteArray *
mm_cdma_manual_activation_properties_get_prl_bytearray (MMCdmaManualActivationProperties *self)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    return (self->priv->prl ? g_byte_array_ref (self->priv->prl) : NULL);
}

static gboolean
validate_prl (const guint8 *prl,
              gsize prl_size,
              GError **error)
{
    if (prl_size > 16384) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "PRL must be maximum 16384 bytes long");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_prl:
 * @self: A #MMCdmaManualActivationProperties.
 * @prl: The PRL.
 * @prl_length: Length of @prl.
 * @error: Return location for error or %NULL.
 *
 * Sets the Preferred Roaming List.
 *
 * Returns: %TRUE if the PRL was successfully set, or %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_prl (MMCdmaManualActivationProperties *self,
                                              const guint8 *prl,
                                              gsize prl_length,
                                              GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_prl (prl, prl_length, error))
        return FALSE;

    if (self->priv->prl)
        g_byte_array_unref (self->priv->prl);

    if (prl && prl_length)
        self->priv->prl = g_byte_array_append (g_byte_array_sized_new (prl_length),
                                               prl,
                                               prl_length);
    else
        self->priv->prl = NULL;
    return TRUE;
}

/**
 * mm_cdma_manual_activation_properties_set_prl_bytearray:
 * @self: A #MMCdmaManualActivationProperties.
 * @prl: A #GByteArray with the PRL to set. This method takes a new reference
 *  of @prl.
 * @error: Return location for error or %NULL.
 *
 * Sets the Preferred Roaming List.
 *
 * Returns: %TRUE if the PRL was successfully set, or %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_cdma_manual_activation_properties_set_prl_bytearray (MMCdmaManualActivationProperties *self,
                                                        GByteArray *prl,
                                                        GError **error)
{
    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), FALSE);

    if (!validate_prl (prl->data, prl->len, error))
        return FALSE;

    if (self->priv->prl)
        g_byte_array_unref (self->priv->prl);

    self->priv->prl = (prl ? g_byte_array_ref (prl) : NULL);
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_cdma_manual_activation_properties_get_dictionary: (skip)
 */
GVariant *
mm_cdma_manual_activation_properties_get_dictionary (MMCdmaManualActivationProperties *self)
{
    GVariantBuilder builder;

    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_CDMA_MANUAL_ACTIVATION_PROPERTIES (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->spc)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_SPC,
                               g_variant_new_string (self->priv->spc));
    if (self->priv->sid_set)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_SID,
                               g_variant_new_uint16 (self->priv->sid));
    if (self->priv->mdn)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MDN,
                               g_variant_new_string (self->priv->mdn));
    if (self->priv->min)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MIN,
                               g_variant_new_string (self->priv->min));
    if (self->priv->mn_ha_key)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MN_HA_KEY,
                               g_variant_new_string (self->priv->mn_ha_key));
    if (self->priv->mn_aaa_key)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MN_AAA_KEY,
                               g_variant_new_string (self->priv->mn_aaa_key));
    if (self->priv->prl)
        g_variant_builder_add (
            &builder,
            "{sv}",
            PROPERTY_PRL,
            g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
                                     self->priv->prl->data,
                                     self->priv->prl->len * sizeof (guint8),
                                     TRUE,
                                     NULL,
                                     NULL));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_variant (MMCdmaManualActivationProperties *self,
                 const gchar *key,
                 GVariant *value,
                 GError **error)
{
    if (g_str_equal (key, PROPERTY_SPC))
        return (mm_cdma_manual_activation_properties_set_spc (
                    self,
                    g_variant_get_string (value, NULL),
                    error));

    if (g_str_equal (key, PROPERTY_SID)) {
        mm_cdma_manual_activation_properties_set_sid (
            self,
            g_variant_get_uint16 (value));
        return TRUE;
    }

    if (g_str_equal (key, PROPERTY_MDN))
        return (mm_cdma_manual_activation_properties_set_mdn (
                    self,
                    g_variant_get_string (value, NULL),
                    error));

    if (g_str_equal (key, PROPERTY_MIN))
        return (mm_cdma_manual_activation_properties_set_min (
                    self,
                    g_variant_get_string (value, NULL),
                    error));

    if (g_str_equal (key, PROPERTY_MN_HA_KEY))
        return (mm_cdma_manual_activation_properties_set_mn_ha_key (
                    self,
                    g_variant_get_string (value, NULL),
                    error));

    if (g_str_equal (key, PROPERTY_MN_AAA_KEY))
        return (mm_cdma_manual_activation_properties_set_mn_aaa_key (
                    self,
                    g_variant_get_string (value, NULL),
                    error));

    if (g_str_equal (key, PROPERTY_PRL)) {
        const guint8 *prl;
        gsize prl_len = 0;

        prl = g_variant_get_fixed_array (value, &prl_len, sizeof (guint8));
        return (mm_cdma_manual_activation_properties_set_prl (
                    self,
                    prl,
                    prl_len,
                    error));
    }

    /* Set error */
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Invalid properties dictionary, unexpected key '%s'",
                 key);
    return FALSE;
}

/**
 * mm_cdma_manual_activation_properties_new_from_dictionary: (skip)
 */
MMCdmaManualActivationProperties *
mm_cdma_manual_activation_properties_new_from_dictionary (GVariant *dictionary,
                                                          GError **error)
{
    GError *inner_error = NULL;
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    MMCdmaManualActivationProperties *self;

    if (!dictionary) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create CDMA manual activation properties from empty dictionary");
        return NULL;
    }

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create CDMA manual activation properties from dictionary: "
                     "invalid variant type received");
        return NULL;
    }

    self = mm_cdma_manual_activation_properties_new ();

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (self,
                         key,
                         value,
                         &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_object_unref (self);
        return NULL;
    }

    /* If mandatory properties missing, destroy the object */
    if (!self->priv->spc ||
        !self->priv->sid_set ||
        !self->priv->mdn ||
        !self->priv->min) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create CDMA manual activation properties from dictionary: "
                     "mandatory parameter missing");
        g_object_unref (self);
        return NULL;
    }

    return self;
}

/*****************************************************************************/

static gboolean
consume_string (MMCdmaManualActivationProperties *self,
                const gchar *key,
                const gchar *value,
                GError **error)
{
    if (g_str_equal (key, PROPERTY_SPC))
        return mm_cdma_manual_activation_properties_set_spc (self, value, error);

    if (g_str_equal (key, PROPERTY_SID)) {
        guint sid;

        if (!mm_get_uint_from_str (value, &sid)) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Invalid SID integer value: '%s'",
                         value);
            return FALSE;
        }

        mm_cdma_manual_activation_properties_set_sid (self, sid);
        return TRUE;
    }

    if (g_str_equal (key, PROPERTY_MDN))
        return mm_cdma_manual_activation_properties_set_mdn (self, value, error);

    if (g_str_equal (key, PROPERTY_MIN))
        return mm_cdma_manual_activation_properties_set_min (self, value, error);

    if (g_str_equal (key, PROPERTY_MN_HA_KEY))
        return mm_cdma_manual_activation_properties_set_mn_ha_key (self, value, error);

    if (g_str_equal (key, PROPERTY_MN_AAA_KEY))
        return mm_cdma_manual_activation_properties_set_mn_aaa_key (self, value, error);

    if (g_str_equal (key, PROPERTY_PRL)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid properties string, key '%s' cannot be given in a string",
                     key);
        return FALSE;
    }

    /* Set error */
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Invalid properties dictionary, unexpected key '%s'",
                 key);
    return FALSE;
}

typedef struct {
    MMCdmaManualActivationProperties *properties;
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

/**
 * mm_cdma_manual_activation_properties_new_from_string: (skip)
 */
MMCdmaManualActivationProperties *
mm_cdma_manual_activation_properties_new_from_string (const gchar *str,
                                                      GError **error)
{
    ParseKeyValueContext ctx;

    ctx.properties = mm_cdma_manual_activation_properties_new ();
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

/**
 * mm_cdma_manual_activation_properties_new:
 *
 * Creates a new #MMCdmaManualActivationProperties object.
 *
 * Returns: (transfer full): A #MMCdmaManualActivationProperties. The returned
 * value should be freed with g_object_unref().
 *
 * Since: 1.2
 */
MMCdmaManualActivationProperties *
mm_cdma_manual_activation_properties_new (void)
{
    return (MMCdmaManualActivationProperties *) g_object_new (MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES, NULL);
}

static void
mm_cdma_manual_activation_properties_init (MMCdmaManualActivationProperties *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_CDMA_MANUAL_ACTIVATION_PROPERTIES,
                                              MMCdmaManualActivationPropertiesPrivate);
}

static void
finalize (GObject *object)
{
    MMCdmaManualActivationProperties *self = MM_CDMA_MANUAL_ACTIVATION_PROPERTIES (object);

    g_free (self->priv->spc);
    g_free (self->priv->mdn);
    g_free (self->priv->min);
    g_free (self->priv->mn_ha_key);
    g_free (self->priv->mn_aaa_key);
    if (self->priv->prl)
        g_byte_array_unref (self->priv->prl);

    G_OBJECT_CLASS (mm_cdma_manual_activation_properties_parent_class)->finalize (object);
}

static void
mm_cdma_manual_activation_properties_class_init (MMCdmaManualActivationPropertiesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCdmaManualActivationPropertiesPrivate));

    object_class->finalize = finalize;
}
