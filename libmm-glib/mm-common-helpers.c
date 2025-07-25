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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <gio/gio.h>

#include <ModemManager.h>

#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-errors-types.h"
#include "mm-common-helpers.h"

#if (!GLIB_CHECK_VERSION (2, 58, 0))
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GEnumClass, g_type_class_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GFlagsClass, g_type_class_unref)
#endif

/******************************************************************************/
/* Enums/flags to string builders */

gchar *
mm_common_build_capabilities_string (const MMModemCapability *capabilities,
                                     guint                    n_capabilities)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!capabilities || !n_capabilities)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_capabilities; i++) {
        gchar *tmp;

        tmp = mm_modem_capability_build_string_from_mask (capabilities[i]);
        g_string_append_printf (str, "%s%s",
                                first ? "" : "\n",
                                tmp);
        g_free (tmp);

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_bands_string (const MMModemBand *bands,
                              guint              n_bands)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!bands || !n_bands)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_bands; i++) {
        g_string_append_printf (str, "%s%s",
                                first ? "" : ", ",
                                mm_modem_band_get_string (bands[i]));

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_ports_string (const MMModemPortInfo *ports,
                              guint                  n_ports)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!ports || !n_ports)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_ports; i++) {
        g_string_append_printf (str, "%s%s (%s)",
                                first ? "" : ", ",
                                ports[i].name,
                                mm_modem_port_type_get_string (ports[i].type));

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_sms_storages_string (const MMSmsStorage *storages,
                                     guint               n_storages)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!storages || !n_storages)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_storages; i++) {
        g_string_append_printf (str, "%s%s",
                                first ? "" : ", ",
                                mm_sms_storage_get_string (storages[i]));

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_mode_combinations_string (const MMModemModeCombination *modes,
                                          guint                         n_modes)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!modes || !n_modes)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_modes; i++) {
        gchar *allowed;
        gchar *preferred;

        allowed = mm_modem_mode_build_string_from_mask (modes[i].allowed);
        preferred = mm_modem_mode_build_string_from_mask (modes[i].preferred);
        g_string_append_printf (str, "%sallowed: %s; preferred: %s",
                                first ? "" : "\n",
                                allowed,
                                preferred);
        g_free (allowed);
        g_free (preferred);

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

gchar *
mm_common_build_channels_string (const MMCellBroadcastChannels *channels,
                                 guint                          n_channels)
{
    gboolean first = TRUE;
    GString *str;
    guint i;

    if (!channels || !n_channels)
        return g_strdup ("none");

    str = g_string_new ("");
    for (i = 0; i < n_channels; i++) {
        if (channels[i].start == channels[i].end) {
            g_string_append_printf (str, "%s%u",
                                    first ? "" : ",",
                                    channels[i].start);
        } else {
            g_string_append_printf (str, "%s%u-%u",
                                    first ? "" : ",",
                                    channels[i].start, channels[i].end);
        }

        if (first)
            first = FALSE;
    }

    return g_string_free (str, FALSE);
}

/******************************************************************************/
/* String to enums/flags parsers */


static gint
_enum_from_string (GType         type,
                   const gchar  *str,
                   gint          error_value,
                   GError      **error)
{
    g_autoptr(GEnumClass) enum_class = NULL;
    gint                  value;
    guint                 i;

    enum_class = G_ENUM_CLASS (g_type_class_ref (type));

    for (i = 0; enum_class->values[i].value_nick; i++) {
        if (!g_ascii_strcasecmp (str, enum_class->values[i].value_nick)) {
            value = enum_class->values[i].value;
            return value;
        }
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't match '%s' with a valid %s value",
                 str,
                 g_type_name (type));
    return error_value;
}

static guint
_flags_from_string (GType         type,
                    const gchar  *str,
                    guint         error_value,
                    GError      **error)
{
    g_auto(GStrv)          flags_strings = NULL;
    g_autoptr(GFlagsClass) flags_class = NULL;
    guint                  value = 0;
    guint                  i;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (type));
    flags_strings = g_strsplit (str, "|", -1);

    for (i = 0; flags_strings[i]; i++) {
        guint j;
        gboolean found = FALSE;

        for (j = 0; flags_class->values[j].value_nick; j++) {
            if (!g_ascii_strcasecmp (flags_strings[i], flags_class->values[j].value_nick)) {
                value |= flags_class->values[j].value;
                found = TRUE;
            }
        }

        if (!found) {
            g_set_error (error,
                MM_CORE_ERROR,
                MM_CORE_ERROR_INVALID_ARGS,
                "Couldn't match '%s' with a valid %s value",
                flags_strings[i],
                g_type_name (type));
            return error_value;
        }
    }

    return value;
}

MMModemCapability
mm_common_get_capabilities_from_string (const gchar  *str,
                                        GError      **error)
{
    GError                 *inner_error = NULL;
    MMModemCapability       capabilities;
    g_auto(GStrv)           capability_strings = NULL;
    g_autoptr(GFlagsClass)  flags_class = NULL;

    capabilities = MM_MODEM_CAPABILITY_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_CAPABILITY));
    capability_strings = g_strsplit (str, "|", -1);

    if (capability_strings) {
        guint i;

        for (i = 0; capability_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (capability_strings[i], flags_class->values[j].value_nick)) {
                    capabilities |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMModemCapability value",
                    capability_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        capabilities = MM_MODEM_CAPABILITY_NONE;
    }
    return capabilities;
}

MMModemMode
mm_common_get_modes_from_string (const gchar  *str,
                                 GError      **error)
{
    GError                 *inner_error = NULL;
    MMModemMode             modes;
    g_auto(GStrv)           mode_strings = NULL;
    g_autoptr(GFlagsClass)  flags_class = NULL;

    modes = MM_MODEM_MODE_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_MODE));
    mode_strings = g_strsplit (str, "|", -1);

    if (mode_strings) {
        guint i;

        for (i = 0; mode_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (mode_strings[i], flags_class->values[j].value_nick)) {
                    modes |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMModemMode value",
                    mode_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        modes = MM_MODEM_MODE_NONE;
    }
    return modes;
}

