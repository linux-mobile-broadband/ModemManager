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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>
#include <ModemManager.h>
#include "mm-helper-types.h"

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#ifndef MM_COMMON_HELPERS_H
#define MM_COMMON_HELPERS_H

/******************************************************************************/
/* Enums/flags to string builders */

gchar *mm_common_build_capabilities_string      (const MMModemCapability      *capabilities,
                                                 guint                         n_capabilities);
gchar *mm_common_build_bands_string             (const MMModemBand            *bands,
                                                 guint                         n_bands);
gchar *mm_common_build_ports_string             (const MMModemPortInfo        *ports,
                                                 guint                         n_ports);
gchar *mm_common_build_sms_storages_string      (const MMSmsStorage           *storages,
                                                 guint                         n_storages);
gchar *mm_common_build_mode_combinations_string (const MMModemModeCombination *modes,
                                                 guint                         n_modes);

/******************************************************************************/
/* String to enums/flags parsers */

MMModemCapability             mm_common_get_capabilities_from_string              (const gchar  *str,
                                                                                   GError      **error);
MMModemMode                   mm_common_get_modes_from_string                     (const gchar  *str,
                                                                                   GError      **error);
gboolean                      mm_common_get_bands_from_string                     (const gchar  *str,
                                                                                   MMModemBand **bands,
                                                                                   guint        *n_bands,
                                                                                   GError      **error);
gboolean                      mm_common_get_boolean_from_string                   (const gchar  *value,
                                                                                   GError      **error);
MMModemCdmaRmProtocol         mm_common_get_rm_protocol_from_string               (const gchar  *str,
                                                                                   GError      **error);
MMBearerIpFamily              mm_common_get_ip_type_from_string                   (const gchar  *str,
                                                                                   GError      **error);
MMBearerAllowedAuth           mm_common_get_allowed_auth_from_string              (const gchar  *str,
                                                                                   GError      **error);
MMSmsStorage                  mm_common_get_sms_storage_from_string               (const gchar  *str,
                                                                                   GError      **error);
MMSmsCdmaTeleserviceId        mm_common_get_sms_cdma_teleservice_id_from_string   (const gchar  *str,
                                                                                   GError      **error);
MMSmsCdmaServiceCategory      mm_common_get_sms_cdma_service_category_from_string (const gchar  *str,
                                                                                   GError      **error);
MMCallDirection               mm_common_get_call_direction_from_string            (const gchar  *str,
                                                                                   GError      **error);
MMCallState                   mm_common_get_call_state_from_string                (const gchar  *str,
                                                                                   GError      **error);
MMCallStateReason             mm_common_get_call_state_reason_from_string         (const gchar  *str,
                                                                                   GError      **error);
MMOmaFeature                  mm_common_get_oma_features_from_string              (const gchar  *str,
                                                                                   GError      **error);
MMOmaSessionType              mm_common_get_oma_session_type_from_string          (const gchar  *str,
                                                                                   GError      **error);
MMModem3gppEpsUeModeOperation mm_common_get_eps_ue_mode_operation_from_string     (const gchar  *str,
                                                                                   GError      **error);
MMModemAccessTechnology       mm_common_get_access_technology_from_string         (const gchar  *str,
                                                                                   GError      **error);
MMBearerMultiplexSupport      mm_common_get_multiplex_support_from_string         (const gchar  *str,
                                                                                   GError      **error);
MMBearerApnType               mm_common_get_apn_type_from_string                  (const gchar  *str,
                                                                                   GError      **error);
MMModem3gppFacility           mm_common_get_3gpp_facility_from_string             (const gchar  *str,
                                                                                   GError      **error);
MMModem3gppPacketServiceState mm_common_get_3gpp_packet_service_state_from_string (const gchar  *str,
                                                                                   GError      **error);
MMModem3gppMicoMode           mm_common_get_3gpp_mico_mode_from_string            (const gchar  *str,
                                                                                   GError      **error);
MMModem3gppDrxCycle           mm_common_get_3gpp_drx_cycle_from_string            (const gchar  *str,
                                                                                   GError      **error);
MMBearerAccessTypePreference  mm_common_get_access_type_preference_from_string    (const gchar  *str,
                                                                                   GError      **error);
MMBearerProfileSource         mm_common_get_profile_source_from_string            (const gchar  *str,
                                                                                   GError      **error);

/******************************************************************************/

/* MMModemPortInfo array management */

GArray   *mm_common_ports_variant_to_garray (GVariant              *variant);
GVariant *mm_common_ports_array_to_variant  (const MMModemPortInfo *ports,
                                             guint                  n_ports);
GVariant *mm_common_ports_garray_to_variant (GArray                *array);
gboolean  mm_common_ports_garray_to_array   (GArray                 *array,
                                             MMModemPortInfo       **ports,
                                             guint                  *n_ports);

/* MMSmsStorage array management */

GArray   *mm_common_sms_storages_variant_to_garray (GVariant           *variant);
GVariant *mm_common_sms_storages_array_to_variant  (const MMSmsStorage *storages,
                                                    guint               n_storages);
GVariant *mm_common_sms_storages_garray_to_variant (GArray             *array);

/* MMModemCapability array management */

GArray   *mm_common_capability_combinations_variant_to_garray (GVariant                *variant);
GVariant *mm_common_capability_combinations_array_to_variant  (const MMModemCapability *capabilities,
                                                               guint                    n_capabilities);
