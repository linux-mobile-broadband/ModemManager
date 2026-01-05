/*****************************************************************************/
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
 * Copyright (C) 2026 Andrey Skvortsov <andrej.skvortzov@gmail.com>
 */

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-log-test.h"
#include "mm-location-cache.h"

/*****************************************************************************/

static const gdouble reset_lat = 0.1;
static const gdouble reset_lon = 0.2;

typedef struct {
    const gchar    *file_content;
    const gdouble   expect_lat;
    const gdouble   expect_lon;
    const gboolean  expect_success;
} LoadTestcase;

static const LoadTestcase load_testcases[] = {
    {
        .file_content = "[last_position]\n"
        "latitude=48.137\n"
        "longitude=11.575\n",
        .expect_lat = 48.137,
        .expect_lon = 11.575,
        .expect_success = TRUE,
    },
    {
        .file_content = "[last_position]\n"
        "longitude=11.575\n"
        "latitude=48.137\n",
        .expect_lat = 48.137,
        .expect_lon = 11.575,
        .expect_success = TRUE,
    },
    {
        .file_content = "[last_position]\n"
        "latitude=-27.142\n"
        "longitude=-109.314\n",
        .expect_lat = -27.142,
        .expect_lon = -109.314,
        .expect_success = TRUE,
    },
    {
        .file_content = "[last_position]\n"
        "latitude=48.137\n",
        .expect_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .expect_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_success = FALSE,
    },
    {
        .file_content = "[last_position]\n"
        "longitude=11.575\n",
        .expect_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .expect_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_success = FALSE,
    },
    {
        .file_content = "[last_position]\n",
        .expect_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .expect_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_success = FALSE,
    },
    {
        .file_content = "To be, or not to be, that is the question...\n",
        .expect_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .expect_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_success = FALSE,
    },
};

static void
test_location_cache_load (void)
{
    g_autoptr(MMLocationCache)  cache = NULL;
    gdouble                     get_lat;
    gdouble                     get_lon;
    g_autofree gchar           *filename = NULL;
    g_autoptr(GFile)            loc = NULL;
    g_autoptr(GIOStream)        stream = NULL;
    g_autoptr(GError)           err = NULL;
    gulong                      i;
    gboolean                    ret;

    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    loc = g_file_new_tmp (NULL, (GFileIOStream **)&stream, &err);
    filename = g_file_get_path (loc);
    g_assert_no_error (err);
    g_assert_nonnull (filename);

    mm_location_cache_set_filename (cache, filename);

    for (i = 0; i < G_N_ELEMENTS (load_testcases); i++) {
        g_autoptr(GError)  error = NULL;
        LoadTestcase       test = load_testcases[i];

        g_debug ("load test %ld", i);
        g_file_replace_contents (loc,
                                 test.file_content,
                                 strlen (test.file_content),
                                 NULL, /* etag */
                                 FALSE, /* make backup */
                                 G_FILE_CREATE_NONE, /* flags */
                                 NULL, /* new etag */
                                 NULL, /* cancellable */
                                 &error);
        g_assert_no_error (error);

        /* reset coordinates */
        mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);
        ret = mm_location_cache_load_from_file (cache, filename, &error);
        g_assert (ret == test.expect_success);
        g_assert (test.expect_success ? error == NULL : error != NULL);

        mm_location_cache_get_lat_lon (cache, &get_lat, &get_lon);
        g_assert_cmpfloat_with_epsilon (test.expect_lat, get_lat, 0.01);
        g_assert_cmpfloat_with_epsilon (test.expect_lon, get_lon, 0.01);
    }

    /* try to load missing file */
    g_file_delete (loc, NULL, NULL);
    mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);
    ret = mm_location_cache_load_from_file (cache, filename, &err);
    g_assert (err != NULL);

    mm_location_cache_get_lat_lon (cache, &get_lat, &get_lon);
    g_assert_cmpfloat_with_epsilon (MM_LOCATION_LATITUDE_UNKNOWN, get_lat, 0.01);
    g_assert_cmpfloat_with_epsilon (MM_LOCATION_LONGITUDE_UNKNOWN, get_lon, 0.01);
}