gboolean
mm_common_get_bands_from_string (const gchar  *str,
                                 MMModemBand **bands,
                                 guint        *n_bands,
                                 GError      **error)
{
    GError                *inner_error = NULL;
    GArray                *array;
    g_auto(GStrv)          band_strings = NULL;
    g_autoptr(GEnumClass)  enum_class = NULL;

    array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_BAND));
    band_strings = g_strsplit (str, "|", -1);

    if (band_strings) {
        guint i;

        for (i = 0; band_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; enum_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (band_strings[i], enum_class->values[j].value_nick)) {
                    g_array_append_val (array, enum_class->values[j].value);
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Couldn't match '%s' with a valid MMModemBand value",
                                           band_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_array_free (array, TRUE);
        *n_bands = 0;
        *bands = NULL;
        return FALSE;
    }

    if (!array->len) {
        GEnumValue *value;

        value = g_enum_get_value (enum_class, MM_MODEM_BAND_UNKNOWN);
        g_array_append_val (array, value->value);
    }

    *n_bands = array->len;
    *bands = (MMModemBand *)g_array_free (array, FALSE);
    return TRUE;
}

gboolean
mm_common_get_boolean_from_string (const gchar  *value,
                                   GError      **error)
{
    if (!g_ascii_strcasecmp (value, "true") || g_str_equal (value, "1") || !g_ascii_strcasecmp (value, "yes"))
        return TRUE;

    if (!g_ascii_strcasecmp (value, "false") || g_str_equal (value, "0") || !g_ascii_strcasecmp (value, "no"))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Cannot get boolean from string '%s'", value);
    return FALSE;
}

MMModemCdmaRmProtocol
mm_common_get_rm_protocol_from_string (const gchar  *str,
                                       GError      **error)
{
    return _enum_from_string (MM_TYPE_MODEM_CDMA_RM_PROTOCOL,
                              str,
                              MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN,
                              error);
}

MMBearerIpFamily
mm_common_get_ip_type_from_string (const gchar  *str,
                                   GError      **error)
{
    return _flags_from_string (MM_TYPE_BEARER_IP_FAMILY,
                               str,
                               MM_BEARER_IP_FAMILY_NONE,
                               error);
}

MMBearerAllowedAuth
mm_common_get_allowed_auth_from_string (const gchar  *str,
                                        GError      **error)
{
    GError                 *inner_error = NULL;
    MMBearerAllowedAuth     allowed_auth;
    g_auto(GStrv)           strings = NULL;
    g_autoptr(GFlagsClass)  flags_class = NULL;

    allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_BEARER_ALLOWED_AUTH));
    strings = g_strsplit (str, "|", -1);

    if (strings) {
        guint i;

        for (i = 0; strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (strings[i], flags_class->values[j].value_nick)) {
                    allowed_auth |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMBearerAllowedAuth value",
                    strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        allowed_auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    }
    return allowed_auth;
}

MMSmsStorage
mm_common_get_sms_storage_from_string (const gchar  *str,
                                       GError      **error)
{
    return _enum_from_string (MM_TYPE_SMS_STORAGE,
                              str,
                              MM_SMS_STORAGE_UNKNOWN,
                              error);
}

MMSmsCdmaTeleserviceId
mm_common_get_sms_cdma_teleservice_id_from_string (const gchar  *str,
                                                   GError      **error)
{
    return _enum_from_string (MM_TYPE_SMS_CDMA_TELESERVICE_ID,
                              str,
                              MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN,
                              error);
}

MMSmsCdmaServiceCategory
mm_common_get_sms_cdma_service_category_from_string (const gchar  *str,
                                                     GError      **error)
{
    return _enum_from_string (MM_TYPE_SMS_CDMA_SERVICE_CATEGORY,
                              str,
                              MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
                              error);
}

MMCallDirection
mm_common_get_call_direction_from_string (const gchar  *str,
                                          GError      **error)
{
    return _enum_from_string (MM_TYPE_CALL_DIRECTION,
                              str,
                              MM_CALL_DIRECTION_UNKNOWN,
                              error);
}

MMCallState
mm_common_get_call_state_from_string (const gchar  *str,
                                      GError      **error)
{
    return _enum_from_string (MM_TYPE_CALL_STATE,
                              str,
                              MM_CALL_STATE_UNKNOWN,
                              error);
}

MMCallStateReason
mm_common_get_call_state_reason_from_string (const gchar  *str,
                                             GError      **error)
{
    return _enum_from_string (MM_TYPE_CALL_STATE_REASON,
                              str,
                              MM_CALL_STATE_REASON_UNKNOWN,
                              error);
}

MMOmaFeature
mm_common_get_oma_features_from_string (const gchar  *str,
                                        GError      **error)
{
    GError                 *inner_error = NULL;
    MMOmaFeature            features;
    g_auto(GStrv)           feature_strings = NULL;
    g_autoptr(GFlagsClass)  flags_class = NULL;

    features = MM_OMA_FEATURE_NONE;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_OMA_FEATURE));
    feature_strings = g_strsplit (str, "|", -1);

    if (feature_strings) {
        guint i;

        for (i = 0; feature_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (feature_strings[i], flags_class->values[j].value_nick)) {
                    features |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMOmaFeature value",
                    feature_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        features = MM_OMA_FEATURE_NONE;
    }
    return features;
}

MMOmaSessionType
mm_common_get_oma_session_type_from_string (const gchar  *str,
                                            GError      **error)
{
    return _enum_from_string (MM_TYPE_OMA_SESSION_TYPE,
                              str,
                              MM_OMA_SESSION_TYPE_UNKNOWN,
                              error);
}

MMModem3gppEpsUeModeOperation
mm_common_get_eps_ue_mode_operation_from_string (const gchar  *str,
                                                 GError      **error)
{
    return _enum_from_string (MM_TYPE_MODEM_3GPP_EPS_UE_MODE_OPERATION,
                              str,
                              MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN,
                              error);
}