GVariant *mm_common_capability_combinations_garray_to_variant (GArray                  *array);
GVariant *mm_common_build_capability_combinations_none        (void);

/* MMModemModeCombination array management */

GArray   *mm_common_mode_combinations_variant_to_garray (GVariant                     *variant);
GVariant *mm_common_mode_combinations_array_to_variant  (const MMModemModeCombination *modes,
                                                         guint                         n_modes);
GVariant *mm_common_mode_combinations_garray_to_variant (GArray                       *array);
GVariant *mm_common_build_mode_combinations_default     (void);

/* MMModemBand array management */

GArray   *mm_common_bands_variant_to_garray (GVariant          *variant);
GVariant *mm_common_bands_array_to_variant  (const MMModemBand *bands,
                                             guint              n_bands);
GVariant *mm_common_bands_garray_to_variant (GArray            *array);
GVariant *mm_common_build_bands_any         (void);
GVariant *mm_common_build_bands_unknown     (void);
gboolean  mm_common_bands_garray_cmp        (GArray            *a,
                                             GArray            *b);
void      mm_common_bands_garray_sort       (GArray            *array);
gboolean  mm_common_bands_garray_lookup     (GArray            *array,
                                             MMModemBand        value);
gboolean  mm_common_band_is_gsm             (MMModemBand        band);
gboolean  mm_common_band_is_utran           (MMModemBand        band);
gboolean  mm_common_band_is_eutran          (MMModemBand        band);
gboolean  mm_common_band_is_cdma            (MMModemBand        band);

/* MMOmaPendingNetworkInitiatedSession array management */

GArray   *mm_common_oma_pending_network_initiated_sessions_variant_to_garray (GVariant                                  *variant);
GVariant *mm_common_oma_pending_network_initiated_sessions_array_to_variant  (const MMOmaPendingNetworkInitiatedSession *modes,
                                                                              guint                                      n_modes);
GVariant *mm_common_oma_pending_network_initiated_sessions_garray_to_variant (GArray                                    *array);
GVariant *mm_common_build_oma_pending_network_initiated_sessions_default     (void);

/******************************************************************************/

const gchar *mm_common_str_boolean          (gboolean     value);
const gchar *mm_common_str_personal_info    (const gchar *str,
                                             gboolean     show_personal_info);
void         mm_common_str_array_human_keys (GPtrArray   *array);

/******************************************************************************/
/* Common parsers */

typedef gboolean (* MMParseKeyValueForeachFn)        (const gchar               *key,
                                                      const gchar               *value,
                                                      gpointer                   user_data);
gboolean            mm_common_parse_key_value_string (const gchar               *str,
                                                      GError                   **error,
                                                      MMParseKeyValueForeachFn   callback,
                                                      gpointer                   user_data);

gboolean  mm_get_int_from_str                    (const gchar *str,
                                                  gint        *out);
gboolean  mm_get_int_from_match_info             (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  gint        *out);
gboolean  mm_get_uint_from_str                   (const gchar *str,
                                                  guint       *out);
gboolean  mm_get_u64_from_str                    (const gchar *str,
                                                  guint64     *out);
gboolean  mm_get_uint_from_hex_str               (const gchar *str,
                                                  guint       *out);
gboolean  mm_get_u64_from_hex_str                (const gchar *str,
                                                  guint64     *out);
gboolean  mm_get_uint_from_match_info            (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  guint       *out);
gboolean  mm_get_u64_from_match_info             (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  guint64     *out);
gboolean  mm_get_uint_from_hex_match_info        (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  guint       *out);
gboolean  mm_get_u64_from_hex_match_info         (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  guint64     *out);
gboolean  mm_get_double_from_str                 (const gchar *str,
                                                  gdouble     *out);
gboolean  mm_get_double_from_match_info          (GMatchInfo  *match_info,
                                                  guint32      match_index,
                                                  gdouble     *out);
gchar    *mm_get_string_unquoted_from_match_info (GMatchInfo  *match_info,
                                                  guint32      match_index);

gchar    *mm_new_iso8601_time_from_unix_time     (guint64    timestamp,
                                                  GError   **error);
gchar    *mm_new_iso8601_time                    (guint      year,
                                                  guint      month,
                                                  guint      day,
                                                  guint      hour,
                                                  guint      minute,
                                                  guint      second,
                                                  gboolean   have_offset,
                                                  gint       offset_minutes,
                                                  GError   **error);

/******************************************************************************/
/* Type checkers and conversion utilities */

gint      mm_utils_hex2byte   (const gchar *hex);
guint8   *mm_utils_hexstr2bin (const gchar *hex, gssize len, gsize *out_len, GError **error);
gchar    *mm_utils_bin2hexstr (const guint8 *bin, gsize len);
gboolean  mm_utils_ishexstr   (const gchar *hex);

gboolean  mm_utils_check_for_single_value (guint32 value);

gboolean  mm_is_string_mccmnc (const gchar *str);

const gchar *mm_sms_delivery_state_get_string_extended (guint delivery_state);

/******************************************************************************/
/* DBus error handling */
gboolean  mm_common_register_errors  (void);
GError   *mm_common_error_from_tuple (GVariant      *tuple,
                                      GError       **error);
GVariant *mm_common_error_to_tuple   (const GError  *error);

#endif /* MM_COMMON_HELPERS_H */
