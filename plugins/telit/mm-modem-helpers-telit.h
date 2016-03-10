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
 * Copyright (C) 2015 Telit.
 *
 */
#ifndef MM_MODEM_HELPERS_TELIT_H
#define MM_MODEM_HELPERS_TELIT_H

#include <glib.h>

#define MAX_BANDS_LIST_LEN 20

#define BND_FLAG_UNKNOWN -1

/* AT#BND 2G flags */
typedef enum {
    BND_FLAG_GSM900_DCS1800,
    BND_FLAG_GSM900_PCS1900,
    BND_FLAG_GSM850_DCS1800,
    BND_FLAG_GSM850_PCS1900,
} BndFlag2G;

/* AT#BND 3G flags */
typedef enum {
    BND_FLAG_0,    /* B1 (2100 MHz) */
    BND_FLAG_1,    /* B2 (1900 MHz) */
    BND_FLAG_2,    /* B5 (850 MHz) */
    BND_FLAG_3,    /* B1 (2100 MHz) + B2 (1900 MHz) + B5 (850 MHz) */
    BND_FLAG_4,    /* B2 (1900 MHz) + B5 (850 MHz) */
    BND_FLAG_5,    /* B8 (900 MHz) */
    BND_FLAG_6,    /* B1 (2100 MHz) + B8 (900 MHz) */
    BND_FLAG_7,    /* B4 (1700 MHz) */
    BND_FLAG_8,    /* B1 (2100 MHz) + B5 (850 MHz) */
    BND_FLAG_9,    /* B1 (2100 MHz) + B8 (900 MHz) + B5 (850 MHz) */
    BND_FLAG_10,   /* B2 (1900 MHz) + B4 (1700 MHz) + B5 (850 MHz) */
    BND_FLAG_12,   /* B6 (800 MHz) */
    BND_FLAG_13,   /* B3 (1800 MHz) */
    BND_FLAG_14,   /* B1 (2100 MHz) + B2 (1900 MHz) + B4 (1700 MHz) + B5 (850 MHz) + B6 (800MHz) */
    BND_FLAG_15,   /* B1 (2100 MHz) + B8 (900 MHz) + B3 (1800 MHz) */
    BND_FLAG_16,   /* B8 (900 MHz) + B5 (850 MHz) */
    BND_FLAG_17,   /* B2 (1900 MHz) + B4 (1700 MHz) + B5 (850 MHz) + B6 (800 MHz) */
    BND_FLAG_18,   /* B1 (2100 MHz) + B2 (1900 MHz) + B5 (850 MHz) + B6 (800 MHz) */
    BND_FLAG_19,   /* B2 (1900 MHz) + B6 (800 MHz) */
    BND_FLAG_20,   /* B5 (850 MHz) + B6 (800 MHz) */
    BND_FLAG_21,   /* B2 (1900 MHz) + B5 (850 MHz) + B6 (800 MHz) */
} BndFlag3G;

typedef struct {
    gint flag;
    MMModemBand mm_bands[MAX_BANDS_LIST_LEN];
} TelitToMMBandMap;

/* +CSIM response parser */
gint mm_telit_parse_csim_response (const guint step,
                                   const gchar *response,
                                   GError **error);

typedef enum {
    LOAD_SUPPORTED_BANDS,
    LOAD_CURRENT_BANDS
} MMTelitLoadBandsType;

/* #BND response parser */
gboolean
mm_telit_parse_bnd_response (const gchar *response,
                             gboolean modem_is_2g,
                             gboolean modem_is_3g,
                             gboolean modem_is_4g,
                             MMTelitLoadBandsType band_type,
                             GArray **supported_bands,
                             GError **error);


gboolean mm_telit_bands_contains (GArray *mm_bands, const MMModemBand mm_band);

gboolean mm_telit_update_band_array (const gint bands_flag,
                                     const TelitToMMBandMap *map,
                                     GArray **bands,
                                     GError **error);

gboolean mm_telit_get_band_flags_from_string (const gchar *flag_str, GArray **band_flags, GError **error);
gboolean mm_telit_get_2g_mm_bands(GMatchInfo *match_info, GArray **bands, GError **error);
gboolean mm_telit_get_3g_mm_bands(GMatchInfo *match_info, GArray **bands, GError **error);
gboolean mm_telit_get_4g_mm_bands(GMatchInfo *match_info, GArray **bands, GError **error);

gboolean mm_telit_update_2g_bands(gchar *band_list, GMatchInfo **match_info, GArray **bands, GError **error);
gboolean mm_telit_update_3g_bands(gchar *band_list, GMatchInfo **match_info, GArray **bands, GError **error);
gboolean mm_telit_update_4g_bands(GArray** bands, GMatchInfo *match_info, GError **error);

void mm_telit_get_band_flag (GArray *bands_array, gint *flag_2g, gint *flag_3g, gint *flag_4g);

#endif  /* MM_MODEM_HELPERS_TELIT_H */
