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
 * Copyright (C) 2012 Lanedo GmbH.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "mm-enums-types.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-log-test.h"

static void
test_current_capabilities_expected (MMQmiCurrentCapabilitiesContext *ctx,
                                    MMModemCapability                expected)
{
    MMModemCapability  built;
    g_autofree gchar  *expected_str = NULL;
    g_autofree gchar  *built_str = NULL;

    built = mm_current_capability_from_qmi_current_capabilities_context (ctx, NULL);

    expected_str = mm_modem_capability_build_string_from_mask (expected);
    built_str = mm_modem_capability_build_string_from_mask (built);

    /* compare strings, so that the error shows the string values as well */
    g_assert_cmpstr (built_str, ==, expected_str);
}

/*****************************************************************************/
/* UML290:
 * ∘ +GCAP: +CIS707-A, CIS-856, CIS-856-A, +CGSM, +CLTE2
 * ∘ +WS46: (12,22,25)
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo, gsm, umts, lte' (always)
 * ∘ NAS TP: unsupported
 * ∘ NAS SSP: Band pref & LTE band pref always given
 * ∘ QCDM -> CDMA/EVDO = NAS SSP: Mode preference: 'cdma-1x, cdma-1xevdo'
 * ∘ QCDM -> GSM/UMTS  = NAS SSP: Mode preference: 'gsm, umts'
 * ∘ QCDM -> Automatic = NAS SSP: no mode pref TLV given
 * ∘ QCDM -> LTE-only  = NAS SSP: Mode preference: 'lte'
*/

static void
test_current_capabilities_uml290 (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QCDM -> CDMA/EVDO */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X |
                                        QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO);
    ctx.nas_tp_mask = 0; /* Unsupported */
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE));

    /* QCDM -> GSM/UMTS */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_GSM |
                                        QMI_NAS_RAT_MODE_PREFERENCE_UMTS);
    ctx.nas_tp_mask = 0; /* Unsupported */
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE));

    /* QCDM -> Automatic */
    ctx.nas_ssp_mode_preference_mask = 0;
    ctx.nas_tp_mask = 0; /* Unsupported */
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE));
}

/*****************************************************************************/
/* ADU960S:
 * ∘ +GCAP: +CGSM,+DS,+ES
 * ∘ +WS46: ERROR
 * ∘ NAS TP: unsupported
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo, gsm, umts, lte'
 * ∘ NAS SSP: LTE band preference: '(null)'
 * ∘ (no QCDM port)
 */

static void
test_current_capabilities_adu960s (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.nas_ssp_mode_preference_mask = 0;
    ctx.nas_tp_mask = 0; /* Unsupported */
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE));
}

/*****************************************************************************/
/* Gobi 1K with GSM firmware:
 * ∘ +GCAP: didn't respond to AT commands
 * ∘ NAS TP: Active: 'auto', duration: 'permanent'
 * ∘ DMS GetCapa: Networks: 'gsm, umts'
 * ∘ NAS SSP: unsupported
 * ∘ (no QCDM port)
 */

static void
test_current_capabilities_gobi1k_gsm (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);
}

/*****************************************************************************/
/* Gobi 1K with EVDO firmware:
 * ∘ +GCAP: didn't respond to AT commands
 * ∘ NAS TP: Active: 'auto', duration: 'permanent'
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo'
 * ∘ NAS SSP: unsupported
 * ∘ (no QCDM port)
 */

static void
test_current_capabilities_gobi1k_cdma (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);
}

/*****************************************************************************/
/* Gobi 2K with GSM firmware:
 * ∘ +GCAP: +CGSM,+DS
 * ∘ +WS46: ERROR
 * ∘ DMS GetCapa: Networks: 'gsm, umts'
 * ∘ NAS SSP: unsupported
 * ∘ QCDM -> Automatic = NAS TP: Active: 'auto', duration: 'permanent'
 * ∘ QCDM -> UMTS only = NAS TP: Active: '3gpp, cdma-or-wcdma', duration: 'permanent'
 * ∘ QCDM -> GPRS only = NAS TP: Active: '3gpp, amps-or-gsm', duration: 'permanent'
 */

static void
test_current_capabilities_gobi2k_gsm (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QCDM -> Automatic */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);

    /* QCDM -> UMTS only */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);

    /* QCDM -> GPRS only */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);
}

/*****************************************************************************/
/* Gobi 2K with CDMA firmware:
 * ∘ +GCAP: +CIS707-A, CIS-856, CIS-856-A, CIS707,+MS, +ES, +DS, +FCL
 * ∘ +WS46: ERROR
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo'
 * ∘ NAS SSP: unsupported
 * ∘ QCDM -> Automatic = NAS TP: Active: 'auto', duration: 'permanent'
 * ∘ QCDM -> CDMA only = NAS TP: Active: '3gpp2, cdma-or-wcdma', duration: 'permanent'
 * ∘ QCDM -> EVDO only = NAS TP: Active: '3gpp2, hdr', duration: 'permanent'
 */

static void
test_current_capabilities_gobi2k_cdma (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QCDM -> Automatic */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);

    /* QCDM -> CDMA only */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);

    /* QCDM -> EVDO only */
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);
}

