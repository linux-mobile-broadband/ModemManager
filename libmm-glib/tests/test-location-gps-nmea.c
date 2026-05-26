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
 * Copyright (C) 2026 Dan Williams <dan@ioncontrol.co>
 * Copyright (C) 2026 Andrey Skvortsov <andrej.skvortzov@gmail.com>
 */

#include <glib.h>
#include <libmm-glib.h>
#include <string.h>

static gchar *
combine_traces (const gchar *traces[],
                const guint  num_traces,
                gboolean     trailing_crlf)
{
    guint i;
    GString *all = g_string_new ("");

    for (i = 0; i < num_traces; i++) {
        g_string_append (all, traces[i]);
        if (i != num_traces - 1) {
            g_string_append_c (all, '\r');
            g_string_append_c (all, '\n');
        }
    }

    return g_string_free (all, FALSE);
}

static gchar *
get_trace_type (const gchar *trace)
{
    gchar *trace_type = g_strdup (trace);
    gchar *p;

    p = strchr (trace_type, ',');
    g_assert (p);
    *p = '\0';
    return trace_type;
}

/**************************************************************/

static void
test_round_trip (void)
{
    MMLocationGpsNmea *nmea;
    g_autofree gchar *combined_traces;
    g_autofree gchar **all_traces;
    GError *error = NULL;
    guint traces_len;
    guint i, j;

    #define TRACE0 "$GNGGA,001626.000,,,,,0,00,25.5,,,,,,*79"
    #define TRACE1 "$GPGSV,2,1,08,04,41,189,,07,49,311,28,08,67,149,25,09,55,237,18,0*68"
    #define TRACE2 "$GPGSV,2,2,08,16,31,057,,21,05,153,,27,56,070,,30,19,303,,0*61"
    #define TRACE3 "$BDGSV,1,1,00,0*74"
    #define TRACE4 "$GNGGA,001626.000,,,,,0,00,25.5,,,,,,*79"

    static const char *traces[] = { TRACE0, TRACE1, TRACE2, TRACE3, TRACE4 };

    static const char *expected_traces[] = {
        TRACE1 "\r\n" TRACE2,
        TRACE3,
        TRACE4,
    };

    combined_traces = combine_traces (traces, G_N_ELEMENTS (traces), TRUE);
    nmea = mm_location_gps_nmea_new_from_string_variant (g_variant_new_string (combined_traces), &error);
    g_assert_no_error (error);

    for (i = 0; i < G_N_ELEMENTS (expected_traces); i++) {
        g_autofree gchar *trace_type = get_trace_type (expected_traces[i]);
        const gchar *     trace;

        trace = mm_location_gps_nmea_get_trace (nmea, trace_type);
        g_assert_cmpstr (trace, ==, expected_traces[i]);
    }

    all_traces = mm_location_gps_nmea_get_traces (nmea);
    traces_len = g_strv_length (all_traces);
    g_assert_cmpint (traces_len, ==, G_N_ELEMENTS (expected_traces));

    /* For each expected trace type, assert that trace type exists in the
     * found traces, and matches the expected trace.
     */
    for (i = 0; i < G_N_ELEMENTS (expected_traces); i++) {
        g_autofree gchar *expected_type = get_trace_type (expected_traces[i]);
        gboolean found = FALSE;

        for (j = 0; j < traces_len; j++) {
            g_autofree gchar *found_type = get_trace_type (all_traces[j]);

            if (!g_strcmp0 (found_type, expected_type)) {
                /* Found the trace type; compare */
                g_assert_cmpstr (all_traces[j], ==, expected_traces[i]);
                found = TRUE;
            }
        }
        g_assert_true (found);
    }
}

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4

/**************************************************************/