MMModemAccessTechnology
mm_common_get_access_technology_from_string (const gchar  *str,
                                             GError      **error)
{
    GError                  *inner_error = NULL;
    MMModemAccessTechnology  technologies;
    g_auto(GStrv)            technology_strings = NULL;
    g_autoptr(GFlagsClass)   flags_class = NULL;

    technologies = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_ACCESS_TECHNOLOGY));
    technology_strings = g_strsplit (str, "|", -1);

    if (technology_strings) {
        guint i;

        for (i = 0; technology_strings[i]; i++) {
            guint j;
            gboolean found = FALSE;

            for (j = 0; flags_class->values[j].value_nick; j++) {
                if (!g_ascii_strcasecmp (technology_strings[i], flags_class->values[j].value_nick)) {
                    technologies |= flags_class->values[j].value;
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                inner_error = g_error_new (
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_INVALID_ARGS,
                    "Couldn't match '%s' with a valid MMModemAccessTechnology value",
                    technology_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        technologies = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
    return technologies;
}

MMBearerMultiplexSupport
mm_common_get_multiplex_support_from_string (const gchar  *str,
                                             GError      **error)
{
    return _enum_from_string (MM_TYPE_BEARER_MULTIPLEX_SUPPORT,
                              str,
                              MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN,
                              error);
}

MMBearerApnType
mm_common_get_apn_type_from_string (const gchar  *str,
                                    GError      **error)
{
    return _flags_from_string (MM_TYPE_BEARER_APN_TYPE,
                               str,
                               MM_BEARER_APN_TYPE_NONE,
                               error);
}

MMModem3gppFacility
mm_common_get_3gpp_facility_from_string (const gchar  *str,
                                         GError      **error)
{
    return _flags_from_string (MM_TYPE_MODEM_3GPP_FACILITY,
                               str,
                               MM_MODEM_3GPP_FACILITY_NONE,
                               error);
}

MMModem3gppPacketServiceState
mm_common_get_3gpp_packet_service_state_from_string (const gchar  *str,
                                                     GError      **error)
{
    return _enum_from_string (MM_TYPE_MODEM_3GPP_PACKET_SERVICE_STATE,
                              str,
                              MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN,
                              error);
}

MMModem3gppMicoMode
mm_common_get_3gpp_mico_mode_from_string (const gchar  *str,
                                          GError      **error)
{
    return _enum_from_string (MM_TYPE_MODEM_3GPP_MICO_MODE,
                              str,
                              MM_MODEM_3GPP_MICO_MODE_UNKNOWN,
                              error);
}

MMModem3gppDrxCycle
mm_common_get_3gpp_drx_cycle_from_string (const gchar  *str,
                                          GError      **error)
{
    return _enum_from_string (MM_TYPE_MODEM_3GPP_DRX_CYCLE,
                              str,
                              MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN,
                              error);
}

MMBearerAccessTypePreference
mm_common_get_access_type_preference_from_string (const gchar  *str,
                                                  GError      **error)
{
    return _enum_from_string (MM_TYPE_BEARER_ACCESS_TYPE_PREFERENCE,
                              str,
                              MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE,
                              error);
}

MMBearerProfileSource
mm_common_get_profile_source_from_string (const gchar  *str,
                                          GError      **error)
{
    return _enum_from_string (MM_TYPE_BEARER_PROFILE_SOURCE,
                              str,
                              MM_BEARER_PROFILE_SOURCE_UNKNOWN,
                              error);
}

gboolean
mm_common_get_cell_broadcast_channels_from_string (const gchar              *str,
                                                   MMCellBroadcastChannels **channels,
                                                   guint                    *n_channels,
                                                   GError                  **error)
{
    GError                *inner_error = NULL;
    GArray                *array;
    g_auto(GStrv)          channel_strings = NULL;

    array = g_array_new (FALSE, FALSE, sizeof (MMCellBroadcastChannels));

    channel_strings = g_strsplit (str, ",", -1);

    if (channel_strings) {
        guint i;

        for (i = 0; channel_strings[i]; i++) {
            char *startptr, *endptr;
            gint64 start;

            startptr = channel_strings[i];
            start = g_ascii_strtoll (startptr, &endptr, 10);
            if (startptr == endptr || start > G_MAXUINT16 || start < 0) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Couldn't parse '%s' as MMCellBroadcastChannel start value",
                                           channel_strings[i]);
                break;
            }
            if (*endptr == '\0') {
                MMCellBroadcastChannels ch;

                ch.start = start;
                ch.end = start;
                g_array_append_val (array, ch);
            } else if (*endptr == '-') {
                gint64 end;

                startptr = endptr + 1;
                end = g_ascii_strtoll (startptr, &endptr, 10);
                if (startptr == endptr || end > G_MAXUINT16 || end < 0) {
                    inner_error = g_error_new (MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Couldn't parse '%s' as MMCellBroadcastChannel end value",
                                               channel_strings[i]);
                    break;
                }
                if (*endptr == '\0') {
                    MMCellBroadcastChannels ch;

                    ch.start = start;
                    ch.end = end;
                    g_array_append_val (array, ch);
                } else {
                    inner_error = g_error_new (MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Couldn't parse '%s' as MMCellBroadcastChannel end value",
                                               channel_strings[i]);
                    break;
                }
            } else {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_INVALID_ARGS,
                                           "Couldn't parse '%s' as MMCellBroadcastChannel value",
                                           channel_strings[i]);
                break;
            }
        }
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_array_free (array, TRUE);
        *n_channels = 0;
        *channels = NULL;
        return FALSE;
    }

    *n_channels = array->len;
    *channels = (MMCellBroadcastChannels *)g_array_free (array, FALSE);
    return TRUE;
}

/******************************************************************************/
/* MMModemPortInfo array management */

static void
clear_modem_port_info (MMModemPortInfo *info)
{
    g_free (info->name);
}

GArray *
mm_common_ports_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        guint i;
        guint n;

        n = g_variant_n_children (variant);

        if (n > 0) {
            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemPortInfo), n);
            g_array_set_clear_func (array, (GDestroyNotify) clear_modem_port_info);
            for (i = 0; i < n; i++) {
                MMModemPortInfo info;

                g_variant_get_child (variant, i, "(su)", &info.name, &info.type);
                g_array_append_val (array, info);
            }
        }
    }

    return array;
}

GVariant *
mm_common_ports_array_to_variant (const MMModemPortInfo *ports,
                                  guint                  n_ports)
{
    GVariantBuilder builder;
    guint i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));

    for (i = 0; i < n_ports; i++) {
        GVariant *tuple[2];

        tuple[0] = g_variant_new_string (ports[i].name);
        tuple[1] = g_variant_new_uint32 ((guint32)ports[i].type);
        g_variant_builder_add_value (&builder, g_variant_new_tuple (tuple, 2));
    }
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_ports_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_ports_array_to_variant ((const MMModemPortInfo *)array->data,
                                                 array->len);

    return mm_common_ports_array_to_variant (NULL, 0);
}

