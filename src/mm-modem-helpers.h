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

/* Network scan results expected */
typedef struct {
    MMModem3gppNetworkAvailability status;
    gchar *operator_long;
    gchar *operator_short;
    gchar *operator_code; /* mandatory */
    MMModemAccessTech access_tech;
} MM3gppNetworkInfo;

void mm_3gpp_network_info_list_free (GList *info_list);

GList *mm_3gpp_parse_scan_response (const gchar *reply,
                                    GError **error);

GPtrArray *mm_gsm_creg_regex_get (gboolean solicited);

void mm_gsm_creg_regex_destroy (GPtrArray *array);

gboolean mm_gsm_parse_creg_response (GMatchInfo *info,
                                     MMModem3gppRegistrationState *out_reg_state,
                                     gulong *out_lac,
                                     gulong *out_ci,
                                     MMModemAccessTech *out_act,
                                     gboolean *out_cgreg,
                                     GError **error);

const char *mm_strip_tag (const char *str, const char *cmd);

gboolean mm_cdma_parse_spservice_response (const char *reply,
                                           MMModemCdmaRegistrationState *out_cdma_1x_state,
                                           MMModemCdmaRegistrationState *out_evdo_state);

gboolean mm_cdma_parse_eri (const char *reply,
                            gboolean *out_roaming,
                            guint32 *out_ind,
                            const char **out_desc);

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

guint mm_count_bits_set (gulong number);

#endif  /* MM_MODEM_HELPERS_H */