static void
test_append_sequence (void)
{
    MMLocationGpsNmea *nmea;
    GError *error = NULL;
    g_autofree gchar *combined = NULL;

    #define TRACE0 "$GNGGA,001626.000,,,,,0,00,25.5,,,,,,*79"
    #define TRACE1 "$GPGSV,3,1,12,01,35,282,,02,68,264,,03,02,243,,08,35,206,,1*68"
    #define TRACE2 "$GPGSV,3,2,12,10,46,074,,14,16,312,,17,,,,22,16,327,,1*53"
    #define TRACE3 "$GPGSV,3,3,12,23,07,068,,24,15,030,,27,14,181,,32,51,120,,1*60"
    #define TRACE4 "$BDGSV,1,1,00,0*74"
    #define TRACE5 "$GNGGA,001626.000,,,,,0,00,25.6,,,,,,*79"

    #define GPGSV_TYPE "$GPGSV"
    #define BDGSV_TYPE "$BDGSV"
    #define GNGGA_TYPE "$GNGGA"

    static const gchar *gpgsvs[] = { TRACE1, TRACE2, TRACE3 };

    nmea = mm_location_gps_nmea_new_from_string_variant (g_variant_new_string (TRACE0), &error);
    g_assert_no_error (error);

    /* No part of the GPGSV sequence should be found in traces until a new
     * trace type is added.
     */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE1));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE2));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE3));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);

    /* Add a new type, but it's also a sequence so it won't be found yet */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE4));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, BDGSV_TYPE), ==, NULL);

    /* New type was just added, now we should find the $GPGSV too */
    combined = combine_traces (gpgsvs, G_N_ELEMENTS (gpgsvs), FALSE);
    g_assert (combined);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined);

    /* Add a non-sequence trace */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE5));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GNGGA_TYPE), ==, TRACE5);

    /* And now we should find the $BDGSV */
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, BDGSV_TYPE), ==, TRACE4);
}

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4
#undef TRACE5

/**************************************************************/

static void
test_append_sequence_interleaved (void)
{
    MMLocationGpsNmea *nmea;
    GError *error = NULL;
    g_autofree gchar *combined_start = NULL;
    g_autofree gchar *combined_full = NULL;

    #define TRACE0 "$GNGGA,001626.000,,,,,0,00,25.5,,,,,,*79"
    #define TRACE1 "$GPGSV,3,1,12,01,35,282,,02,68,264,,03,02,243,,08,35,206,,1*68"
    #define TRACE2 "$GPGSV,3,2,12,10,46,074,,14,16,312,,17,,,,22,16,327,,1*53"
    #define TRACE3 "$BDGSV,1,1,00,0*74"
    #define TRACE4 "$GPGSV,3,3,12,23,07,068,,24,15,030,,27,14,181,,32,51,120,,1*60"
    #define TRACE5 "$GNGGA,001626.000,,,,,0,00,25.6,,,,,,*79"

    #define GPGSV_TYPE "$GPGSV"
    #define BDGSV_TYPE "$BDGSV"
    #define GNGGA_TYPE "$GNGGA"

    static const gchar *gpgsvs_start[] = { TRACE1, TRACE2 };
    static const gchar *gpgsvs_full[] = { TRACE1, TRACE2, TRACE4 };

    nmea = mm_location_gps_nmea_new_from_string_variant (g_variant_new_string (TRACE0), &error);
    g_assert_no_error (error);

    /* No part of the GPGSV sequence should be found in traces until a new
     * trace type is added.
     */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE1));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE2));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);

    /* Add a new type, but it's also a sequence so it won't be found yet */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE3));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, BDGSV_TYPE), ==, NULL);

    /* New type was just added, now first part of $GPGSV should be available */
    combined_start = combine_traces (gpgsvs_start, G_N_ELEMENTS (gpgsvs_start), FALSE);
    g_assert (combined_start);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined_start);

    /* Add third GPGSV, but it won't be found yet, only previous GPGSV records
     * But $BDGSV should be found now.
     */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE4));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined_start);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, BDGSV_TYPE), ==, TRACE3);

    /* Add a non-sequence trace */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE5));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GNGGA_TYPE), ==, TRACE5);

    /* New type was just added, now we should find full $GPGSV sequence */
    combined_full = combine_traces (gpgsvs_full, G_N_ELEMENTS (gpgsvs_full), FALSE);
    g_assert (combined_full);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined_full);

}

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4
#undef TRACE5

/**************************************************************/