gboolean
mm_common_ports_garray_to_array (GArray           *array,
                                 MMModemPortInfo **ports,
                                 guint            *n_ports)
{
    if (!array)
        return FALSE;

    *ports = NULL;
    *n_ports = array->len;
    if (array->len > 0) {
        guint i;

        *ports = g_malloc (sizeof (MMModemPortInfo) * array->len);

        /* Deep-copy the array */
        for (i = 0; i < array->len; i++) {
            MMModemPortInfo *src;

            src = &g_array_index (array, MMModemPortInfo, i);
            (*ports)[i].name = g_strdup (src->name);
            (*ports)[i].type = src->type;
        }
    }
    return TRUE;
}

/******************************************************************************/
/* MMSmsStorage array management */

GArray *
mm_common_sms_storages_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 storage;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), n);
            while (g_variant_iter_loop (&iter, "u", &storage))
                g_array_append_val (array, storage);
        }
    }

    return array;
}

GVariant *
mm_common_sms_storages_array_to_variant (const MMSmsStorage *storages,
                                         guint               n_storages)
{
    GVariantBuilder builder;
    guint i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

    for (i = 0; i < n_storages; i++)
        g_variant_builder_add_value (&builder,
                                     g_variant_new_uint32 ((guint32)storages[i]));
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_sms_storages_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_sms_storages_array_to_variant ((const MMSmsStorage *)array->data,
                                                        array->len);

    return mm_common_sms_storages_array_to_variant (NULL, 0);
}

/******************************************************************************/
/* MMModemCapability array management */

GArray *
mm_common_capability_combinations_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 capability;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), n);
            while (g_variant_iter_loop (&iter, "u", &capability))
                g_array_append_val (array, capability);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        guint32 capability = MM_MODEM_CAPABILITY_NONE;

        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 1);
        g_array_append_val (array, capability);
    }

    return array;
}

GVariant *
mm_common_capability_combinations_array_to_variant (const MMModemCapability *capabilities,
                                                    guint                    n_capabilities)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

    if (n_capabilities > 0) {
        guint i;

        for (i = 0; i < n_capabilities; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new_uint32 ((guint32)capabilities[i]));
    } else
        g_variant_builder_add_value (&builder,
                                     g_variant_new_uint32 (MM_MODEM_CAPABILITY_NONE));

    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_capability_combinations_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_capability_combinations_array_to_variant ((const MMModemCapability *)array->data,
                                                                   array->len);

    return mm_common_capability_combinations_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_capability_combinations_none (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_CAPABILITY_NONE));
    return g_variant_builder_end (&builder);
}

/******************************************************************************/
/* MMModemModeCombination array management */

GArray *
mm_common_mode_combinations_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            MMModemModeCombination mode;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), n);
            while (g_variant_iter_loop (&iter, "(uu)", &mode.allowed, &mode.preferred))
                g_array_append_val (array, mode);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        MMModemModeCombination default_mode;

        default_mode.allowed = MM_MODEM_MODE_ANY;
        default_mode.preferred = MM_MODEM_MODE_NONE;
        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
        g_array_append_val (array, default_mode);
    }

    return array;
}

GVariant *
mm_common_mode_combinations_array_to_variant (const MMModemModeCombination *modes,
                                              guint                         n_modes)
{
    if (n_modes > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));

        for (i = 0; i < n_modes; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new ("(uu)",
                                                        ((guint32)modes[i].allowed),
                                                        ((guint32)modes[i].preferred)));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_mode_combinations_default ();
}

GVariant *
mm_common_mode_combinations_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_mode_combinations_array_to_variant ((const MMModemModeCombination *)array->data,
                                                             array->len);

    return mm_common_mode_combinations_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_mode_combinations_default (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new ("(uu)",
                                                MM_MODEM_MODE_ANY,
                                                MM_MODEM_MODE_NONE));
    return g_variant_builder_end (&builder);
}

/******************************************************************************/
/* MMModemBand array management */

GArray *
mm_common_bands_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            guint32 band;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n);
            while (g_variant_iter_loop (&iter, "u", &band))
                g_array_append_val (array, band);
        }
    }

    /* If nothing set, fallback to default */
    if (!array) {
        guint32 band = MM_MODEM_BAND_UNKNOWN;

        array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
        g_array_append_val (array, band);
    }

    return array;
}

GVariant *
mm_common_bands_array_to_variant (const MMModemBand *bands,
                                  guint              n_bands)
{
    if (n_bands > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

        for (i = 0; i < n_bands; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new_uint32 ((guint32)bands[i]));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_bands_unknown ();
}

GVariant *
mm_common_bands_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_bands_array_to_variant ((const MMModemBand *)array->data,
                                                 array->len);

    return mm_common_bands_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_bands_unknown (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_BAND_UNKNOWN));
    return g_variant_builder_end (&builder);
}

GVariant *
mm_common_build_bands_any (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));
    g_variant_builder_add_value (&builder,
                                 g_variant_new_uint32 (MM_MODEM_BAND_ANY));
    return g_variant_builder_end (&builder);
}

static guint
cmp_band (MMModemBand *a, MMModemBand *b)
{
    return (*a - *b);
}

gboolean
mm_common_bands_garray_cmp (GArray *a, GArray *b)
{
    GArray *dup_a;
    GArray *dup_b;
    guint i;
    gboolean different;

    if (a->len != b->len)
        return FALSE;

    dup_a = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), a->len);
    g_array_append_vals (dup_a, a->data, a->len);

    dup_b = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), b->len);
    g_array_append_vals (dup_b, b->data, b->len);

    g_array_sort (dup_a, (GCompareFunc)cmp_band);
    g_array_sort (dup_b, (GCompareFunc)cmp_band);

    different = FALSE;
    for (i = 0; !different && i < a->len; i++) {
        if (g_array_index (dup_a, MMModemBand, i) != g_array_index (dup_b, MMModemBand, i))
            different = TRUE;
    }

    g_array_unref (dup_a);
    g_array_unref (dup_b);

    return !different;
}

