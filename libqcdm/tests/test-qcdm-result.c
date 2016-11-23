/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <string.h>

#include "test-qcdm-result.h"
#include "result.h"
#include "result-private.h"

#define TEST_TAG "test"

void
test_result_string (void *f, void *data)
{
    const char *str = "foobarblahblahblah";
    const char *tmp = NULL;
    QcdmResult *result;

    result = qcdm_result_new ();
    qcdm_result_add_string (result, TEST_TAG, str);

    qcdm_result_get_string (result, TEST_TAG, &tmp);
    g_assert (tmp);
    g_assert (strcmp (tmp, str) == 0);

    qcdm_result_unref (result);
}

void
test_result_uint32 (void *f, void *data)
{
    guint32 num = 0xDEADBEEF;
    guint32 tmp = 0;
    QcdmResult *result;

    result = qcdm_result_new ();
    qcdm_result_add_u32 (result, TEST_TAG, num);

    qcdm_result_get_u32 (result, TEST_TAG, &tmp);
    g_assert_cmpint (tmp, ==, num);

    qcdm_result_unref (result);
}

void
test_result_uint8 (void *f, void *data)
{
    guint8 num = 0x1E;
    guint8 tmp = 0;
    QcdmResult *result;

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, TEST_TAG, num);

    qcdm_result_get_u8 (result, TEST_TAG, &tmp);
    g_assert (tmp == num);

    qcdm_result_unref (result);
}

void
test_result_uint8_array (void *f, void *data)
{
    uint8_t array[] = { 0, 1, 255, 32, 128, 127 };
    const uint8_t *tmp = NULL;
    size_t tmp_len = 0;
    QcdmResult *result;

    result = qcdm_result_new ();
    qcdm_result_add_u8_array (result, TEST_TAG, array, sizeof (array));

    qcdm_result_get_u8_array (result, TEST_TAG, &tmp, &tmp_len);
    g_assert_cmpint (tmp_len, ==, sizeof (array));
    g_assert_cmpint (memcmp (tmp, array, tmp_len), ==, 0);

    qcdm_result_unref (result);
}
