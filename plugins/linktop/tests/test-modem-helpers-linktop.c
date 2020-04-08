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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-linktop.h"

/*****************************************************************************/

typedef struct {
    const gchar *str;
    MMModemMode  allowed;
} CfunQueryCurrentModeTest;

static const CfunQueryCurrentModeTest cfun_query_current_mode_tests[] = {
    { "+CFUN: 0",   MM_MODEM_MODE_NONE                  },
    { "+CFUN: 1",   MM_MODEM_MODE_2G | MM_MODEM_MODE_3G },
    { "+CFUN: 4",   MM_MODEM_MODE_NONE                  },
    { "+CFUN: 5",   MM_MODEM_MODE_2G                    },
    { "+CFUN: 6",   MM_MODEM_MODE_3G                    },
};

static void
test_cfun_query_current_modes (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (cfun_query_current_mode_tests); i++) {
        GError      *error = NULL;
        gboolean     success;
        MMModemMode  allowed = MM_MODEM_MODE_NONE;

        success = mm_linktop_parse_cfun_query_current_modes (cfun_query_current_mode_tests[i].str, &allowed, &error);
        g_assert_no_error (error);
        g_assert (success);
        g_assert_cmpuint (cfun_query_current_mode_tests[i].allowed, ==, allowed);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/linktop/cfun/query/current-modes", test_cfun_query_current_modes);

    return g_test_run ();
}