gboolean
mm_common_bands_garray_lookup (GArray      *array,
                               MMModemBand  value)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        if (value == g_array_index (array, MMModemBand, i))
            return TRUE;
    }

    return FALSE;
}

void
mm_common_bands_garray_sort (GArray *array)
{
    g_array_sort (array, (GCompareFunc) cmp_band);
}

gboolean
mm_common_band_is_gsm (MMModemBand band)
{
    return ((band >= MM_MODEM_BAND_EGSM && band <= MM_MODEM_BAND_G850) ||
            (band >= MM_MODEM_BAND_G450 && band <= MM_MODEM_BAND_G810));
}

gboolean
mm_common_band_is_utran (MMModemBand band)
{
    return ((band >= MM_MODEM_BAND_UTRAN_1 && band <= MM_MODEM_BAND_UTRAN_7) ||
            (band >= MM_MODEM_BAND_UTRAN_10 && band <= MM_MODEM_BAND_UTRAN_32));
}

gboolean
mm_common_band_is_eutran (MMModemBand band)
{
    return ((band >= MM_MODEM_BAND_EUTRAN_1 && band <= MM_MODEM_BAND_EUTRAN_71) ||
            (band == MM_MODEM_BAND_EUTRAN_85) ||
            (band == MM_MODEM_BAND_EUTRAN_106));
}

gboolean
mm_common_band_is_cdma (MMModemBand band)
{
    return (band >= MM_MODEM_BAND_CDMA_BC0 && band <= MM_MODEM_BAND_CDMA_BC19);
}

/******************************************************************************/
/* MMOmaPendingNetworkInitiatedSession array management */

GArray *
mm_common_oma_pending_network_initiated_sessions_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            MMOmaPendingNetworkInitiatedSession session;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMOmaPendingNetworkInitiatedSession), n);
            while (g_variant_iter_loop (&iter, "(uu)", &session.session_type, &session.session_id))
                g_array_append_val (array, session);
        }
    }

    /* If nothing set, fallback to empty */
    if (!array)
        array = g_array_new (FALSE, FALSE, sizeof (MMOmaPendingNetworkInitiatedSession));

    return array;
}

GVariant *
mm_common_oma_pending_network_initiated_sessions_array_to_variant (const MMOmaPendingNetworkInitiatedSession *sessions,
                                                                   guint                                      n_sessions)
{
    if (n_sessions > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));

        for (i = 0; i < n_sessions; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new ("(uu)",
                                                        ((guint32)sessions[i].session_type),
                                                        ((guint32)sessions[i].session_id)));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_oma_pending_network_initiated_sessions_default ();
}

GVariant *
mm_common_oma_pending_network_initiated_sessions_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_oma_pending_network_initiated_sessions_array_to_variant ((const MMOmaPendingNetworkInitiatedSession *)array->data,
                                                                                  array->len);

    return mm_common_oma_pending_network_initiated_sessions_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_oma_pending_network_initiated_sessions_default (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));
    return g_variant_builder_end (&builder);
}

/******************************************************************************/
/* MMModemCellbroadcastChannel array management */

GArray *
mm_common_cell_broadcast_channels_variant_to_garray (GVariant *variant)
{
    GArray *array = NULL;

    if (variant) {
        GVariantIter iter;
        guint n;

        g_variant_iter_init (&iter, variant);
        n = g_variant_iter_n_children (&iter);

        if (n > 0) {
            MMCellBroadcastChannels channels;

            array = g_array_sized_new (FALSE, FALSE, sizeof (MMCellBroadcastChannels), n);
            while (g_variant_iter_loop (&iter, "(uu)", &channels.start, &channels.end))
                g_array_append_val (array, channels);
        }
    }

    /* If nothing set, fallback to empty */
    if (!array)
        array = g_array_new (FALSE, FALSE, sizeof (MMCellBroadcastChannels));

    return array;
}

GVariant *
mm_common_cell_broadcast_channels_array_to_variant (const MMCellBroadcastChannels *channels,
                                                    guint                          n_sessions)
{
    if (n_sessions > 0) {
        GVariantBuilder builder;
        guint i;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));

        for (i = 0; i < n_sessions; i++)
            g_variant_builder_add_value (&builder,
                                         g_variant_new ("(uu)",
                                                        ((guint32)channels[i].start),
                                                        ((guint32)channels[i].end)));
        return g_variant_builder_end (&builder);
    }

    return mm_common_build_cell_broadcast_channels_default ();
}

GVariant *
mm_common_cell_broadcast_channels_garray_to_variant (GArray *array)
{
    if (array)
        return mm_common_cell_broadcast_channels_array_to_variant ((const MMCellBroadcastChannels *)array->data,
                                                                   array->len);

    return mm_common_cell_broadcast_channels_array_to_variant (NULL, 0);
}

GVariant *
mm_common_build_cell_broadcast_channels_default (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(uu)"));
    return g_variant_builder_end (&builder);
}

/******************************************************************************/
/* Common parsers */

/* Expecting input as:
 *   key1=string,key2=true,key3=false...
 * Strings may also be passed enclosed between double or single quotes, like:
 *   key1="this is a string", key2='and so is this'
 */
