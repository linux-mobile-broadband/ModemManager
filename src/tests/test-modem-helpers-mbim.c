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
 * Copyright (c) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-modem-helpers-mbim.h"
#include "mm-log-test.h"

/*****************************************************************************/

typedef struct {
    const MbimDataClass data_class;
    const gdouble       rssi;
    const gdouble       rscp;
    const gdouble       ecio;
    const gdouble       sinr;
    const gdouble       io;
    const gdouble       rsrq;
    const gdouble       rsrp;
    const gdouble       snr;
    const gdouble       error_rate;
} ExpectedSignal;

typedef struct {
    const gchar    *detail;
    const gboolean  expect_success;

    const MbimDataClass   data_class;
    const guint           coded_rssi;
    const guint           coded_error_rate;
    const MbimRsrpSnrInfo rsrp_snr[2];

    const ExpectedSignal expected[2];
} SignalStateTestcase;

const SignalStateTestcase signal_tests[] = {
    { "5g-no-rsrp-snr",
      TRUE,
      MBIM_DATA_CLASS_5G_SA,
      16, 99,
      { { .system_type = MBIM_DATA_CLASS_NONE }, { .system_type = MBIM_DATA_CLASS_NONE } },
      { { MBIM_DATA_CLASS_5G_SA,
          -97,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
          MM_SIGNAL_UNKNOWN,
        },
        { MBIM_DATA_CLASS_NONE }
      },
    },
};

#if 0
typedef struct {
    guint32 rsrp;
    guint32 snr;
    guint32 rsrp_threshold;
    guint32 snr_threshold;
    guint32 system_type;
} MbimRsrpSnrInfo;
#endif

static MMSignal *
select_signal_for_data_class (MbimDataClass   data_class,
                              MMSignal      **cdma,
                              MMSignal      **evdo,
                              MMSignal      **gsm,
                              MMSignal      **umts,
                              MMSignal      **lte,
                              MMSignal      **nr5g)
{
    if (data_class & (MBIM_DATA_CLASS_5G_NSA |
                      MBIM_DATA_CLASS_5G_SA))
        return *nr5g;
    if (data_class & (MBIM_DATA_CLASS_LTE))
        return *lte;
    if (data_class & (MBIM_DATA_CLASS_UMTS |
                      MBIM_DATA_CLASS_HSDPA |
                      MBIM_DATA_CLASS_HSUPA))
        return *umts;
    if (data_class & (MBIM_DATA_CLASS_GPRS |
                      MBIM_DATA_CLASS_EDGE))
        return *gsm;
    if (data_class & (MBIM_DATA_CLASS_1XEVDO |
                      MBIM_DATA_CLASS_1XEVDO_REVA |
                      MBIM_DATA_CLASS_1XEVDV |
                      MBIM_DATA_CLASS_3XRTT |
                      MBIM_DATA_CLASS_1XEVDO_REVB))
        return *evdo;
    if (data_class & MBIM_DATA_CLASS_1XRTT)
        return *cdma;
    return NULL;
}

static void
test_signal_state_case (gconstpointer user_data)
{
    const SignalStateTestcase *tc = user_data;
    gboolean                   success;
    guint                      count;
    g_autoptr(MMSignal)        cdma;
    g_autoptr(MMSignal)        evdo;
    g_autoptr(MMSignal)        gsm;
    g_autoptr(MMSignal)        umts;
    g_autoptr(MMSignal)        lte;
    g_autoptr(MMSignal)        nr5g;
    MMSignal                  *tmp;

    for (count = 0;
         count < G_N_ELEMENTS (tc->rsrp_snr) && tc->rsrp_snr[count].system_type;
         count++);

    success = mm_signal_from_mbim_signal_state (tc->data_class,
                                                tc->coded_rssi,
                                                tc->coded_error_rate,
                                                (MbimRsrpSnrInfo **) &tc->rsrp_snr,
                                                count,
                                                NULL,
                                                &cdma,
                                                &evdo,
                                                &gsm,
                                                &umts,
                                                &lte,
                                                &nr5g);
    g_assert_cmpint (success, ==, tc->expect_success);

    for (count = 0;
         count < G_N_ELEMENTS (tc->expected) && (tc->expected[count].data_class != MBIM_DATA_CLASS_NONE);
         count++) {
        tmp = select_signal_for_data_class (tc->expected[count].data_class,
                                            &cdma, &evdo, &gsm, &umts, &lte, &nr5g);
        g_assert (tmp);
        g_assert_cmpfloat (mm_signal_get_rssi (tmp), ==, tc->expected[count].rssi);
        g_assert_cmpfloat (mm_signal_get_rscp (tmp), ==, tc->expected[count].rscp);
        g_assert_cmpfloat (mm_signal_get_ecio (tmp), ==, tc->expected[count].ecio);
        g_assert_cmpfloat (mm_signal_get_sinr (tmp), ==, tc->expected[count].sinr);
        g_assert_cmpfloat (mm_signal_get_io (tmp), ==, tc->expected[count].io);
        g_assert_cmpfloat (mm_signal_get_rsrq (tmp), ==, tc->expected[count].rsrq);
        g_assert_cmpfloat (mm_signal_get_rsrp (tmp), ==, tc->expected[count].rsrp);
        g_assert_cmpfloat (mm_signal_get_snr (tmp), ==, tc->expected[count].snr);
        g_assert_cmpfloat (mm_signal_get_error_rate (tmp), ==, tc->expected[count].error_rate);
    }
    if (count == 0 ) {
        g_assert (!cdma);
        g_assert (!evdo);
        g_assert (!gsm);
        g_assert (!umts);
        g_assert (!lte);
        g_assert (!nr5g);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    guint i;

    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    for (i = 0; i < G_N_ELEMENTS (signal_tests); i++) {
        g_autofree gchar *detail;

        detail = g_strdup_printf ("/MM/mbim/signal-state/%s", signal_tests[i].detail);
        g_test_add_data_func (detail, &signal_tests[i], test_signal_state_case);
    }

    return g_test_run ();
}
