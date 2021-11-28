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
 */

#include <string.h>

#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-nr5g-registration-settings.h"

/**
 * SECTION: mm-nr5g-registration-settings
 * @title: MMNr5gRegistrationSettings
 * @short_description: Helper object to handle 5GNR registration settings.
 *
 * The #MMNr5gRegistrationSettings is an object handling the settings used
 * to configure the 5G registration process.
 */

G_DEFINE_TYPE (MMNr5gRegistrationSettings, mm_nr5g_registration_settings, G_TYPE_OBJECT)

#define PROPERTY_MICO_MODE "mico-mode"
#define PROPERTY_DRX_CYCLE "drx-cycle"

struct _MMNr5gRegistrationSettingsPrivate {
    MMModem3gppMicoMode mico_mode;
    MMModem3gppDrxCycle drx_cycle;
};

/*****************************************************************************/

/**
 * mm_nr5g_registration_settings_set_mico_mode:
 * @self: a #MMNr5gRegistrationSettings.
 * @mico_mode: a #MMModem3gppMicoMode.
 *
 * Sets the MICO mode configuration.
 *
 * Since: 1.20
 */
void
mm_nr5g_registration_settings_set_mico_mode (MMNr5gRegistrationSettings *self,
                                             MMModem3gppMicoMode         mico_mode)
{
    g_return_if_fail (MM_IS_NR5G_REGISTRATION_SETTINGS (self));

    self->priv->mico_mode = mico_mode;
}

/**
 * mm_nr5g_registration_settings_get_mico_mode:
 * @self: a #MMNr5gRegistrationSettings.
 *
 * Gets the MICO mode configuration.
 *
 * Returns: a #MMModem3gppMicoMode.
 *
 * Since: 1.20
 */
MMModem3gppMicoMode
mm_nr5g_registration_settings_get_mico_mode (MMNr5gRegistrationSettings *self)
{
    g_return_val_if_fail (MM_IS_NR5G_REGISTRATION_SETTINGS (self), MM_MODEM_3GPP_MICO_MODE_UNKNOWN);

    return self->priv->mico_mode;
}

/*****************************************************************************/

/**
 * mm_nr5g_registration_settings_set_drx_cycle:
 * @self: a #MMNr5gRegistrationSettings.
 * @drx_cycle: a #MMModem3gppDrxCycle.
 *
 * Sets the MICO mode configuration.
 *
 * Since: 1.20
 */
void
mm_nr5g_registration_settings_set_drx_cycle (MMNr5gRegistrationSettings *self,
                                             MMModem3gppDrxCycle         drx_cycle)
{
    g_return_if_fail (MM_IS_NR5G_REGISTRATION_SETTINGS (self));

    self->priv->drx_cycle = drx_cycle;
}

/**
 * mm_nr5g_registration_settings_get_drx_cycle:
 * @self: a #MMNr5gRegistrationSettings.
 *
 * Gets the MICO mode configuration.
 *
 * Returns: a #MMModem3gppDrxCycle.
 *
 * Since: 1.20
 */
MMModem3gppDrxCycle
mm_nr5g_registration_settings_get_drx_cycle (MMNr5gRegistrationSettings *self)
{
    g_return_val_if_fail (MM_IS_NR5G_REGISTRATION_SETTINGS (self), MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN);

    return self->priv->drx_cycle;
}

/*****************************************************************************/