gboolean
mm_common_parse_key_value_string (const gchar *str,
                                  GError **error,
                                  MMParseKeyValueForeachFn callback,
                                  gpointer user_data)
{
    GError *inner_error = NULL;
    gchar *dup, *p, *key, *key_end, *value, *value_end, quote;

    g_return_val_if_fail (callback != NULL, FALSE);
    g_return_val_if_fail (str != NULL, FALSE);

    /* Allow empty strings, we'll just return with success */
    while (g_ascii_isspace (*str))
        str++;
    if (!str[0])
        return TRUE;

    dup = g_strdup (str);
    p = dup;

    while (TRUE) {
        gboolean keep_iteration = FALSE;

        /* Skip leading spaces */
        while (g_ascii_isspace (*p))
            p++;

        /* Key start */
        key = p;
        if (!g_ascii_isalnum (*key)) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Key must start with alpha/num, starts with '%c'",
                                       *key);
            break;
        }

        /* Key end */
        while (g_ascii_isalnum (*p) || (*p == '-') || (*p == '_'))
            p++;
        key_end = p;
        if (key_end == key) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't find a proper key");
            break;
        }

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* Equal sign must be here */
        if (*p != '=') {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't find equal sign separator");
            break;
        }
        /* Skip the equal */
        p++;

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* Do we have a quote-enclosed string? */
        if (*p == '\"' || *p == '\'') {
            quote = *p;
            /* Skip the quote */
            p++;
            /* Value start */
            value = p;
            /* Find the closing quote */
            p = strchr (p, quote);
            if (!p) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_FAILED,
                                           "Unmatched quotes in string value");
                break;
            }

            /* Value end */
            value_end = p;
            /* Skip the quote */
            p++;
        } else {
            /* Value start */
            value = p;

            /* Value end */
            while ((*p != ',') && (*p != '\0') && !g_ascii_isspace (*p))
                p++;
            value_end = p;
        }

        /* Note that we allow value == value_end here */

        /* Skip whitespaces, if any */
        while (g_ascii_isspace (*p))
            p++;

        /* If a comma is found, we should keep the iteration */
        if (*p == ',') {
            /* skip the comma */
            p++;
            keep_iteration = TRUE;
        }

        /* Got key and value, prepare them and run the callback */
        *value_end = '\0';
        *key_end = '\0';
        if (!callback (key, value, user_data)) {
            /* We were told to abort */
            break;
        }

        if (keep_iteration)
            continue;

        /* Check if no more key/value pairs expected */
        if (*p == '\0')
            break;

        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Unexpected content (%s) after value",
                                   p);
        break;
    }

    g_free (dup);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

