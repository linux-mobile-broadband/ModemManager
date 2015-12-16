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
 * Copyright (C) 2015 Telit
 *
 */
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-telit.h"

typedef struct {
    gchar* response;
    gint result;
} CSIMResponseTest;

static CSIMResponseTest valid_csim_response_test_list [] = {
    /* The parser expects that 2nd arg contains
     * substring "63Cx" where x is an HEX string
     * representing the retry value */
    {"+CSIM:8,\"xxxx63C1\"", 1},
    {"+CSIM:8,\"xxxx63CA\"", 10},
    {"+CSIM:8,\"xxxx63CF\"", 15},
    /* The parser accepts spaces */
    {"+CSIM:8,\"xxxx63C1\"", 1},
    {"+CSIM:8, \"xxxx63C1\"", 1},
    {"+CSIM: 8, \"xxxx63C1\"", 1},
    {"+CSIM:  8, \"xxxx63C1\"", 1},
    /* the parser expects an int as first argument (2nd arg's length),
     * but does not check if it is correct */
    {"+CSIM: 10, \"63CF\"", 15},
    /* the parser expect a quotation mark at the end
     * of the response, but not at the begin */
    {"+CSIM: 10, 63CF\"", 15},
    { NULL, -1}
};

static CSIMResponseTest invalid_csim_response_test_list [] = {
    /* Test missing final quotation mark */
    {"+CSIM: 8, xxxx63CF", -1},
    /* Negative test for substring "63Cx" */
    {"+CSIM: 4, xxxx73CF\"", -1},
    /* Test missing first argument */
    {"+CSIM:xxxx63CF\"", -1},
    { NULL, -1}
};

static void
test_parse_csim_response (void)
{
    const gint step = 1;
    guint i;
    gint res;
    GError* error = NULL;

    /* Test valid responses */
    for (i = 0; valid_csim_response_test_list[i].response != NULL; i++) {
        res = parse_csim_response (step, valid_csim_response_test_list[i].response, &error);

        g_assert_no_error (error);
        g_assert_cmpint (res, ==, valid_csim_response_test_list[i].result);
    }

    /* Test invalid responses */
    for (i = 0; invalid_csim_response_test_list[i].response != NULL; i++) {
        res = parse_csim_response (step, invalid_csim_response_test_list[i].response, &error);

        g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
        g_assert_cmpint (res, ==, invalid_csim_response_test_list[i].result);

        if (NULL != error) {
            g_error_free (error);
            error = NULL;
        }
    }
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/telit/csim", test_parse_csim_response);
    return g_test_run ();
}
