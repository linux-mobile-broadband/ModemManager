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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_SIMTECH_H
#define MM_MODEM_HELPERS_SIMTECH_H

#include <glib.h>

#include <ModemManager.h>
#include <mm-base-bearer.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

/*****************************************************************************/
/* +CLCC URC helpers */

gboolean mm_simtech_parse_clcc_test (const gchar  *response,
                                     gboolean     *clcc_urcs_supported,
                                     GError      **error);

GRegex   *mm_simtech_get_clcc_urc_regex  (void);
gboolean  mm_simtech_parse_clcc_list     (const gchar *str,
                                          gpointer     log_object,
                                          GList      **out_list,
                                          GError     **error);
void      mm_simtech_call_info_list_free (GList       *call_info_list);

/*****************************************************************************/
/* VOICE CALL URC helpers */

GRegex   *mm_simtech_get_voice_call_urc_regex (void);
gboolean  mm_simtech_parse_voice_call_urc     (GMatchInfo  *match_info,
                                               gboolean    *start_or_stop,
                                               guint       *duration,
                                               GError     **error);

/*****************************************************************************/
/* MISSED_CALL URC helpers */

GRegex   *mm_simtech_get_missed_call_urc_regex (void);
gboolean  mm_simtech_parse_missed_call_urc     (GMatchInfo  *match_info,
                                                gchar      **details,
                                                GError     **error);

/*****************************************************************************/
/* Non-standard CRING URC helpers */

GRegex *mm_simtech_get_cring_urc_regex (void);

/*****************************************************************************/
/* +RXDTMF URC helpers */

GRegex *mm_simtech_get_rxdtmf_urc_regex (void);

#endif  /* MM_MODEM_HELPERS_SIMTECH_H */
