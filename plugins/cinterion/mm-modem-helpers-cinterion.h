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

/*****************************************************************************/
/* ^SCFG test parser */

gboolean mm_cinterion_parse_scfg_test (const gchar *response,
                                       MMModemCharset charset,
                                       GArray **supported_bands,
                                       GError **error);

/*****************************************************************************/
/* ^SCFG response parser */

gboolean mm_cinterion_parse_scfg_response (const gchar *response,
                                           MMModemCharset charset,
                                           GArray **bands,
                                           GError **error);

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

gboolean mm_cinterion_build_band (GArray *bands,
                                  guint supported,
                                  gboolean only_2g,
                                  guint *out_band,
                                  GError **error);

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
                                                            GError      **error);

/*****************************************************************************/
/* ^SMONG response parser */

gboolean mm_cinterion_parse_smong_response (const gchar              *response,
                                            MMModemAccessTechnology  *access_tech,
                                            GError                  **error);

/*****************************************************************************/
/* ^SIND psinfo helper */

MMModemAccessTechnology mm_cinterion_get_access_technology_from_sind_psinfo (guint val);

/*****************************************************************************/
/* ^SLCC URC helpers */

GRegex *mm_cinterion_get_slcc_regex (void);

/* MMCallInfo list management */
gboolean mm_cinterion_parse_slcc_list     (const gchar  *str,
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

#endif  /* MM_MODEM_HELPERS_CINTERION_H */
