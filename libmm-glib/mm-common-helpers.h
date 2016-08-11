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

gchar *mm_common_build_capabilities_string (const MMModemCapability *capabilities,
                                            guint n_capabilities);

gchar *mm_common_build_bands_string (const MMModemBand *bands,
                                     guint n_bands);

gchar *mm_common_build_ports_string (const MMModemPortInfo *ports,
                                     guint n_ports);

gchar *mm_common_build_sms_storages_string (const MMSmsStorage *storages,
                                            guint n_storages);

gchar *mm_common_build_mode_combinations_string (const MMModemModeCombination *modes,
                                                 guint n_modes);

MMModemCapability     mm_common_get_capabilities_from_string (const gchar *str,
                                                              GError **error);
MMModemMode           mm_common_get_modes_from_string        (const gchar *str,
                                                              GError **error);
void                  mm_common_get_bands_from_string        (const gchar *str,
                                                              MMModemBand **bands,
                                                              guint *n_bands,
                                                              GError **error);
gboolean              mm_common_get_boolean_from_string      (const gchar *value,
                                                              GError **error);
MMModemCdmaRmProtocol mm_common_get_rm_protocol_from_string  (const gchar *str,
                                                              GError **error);
MMBearerIpFamily      mm_common_get_ip_type_from_string      (const gchar *str,
                                                              GError **error);
MMBearerAllowedAuth   mm_common_get_allowed_auth_from_string (const gchar *str,
                                                              GError **error);
MMSmsStorage          mm_common_get_sms_storage_from_string  (const gchar *str,
                                                              GError **error);
MMSmsCdmaTeleserviceId   mm_common_get_sms_cdma_teleservice_id_from_string   (const gchar *str,
                                                                              GError **error);
MMSmsCdmaServiceCategory mm_common_get_sms_cdma_service_category_from_string (const gchar *str,
                                                                              GError **error);

MMCallDirection     mm_common_get_call_direction_from_string    (const gchar *str,
                                                                 GError **error);
MMCallState         mm_common_get_call_state_from_string        (const gchar *str,
                                                                 GError **error);
MMCallStateReason   mm_common_get_call_state_reason_from_string (const gchar *str,
                                                                 GError **error);

MMOmaFeature          mm_common_get_oma_features_from_string (const gchar *str,
                                                              GError **error);
MMOmaSessionType      mm_common_get_oma_session_type_from_string (const gchar *str,
                                                                  GError **error);

GArray          *mm_common_ports_variant_to_garray (GVariant *variant);
MMModemPortInfo *mm_common_ports_variant_to_array  (GVariant *variant,
                                                 guint *n_ports);
GVariant        *mm_common_ports_array_to_variant  (const MMModemPortInfo *ports,
                                                    guint n_ports);
GVariant        *mm_common_ports_garray_to_variant (GArray *array);

GArray       *mm_common_sms_storages_variant_to_garray (GVariant *variant);
MMSmsStorage *mm_common_sms_storages_variant_to_array  (GVariant *variant,
                                                        guint *n_storages);
GVariant     *mm_common_sms_storages_array_to_variant  (const MMSmsStorage *storages,
                                                        guint n_storages);
GVariant     *mm_common_sms_storages_garray_to_variant (GArray *array);

GArray      *mm_common_bands_variant_to_garray (GVariant *variant);
MMModemBand *mm_common_bands_variant_to_array  (GVariant *variant,
                                                guint *n_bands);
GVariant    *mm_common_bands_array_to_variant  (const MMModemBand *bands,
                                                guint n_bands);
GVariant    *mm_common_bands_garray_to_variant (GArray *array);

GVariant    *mm_common_build_bands_any     (void);
GVariant    *mm_common_build_bands_unknown (void);

gboolean     mm_common_bands_garray_cmp  (GArray *a, GArray *b);
void         mm_common_bands_garray_sort (GArray *array);

GArray                 *mm_common_mode_combinations_variant_to_garray (GVariant *variant);
MMModemModeCombination *mm_common_mode_combinations_variant_to_array  (GVariant *variant,
                                                                       guint *n_modes);
GVariant               *mm_common_mode_combinations_array_to_variant  (const MMModemModeCombination *modes,
                                                                       guint n_modes);
GVariant               *mm_common_mode_combinations_garray_to_variant (GArray *array);
GVariant               *mm_common_build_mode_combinations_default     (void);

GArray            *mm_common_capability_combinations_variant_to_garray (GVariant *variant);
MMModemCapability *mm_common_capability_combinations_variant_to_array  (GVariant *variant,
                                                                        guint *n_capabilities);
GVariant          *mm_common_capability_combinations_array_to_variant  (const MMModemCapability *capabilities,
                                                                        guint n_capabilities);
GVariant          *mm_common_capability_combinations_garray_to_variant (GArray *array);
GVariant          *mm_common_build_capability_combinations_any         (void);
GVariant          *mm_common_build_capability_combinations_none        (void);

GArray                              *mm_common_oma_pending_network_initiated_sessions_variant_to_garray (GVariant *variant);
MMOmaPendingNetworkInitiatedSession *mm_common_oma_pending_network_initiated_sessions_variant_to_array  (GVariant *variant,
                                                                                                         guint *n_modes);
GVariant                            *mm_common_oma_pending_network_initiated_sessions_array_to_variant  (const MMOmaPendingNetworkInitiatedSession *modes,
                                                                                                         guint n_modes);
GVariant                            *mm_common_oma_pending_network_initiated_sessions_garray_to_variant (GArray *array);
GVariant                            *mm_common_build_oma_pending_network_initiated_sessions_default     (void);

typedef gboolean (*MMParseKeyValueForeachFn) (const gchar *key,
                                              const gchar *value,
                                              gpointer user_data);
gboolean mm_common_parse_key_value_string (const gchar *str,
                                           GError **error,
                                           MMParseKeyValueForeachFn callback,
                                           gpointer user_data);

/* Common parsers */
gboolean  mm_get_int_from_str                    (const gchar *str,
                                                  gint *out);
gboolean  mm_get_int_from_match_info             (GMatchInfo *match_info,
                                                  guint32 match_index,
                                                  gint *out);
gboolean  mm_get_uint_from_str                   (const gchar *str,
                                                  guint *out);
gboolean  mm_get_uint_from_hex_str               (const gchar *str,
                                                  guint       *out);
gboolean  mm_get_uint_from_match_info            (GMatchInfo *match_info,
                                                  guint32 match_index,
                                                  guint *out);
gboolean  mm_get_double_from_str                 (const gchar *str,
                                                  gdouble *out);
gboolean  mm_get_double_from_match_info          (GMatchInfo *match_info,
                                                  guint32 match_index,
                                                  gdouble *out);
gchar    *mm_get_string_unquoted_from_match_info (GMatchInfo *match_info,
                                                  guint32 match_index);

const gchar *mm_sms_delivery_state_get_string_extended (guint delivery_state);

gint      mm_utils_hex2byte   (const gchar *hex);
gchar    *mm_utils_hexstr2bin (const gchar *hex, gsize *out_len);
gchar    *mm_utils_bin2hexstr (const guint8 *bin, gsize len);
gboolean  mm_utils_ishexstr   (const gchar *hex);

gboolean  mm_utils_check_for_single_value (guint32 value);

#endif /* MM_COMMON_HELPERS_H */
