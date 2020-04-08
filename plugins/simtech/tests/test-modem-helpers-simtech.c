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

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-simtech.h"

/*****************************************************************************/
/* Test +CLCC URCs */

static void
common_test_clcc_urc (const gchar      *urc,
                      const MMCallInfo *expected_call_info_list,
                      guint             expected_call_info_list_size)
{
    GError     *error = NULL;
    GRegex     *clcc_regex = NULL;
    gboolean    result;
    GMatchInfo *match_info = NULL;
    gchar      *str;
    GList      *call_info_list = NULL;
    GList      *l;

    clcc_regex = mm_simtech_get_clcc_urc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (clcc_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    /* read full matched content */
    str = g_match_info_fetch (match_info, 0);
    g_assert (str);

    result = mm_simtech_parse_clcc_list (str, NULL, &call_info_list, &error);
    g_assert_no_error (error);
    g_assert (result);

    g_debug ("found %u calls", g_list_length (call_info_list));

    if (expected_call_info_list) {
        g_assert (call_info_list);
        g_assert_cmpuint (g_list_length (call_info_list), ==, expected_call_info_list_size);
    } else
        g_assert (!call_info_list);

    for (l = call_info_list; l; l = g_list_next (l)) {
        const MMCallInfo *call_info = (const MMCallInfo *)(l->data);
        gboolean                   found = FALSE;
        guint                      i;

        g_debug ("call at index %u: direction %s, state %s, number %s",
                 call_info->index,
                 mm_call_direction_get_string (call_info->direction),
                 mm_call_state_get_string (call_info->state),
                 call_info->number ? call_info->number : "n/a");

        for (i = 0; !found && i < expected_call_info_list_size; i++)
            found = ((call_info->index == expected_call_info_list[i].index) &&
                     (call_info->direction  == expected_call_info_list[i].direction) &&
                     (call_info->state  == expected_call_info_list[i].state) &&
                     (g_strcmp0 (call_info->number, expected_call_info_list[i].number) == 0));

        g_assert (found);
    }

    g_match_info_free (match_info);
    g_regex_unref (clcc_regex);
    g_free (str);

    mm_simtech_call_info_list_free (call_info_list);
}

static void
test_clcc_urc_single (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, (gchar *) "123456789" }
    };

    const gchar *urc =
        "\r\n+CLCC: 1,1,0,0,0,\"123456789\",161"
        "\r\n";

    common_test_clcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

static void
test_clcc_urc_multiple (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  NULL                  },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  (gchar *) "123456789" },
        { 3, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  (gchar *) "987654321" },
    };

    const gchar *urc =
        "\r\n+CLCC: 1,1,0,0,0" /* number unknown */
        "\r\n+CLCC: 2,1,0,0,0,\"123456789\",161"
        "\r\n+CLCC: 3,1,0,0,0,\"987654321\",161,\"Alice\""
        "\r\n";

    common_test_clcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

static void
test_clcc_urc_complex (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  (gchar *) "123456789" },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_WAITING, (gchar *) "987654321" },
    };

    const gchar *urc =
        "\r\n^CIEV: 1,0" /* some different URC before our match */
        "\r\n+CLCC: 1,1,0,0,0,\"123456789\",161"
        "\r\n+CLCC: 2,1,5,0,0,\"987654321\",161"
        "\r\n^CIEV: 1,0" /* some different URC after our match */
        "\r\n";

    common_test_clcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

/*****************************************************************************/

