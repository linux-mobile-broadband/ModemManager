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
 * Copyright (C) 2011 Google, Inc.
 */

#include <string.h>

#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"
#include "mm-simple-status.h"
#include "mm-modem-cdma.h"

/**
 * SECTION: mm-simple-status
 * @title: MMSimpleStatus
 * @short_description: Helper object to handle overall modem status.
 *
 * The #MMSimpleStatus is an object handling the general modem status properties,
 * available in the Simple interface.
 *
 * This object is retrieved with either mm_modem_simple_get_status() or
 * mm_modem_simple_get_status_sync().
 */

G_DEFINE_TYPE (MMSimpleStatus, mm_simple_status, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_STATE,
    PROP_SIGNAL_QUALITY,
    PROP_CURRENT_BANDS,
    PROP_ACCESS_TECHNOLOGIES,
    PROP_3GPP_REGISTRATION_STATE,
    PROP_3GPP_OPERATOR_CODE,
    PROP_3GPP_OPERATOR_NAME,
    PROP_3GPP_SUBSCRIPTION_STATE,
    PROP_CDMA_CDMA1X_REGISTRATION_STATE,
    PROP_CDMA_EVDO_REGISTRATION_STATE,
    PROP_CDMA_SID,
    PROP_CDMA_NID,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMSimpleStatusPrivate {
    /* <--- From the Modem interface ---> */
    /* Overall modem state, signature 'u' */
    MMModemState state;
    /* Signal quality, given only when registered, signature '(ub)' */
    GVariant *signal_quality;
    /* List of bands, given only when registered, signature: au */
    GVariant *current_bands;
    GArray *current_bands_array;
    /* Access technologies, given only when registered, signature: u */
    MMModemAccessTechnology access_technologies;

    /* <--- From the Modem 3GPP interface ---> */
    /* 3GPP registration state, signature 'u' */
    MMModem3gppRegistrationState modem_3gpp_registration_state;
    /* 3GPP operator code, given only when registered, signature 's' */
    gchar *modem_3gpp_operator_code;
    /* 3GPP operator name, given only when registered, signature 's' */
    gchar *modem_3gpp_operator_name;

    /* <--- From the Modem CDMA interface ---> */
    /* CDMA/CDMA1x registration state, signature 'u' */
    MMModemCdmaRegistrationState modem_cdma_cdma1x_registration_state;
    /* CDMA/EV-DO registration state, signature 'u' */
    MMModemCdmaRegistrationState modem_cdma_evdo_registration_state;
    /* SID, signature 'u' */
    guint modem_cdma_sid;
    /* NID, signature 'u' */
    guint modem_cdma_nid;
};

/*****************************************************************************/

/**
 * mm_simple_status_get_state:
 * @self: a #MMSimpleStatus.
 *
 * Gets the state of the modem.
 *
 * Returns: a #MMModemState.
 *
 * Since: 1.0
 */
MMModemState
mm_simple_status_get_state (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_STATE_UNKNOWN);

    return self->priv->state;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_signal_quality:
 * @self: a #MMSimpleStatus.
 * @recent: (out) (allow-none): indication of whether the given signal quality
 *  is considered recent.
 *
 * Gets the signal quality.
 *
 * Returns: the signal quality.
 *
 * Since: 1.0
 */
guint32
mm_simple_status_get_signal_quality (MMSimpleStatus *self,
                                     gboolean *recent)
{
    guint32 signal_quality = 0;
    gboolean signal_quality_recent = FALSE;

    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), 0);

    if (self->priv->signal_quality) {
        g_variant_get (self->priv->signal_quality,
                       "(ub)",
                       &signal_quality,
                       &signal_quality_recent);
    }

    if (recent)
        *recent = signal_quality_recent;
    return signal_quality;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_current_bands:
 * @self: a #MMSimpleStatus.
 * @bands: (out): location for an array of #MMModemBand values. Do not free the
 *  returned value, it is owned by @self.
 * @n_bands: (out): number of elements in @bands.
 *
 * Gets the currently used frequency bands.
 *
 * Since: 1.0
 */
