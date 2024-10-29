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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include <math.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-quectel.h"

/*****************************************************************************/
/* Test ^CTZU test responses */

typedef struct {
    const gchar *response;
    gboolean     expect_supports_disable;
    gboolean     expect_supports_enable;
    gboolean     expect_supports_enable_update_rtc;
    gboolean     expect_error;
} TestCtzuResponse;

static const TestCtzuResponse test_ctzu_response[] = {
    { "+CTZU: ",        FALSE, FALSE, FALSE, TRUE  },
    { "+CTZU: ()",      FALSE, FALSE, FALSE, TRUE  },
    { "+CTZU: (,)",     FALSE, FALSE, FALSE, TRUE  },
    { "+CTZU: (0,1)",   TRUE,  TRUE,  FALSE, FALSE },
    { "+CTZU: (0,1,3)", TRUE,  TRUE,  TRUE,  FALSE },
};

static void
common_test_ctzu (const gchar *response,
                  gboolean     expect_supports_disable,
                  gboolean     expect_supports_enable,
                  gboolean     expect_supports_enable_update_rtc,
                  gboolean     expect_error)
{
    g_autoptr(GError) error = NULL;
    gboolean          res;
    gboolean          supports_disable = FALSE;
    gboolean          supports_enable = FALSE;
    gboolean          supports_enable_update_rtc = FALSE;

    res = mm_quectel_parse_ctzu_test_response (response,
                                               NULL,
                                               &supports_disable,
                                               &supports_enable,
                                               &supports_enable_update_rtc,
                                               &error);
    if (expect_error) {
        g_assert (error);
        g_assert (!res);
    } else {
        g_assert_no_error (error);
        g_assert (res);

        g_assert_cmpuint (expect_supports_disable,           ==, supports_disable);
        g_assert_cmpuint (expect_supports_enable,            ==, supports_enable);
        g_assert_cmpuint (expect_supports_enable_update_rtc, ==, supports_enable_update_rtc);
    }
}

static void
test_ctzu (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (test_ctzu_response); i++)
        common_test_ctzu (test_ctzu_response[i].response,
                          test_ctzu_response[i].expect_supports_disable,
                          test_ctzu_response[i].expect_supports_enable,
                          test_ctzu_response[i].expect_supports_enable_update_rtc,
                          test_ctzu_response[i].expect_error);
}

/*****************************************************************************/
/* Test ^FIRMVERSION test responses */
static void
test_firmversion (void)
{
    gboolean valid = TRUE;

    valid = mm_quectel_check_standard_firmware_version_valid ("EM05GFAR07A07M1G_01.016.01.016");
    g_assert_cmpuint (valid, ==, TRUE);

    valid = mm_quectel_check_standard_firmware_version_valid ("EM05GFAR07A07M1G_01.016.00.000");
    g_assert_cmpuint (valid, ==, FALSE);
}

static void
test_parse_revision (void)
{
    gboolean valid;
    guint release;
    guint minor;

    valid = mm_quectel_get_version_from_revision ("EM05GFAR07A07M1G_01.016.01.016", &release, &minor, NULL);
    g_assert_cmpuint (valid, ==, TRUE);
    g_assert_cmpuint (release, ==, 7);
    g_assert_cmpuint (minor, ==, 7);

    valid = mm_quectel_get_version_from_revision ("EM05GFAR10A02M1G", &release, &minor, NULL);
    g_assert_cmpuint (valid, ==, TRUE);
    g_assert_cmpuint (release, ==, 10);
    g_assert_cmpuint (minor, ==, 2);

    valid = mm_quectel_get_version_from_revision ("EM05GFAR07AM1G", &release, &minor, NULL);
    g_assert_cmpuint (valid, ==, FALSE);

    valid = mm_quectel_get_version_from_revision ("EM05GFARA07M1G", &release, &minor, NULL);
    g_assert_cmpuint (valid, ==, FALSE);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/quectel/ctzu", test_ctzu);

    g_test_add_func ("/MM/quectel/firmversion", test_firmversion);

    g_test_add_func ("/MM/quectel/parse_revision", test_parse_revision);

    return g_test_run ();
}
