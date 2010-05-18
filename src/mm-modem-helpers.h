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

#include "mm-modem-cdma.h"
#include "mm-modem-gsm.h"
#include "mm-charsets.h"

#define MM_SCAN_TAG_STATUS "status"
#define MM_SCAN_TAG_OPER_LONG "operator-long"
#define MM_SCAN_TAG_OPER_SHORT "operator-short"
#define MM_SCAN_TAG_OPER_NUM "operator-num"
#define MM_SCAN_TAG_ACCESS_TECH "access-tech"

GPtrArray *mm_gsm_parse_scan_response (const char *reply, GError **error);

void mm_gsm_destroy_scan_data (gpointer data);

GPtrArray *mm_gsm_creg_regex_get (gboolean solicited);

void mm_gsm_creg_regex_destroy (GPtrArray *array);

gboolean mm_gsm_parse_creg_response (GMatchInfo *info,
                                     guint32 *out_reg_state,
                                     gulong *out_lac,
                                     gulong *out_ci,
                                     gint *out_act,
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

MMModemGsmAccessTech mm_gsm_string_to_access_tech (const char *string);

#endif  /* MM_MODEM_HELPERS_H */