void
mm_simple_status_get_current_bands (MMSimpleStatus *self,
                                    const MMModemBand **bands,
                                    guint *n_bands)
{
    g_return_if_fail (MM_IS_SIMPLE_STATUS (self));

    if (!self->priv->current_bands_array)
        self->priv->current_bands_array = mm_common_bands_variant_to_garray (self->priv->current_bands);

    *n_bands = self->priv->current_bands_array->len;
    *bands = (const MMModemBand *)self->priv->current_bands_array->data;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_access_technologies:
 * @self: a #MMSimpleStatus.
 *
 * Gets the currently used access technologies.
 *
 * Returns: a bitmask of #MMModemAccessTechnology values.
 *
 * Since: 1.0
 */
MMModemAccessTechnology
mm_simple_status_get_access_technologies (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return self->priv->access_technologies;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_3gpp_registration_state:
 * @self: a #MMSimpleStatus.
 *
 * Gets the current state of the registration in the 3GPP network.
 *
 * Returns: a #MMModem3gppRegistrationState.
 *
 * Since: 1.0
 */
MMModem3gppRegistrationState
mm_simple_status_get_3gpp_registration_state (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN);

    return self->priv->modem_3gpp_registration_state;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_3gpp_operator_code:
 * @self: a #MMSimpleStatus.
 *
 * Gets the MCC/MNC of the operator of the 3GPP network where the modem is
 * registered.
 *
 * Returns: the operator code, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_status_get_3gpp_operator_code (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), NULL);

    return self->priv->modem_3gpp_operator_code;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_3gpp_operator_name:
 * @self: a #MMSimpleStatus.
 *
 * Gets the name of the operator of the 3GPP network where the modem is
 * registered.
 *
 * Returns: the operator name, or %NULL if unknown. Do not free the returned
 * value, it is owned by @self.
 *
 * Since: 1.0
 */
const gchar *
mm_simple_status_get_3gpp_operator_name (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), NULL);

    return self->priv->modem_3gpp_operator_name;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_cdma_cdma1x_registration_state:
 * @self: a #MMSimpleStatus.
 *
 * Gets the current state of the registration in the CDMA-1x network.
 *
 * Returns: a #MMModemCdmaRegistrationState.
 *
 * Since: 1.0
 */
MMModemCdmaRegistrationState
mm_simple_status_get_cdma_cdma1x_registration_state (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return self->priv->modem_cdma_cdma1x_registration_state;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_cdma_evdo_registration_state:
 * @self: a #MMSimpleStatus.
 *
 * Gets the current state of the registration in the EV-DO network.
 *
 * Returns: a #MMModemCdmaRegistrationState.
 *
 * Since: 1.0
 */
MMModemCdmaRegistrationState
mm_simple_status_get_cdma_evdo_registration_state (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return self->priv->modem_cdma_evdo_registration_state;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_cdma_sid:
 * @self: a #MMSimpleStatus.
 *
 * Gets the System Identification number of the CDMA network.
 *
 * Returns: the SID, or %MM_MODEM_CDMA_SID_UNKNOWN if unknown.
 *
 * Since: 1.0
 */
guint
mm_simple_status_get_cdma_sid (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_CDMA_SID_UNKNOWN);

    return self->priv->modem_cdma_sid;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_cdma_nid:
 * @self: a #MMSimpleStatus.
 *
 * Gets the Network Identification number of the CDMA network.
 *
 * Returns: the NID, or %MM_MODEM_CDMA_NID_UNKNOWN if unknown.
 *
 * Since: 1.0
 */
guint
mm_simple_status_get_cdma_nid (MMSimpleStatus *self)
{
    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), MM_MODEM_CDMA_NID_UNKNOWN);

    return self->priv->modem_cdma_nid;
}

/*****************************************************************************/

/**
 * mm_simple_status_get_dictionary: (skip)
 */
