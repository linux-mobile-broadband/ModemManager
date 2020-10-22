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

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device-generic-rules.h"
#include "mm-log-test.h"

/************************************************************/

static void
common_test (const gchar *plugindir)
{
    GArray *rules;
    GError *error = NULL;

    if (!plugindir)
        return;

    rules = mm_kernel_device_generic_rules_load (plugindir, &error);
    g_assert_no_error (error);
    g_assert (rules);
    g_assert (rules->len > 0);

    g_array_unref (rules);
}

/* Dummy test to avoid compiler warning about common_test() being unused
 * when none of the plugins enabled in build have custom udev rules. */
static void
test_dummy (void)
{
    common_test (NULL);
}

/************************************************************/

#if defined ENABLE_PLUGIN_HUAWEI
static void
test_huawei (void)
{
    common_test (TESTUDEVRULESDIR_HUAWEI);
}
#endif

#if defined ENABLE_PLUGIN_MBM
static void
test_mbm (void)
{
    common_test (TESTUDEVRULESDIR_MBM);
}
#endif

#if defined ENABLE_PLUGIN_NOKIA_ICERA
static void
test_nokia_icera (void)
{
    common_test (TESTUDEVRULESDIR_NOKIA_ICERA);
}
#endif

#if defined ENABLE_PLUGIN_ZTE
static void
test_zte (void)
{
    common_test (TESTUDEVRULESDIR_ZTE);
}
#endif

#if defined ENABLE_PLUGIN_LONGCHEER
static void
test_longcheer (void)
{
    common_test (TESTUDEVRULESDIR_LONGCHEER);
}
#endif

#if defined ENABLE_PLUGIN_SIMTECH
static void
test_simtech (void)
{
    common_test (TESTUDEVRULESDIR_SIMTECH);
}
#endif

#if defined ENABLE_PLUGIN_X22X
static void
test_x22x (void)
{
    common_test (TESTUDEVRULESDIR_X22X);
}
#endif

#if defined ENABLE_PLUGIN_CINTERION
static void
test_cinterion (void)
{
    common_test (TESTUDEVRULESDIR_CINTERION);
}
#endif

#if defined ENABLE_PLUGIN_DELL
static void
test_dell (void)
{
    common_test (TESTUDEVRULESDIR_DELL);
}
#endif

#if defined ENABLE_PLUGIN_TELIT
static void
test_telit (void)
{
    common_test (TESTUDEVRULESDIR_TELIT);
}
#endif

#if defined ENABLE_PLUGIN_MTK
static void
test_mtk (void)
{
    common_test (TESTUDEVRULESDIR_MTK);
}
#endif

#if defined ENABLE_PLUGIN_HAIER
static void
test_haier (void)
{
    common_test (TESTUDEVRULESDIR_HAIER);
}
#endif

#if defined ENABLE_PLUGIN_FIBOCOM
static void
test_fibocom (void)
{
    common_test (TESTUDEVRULESDIR_FIBOCOM);
}
#endif

#if defined ENABLE_PLUGIN_QUECTEL
static void
test_quectel (void)
{
    common_test (TESTUDEVRULESDIR_QUECTEL);
}
#endif

#if defined ENABLE_PLUGIN_GOSUNCN
static void
test_gosuncn (void)
{
    common_test (TESTUDEVRULESDIR_GOSUNCN);
}
#endif

#if defined ENABLE_PLUGIN_QCOM_SOC && defined WITH_QMI
static void
test_qcom_soc (void)
{
    common_test (TESTUDEVRULESDIR_QCOM_SOC);
}
#endif

/************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/MM/test-udev-rules/dummy", test_dummy);

#if defined ENABLE_PLUGIN_HUAWEI
    g_test_add_func ("/MM/test-udev-rules/huawei", test_huawei);
#endif
#if defined ENABLE_PLUGIN_MBM
    g_test_add_func ("/MM/test-udev-rules/mbm", test_mbm);
#endif
#if defined ENABLE_PLUGIN_NOKIA_ICERA
    g_test_add_func ("/MM/test-udev-rules/nokia-icera", test_nokia_icera);
#endif
#if defined ENABLE_PLUGIN_ZTE
    g_test_add_func ("/MM/test-udev-rules/zte", test_zte);
#endif
#if defined ENABLE_PLUGIN_LONGCHEER
    g_test_add_func ("/MM/test-udev-rules/longcheer", test_longcheer);
#endif
#if defined ENABLE_PLUGIN_SIMTECH
    g_test_add_func ("/MM/test-udev-rules/simtech", test_simtech);
#endif
#if defined ENABLE_PLUGIN_X22X
    g_test_add_func ("/MM/test-udev-rules/x22x", test_x22x);
#endif
#if defined ENABLE_PLUGIN_CINTERION
    g_test_add_func ("/MM/test-udev-rules/cinterion", test_cinterion);
#endif
#if defined ENABLE_PLUGIN_DELL
    g_test_add_func ("/MM/test-udev-rules/dell", test_dell);
#endif
#if defined ENABLE_PLUGIN_TELIT
    g_test_add_func ("/MM/test-udev-rules/telit", test_telit);
#endif
#if defined ENABLE_PLUGIN_MTK
    g_test_add_func ("/MM/test-udev-rules/mtk", test_mtk);
#endif
#if defined ENABLE_PLUGIN_HAIER
    g_test_add_func ("/MM/test-udev-rules/haier", test_haier);
#endif
#if defined ENABLE_PLUGIN_FIBOCOM
    g_test_add_func ("/MM/test-udev-rules/fibocom", test_fibocom);
#endif
#if defined ENABLE_PLUGIN_QUECTEL
    g_test_add_func ("/MM/test-udev-rules/quectel", test_quectel);
#endif
#if defined ENABLE_PLUGIN_GOSUNCN
    g_test_add_func ("/MM/test-udev-rules/gosuncn", test_gosuncn);
#endif
#if defined ENABLE_PLUGIN_QCOM_SOC && defined WITH_QMI
    g_test_add_func ("/MM/test-udev-rules/qcom-soc", test_qcom_soc);
#endif

    return g_test_run ();
}
