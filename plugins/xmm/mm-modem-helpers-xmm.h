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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_MODEM_HELPERS_XMM_H
#define MM_MODEM_HELPERS_XMM_H

#include <glib.h>
#include <ModemManager.h>

/* AT+XACT=? response parser */
gboolean mm_xmm_parse_xact_test_response (const gchar  *response,
                                          gpointer      logger,
                                          GArray      **modes_out,
                                          GArray      **bands_out,
                                          GError      **error);

/* AT+XACT? response parser */
gboolean mm_xmm_parse_xact_query_response (const gchar             *response,
                                           MMModemModeCombination  *mode_out,
                                           GArray                 **bands_out,
                                           GError                 **error);

/* AT+XACT=X command builder */
gchar *mm_xmm_build_xact_set_command (const MMModemModeCombination  *mode,
                                      const GArray                  *bands,
                                      GError                       **error);

/* Mode to apply when ANY */
MMModemMode mm_xmm_get_modem_mode_any (const GArray *combinations);

gboolean mm_xmm_parse_xcesq_query_response (const gchar  *response,
                                            guint        *out_rxlev,
                                            guint        *out_ber,
                                            guint        *out_rscp,
                                            guint        *out_ecn0,
                                            guint        *out_rsrq,
                                            guint        *out_rsrp,
                                            gint         *out_rssnr,
                                            GError      **error);

gboolean mm_xmm_xcesq_response_to_signal_info (const gchar  *response,
                                               gpointer      log_object,
                                               MMSignal    **out_gsm,
                                               MMSignal    **out_umts,
                                               MMSignal    **out_lte,
                                               GError      **error);

/* AT+XLCSLSR=? response parser */
gboolean mm_xmm_parse_xlcslsr_test_response (const gchar  *response,
                                             gboolean     *transport_protocol_invalid_supported,
                                             gboolean     *transport_protocol_supl_supported,
                                             gboolean     *standalone_position_mode_supported,
                                             gboolean     *ms_assisted_based_position_mode_supported,
                                             gboolean     *loc_response_type_nmea_supported,
                                             gboolean     *gnss_type_gps_glonass_supported,
                                             GError      **error);

/* AT+XLCSSLP? response parser */
gboolean mm_xmm_parse_xlcsslp_query_response (const gchar  *response,
                                              gchar       **supl_address,
                                              GError      **error);

#endif  /* MM_MODEM_HELPERS_XMM_H */