GVariant *
mm_simple_status_get_dictionary (MMSimpleStatus *self)
{
    GVariantBuilder builder;

    /* Allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_SIMPLE_STATUS (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder,
                           "{sv}",
                           MM_SIMPLE_PROPERTY_STATE,
                           g_variant_new_uint32 (self->priv->state));

    /* Next ones, only when registered */
    if (self->priv->state >= MM_MODEM_STATE_REGISTERED) {
        g_variant_builder_add (&builder,
                               "{sv}",
                               MM_SIMPLE_PROPERTY_SIGNAL_QUALITY,
                               self->priv->signal_quality);
        g_variant_builder_add (&builder,
                               "{sv}",
                               MM_SIMPLE_PROPERTY_CURRENT_BANDS,
                               self->priv->current_bands);
        g_variant_builder_add (&builder,
                               "{sv}",
                               MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES,
                               g_variant_new_uint32 (self->priv->access_technologies));

        g_variant_builder_add (&builder,
                               "{sv}",
                               MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE,
                               g_variant_new_uint32 (self->priv->modem_3gpp_registration_state));
        if (self->priv->modem_3gpp_operator_code)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE,
                                   g_variant_new_string (self->priv->modem_3gpp_operator_code));
        if (self->priv->modem_3gpp_operator_name)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME,
                                   g_variant_new_string (self->priv->modem_3gpp_operator_name));

        if (self->priv->modem_cdma_cdma1x_registration_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN) {
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   MM_SIMPLE_PROPERTY_CDMA_CDMA1X_REGISTRATION_STATE,
                                   g_variant_new_uint32 (self->priv->modem_cdma_cdma1x_registration_state));
            if (self->priv->modem_cdma_sid  != MM_MODEM_CDMA_SID_UNKNOWN)
                g_variant_builder_add (&builder,
                                       "{sv}",
                                       MM_SIMPLE_PROPERTY_CDMA_SID,
                                       g_variant_new_uint32 (self->priv->modem_cdma_sid));
            if (self->priv->modem_cdma_nid  != MM_MODEM_CDMA_NID_UNKNOWN)
                g_variant_builder_add (&builder,
                                       "{sv}",
                                       MM_SIMPLE_PROPERTY_CDMA_NID,
                                       g_variant_new_uint32 (self->priv->modem_cdma_nid));
        }
        if (self->priv->modem_cdma_evdo_registration_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
            g_variant_builder_add (&builder,
                                   "{sv}",
                                   MM_SIMPLE_PROPERTY_CDMA_EVDO_REGISTRATION_STATE,
                                   g_variant_new_uint32 (self->priv->modem_cdma_evdo_registration_state));

    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

/**
 * mm_simple_status_new_from_dictionary: (skip)
 */
MMSimpleStatus *
mm_simple_status_new_from_dictionary (GVariant *dictionary,
                                      GError **error)
{
    GError                    *inner_error = NULL;
    GVariantIter               iter;
    gchar                     *key;
    GVariant                  *value;
    g_autoptr(MMSimpleStatus)  props = NULL;

    props = mm_simple_status_new ();

    if (!dictionary)
        return g_steal_pointer (&props);

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create Simple status from dictionary: "
                     "invalid variant type received");
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error && g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        /* Note: we could do a more efficient matching by checking the variant type
         * and just g_object_set()-ing they specific 'key' and value, but we do want
         * to check which input keys we receive, in order to propagate the error.
         */
        if (g_str_equal (key, MM_SIMPLE_PROPERTY_STATE) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_CDMA_CDMA1X_REGISTRATION_STATE) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_CDMA_EVDO_REGISTRATION_STATE) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_CDMA_SID) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_CDMA_NID)) {
            /* uint properties */
            g_object_set (props,
                          key, g_variant_get_uint32 (value),
                          NULL);
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE) ||
                   g_str_equal (key, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME)) {
            /* string properties */
            g_object_set (props,
                          key, g_variant_get_string (value, NULL),
                          NULL);
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_CURRENT_BANDS) ||
                   g_str_equal (key, MM_SIMPLE_PROPERTY_SIGNAL_QUALITY)) {
            /* remaining complex types, as variant */
            g_object_set (props,
                          key, value,
                          NULL);
        } else {
            /* Set inner error, will stop the loop */
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid status dictionary, unexpected key '%s'",
                                       key);
        }

        g_free (key);
        g_variant_unref (value);
    }

    /* If error, destroy the object */
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    return g_steal_pointer (&props);
}

/*****************************************************************************/

/**
 * mm_simple_status_new: (skip)
 */