/*****************************************************************************/
/* Gobi 3K with GSM firmware:
 * ∘ +GCAP: +CGSM,+DS,+ES
 * ∘ +WS46: ERROR
 * ∘ DMS GetCapa: Networks: 'gsm, umts'
 * ∘ QCDM -> Automatic = NAS TP:  Active: 'auto', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'cdma-1x, cdma-1xevdo, gsm, umts'
 * ∘ QCDM -> UMTS only = NAS TP:  Active: '3gpp, cdma-or-wcdma', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'umts'
 * ∘ QCDM -> GPRS only = NAS TP:  Active: '3gpp, amps-or-gsm', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'gsm'
 */

static void
test_current_capabilities_gobi3k_gsm (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QCDM -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X |
                                        QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO |
                                        QMI_NAS_RAT_MODE_PREFERENCE_GSM |
                                        QMI_NAS_RAT_MODE_PREFERENCE_UMTS);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);

    /* QCDM -> GSM only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_GSM);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);

    /* QCDM -> UMTS only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_UMTS);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);
}

/*****************************************************************************/
/* Gobi 3K with CDMA firmware:
 * ∘ +GCAP: +CIS707-A, CIS-856, CIS-856-A, CIS707,+MS, +ES, +DS, +FCL
 * ∘ +WS46: ERROR
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo'
 * ∘ QCDM -> Automatic = NAS TP:  Active: 'auto', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'cdma-1x, cdma-1xevdo, gsm, umts'
 * ∘ QCDM -> EVDO only = NAS TP:  Active: '3gpp2, hdr', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'cdma-1xevdo'
 * ∘ QCDM -> CDMA only = NAS TP:  Active: '3gpp2, cdma-or-wcdma', duration: 'permanent'
 *                       NAS SSP: Mode preference: 'cdma-1x'
 */

static void
test_current_capabilities_gobi3k_cdma (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QCDM -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X |
                                        QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO |
                                        QMI_NAS_RAT_MODE_PREFERENCE_GSM |
                                        QMI_NAS_RAT_MODE_PREFERENCE_UMTS);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);

    /* QCDM -> CDMA only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);

    /* QCDM -> EVDO only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR);
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);
}

/*****************************************************************************/
/* Generic with NR5G:
 * ∘ +GCAP: +CGSM
 * ∘ +WS46: (12,22,25,28,29,30,31)
 * ∘ DMS GetCapa: Networks: 'cdma20001x, evdo, gsm, umts, lte, 5gnr'
 * ∘ QMI -> Automatic = NAS TP:  Active: 'auto', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'cdma-1x, cdma-1xevdo, gsm, umts, lte, td-scdma, 5gnr'
 * ∘ QMI -> GSM only  = NAS TP:  Active: '3gpp, amps-or-gsm', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'gsm'
 * ∘ QMI -> UMTS only = NAS TP:  Active: '3gpp, cdma-or-wcdma', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'umts'
 * ∘ QMI -> EVDO only = NAS TP:  Active: '3gpp2, hdr', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'cdma-1xevdo'
 * ∘ QMI -> CDMA only = NAS TP:  Active: '3gpp2, cdma-or-wcdma', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'cdma-1x'
 * ∘ QMI -> LTE only  = NAS TP:  Active: '3gpp, lte', duration: 'permanent'
 *                      NAS SSP: Mode preference: 'lte'
 * ∘ QMI -> 5GNR only = NAS TP:  Active: 'auto', duration: 'permanent'
 *                      NAS SSP: Mode preference: '5gnr'
 */

static void
test_current_capabilities_generic_nr5g (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    /* QMI -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X |
                                        QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO |
                                        QMI_NAS_RAT_MODE_PREFERENCE_GSM |
                                        QMI_NAS_RAT_MODE_PREFERENCE_UMTS |
                                        QMI_NAS_RAT_MODE_PREFERENCE_LTE |
                                        QMI_NAS_RAT_MODE_PREFERENCE_TD_SCDMA |
                                        QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> GSM only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_GSM);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> UMTS only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_UMTS);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> EVDO only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> CDMA only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> LTE only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_LTE);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> 5GNR only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/qmi/current-capabilities/UML290",       test_current_capabilities_uml290);
    g_test_add_func ("/MM/qmi/current-capabilities/ADU960S",      test_current_capabilities_adu960s);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi1k/GSM",   test_current_capabilities_gobi1k_gsm);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi1k/CDMA",  test_current_capabilities_gobi1k_cdma);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi2k/GSM",   test_current_capabilities_gobi2k_gsm);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi2k/CDMA",  test_current_capabilities_gobi2k_cdma);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi3k/GSM",   test_current_capabilities_gobi3k_gsm);
    g_test_add_func ("/MM/qmi/current-capabilities/Gobi3k/CDMA",  test_current_capabilities_gobi3k_cdma);
    g_test_add_func ("/MM/qmi/current-capabilities/generic/NR5G", test_current_capabilities_generic_nr5g);

    return g_test_run ();
}
