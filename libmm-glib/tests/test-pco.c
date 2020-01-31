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
 * Copyright 2018 Google LLC.
 */

#include <glib.h>
#include <libmm-glib.h>
#include <string.h>

typedef struct {
    guint32 session_id;
    gboolean is_complete;
    gsize pco_data_size;
    guint8 pco_data[50];
} TestPco;

static const TestPco test_pco_list[] = {
    { 3, TRUE, 8, { 0x27, 0x06, 0x80, 0x00, 0x10, 0x02, 0x05, 0x94 } },
    { 1, FALSE, 3, { 0x27, 0x01, 0x80 } },
    { 5, FALSE, 10, { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    { 4, TRUE, 14, { 0x27, 0x0C, 0x80, 0x10, 0x02, 0x05, 0x94, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    { 3, FALSE, 10, { 0x27, 0x08, 0x80, 0x00, 0x0D, 0x04, 0xC6, 0xE0, 0xAD, 0x87 } },
};

static const TestPco expected_pco_list[] = {
    { 1, FALSE, 3, { 0x27, 0x01, 0x80 } },
    { 3, FALSE, 10, { 0x27, 0x08, 0x80, 0x00, 0x0D, 0x04, 0xC6, 0xE0, 0xAD, 0x87 } },
    { 4, TRUE, 14, { 0x27, 0x0C, 0x80, 0x10, 0x02, 0x05, 0x94, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    { 5, FALSE, 10, { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
};

static void
test_pco_list_add (void)
{
    GList *list = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (test_pco_list); ++i) {
        const TestPco *test_pco = &test_pco_list[i];
        MMPco *pco;

        pco = mm_pco_new ();
        mm_pco_set_session_id (pco, test_pco->session_id);
        mm_pco_set_complete (pco, test_pco->is_complete);
        mm_pco_set_data (pco, test_pco->pco_data, test_pco->pco_data_size);
        list = mm_pco_list_add (list, pco);
    }

    g_assert (list != NULL);
    g_assert_cmpuint (g_list_length (list), ==, G_N_ELEMENTS (expected_pco_list));

    for (i = 0; i < G_N_ELEMENTS (expected_pco_list); ++i) {
        GList *current;
        MMPco *pco;
        const TestPco *expected_pco;
        gsize pco_data_size;
        const guint8 *pco_data;

        current = g_list_nth (list, i);
        pco = current->data;
        expected_pco = &expected_pco_list[i];

        g_assert (pco != NULL);
        g_assert_cmpuint (mm_pco_get_session_id (pco), ==, expected_pco->session_id);
        g_assert (mm_pco_is_complete (pco) == expected_pco->is_complete);
        pco_data = mm_pco_get_data (pco, &pco_data_size);
        g_assert (pco_data != NULL);
        g_assert_cmpuint (pco_data_size, ==, expected_pco->pco_data_size);
        g_assert_cmpint (memcmp (pco_data, expected_pco->pco_data, pco_data_size), ==, 0);
    }

    g_list_free_full (list, g_object_unref);
}

/**************************************************************/

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/Pco/pco-list-add", test_pco_list_add);

    return g_test_run ();
}