MMSimpleStatus *
mm_simple_status_new (void)
{
    return (MM_SIMPLE_STATUS (
                g_object_new (MM_TYPE_SIMPLE_STATUS, NULL)));
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSimpleStatus *self = MM_SIMPLE_STATUS (object);

    switch (prop_id) {
    case PROP_STATE:
        self->priv->state = g_value_get_enum (value);
        break;
    case PROP_SIGNAL_QUALITY:
        if (self->priv->signal_quality)
            g_variant_unref (self->priv->signal_quality);
        self->priv->signal_quality = g_value_dup_variant (value);
        break;
    case PROP_CURRENT_BANDS:
        if (self->priv->current_bands)
            g_variant_unref (self->priv->current_bands);
        if (self->priv->current_bands_array) {
            g_array_unref (self->priv->current_bands_array);
            self->priv->current_bands_array = NULL;
        }
        self->priv->current_bands = g_value_dup_variant (value);
        break;
    case PROP_ACCESS_TECHNOLOGIES:
        self->priv->access_technologies = g_value_get_flags (value);
        break;
    case PROP_3GPP_REGISTRATION_STATE:
        self->priv->modem_3gpp_registration_state = g_value_get_enum (value);
        break;
    case PROP_3GPP_OPERATOR_CODE:
        g_free (self->priv->modem_3gpp_operator_code);
        self->priv->modem_3gpp_operator_code = g_value_dup_string (value);
        break;
    case PROP_3GPP_OPERATOR_NAME:
        g_free (self->priv->modem_3gpp_operator_name);
        self->priv->modem_3gpp_operator_name = g_value_dup_string (value);
        break;
    case PROP_3GPP_SUBSCRIPTION_STATE:
        /* no-op */
        break;
    case PROP_CDMA_CDMA1X_REGISTRATION_STATE:
        self->priv->modem_cdma_cdma1x_registration_state = g_value_get_enum (value);
        break;
    case PROP_CDMA_EVDO_REGISTRATION_STATE:
        self->priv->modem_cdma_evdo_registration_state = g_value_get_enum (value);
        break;
    case PROP_CDMA_SID:
        self->priv->modem_cdma_sid = g_value_get_uint (value);
        break;
    case PROP_CDMA_NID:
        self->priv->modem_cdma_nid = g_value_get_uint (value);
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
    MMSimpleStatus *self = MM_SIMPLE_STATUS (object);

    switch (prop_id) {
    case PROP_STATE:
        g_value_set_enum (value, self->priv->state);
        break;
    case PROP_SIGNAL_QUALITY:
        g_value_set_variant (value, self->priv->signal_quality);
        break;
    case PROP_CURRENT_BANDS:
        g_value_set_variant (value, self->priv->current_bands);
        break;
    case PROP_ACCESS_TECHNOLOGIES:
        g_value_set_flags (value, self->priv->access_technologies);
        break;
    case PROP_3GPP_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_3gpp_registration_state);
        break;
    case PROP_3GPP_OPERATOR_CODE:
        g_value_set_string (value, self->priv->modem_3gpp_operator_code);
        break;
    case PROP_3GPP_OPERATOR_NAME:
        g_value_set_string (value, self->priv->modem_3gpp_operator_name);
        break;
    case PROP_3GPP_SUBSCRIPTION_STATE:
        g_value_set_enum (value, MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN);
        break;
    case PROP_CDMA_CDMA1X_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_cdma_cdma1x_registration_state);
        break;
    case PROP_CDMA_EVDO_REGISTRATION_STATE:
        g_value_set_enum (value, self->priv->modem_cdma_evdo_registration_state);
        break;
    case PROP_CDMA_SID:
        g_value_set_uint (value, self->priv->modem_cdma_sid);
        break;
    case PROP_CDMA_NID:
        g_value_set_uint (value, self->priv->modem_cdma_nid);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_simple_status_init (MMSimpleStatus *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SIMPLE_STATUS,
                                              MMSimpleStatusPrivate);

    /* Some defaults */
    self->priv->state = MM_MODEM_STATE_UNKNOWN;
    self->priv->access_technologies = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    self->priv->modem_3gpp_registration_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    self->priv->current_bands = g_variant_ref_sink (mm_common_build_bands_unknown ());
    self->priv->signal_quality = g_variant_ref_sink (g_variant_new ("(ub)", 0, 0));
    self->priv->modem_cdma_cdma1x_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_evdo_registration_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    self->priv->modem_cdma_sid = MM_MODEM_CDMA_SID_UNKNOWN;
    self->priv->modem_cdma_nid = MM_MODEM_CDMA_NID_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMSimpleStatus *self = MM_SIMPLE_STATUS (object);

    g_variant_unref (self->priv->signal_quality);
    g_variant_unref (self->priv->current_bands);
    if (self->priv->current_bands_array)
        g_array_unref (self->priv->current_bands_array);
    g_free (self->priv->modem_3gpp_operator_code);
    g_free (self->priv->modem_3gpp_operator_name);

    G_OBJECT_CLASS (mm_simple_status_parent_class)->finalize (object);
}