/*****************************************************************************/

typedef struct {
    const gchar    *nmea;
    const gdouble   set_lat;
    const gdouble   set_lon;
    const gdouble   expect_lat;
    const gdouble   expect_lon;
} UpdateTestcase;

static const UpdateTestcase update_testcases[] = {
    {
        .nmea = NULL,
        .set_lat = 48.137,
        .set_lon = 11.575,
        .expect_lat = 48.137,
        .expect_lon = 11.575,
    },
    {
        .nmea = NULL,
        .set_lat = 48.137,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    {
        .nmea = NULL,
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = 11.575,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    /* only GGA NMEA sentences are supported by current implementation */
    {
        .nmea = "$GNGLL,4808.2200,N,01134.5000,E,012345.000,A,A*40",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    {
        .nmea = "$GNRMC,012345.000,A,4808.2200,N,01134.5000,E,1.0,300.5,100817,3.6,E,A,V*6B",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    {
        .nmea = "$GNGGA,012345.000,4808.2200,N,01134.5000,E,1,12,1.1,-14.1,M,32.0,M,,*5E",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = 48.137,
        .expect_lon = 11.575,
    },
    {
        .nmea = NULL,
        .set_lat = -27.142,
        .set_lon = -109.314,
        .expect_lat = -27.142,
        .expect_lon = -109.314,
    },
    {
        .nmea = "$GNGLL,2708.5200,S,10918.8400,W,012345.000,A,A*4E",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    {
        .nmea = "$GNRMC,012345.000,A,2708.5200,S,10918.8400,W,1.0,300.5,100817,3.6,E,A,V*65",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = reset_lat,
        .expect_lon = reset_lon,
    },
    {
        .nmea = "$GNGGA,012345.000,2708.5200,S,10918.8400,W,1,12,1.1,-14.1,M,32.0,M,,*50",
        .set_lat = MM_LOCATION_LATITUDE_UNKNOWN,
        .set_lon = MM_LOCATION_LONGITUDE_UNKNOWN,
        .expect_lat = -27.142,
        .expect_lon = -109.314,
    },
};

static void
test_location_cache_update (void)
{
    g_autoptr(MMLocationCache)  cache = NULL;
    gulong                      i;

    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    for (i = 0; i < G_N_ELEMENTS (update_testcases); i++) {
        gdouble        get_lat = 0;
        gdouble        get_lon = 0;
        UpdateTestcase test = update_testcases[i];

        g_debug ("update test %ld", i);

        /* reset coordinates */
        mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);

        if (test.nmea)
            mm_location_cache_update_from_nmea (cache, test.nmea);
        else
            mm_location_cache_update_from_lat_lon (cache, test.set_lat, test.set_lon);
        mm_location_cache_get_lat_lon (cache, &get_lat, &get_lon);
        g_assert_cmpfloat_with_epsilon (test.expect_lat, get_lat, 0.01);
        g_assert_cmpfloat_with_epsilon (test.expect_lon, get_lon, 0.01);
    }
}

/*****************************************************************************/

static gchar *
get_temp_filename (void)
{
    g_autoptr(GError)  error = NULL;
    gchar             *filename;
    gint               fd;

    fd = g_file_open_tmp (NULL, &filename, &error);
    g_assert_no_error (error);
    g_assert_nonnull (filename);

    g_close (fd, &error);
    g_assert_no_error (error);

    g_unlink (filename);

    return filename;
}

static void
test_location_cache_save (void)
{
    g_autoptr(MMLocationCache)  cache = NULL;
    g_autoptr(GError)           error = NULL;
    g_autofree gchar           *filename = NULL;
    g_autofree gchar           *path = NULL;
    gboolean                    ret;

    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    filename = get_temp_filename ();

    // write empty cache
    g_unlink (filename);
    ret = mm_location_cache_save_to_file (cache, filename, &error);
    g_assert_true (ret);
    g_assert_no_error (error);

    ret = g_file_test (filename, G_FILE_TEST_EXISTS);
    g_assert_false (ret);

    // write non-empty cache
    g_unlink (filename);
    mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);
    ret = mm_location_cache_save_to_file (cache, filename, &error);
    g_assert_true (ret);
    g_assert_no_error (error);

    ret = g_file_test (filename, G_FILE_TEST_EXISTS);
    g_assert_true (ret);
    g_unlink (filename);

    // write to non-existing path
    path = g_build_path (G_DIR_SEPARATOR_S, "missing", "X", "Y", "Z", "location.ini", NULL);
    ret = mm_location_cache_save_to_file (cache, path, &error);
    g_assert_false (ret);
    g_assert (error != NULL);
}

/*****************************************************************************/

typedef struct {
    const gdouble   set_lat;
    const gdouble   set_lon;
} SaveLoadTestcase;

static const SaveLoadTestcase save_load_testcases[] = {
    {
        .set_lat = 48.137,
        .set_lon = 11.575,
    },
    {
        .set_lat = -27.142,
        .set_lon = -109.314,
    },
};

static void
test_location_cache_save_load (void)
{
    g_autoptr(MMLocationCache)  cache = NULL;
    g_autoptr(GError)           error = NULL;
    g_autofree gchar           *filename = NULL;
    gboolean                    ret;
    gulong                      i;

    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    filename = get_temp_filename ();
    mm_location_cache_set_filename (cache, filename);

    for (i = 0; i < G_N_ELEMENTS (save_load_testcases); i++) {
        gdouble        get_lat = 0;
        gdouble        get_lon = 0;
        SaveLoadTestcase test = save_load_testcases[i];

        g_debug ("save/load test %ld", i);

        mm_location_cache_update_from_lat_lon (cache, test.set_lat, test.set_lon);
        g_unlink (filename);
        ret = mm_location_cache_save (cache, &error);
        g_assert_true (ret);
        g_assert_no_error (error);

        ret = g_file_test (filename, G_FILE_TEST_EXISTS);
        g_assert_true (ret);

        mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);
        ret = mm_location_cache_load (cache, &error);
        g_assert_true (ret);
        g_assert_no_error (error);

        mm_location_cache_get_lat_lon (cache, &get_lat, &get_lon);
        g_assert_cmpfloat_with_epsilon (test.set_lat, get_lat, 0.01);
        g_assert_cmpfloat_with_epsilon (test.set_lon, get_lon, 0.01);
    }

    g_unlink (filename);
}

/*****************************************************************************/

static void
test_location_cache_create_dispose (void)
{
    MMLocationCache    *cache = NULL;
    g_autoptr(GError)   error = NULL;
    g_autofree gchar   *filename = NULL;
    gboolean            ret;
    gdouble             get_lat = 0;
    gdouble             get_lon = 0;

    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    // to check later, that MMLocationCache object is finalized
    g_object_add_weak_pointer (G_OBJECT (cache), (gpointer *) &cache);

    filename = get_temp_filename ();
    mm_location_cache_set_filename (cache, filename);
    mm_location_cache_update_from_lat_lon (cache, reset_lat, reset_lon);

    // drop the last reference and save cached location
    g_object_unref (cache);
    g_assert_null (cache);

    // check whether file is created
    ret = g_file_test (filename, G_FILE_TEST_EXISTS);
    g_assert_true (ret);

    // check file content
    cache = mm_location_cache_new ();
    g_assert_nonnull (cache);

    mm_location_cache_set_filename (cache, filename);
    ret = mm_location_cache_load (cache, &error);
    g_assert_true (ret);
    g_assert_no_error (error);

    mm_location_cache_get_lat_lon (cache, &get_lat, &get_lon);
    g_assert_cmpfloat_with_epsilon (reset_lat, get_lat, 0.01);
    g_assert_cmpfloat_with_epsilon (reset_lon, get_lon, 0.01);

    g_object_unref (cache);
    g_unlink (filename);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/location-cache/update",         test_location_cache_update);
    g_test_add_func ("/MM/location-cache/load",           test_location_cache_load);
    g_test_add_func ("/MM/location-cache/save",           test_location_cache_save);
    g_test_add_func ("/MM/location-cache/save_load",      test_location_cache_save_load);
    g_test_add_func ("/MM/location-cache/create_dispose", test_location_cache_create_dispose);

    return g_test_run ();
}