static void
test_append_multiple_sequences (void)
{
    MMLocationGpsNmea *nmea;
    GError *error = NULL;
    g_autofree gchar *combined1 = NULL;
    g_autofree gchar *combined2 = NULL;

    #define TRACE0 "$GPGSV,3,1,12,01,35,282,,02,68,264,,03,02,243,,08,35,206,,1*68"
    #define TRACE1 "$GPGSV,3,2,12,10,46,074,,14,16,312,,17,,,,22,16,327,,1*53"
    #define TRACE2 "$GPGSV,3,3,12,23,07,068,,24,15,030,,27,14,181,,32,51,120,,1*60"
    #define TRACE3 "$GPGSV,3,1,12,01,35,282,23,02,68,264,,03,02,243,,08,35,206,,1*69"
    #define TRACE4 "$GPGSV,3,2,12,10,46,074,28,14,16,312,,17,,,,22,16,327,,1*59"
    #define TRACE5 "$GPGSV,3,3,12,23,07,068,18,24,15,030,,27,14,181,,32,51,120,,1*69"
    #define TRACE6 "$GPGSV,3,1,12,01,35,282,25,02,68,264,,03,02,243,,08,35,206,,1*6F"

    #define GPGSV_TYPE "$GPGSV"

    static const gchar *gpgsvs1[] = { TRACE0, TRACE1, TRACE2 };
    static const gchar *gpgsvs2[] = { TRACE3, TRACE4, TRACE5 };

    /* No part of the GPGSV sequence should be found in traces until a new
     * trace sequence is added.
     */
    nmea = mm_location_gps_nmea_new_from_string_variant (g_variant_new_string (TRACE0), &error);
    g_assert_no_error (error);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE1));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE2));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);

    /* Add first record for next sequence, old sequence should be available */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE3));
    combined1 = combine_traces (gpgsvs1, G_N_ELEMENTS (gpgsvs1), FALSE);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined1);

    /* No part of new the GPGSV sequence should be found in traces until a new
     * trace sequence is added.
     */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE4));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined1);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE5));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined1);

    /* Add first record for next sequence, updated sequence should be available */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE6));
    combined2 = combine_traces (gpgsvs2, G_N_ELEMENTS (gpgsvs2), FALSE);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined2);
}

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4
#undef TRACE5
#undef TRACE6

/**************************************************************/

static void
test_start_from_incomplete_sequence (void)
{
    MMLocationGpsNmea *nmea;
    GError *error = NULL;
    g_autofree gchar *combined = NULL;

    #define TRACE0 "$GPGSV,3,3,12,23,07,068,,24,15,030,,27,14,181,,32,51,120,,1*60"
    #define TRACE1 "$GNGGA,001626.000,,,,,0,00,25.5,,,,,,*79"
    #define TRACE2 "$GPGSV,3,1,12,01,35,282,,02,68,264,,03,02,243,,08,35,206,,1*68"
    #define TRACE3 "$GPGSV,3,2,12,10,46,074,,14,16,312,,17,,,,22,16,327,,1*53"
    #define TRACE4 "$GPGSV,3,3,12,23,07,068,,24,15,030,,27,14,181,,32,51,120,,1*60"
    #define TRACE5 "$GNGGA,001626.000,,,,,0,00,25.6,,,,,,*7A"

    #define GPGSV_TYPE "$GPGSV"
    #define GNGGA_TYPE "$GNGGA"

    static const gchar *gpgsvs[] = { TRACE2, TRACE3, TRACE4 };

    /* Start sequence not from the first record */
    nmea = mm_location_gps_nmea_new_from_string_variant (g_variant_new_string (TRACE0), &error);
    g_assert_no_error (error);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, NULL);

    /* New type was just added, now we should find $GPGSV sequence */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE1));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GNGGA_TYPE), ==, TRACE1);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, TRACE0);

    /* Add first record for next sequence, old sequence should be available */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE2));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, TRACE0);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE3));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, TRACE0);
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE4));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, TRACE0);

    /* New type was just added, now we should find new full $GPGSV sequence */
    g_assert (mm_location_gps_nmea_add_trace (nmea, TRACE5));
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GNGGA_TYPE), ==, TRACE5);

    combined = combine_traces (gpgsvs, G_N_ELEMENTS (gpgsvs), FALSE);
    g_assert_cmpstr (mm_location_gps_nmea_get_trace (nmea, GPGSV_TYPE), ==, combined);
}

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4
#undef TRACE5

/**************************************************************/

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/Location/GPS/NMEA/round-trip", test_round_trip);
    g_test_add_func ("/MM/Location/GPS/NMEA/append-sequence", test_append_sequence);
    g_test_add_func ("/MM/Location/GPS/NMEA/append-sequence-interleaved", test_append_sequence_interleaved);
    g_test_add_func ("/MM/Location/GPS/NMEA/append-multiple-sequences", test_append_multiple_sequences);
    g_test_add_func ("/MM/Location/GPS/NMEA/start-from-incomplete-sequence", test_start_from_incomplete_sequence);

    return g_test_run ();
}