static void
mm_simple_status_class_init (MMSimpleStatusClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimpleStatusPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    properties[PROP_STATE] =
        g_param_spec_enum (MM_SIMPLE_PROPERTY_STATE,
                           "State",
                           "State of the modem",
                           MM_TYPE_MODEM_STATE,
                           MM_MODEM_STATE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_STATE, properties[PROP_STATE]);

    properties[PROP_SIGNAL_QUALITY] =
        g_param_spec_variant (MM_SIMPLE_PROPERTY_SIGNAL_QUALITY,
                              "Signal quality",
                              "Signal quality reported by the modem",
                              G_VARIANT_TYPE ("(ub)"),
                              NULL,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SIGNAL_QUALITY, properties[PROP_SIGNAL_QUALITY]);

    properties[PROP_CURRENT_BANDS] =
        g_param_spec_variant (MM_SIMPLE_PROPERTY_CURRENT_BANDS,
                              "Current Bands",
                              "Frequency bands used by the modem",
                              G_VARIANT_TYPE ("au"),
                              NULL,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CURRENT_BANDS, properties[PROP_CURRENT_BANDS]);

    properties[PROP_ACCESS_TECHNOLOGIES] =
        g_param_spec_flags (MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES,
                            "Access Technologies",
                            "Access technologies used by the modem",
                            MM_TYPE_MODEM_ACCESS_TECHNOLOGY,
                            MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                            G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_ACCESS_TECHNOLOGIES, properties[PROP_ACCESS_TECHNOLOGIES]);

    properties[PROP_3GPP_REGISTRATION_STATE] =
        g_param_spec_enum (MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE,
                           "3GPP registration state",
                           "Registration state in the 3GPP network",
                           MM_TYPE_MODEM_3GPP_REGISTRATION_STATE,
                           MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_3GPP_REGISTRATION_STATE, properties[PROP_3GPP_REGISTRATION_STATE]);

    properties[PROP_3GPP_OPERATOR_CODE] =
        g_param_spec_string (MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE,
                             "3GPP operator code",
                             "Code of the current operator in the 3GPP network",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_3GPP_OPERATOR_CODE, properties[PROP_3GPP_OPERATOR_CODE]);

    properties[PROP_3GPP_OPERATOR_NAME] =
        g_param_spec_string (MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME,
                             "3GPP operator name",
                             "Name of the current operator in the 3GPP network",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_3GPP_OPERATOR_NAME, properties[PROP_3GPP_OPERATOR_NAME]);

    properties[PROP_3GPP_SUBSCRIPTION_STATE] =
        g_param_spec_enum (MM_SIMPLE_PROPERTY_3GPP_SUBSCRIPTION_STATE,
                           "3GPP subscription state",
                           "Subscription state of the account (deprecated)",
                           MM_TYPE_MODEM_3GPP_SUBSCRIPTION_STATE,
                           MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_3GPP_SUBSCRIPTION_STATE, properties[PROP_3GPP_SUBSCRIPTION_STATE]);

    properties[PROP_CDMA_CDMA1X_REGISTRATION_STATE] =
        g_param_spec_enum (MM_SIMPLE_PROPERTY_CDMA_CDMA1X_REGISTRATION_STATE,
                           "CDMA1x registration state",
                           "Registration state in the CDMA1x network",
                           MM_TYPE_MODEM_CDMA_REGISTRATION_STATE,
                           MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CDMA_CDMA1X_REGISTRATION_STATE, properties[PROP_CDMA_CDMA1X_REGISTRATION_STATE]);

    properties[PROP_CDMA_EVDO_REGISTRATION_STATE] =
        g_param_spec_enum (MM_SIMPLE_PROPERTY_CDMA_EVDO_REGISTRATION_STATE,
                           "EV-DO registration state",
                           "Registration state in the EV-DO network",
                           MM_TYPE_MODEM_CDMA_REGISTRATION_STATE,
                           MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CDMA_EVDO_REGISTRATION_STATE, properties[PROP_CDMA_EVDO_REGISTRATION_STATE]);

    properties[PROP_CDMA_SID] =
        g_param_spec_uint (MM_SIMPLE_PROPERTY_CDMA_SID,
                           "CDMA1x SID",
                           "System Identifier of the serving CDMA1x network",
                           0,
                           MM_MODEM_CDMA_SID_UNKNOWN,
                           MM_MODEM_CDMA_SID_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CDMA_SID, properties[PROP_CDMA_SID]);

    properties[PROP_CDMA_NID] =
        g_param_spec_uint (MM_SIMPLE_PROPERTY_CDMA_NID,
                           "CDMA1x NID",
                           "Network Identifier of the serving CDMA1x network",
                           0,
                           MM_MODEM_CDMA_NID_UNKNOWN,
                           MM_MODEM_CDMA_NID_UNKNOWN,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CDMA_NID, properties[PROP_CDMA_NID]);
}
