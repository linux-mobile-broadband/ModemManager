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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2016 Trimble Navigation Limited
 * Copyright (C) 2016 Matthew Stanger <matthew_stanger@trimble.com>
 * Copyright (C) 2019 Purism SPC
 */

#ifndef MM_MODEM_HELPERS_CINTERION_H
#define MM_MODEM_HELPERS_CINTERION_H

#include <glib.h>

#include <ModemManager.h>
#include <mm-base-bearer.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

typedef enum {
    MM_CINTERION_MODEM_FAMILY_DEFAULT  = 0,
    MM_CINTERION_MODEM_FAMILY_IMT      = 1,
} MMCinterionModemFamily;

typedef enum {
    MM_CINTERION_RADIO_BAND_FORMAT_SINGLE = 0,
    MM_CINTERION_RADIO_BAND_FORMAT_MULTIPLE  = 1,
} MMCinterionRadioBandFormat;

typedef enum {
    MM_CINTERION_RB_BLOCK_LEGACY   = 0,
    MM_CINTERION_RB_BLOCK_GSM      = 0,
    MM_CINTERION_RB_BLOCK_UMTS     = 1,
    MM_CINTERION_RB_BLOCK_LTE_LOW  = 2,
    MM_CINTERION_RB_BLOCK_LTE_HIGH = 3,
    MM_CINTERION_RB_BLOCK_N        = 4
} MMCinterionRbBlock;

typedef enum {
    MM_CINTERION_RADIO_GEN_NONE    = 0,
    MM_CINTERION_RADIO_GEN_2G      = 2,
    MM_CINTERION_RADIO_GEN_3G      = 3,
    MM_CINTERION_RADIO_GEN_4G      = 4,
} MMCinterionRadioGen;

/*****************************************************************************/
/* ^SCFG test parser */

gboolean mm_cinterion_parse_scfg_test (const gchar                   *response,
                                       MMCinterionModemFamily         modem_family,
                                       MMModemCharset                 charset,
                                       GArray                       **supported_bands,
                                       MMCinterionRadioBandFormat    *format,
                                       GError                       **error);

/*****************************************************************************/
/* ^SCFG response parser */

gboolean mm_cinterion_parse_scfg_response (const gchar                   *response,
                                           MMCinterionModemFamily         modem_family,
                                           MMModemCharset                 charset,
                                           GArray                       **bands,
                                           MMCinterionRadioBandFormat     format,
                                           GError                       **error);

/*****************************************************************************/
/* +CNMI test parser */

gboolean mm_cinterion_parse_cnmi_test (const gchar *response,
                                       GArray **supported_mode,
                                       GArray **supported_mt,
                                       GArray **supported_bm,
                                       GArray **supported_ds,
                                       GArray **supported_bfr,
                                       GError **error);

/*****************************************************************************/
/* Build Cinterion-specific band value */

gboolean mm_cinterion_build_band (GArray                       *bands,
                                  guint                        *supported,
                                  gboolean                      only_2g,
                                  MMCinterionRadioBandFormat    format,
                                  MMCinterionModemFamily        modem_family,
                                  guint                        *out_band,
                                  GError                      **error);

/*****************************************************************************/
/* Single ^SIND response parser */

gboolean mm_cinterion_parse_sind_response (const gchar *response,
                                           gchar **description,
                                           guint *mode,
                                           guint *value,
                                           GError **error);

/*****************************************************************************/
/* ^SWWAN response parser */

MMBearerConnectionStatus mm_cinterion_parse_swwan_response (const gchar  *response,
                                                            guint         swwan_index,
                                                            gpointer      log_object,
                                                            GError      **error);

/*****************************************************************************/
/* ^SGAUTH response parser */

gboolean mm_cinterion_parse_sgauth_response (const gchar          *response,
                                             guint                 cid,
                                             MMBearerAllowedAuth  *out_auth,
                                             gchar               **out_username,
                                             GError              **error);

/*****************************************************************************/
/* ^SMONG response parser */

gboolean mm_cinterion_parse_smong_response (const gchar              *response,
                                            MMModemAccessTechnology  *access_tech,
                                            GError                  **error);

/*****************************************************************************/
/* ^SIND psinfo helper */

MMModemAccessTechnology mm_cinterion_get_access_technology_from_sind_psinfo (guint    val,
                                                                             gpointer log_object);

/*****************************************************************************/
/* ^SLCC URC helpers */

GRegex *mm_cinterion_get_slcc_regex (void);

/* MMCallInfo list management */
gboolean mm_cinterion_parse_slcc_list     (const gchar  *str,
                                           gpointer      log_object,
                                           GList       **out_list,
                                           GError      **error);
void     mm_cinterion_call_info_list_free (GList        *call_info_list);

/*****************************************************************************/
/* +CTZU URC helpers */

GRegex   *mm_cinterion_get_ctzu_regex (void);
gboolean  mm_cinterion_parse_ctzu_urc (GMatchInfo         *match_info,
                                       gchar             **iso8601p,
                                       MMNetworkTimezone **tzp,
                                       GError            **error);

/*****************************************************************************/
/* ^SMONI helper */

gboolean mm_cinterion_parse_smoni_query_response (const gchar           *response,
                                                  MMCinterionRadioGen   *out_tech,
                                                  gdouble               *out_rssi,
                                                  gdouble               *out_ecn0,
                                                  gdouble               *out_rscp,
                                                  gdouble               *out_rsrp,
                                                  gdouble               *out_rsrq,
                                                  GError               **error);

gboolean mm_cinterion_smoni_response_to_signal_info (const gchar  *response,
                                                     MMSignal    **out_gsm,
                                                     MMSignal    **out_umts,
                                                     MMSignal    **out_lte,
                                                     GError      **error);

/*****************************************************************************/
/* ^SCFG="MEopMode/Prov/Cfg" helper */

gboolean mm_cinterion_provcfg_response_to_cid (const gchar             *response,
                                               MMCinterionModemFamily   modem_family,
                                               MMModemCharset           charset,
                                               gpointer                 log_object,
                                               gint                    *cid,
                                               GError                 **error);

/*****************************************************************************/
/* Auth related helpers */

MMBearerAllowedAuth mm_auth_type_from_cinterion_auth_type (guint cinterion_auth);

gchar *mm_cinterion_build_auth_string (gpointer                log_object,
                                       MMCinterionModemFamily  modem_family,
                                       MMBearerProperties     *config,
                                       guint                   cid);

#endif  /* MM_MODEM_HELPERS_CINTERION_H */