static gboolean
consume_string (MMNr5gRegistrationSettings  *self,
                const gchar                 *key,
                const gchar                 *value,
                GError                     **error)
{
    GError *inner_error = NULL;

    if (g_str_equal (key, PROPERTY_MICO_MODE)) {
        MMModem3gppMicoMode mico_mode;

        mico_mode = mm_common_get_3gpp_mico_mode_from_string (value, &inner_error);
        if (!inner_error)
            mm_nr5g_registration_settings_set_mico_mode (self, mico_mode);
    } else if (g_str_equal (key, PROPERTY_DRX_CYCLE)) {
        MMModem3gppDrxCycle drx_cycle;

        drx_cycle = mm_common_get_3gpp_drx_cycle_from_string (value, &inner_error);
        if (!inner_error)
            mm_nr5g_registration_settings_set_drx_cycle (self, drx_cycle);
    } else {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                   "Invalid properties string, unsupported key '%s'", key);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

typedef struct {
    MMNr5gRegistrationSettings *settings;
    GError                     *error;
} ParseKeyValueContext;

static gboolean
key_value_foreach (const gchar          *key,
                   const gchar          *value,
                   ParseKeyValueContext *ctx)
{
    return consume_string (ctx->settings, key, value, &ctx->error);
}

/**
 * mm_nr5g_registration_settings_new_from_string: (skip)
 */
MMNr5gRegistrationSettings *
mm_nr5g_registration_settings_new_from_string (const gchar  *str,
                                               GError      **error)
{
    ParseKeyValueContext ctx;

    ctx.error = NULL;
    ctx.settings = mm_nr5g_registration_settings_new ();

    mm_common_parse_key_value_string (str,
                                      &ctx.error,
                                      (MMParseKeyValueForeachFn)key_value_foreach,
                                      &ctx);

    /* If error, destroy the object */
    if (ctx.error) {
        g_propagate_error (error, ctx.error);
        g_clear_object (&ctx.settings);
    }

    return ctx.settings;
}

/*****************************************************************************/

/**
 * mm_nr5g_registration_settings_get_dictionary: (skip)
 */
GVariant *
mm_nr5g_registration_settings_get_dictionary (MMNr5gRegistrationSettings *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_NR5G_REGISTRATION_SETTINGS (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    if (self->priv->mico_mode != MM_MODEM_3GPP_MICO_MODE_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_MICO_MODE,
                               g_variant_new_uint32 (self->priv->mico_mode));
    if (self->priv->drx_cycle != MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_DRX_CYCLE,
                               g_variant_new_uint32 (self->priv->drx_cycle));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

static gboolean
consume_variant (MMNr5gRegistrationSettings  *self,
                 const gchar                 *key,
                 GVariant                    *value,
                 GError                     **error)
{
    if (g_str_equal (key, PROPERTY_MICO_MODE))
        self->priv->mico_mode = g_variant_get_uint32 (value);
    else if (g_str_equal (key, PROPERTY_DRX_CYCLE))
        self->priv->drx_cycle = g_variant_get_uint32 (value);
    else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid settings dictionary, unexpected key '%s'", key);
        return FALSE;
    }
    return TRUE;
}

/**
 * mm_nr5g_registration_settings_new_from_dictionary: (skip)
 */
MMNr5gRegistrationSettings *
mm_nr5g_registration_settings_new_from_dictionary (GVariant  *dictionary,
                                                   GError   **error)
{
    g_autoptr(MMNr5gRegistrationSettings) self = NULL;
    GVariantIter  iter;
    gchar        *key;
    GVariant     *value;
    GError       *inner_error = NULL;

    self = mm_nr5g_registration_settings_new ();
    if (!dictionary)
        return g_steal_pointer (&self);

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid input type");
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error && g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        consume_variant (self, key, value, &inner_error);
        g_free (key);
        g_variant_unref (value);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    return g_steal_pointer (&self);
}

/*****************************************************************************/

/**
 * mm_nr5g_registration_settings_cmp: (skip)
 */
gboolean
mm_nr5g_registration_settings_cmp (MMNr5gRegistrationSettings *a,
                                   MMNr5gRegistrationSettings *b)
{
    if (a->priv->mico_mode != b->priv->mico_mode)
        return FALSE;
    if (a->priv->drx_cycle != b->priv->drx_cycle)
        return FALSE;
    return TRUE;
}

/*****************************************************************************/

/**
 * mm_nr5g_registration_settings_new:
 *
 * Creates a new empty #MMNr5gRegistrationSettings.
 *
 * Returns: (transfer full): a #MMNr5gRegistrationSettings. The returned value should be freed with g_object_unref().
 *
 * Since: 1.20
 */
MMNr5gRegistrationSettings *
mm_nr5g_registration_settings_new (void)
{
    return MM_NR5G_REGISTRATION_SETTINGS (g_object_new (MM_TYPE_NR5G_REGISTRATION_SETTINGS, NULL));
}

static void
mm_nr5g_registration_settings_init (MMNr5gRegistrationSettings *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_NR5G_REGISTRATION_SETTINGS, MMNr5gRegistrationSettingsPrivate);
    self->priv->mico_mode = MM_MODEM_3GPP_MICO_MODE_UNKNOWN;
    self->priv->drx_cycle = MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN;
}

static void
mm_nr5g_registration_settings_class_init (MMNr5gRegistrationSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMNr5gRegistrationSettingsPrivate));
}