static void
common_test_voice_call_urc (const gchar *urc,
                            gboolean     expected_start_or_stop,
                            guint        expected_duration)
{
    GError     *error = NULL;
    gboolean    start_or_stop = FALSE; /* start = TRUE, stop = FALSE */
    guint       duration = 0;
    GRegex     *voice_call_regex = NULL;
    gboolean    result;
    GMatchInfo *match_info = NULL;

    voice_call_regex = mm_simtech_get_voice_call_urc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (voice_call_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    result = mm_simtech_parse_voice_call_urc (match_info, &start_or_stop, &duration, &error);
    g_assert_no_error (error);
    g_assert (result);

    g_assert_cmpuint (expected_start_or_stop, ==, start_or_stop);
    g_assert_cmpuint (expected_duration, ==, duration);

    g_match_info_free (match_info);
    g_regex_unref (voice_call_regex);
}

static void
test_voice_call_begin_urc (void)
{
    common_test_voice_call_urc ("\r\nVOICE CALL: BEGIN\r\n", TRUE, 0);
}

static void
test_voice_call_end_urc (void)
{
    common_test_voice_call_urc ("\r\nVOICE CALL: END\r\n", FALSE, 0);
}

static void
test_voice_call_end_duration_urc (void)
{
    common_test_voice_call_urc ("\r\nVOICE CALL: END: 000041\r\n", FALSE, 41);
}

/*****************************************************************************/

static void
common_test_missed_call_urc (const gchar *urc,
                             const gchar *expected_details)
{
    GError     *error = NULL;
    gchar      *details = NULL;
    GRegex     *missed_call_regex = NULL;
    gboolean    result;
    GMatchInfo *match_info = NULL;

    missed_call_regex = mm_simtech_get_missed_call_urc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (missed_call_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    result = mm_simtech_parse_missed_call_urc (match_info, &details, &error);
    g_assert_no_error (error);
    g_assert (result);

    g_assert_cmpstr (expected_details, ==, details);
    g_free (details);

    g_match_info_free (match_info);
    g_regex_unref (missed_call_regex);
}

static void
test_missed_call_urc (void)
{
    common_test_missed_call_urc ("\r\nMISSED_CALL: 11:01AM 07712345678\r\n", "11:01AM 07712345678");
}

/*****************************************************************************/

static void
common_test_cring_urc (const gchar *urc,
                       const gchar *expected_type)
{
    GError     *error = NULL;
    GRegex     *cring_regex = NULL;
    GMatchInfo *match_info = NULL;
    gchar      *type;
    gboolean    result;

    cring_regex = mm_simtech_get_cring_urc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (cring_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    type = g_match_info_fetch (match_info, 1);
    g_assert (type);

    g_assert_cmpstr (type, ==, expected_type);

    g_match_info_free (match_info);
    g_regex_unref (cring_regex);
    g_free (type);
}

static void
test_cring_urc_two_crs (void)
{
    common_test_cring_urc ("\r\r\n+CRING: VOICE\r\r\n", "VOICE");
}

static void
test_cring_urc_one_cr (void)
{
    common_test_cring_urc ("\r\n+CRING: VOICE\r\n", "VOICE");
}

/*****************************************************************************/

static void
common_test_rxdtmf_urc (const gchar *urc,
                        const gchar *expected_str)
{
    GError     *error = NULL;
    GRegex     *rxdtmf_regex = NULL;
    GMatchInfo *match_info = NULL;
    gchar      *type;
    gboolean    result;

    rxdtmf_regex = mm_simtech_get_rxdtmf_urc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (rxdtmf_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    type = g_match_info_fetch (match_info, 1);
    g_assert (type);

    g_assert_cmpstr (type, ==, expected_str);

    g_match_info_free (match_info);
    g_regex_unref (rxdtmf_regex);
    g_free (type);
}

static void
test_rxdtmf_urc_two_crs (void)
{
    common_test_rxdtmf_urc ("\r\r\n+RXDTMF: 8\r\r\n", "8");
    common_test_rxdtmf_urc ("\r\r\n+RXDTMF: *\r\r\n", "*");
    common_test_rxdtmf_urc ("\r\r\n+RXDTMF: #\r\r\n", "#");
    common_test_rxdtmf_urc ("\r\r\n+RXDTMF: A\r\r\n", "A");
}

static void
test_rxdtmf_urc_one_cr (void)
{
    common_test_rxdtmf_urc ("\r\n+RXDTMF: 8\r\n", "8");
    common_test_rxdtmf_urc ("\r\n+RXDTMF: *\r\n", "*");
    common_test_rxdtmf_urc ("\r\n+RXDTMF: #\r\n", "#");
    common_test_rxdtmf_urc ("\r\n+RXDTMF: A\r\n", "A");
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/simtech/clcc/urc/single",   test_clcc_urc_single);
    g_test_add_func ("/MM/simtech/clcc/urc/multiple", test_clcc_urc_multiple);
    g_test_add_func ("/MM/simtech/clcc/urc/complex",  test_clcc_urc_complex);

    g_test_add_func ("/MM/simtech/voicecall/urc/begin",        test_voice_call_begin_urc);
    g_test_add_func ("/MM/simtech/voicecall/urc/end",          test_voice_call_end_urc);
    g_test_add_func ("/MM/simtech/voicecall/urc/end-duration", test_voice_call_end_duration_urc);

    g_test_add_func ("/MM/simtech/missedcall/urc", test_missed_call_urc);

    g_test_add_func ("/MM/simtech/cring/urc/two-crs", test_cring_urc_two_crs);
    g_test_add_func ("/MM/simtech/cring/urc/one-cr", test_cring_urc_one_cr);

    g_test_add_func ("/MM/simtech/rxdtmf/urc/two-crs", test_rxdtmf_urc_two_crs);
    g_test_add_func ("/MM/simtech/rxdtmf/urc/one-cr", test_rxdtmf_urc_one_cr);

    return g_test_run ();
}