gboolean
mm_get_int_from_str (const gchar *str,
                     gint        *out)
{
    glong num;
    guint i;
    guint eol = 0;

    if (!str)
        return FALSE;

    /* ignore all leading whitespaces */
    while (str[0] == ' ')
        str++;

    if (!str[0])
        return FALSE;

    for (i = 0; str[i]; i++) {
        if (str[i] != '+' && str[i] != '-' && !g_ascii_isdigit (str[i])) {
            /* ignore \r\n at the end of the string */
            if ((str[i] == '\r') || (str[i] == '\n')) {
                eol++;
                continue;
            }
            return FALSE;
        }
        /* if eol found before a valid char, the string is not parseable */
        if (eol)
            return FALSE;
    }
    /* if all characters were eol, the string is not parseable */
    if (eol == i)
        return FALSE;

    errno = 0;
    num = strtol (str, NULL, 10);
    if (!errno && num >= G_MININT && num <= G_MAXINT) {
        *out = (gint)num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_int_from_match_info (GMatchInfo *match_info,
                            guint32     match_index,
                            gint       *out)
{
    g_autofree gchar *s = NULL;

    s = mm_get_string_unquoted_from_match_info (match_info, match_index);
    return (s ? mm_get_int_from_str (s, out) : FALSE);
}

gboolean
mm_get_uint_from_str (const gchar *str,
                      guint       *out)
{
    guint64 num;

    if (!mm_get_u64_from_str (str, &num) || num > G_MAXUINT)
        return FALSE;

    *out = (guint)num;
    return TRUE;
}

gboolean
mm_get_u64_from_str (const gchar *str,
                     guint64     *out)
{
    guint64 num;
    guint   eol = 0;

    if (!str)
        return FALSE;

    /* ignore all leading whitespaces */
    while (str[0] == ' ')
        str++;

    if (!str[0])
        return FALSE;

    for (num = 0; str[num]; num++) {
        if (!g_ascii_isdigit (str[num])) {
            /* ignore \r\n at the end of the string */
            if ((str[num] == '\r') || (str[num] == '\n')) {
                eol++;
                continue;
            }
            return FALSE;
        }
        /* if eol found before a valid char, the string is not parseable */
        if (eol)
            return FALSE;
    }
    /* if all characters were eol, the string is not parseable */
    if (eol == num)
        return FALSE;

    errno = 0;
    num = (guint64) strtoull (str, NULL, 10);
    if (!errno) {
        *out = num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_uint_from_hex_str (const gchar *str,
                          guint       *out)
{
    guint64 num;

    if (!mm_get_u64_from_hex_str (str, &num) || num > G_MAXUINT)
        return FALSE;

    *out = (guint)num;
    return TRUE;
}

gboolean
mm_get_u64_from_hex_str (const gchar *str,
                         guint64     *out)
{
    guint64 num;
    guint   eol = 0;

    if (!str)
        return FALSE;

    /* ignore all leading whitespaces */
    while (str[0] == ' ')
        str++;

    if (g_str_has_prefix (str, "0x"))
        str = &str[2];

    if (!str[0])
        return FALSE;

    for (num = 0; str[num]; num++) {
        if (!g_ascii_isxdigit (str[num])) {
            /* ignore \r\n at the end of the string */
            if ((str[num] == '\r') || (str[num] == '\n')) {
                eol++;
                continue;
            }
            return FALSE;
        }
        /* if eol found before a valid char, the string is not parseable */
        if (eol)
            return FALSE;
    }
    /* if all characters were eol, the string is not parseable */
    if (eol == num)
        return FALSE;

    errno = 0;
    num = (guint64) strtoull (str, NULL, 16);
    if (!errno) {
        *out = num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_uint_from_match_info (GMatchInfo *match_info,
                             guint32     match_index,
                             guint      *out)
{
    guint64 num;

    if (!mm_get_u64_from_match_info (match_info, match_index, &num) || num > G_MAXUINT)
        return FALSE;

    *out = (guint)num;
    return TRUE;
}

gboolean
mm_get_u64_from_match_info (GMatchInfo *match_info,
                            guint32     match_index,
                            guint64    *out)
{
    g_autofree gchar *s = NULL;

    s = mm_get_string_unquoted_from_match_info (match_info, match_index);
    return (s ? mm_get_u64_from_str (s, out) : FALSE);
}

gboolean
mm_get_uint_from_hex_match_info (GMatchInfo *match_info,
                                 guint32     match_index,
                                 guint      *out)
{
    guint64 num;

    if (!mm_get_u64_from_hex_match_info (match_info, match_index, &num) || num > G_MAXUINT)
        return FALSE;

    *out = (guint)num;
    return TRUE;
}

gboolean
mm_get_u64_from_hex_match_info (GMatchInfo *match_info,
                                guint32     match_index,
                                guint64    *out)
{
    g_autofree gchar *s = NULL;

    s = mm_get_string_unquoted_from_match_info (match_info, match_index);
    return (s ? mm_get_u64_from_hex_str (s, out) : FALSE);
}

gboolean
mm_get_double_from_str (const gchar *str,
                        gdouble     *out)
{
    gdouble  num;
    guint    i;
    guint    eol = 0;

    if (!str || !str[0])
        return FALSE;

    for (i = 0; str[i]; i++) {
        /* we don't really expect numbers in scientific notation, so
         * don't bother looking for exponents and such */
        if ((str[i] != '-') && (str[i] != '.') && !g_ascii_isdigit (str[i])) {
            /* ignore \r\n at the end of the string */
            if ((str[i] == '\r') || (str[i] == '\n')) {
                eol++;
                continue;
            }
            return FALSE;
        }
        /* if eol found before a valid char, the string is not parseable */
        if (eol)
            return FALSE;
    }
    /* if all characters were eol, the string is not parseable */
    if (eol == i)
        return FALSE;

    errno = 0;
    num = g_ascii_strtod (str, NULL);
    if (!errno) {
        *out = num;
        return TRUE;
    }
    return FALSE;
}

gboolean
mm_get_double_from_match_info (GMatchInfo *match_info,
                               guint32     match_index,
                               gdouble    *out)
{
    g_autofree gchar *s = NULL;

    s = mm_get_string_unquoted_from_match_info (match_info, match_index);
    return (s ? mm_get_double_from_str (s, out) : FALSE);
}

gchar *
mm_get_string_unquoted_from_match_info (GMatchInfo *match_info,
                                        guint32     match_index)
{
    gchar *str;
    gsize len;

    str = g_match_info_fetch (match_info, match_index);
    if (!str)
        return NULL;

    len = strlen (str);

    /* Unquote the item if needed */
    if ((len >= 2) && (str[0] == '"') && (str[len - 1] == '"')) {
        str[0] = ' ';
        str[len - 1] = ' ';
        str = g_strstrip (str);
    }

    if (!str[0]) {
        g_free (str);
        return NULL;
    }

    return str;
}

/*
 * The following implementation is taken from glib g_date_time_format_iso8601 code
 * https://gitlab.gnome.org/GNOME/glib/-/blob/main/glib/gdatetime.c#L3490
 */
static gchar *
date_time_format_iso8601 (GDateTime *dt)
{
#if GLIB_CHECK_VERSION (2, 62, 0)
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return g_date_time_format_iso8601 (dt);
    G_GNUC_END_IGNORE_DEPRECATIONS
#else
    GString          *outstr = NULL;
    g_autofree gchar *main_date = NULL;
    gint64            offset = 0;

    main_date = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%S");
    outstr = g_string_new (main_date);

    /* Timezone. Format it as `%:::z` unless the offset is zero, in which case
     * we can simply use `Z`. */
    offset = g_date_time_get_utc_offset (dt);
    if (offset == 0) {
        g_string_append_c (outstr, 'Z');
    } else {
        g_autofree gchar *time_zone = NULL;

        time_zone = g_date_time_format (dt, "%:::z");
        g_string_append (outstr, time_zone);
    }

    return g_string_free (outstr, FALSE);
#endif
}

gchar *
mm_new_iso8601_time_from_unix_time (guint64   timestamp,
                                    GError  **error)
{
    g_autoptr(GDateTime) dt = NULL;

    dt = g_date_time_new_from_unix_utc ((gint64)timestamp);
    if (!dt) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid unix time: %" G_GUINT64_FORMAT,
                     timestamp);
        return NULL;
    }

    return date_time_format_iso8601 (dt);
}

gchar *
mm_new_iso8601_time (guint    year,
                     guint    month,
                     guint    day,
                     guint    hour,
                     guint    minute,
                     guint    second,
                     gboolean have_offset,
                     gint     offset_minutes,
                     GError **error)
{
    g_autoptr(GDateTime) dt = NULL;

    if (have_offset) {
        g_autoptr(GTimeZone) tz = NULL;
#if GLIB_CHECK_VERSION (2, 58, 0)
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        tz = g_time_zone_new_offset (offset_minutes * 60);
        G_GNUC_END_IGNORE_DEPRECATIONS
#else
        g_autofree gchar *identifier = NULL;

        identifier = g_strdup_printf ("%c%02u:%02u:00",
                                      (offset_minutes >= 0) ? '+' : '-',
                                      ABS (offset_minutes) / 60,
                                      ABS (offset_minutes) % 60);

        tz = g_time_zone_new (identifier);
#endif
        dt = g_date_time_new (tz, year, month, day, hour, minute, second);
    } else
        dt = g_date_time_new_utc (year, month, day, hour, minute, second);

    if (!dt) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid date: year: %u, month: %u, day: %u, hour: %u, minute: %u, second: %u",
                     year, month, day, hour, minute, second);
        return NULL;
    }
    return date_time_format_iso8601 (dt);
}

/*****************************************************************************/

/* From hostap, Copyright (c) 2002-2005, Jouni Malinen <jkmaline@cc.hut.fi> */

static gint
hex2num (gchar c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

gint
mm_utils_hex2byte (const gchar *hex)
{
    gint a, b;

    a = hex2num (*hex++);
    if (a < 0)
        return -1;
    b = hex2num (*hex++);
    if (b < 0)
        return -1;
    return (a << 4) | b;
}

guint8 *
mm_utils_hexstr2bin (const gchar  *hex,
                     gssize        len,
                     gsize        *out_len,
                     GError      **error)
{
    const gchar *ipos = hex;
    g_autofree guint8 *buf = NULL;
    gssize i;
    gint a;
    guint8 *opos;

    if (len < 0)
        len = strlen (hex);

    if (len == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Hex conversion failed: empty string");
        return NULL;
    }

    /* Length must be a multiple of 2 */
    if ((len % 2) != 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "Hex conversion failed: invalid input length");
        return NULL;
    }

    opos = buf = g_malloc0 (len / 2);
    for (i = 0; i < len; i += 2) {
        a = mm_utils_hex2byte (ipos);
        if (a < 0) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "Hex byte conversion from '%c%c' failed",
                         ipos[0], ipos[1]);
            return NULL;
        }
        *opos++ = (guint8)a;
        ipos += 2;
    }
    *out_len = len / 2;
    return g_steal_pointer (&buf);
}

