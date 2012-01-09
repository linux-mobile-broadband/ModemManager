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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_MODEM_HELPERS_H
#define MM_MODEM_HELPERS_H

#include <ModemManager.h>

#include "mm-modem-cdma.h"
#include "mm-charsets.h"

#define MM_MODEM_CAPABILITY_3GPP_LTE    \
    (MM_MODEM_CAPABILITY_LTE |          \
     MM_MODEM_CAPABILITY_LTE_ADVANCED)

#define MM_MODEM_CAPABILITY_3GPP        \
    (MM_MODEM_CAPABILITY_GSM_UMTS |     \
     MM_MODEM_CAPABILITY_3GPP_LTE)

/* Network scan results expected */
typedef struct {
    MMModem3gppNetworkAvailability status;
    gchar *operator_long;
    gchar *operator_short;
    gchar *operator_code; /* mandatory */
    MMModemAccessTechnology access_tech;
} MM3gppNetworkInfo;

void mm_3gpp_network_info_list_free (GList *info_list);
GList *mm_3gpp_parse_scan_response (const gchar *reply,
                                    GError **error);

/* PDP context query results */
typedef struct {
    guint cid;
    gchar *pdp_type;
    gchar *apn;
} MM3gppPdpContext;

void mm_3gpp_pdp_context_list_free (GList *pdp_list);
GList *mm_3gpp_parse_pdp_query_response (const gchar *reply,
                                         GError **error);

GPtrArray *mm_3gpp_creg_regex_get (gboolean solicited);
void mm_3gpp_creg_regex_destroy (GPtrArray *array);
gboolean mm_3gpp_parse_creg_response (GMatchInfo *info,
                                      MMModem3gppRegistrationState *out_reg_state,
                                      gulong *out_lac,
                                      gulong *out_ci,
                                      MMModemAccessTechnology *out_act,
                                      gboolean *out_cgreg,
                                      GError **error);

GRegex *mm_3gpp_ciev_regex_get (void);

const char *mm_strip_tag (const char *str, const char *cmd);

gboolean mm_cdma_parse_spservice_response (const char *reply,
                                           MMModemCdmaRegistrationState *out_cdma_1x_state,
                                           MMModemCdmaRegistrationState *out_evdo_state);

gboolean mm_cdma_parse_eri (const char *reply,
                            gboolean *out_roaming,
                            guint32 *out_ind,
                            const char **out_desc);

MMModemCdmaRmProtocol mm_cdma_get_rm_protocol_from_index (guint index,
                                                          GError **error);
guint mm_cdma_get_index_from_rm_protocol (MMModemCdmaRmProtocol protocol,
                                          GError **error);

gboolean mm_cdma_parse_crm_range_response (const gchar *reply,
                                           MMModemCdmaRmProtocol *min,
                                           MMModemCdmaRmProtocol *max,
                                           GError **error);

gboolean mm_gsm_parse_cscs_support_response (const char *reply,
                                             MMModemCharset *out_charsets);

gboolean mm_gsm_parse_clck_test_response (const char *reply,
                                          MMModemGsmFacility *out_facilities);
gboolean mm_gsm_parse_clck_response (const char *reply,
                                     gboolean *enabled);

gchar *mm_3gpp_parse_operator (const gchar *reply,
                               MMModemCharset cur_charset);

char *mm_gsm_get_facility_name (MMModemGsmFacility facility);

MMModemGsmAccessTech mm_gsm_string_to_access_tech (const char *string);

char *mm_create_device_identifier (guint vid,
                                   guint pid,
                                   const char *ati,
                                   const char *ati1,
                                   const char *gsn,
                                   const char *revision,
                                   const char *model,
                                   const char *manf);

typedef struct CindResponse CindResponse;
GHashTable *mm_parse_cind_test_response (const char *reply, GError **error);
const char *cind_response_get_desc      (CindResponse *r);
guint       cind_response_get_index     (CindResponse *r);
gint        cind_response_get_min       (CindResponse *r);
gint        cind_response_get_max       (CindResponse *r);

GByteArray *mm_parse_cind_query_response(const char *reply, GError **error);

#define MM_MODEM_CDMA_SID_UNKNOWN 99999
#define MM_MODEM_CDMA_NID_UNKNOWN 99999

gint  mm_cdma_normalize_class (const gchar *orig_class);
gchar mm_cdma_normalize_band  (const gchar *long_band,
                               gint *out_class);
gint  mm_cdma_convert_sid     (const gchar *sid);

guint mm_count_bits_set (gulong number);

#endif  /* MM_MODEM_HELPERS_H */