/* End from hostap */

gboolean
mm_utils_ishexstr (const gchar *hex)
{
    gsize len;
    gsize i;

    /* Empty string or length not multiple of 2? */
    len = strlen (hex);
    if (len == 0 || (len % 2) != 0)
        return FALSE;

    for (i = 0; i < len; i++) {
        /* Non-hex char? */
        if (hex[i] >= '0' && hex[i] <= '9')
            continue;
        if (hex[i] >= 'a' && hex[i] <= 'f')
            continue;
        if (hex[i] >= 'A' && hex[i] <= 'F')
            continue;
        return FALSE;
    }

    return TRUE;
}

gchar *
mm_utils_bin2hexstr (const guint8 *bin,
                     gsize         len)
{
    GString *ret;
    gsize i;

    g_return_val_if_fail (bin != NULL, NULL);

    ret = g_string_sized_new (len * 2 + 1);
    for (i = 0; i < len; i++)
        g_string_append_printf (ret, "%.2X", bin[i]);
    return g_string_free (ret, FALSE);
}

gboolean
mm_utils_check_for_single_value (guint32 value)
{
    gboolean found = FALSE;
    guint32 i;

    for (i = 1; i <= 32; i++) {
        if (value & 0x1) {
            if (found)
                return FALSE;  /* More than one bit set */
            found = TRUE;
        }
        value >>= 1;
    }

    return TRUE;
}

/*****************************************************************************/

gboolean
mm_is_string_mccmnc (const gchar *str)
{
    gsize len;
    guint i;

    if (!str)
        return FALSE;

    len = strlen (str);
    if (len < 5 || len > 6)
        return FALSE;

    for (i = 0; i < len; i++)
        if (str[i] < '0' || str[i] > '9')
            return FALSE;

    return TRUE;
}

/*****************************************************************************/

const gchar *
mm_sms_delivery_state_get_string_extended (guint delivery_state)
{
    if (delivery_state > 0x02 && delivery_state < 0x20) {
        if (delivery_state < 0x10)
            return "completed-reason-reserved";
        else
            return "completed-sc-specific-reason";
    }

    if (delivery_state > 0x25 && delivery_state < 0x40) {
        if (delivery_state < 0x30)
            return "temporary-error-reason-reserved";
        else
            return "temporary-error-sc-specific-reason";
    }

    if (delivery_state > 0x49 && delivery_state < 0x60) {
        if (delivery_state < 0x50)
            return "error-reason-reserved";
        else
            return "error-sc-specific-reason";
    }

    if (delivery_state > 0x65 && delivery_state < 0x80) {
        if (delivery_state < 0x70)
            return "temporary-fatal-error-reason-reserved";
        else
            return "temporary-fatal-error-sc-specific-reason";
    }

    if (delivery_state >= 0x80 && delivery_state < 0x100)
        return "unknown-reason-reserved";

    if (delivery_state >= 0x100)
        return "unknown";

    /* Otherwise, use the MMSmsDeliveryState enum as we can match the known
     * value */
    return mm_sms_delivery_state_get_string ((MMSmsDeliveryState)delivery_state);
}

/*****************************************************************************/

const gchar *
mm_common_str_boolean (gboolean value)
{
    return value ? "yes" : "no";
}

const gchar *
mm_common_str_personal_info (const gchar *str,
                             gboolean     show_personal_info)
{
    static const gchar *hidden_personal_info = "###";

    return show_personal_info ? str : hidden_personal_info;
}

void
mm_common_str_array_human_keys (GPtrArray *array)
{
    guint i;

    /* Transforms from:
     *   strings-as-keys: value...
     * Into:
     *   strings as keys: value...
     */
    for (i = 0; i < array->len; i++) {
        gchar *str;
        guint j;

        str = g_ptr_array_index (array, i);
        for (j = 0; str[j] && str[j] != ':'; j++) {
            if (str[j] == '-')
                str[j] = ' ';
        }
    }
}

/*****************************************************************************/
/* DBus error handling */

gboolean
mm_common_register_errors (void)
{
    static volatile guint32 aux = 0;

    if (G_LIKELY (aux))
        return FALSE;

    /* Register all known own errors */
    aux |= MM_CORE_ERROR;
    aux |= MM_MOBILE_EQUIPMENT_ERROR;
    aux |= MM_CONNECTION_ERROR;
    aux |= MM_SERIAL_ERROR;
    aux |= MM_MESSAGE_ERROR;
    aux |= MM_CDMA_ACTIVATION_ERROR;
    aux |= MM_CARRIER_LOCK_ERROR;

    return TRUE;
}

GError *
mm_common_error_from_tuple (GVariant  *tuple,
                            GError   **error)
{
    g_autoptr(GError)  dbus_error = NULL;
    g_autofree gchar  *error_name = NULL;
    g_autofree gchar  *error_message = NULL;

    mm_common_register_errors ();

    if (!g_variant_is_of_type (tuple, G_VARIANT_TYPE ("(ss)"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create error from tuple: "
                     "invalid variant type received");
        return NULL;
    }

    g_variant_get (tuple, "(ss)", &error_name, &error_message);
    if (!error_name || !error_name[0])
        return NULL;

    /* We convert the error name into a proper GError (domain+code), but we
     * don't attempt to give the error message to new_for_dbus_error() as that
     * would generate a string we don't want (e.g. instead of just "Unknown
     * Error" we would get "GDBus.Error:org.freedesktop.ModemManager1.Error.MobileEquipment.Unknown: Unknown error"
     */
    dbus_error = g_dbus_error_new_for_dbus_error (error_name, "");

    /* And now we build a new GError with same domain+code but with the received
     * error message */
    return g_error_new (dbus_error->domain, dbus_error->code, "%s", error_message);
}

GVariant *
mm_common_error_to_tuple (const GError *error)
{
    g_autofree gchar *error_name = NULL;
    GVariant         *tuple[2];

    mm_common_register_errors ();

    error_name = g_dbus_error_encode_gerror (error);
    tuple[0] = g_variant_new_string (error_name);
    tuple[1] = g_variant_new_string (error->message);

    return g_variant_ref_sink (g_variant_new_tuple (tuple, 2));
}
