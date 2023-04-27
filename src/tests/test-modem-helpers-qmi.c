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
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc.
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

static void
test_supported_capabilities_expected (MMQmiSupportedCapabilitiesContext *ctx,
                                      const MMModemCapability           *expected_capabilities,
                                      guint                              n_expected_capabilities)
{
    g_autoptr(GArray)  built = NULL;
    g_autofree gchar  *expected_str = NULL;
    g_autofree gchar  *built_str = NULL;

    built = mm_supported_capabilities_from_qmi_supported_capabilities_context (ctx, NULL);

    expected_str = mm_common_build_capabilities_string (expected_capabilities, n_expected_capabilities);
    built_str = mm_common_build_capabilities_string ((MMModemCapability *)built->data, built->len);

    /* compare strings, so that the error shows the string values as well */
    g_assert_cmpstr (built_str, ==, expected_str);
}

static void
test_supported_modes_expected (MMQmiSupportedModesContext   *ctx,
                               const MMModemModeCombination *expected_modes,
                               guint                         n_expected_modes)
{
    g_autoptr(GArray)  built = NULL;
    g_autofree gchar  *expected_str = NULL;
    g_autofree gchar  *built_str = NULL;

    built = mm_supported_modes_from_qmi_supported_modes_context (ctx, NULL);

    expected_str = mm_common_build_mode_combinations_string (expected_modes, n_expected_modes);
    built_str = mm_common_build_mode_combinations_string ((MMModemModeCombination *)built->data, built->len);

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

    ctx.multimode = TRUE;

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

static void
test_supported_capabilities_uml290 (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE,
        MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE,
        MM_MODEM_CAPABILITY_LTE,
        MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE,
    };

    ctx.multimode = TRUE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = FALSE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_uml290_cdma_evdo_gsm_umts_lte (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        /* we MUST not have 4G-only in multimode devices */
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = FALSE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                                MM_MODEM_CAPABILITY_CDMA_EVDO |
                                MM_MODEM_CAPABILITY_LTE);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_supported_modes_uml290_cdma_evdo_lte (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        /* we MUST not have 4G-only in multimode devices */
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = FALSE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                MM_MODEM_CAPABILITY_LTE);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_supported_modes_uml290_gsm_umts_lte (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        /* we MUST not have 4G-only in multimode devices */
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = FALSE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                                MM_MODEM_CAPABILITY_LTE);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_supported_modes_uml290_lte (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        /* we MUST only have 4G-only in a multimode device with only LTE capability */
        { .allowed = MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_4G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = FALSE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_LTE;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = TRUE;
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = 0; /* Unsupported */
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE));
}

static void
test_supported_capabilities_adu960s (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE,
    };

    ctx.multimode = TRUE;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = FALSE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_adu960s (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = FALSE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                                MM_MODEM_CAPABILITY_LTE);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_GSM_UMTS);
}

static void
test_supported_capabilities_gobi1k_gsm (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi1k_gsm (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;
    ctx.nas_ssp_mode_preference_mask = 0; /* Unsupported */
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_CDMA_EVDO);
}

static void
test_supported_capabilities_gobi1k_cdma (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_CDMA_EVDO,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi1k_cdma (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;

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

static void
test_supported_capabilities_gobi2k_gsm (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi2k_gsm (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;

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

static void
test_supported_capabilities_gobi2k_cdma (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_CDMA_EVDO,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi2k_cdma (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = FALSE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;

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

static void
test_supported_capabilities_gobi3k_gsm (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi3k_gsm (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_GSM_UMTS;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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

    ctx.multimode = FALSE;

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

static void
test_supported_capabilities_gobi3k_cdma (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_CDMA_EVDO,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_gobi3k_cdma (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_CDMA_EVDO;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
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
test_current_capabilities_generic_nr5g_multimode (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.multimode = TRUE;

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

static void
test_supported_capabilities_generic_nr5g_multimode (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
        MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
        MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
        MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO | MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
    };

    ctx.multimode = TRUE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_generic_nr5g_multimode (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        /* we MUST not have 4G-only in multimode devices */
        /* we MUST not have 5G-only in multimode devices */
        /* we MUST not have 4G+5G in multimode devices */
        { .allowed = MM_MODEM_MODE_2G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_2G },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_2G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_2G },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_2G },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },

        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },
        { .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_2G },
    };

    ctx.multimode = TRUE;
    ctx.all = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                                MM_MODEM_CAPABILITY_CDMA_EVDO |
                                MM_MODEM_CAPABILITY_LTE |
                                MM_MODEM_CAPABILITY_5GNR);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_current_capabilities_generic_nr5g_only (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.multimode = FALSE;
    ctx.nas_ssp_mode_preference_mask = QMI_NAS_RAT_MODE_PREFERENCE_5GNR;
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_5GNR;
    test_current_capabilities_expected (&ctx, MM_MODEM_CAPABILITY_5GNR);
}

static void
test_supported_capabilities_generic_nr5g_only (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_5GNR,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = MM_MODEM_CAPABILITY_5GNR;

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_generic_nr5g_only (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_NONE },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_5G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = MM_MODEM_CAPABILITY_5GNR;

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_current_capabilities_generic_nr5g_lte (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.multimode = FALSE;

    /* QMI -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_LTE |
                                        QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> LTE only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_LTE);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> 5GNR only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));
}

static void
test_supported_capabilities_generic_nr5g_lte (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_generic_nr5g_lte (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_NONE },
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_LTE |
                                MM_MODEM_CAPABILITY_5GNR);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_current_capabilities_generic_nr5g_lte_umts (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.multimode = FALSE;

    /* QMI -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_UMTS |
                                        QMI_NAS_RAT_MODE_PREFERENCE_LTE |
                                        QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
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
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> LTE only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_LTE);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> 5GNR only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_GSM_UMTS |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));
}

static void
test_supported_capabilities_generic_nr5g_lte_umts (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_GSM_UMTS |MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_generic_nr5g_lte_umts (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        /* we MUST have 4G-only in non-multimode devices */
        { .allowed = MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_NONE },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },

        /* we MUST have 5G-only in non-multimode devices */
        { .allowed = MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_NONE },

        /* we MUST have 4G+5G in non-multimode devices */
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_GSM_UMTS |
                                MM_MODEM_CAPABILITY_LTE |
                                MM_MODEM_CAPABILITY_5GNR);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

static void
test_current_capabilities_generic_nr5g_lte_evdo (void)
{
    MMQmiCurrentCapabilitiesContext ctx;

    ctx.multimode = FALSE;

    /* QMI -> Automatic */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO |
                                        QMI_NAS_RAT_MODE_PREFERENCE_LTE |
                                        QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> EVDO only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2 | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> LTE only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_LTE);
    ctx.nas_tp_mask = (QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP | QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE);
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));

    /* QMI -> 5GNR only */
    ctx.nas_ssp_mode_preference_mask = (QMI_NAS_RAT_MODE_PREFERENCE_5GNR);
    ctx.nas_tp_mask = QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);
    test_current_capabilities_expected (&ctx,
                                        (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                         MM_MODEM_CAPABILITY_LTE |
                                         MM_MODEM_CAPABILITY_5GNR));
}

static void
test_supported_capabilities_generic_nr5g_lte_evdo (void)
{
    MMQmiSupportedCapabilitiesContext ctx;
    static const MMModemCapability expected_capabilities[] = {
        MM_MODEM_CAPABILITY_CDMA_EVDO |MM_MODEM_CAPABILITY_LTE | MM_MODEM_CAPABILITY_5GNR,
    };

    ctx.multimode = FALSE;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.dms_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                            MM_MODEM_CAPABILITY_LTE |
                            MM_MODEM_CAPABILITY_5GNR);

    test_supported_capabilities_expected (&ctx,
                                          expected_capabilities,
                                          G_N_ELEMENTS (expected_capabilities));
}

static void
test_supported_modes_generic_nr5g_lte_evdo (void)
{
    MMQmiSupportedModesContext ctx;
    static const MMModemModeCombination expected_modes[] = {
        { .allowed = MM_MODEM_MODE_3G, .preferred = MM_MODEM_MODE_NONE },
        /* we MUST have 4G-only in non-multimode devices */
        { .allowed = MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_NONE },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, .preferred = MM_MODEM_MODE_3G },

        /* we MUST have 5G-only in non-multimode devices */
        { .allowed = MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_NONE },

        /* we MUST have 4G+5G in non-multimode devices */
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },

        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_5G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_4G },
        { .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G, .preferred = MM_MODEM_MODE_3G },
    };

    ctx.multimode = FALSE;
    ctx.all = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G;
    ctx.nas_ssp_supported = TRUE;
    ctx.nas_tp_supported = TRUE;
    ctx.current_capabilities = (MM_MODEM_CAPABILITY_CDMA_EVDO |
                                MM_MODEM_CAPABILITY_LTE |
                                MM_MODEM_CAPABILITY_5GNR);

    test_supported_modes_expected (&ctx,
                                   expected_modes,
                                   G_N_ELEMENTS (expected_modes));
}

/*****************************************************************************/
/* System Info processor helper */

static void
common_test_registration_state_from_qmi_system_info (const guint8                 *data,
                                                     gsize                         data_size,
                                                     MMModem3gppRegistrationState  expected_registration_state_cs,
                                                     MMModem3gppRegistrationState  expected_registration_state_ps,
                                                     MMModem3gppRegistrationState  expected_registration_state_eps,
                                                     MMModem3gppRegistrationState  expected_registration_state_5gs,
                                                     guint16                       expected_lac,
                                                     guint16                       expected_tac,
                                                     guint32                       expected_cid,
                                                     const gchar                  *expected_operator_id,
                                                     MMModemAccessTechnology       expected_act)
{
    g_autoptr(GByteArray)                       buffer = NULL;
    g_autoptr(GError)                           error = NULL;
    g_autoptr(QmiMessage)                       message = NULL;
    g_autoptr(QmiMessageNasGetSystemInfoOutput) output = NULL;
    MMModem3gppRegistrationState                registration_state_cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState                registration_state_ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState                registration_state_eps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    MMModem3gppRegistrationState                registration_state_5gs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    guint16                                     lac = 0;
    guint16                                     tac = 0;
    guint32                                     cid = 0;
    g_autofree gchar                           *operator_id = NULL;
    MMModemAccessTechnology                     act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    buffer = g_byte_array_append (g_byte_array_new (), data, data_size);
    message = qmi_message_new_from_raw (buffer, &error);
    g_assert_no_error (error);
    g_assert (message);

    output = qmi_message_nas_get_system_info_response_parse (message, &error);
    g_assert_no_error (error);
    g_assert (output);

    mm_modem_registration_state_from_qmi_system_info (output, NULL,
                                                      &registration_state_cs,
                                                      &registration_state_ps,
                                                      &registration_state_eps,
                                                      &registration_state_5gs,
                                                      &lac,
                                                      &tac,
                                                      &cid,
                                                      &operator_id,
                                                      &act,
                                                      NULL); /* log_object */

    g_assert_cmpuint (registration_state_cs, ==, expected_registration_state_cs);
    g_assert_cmpuint (registration_state_ps, ==, expected_registration_state_ps);
    g_assert_cmpuint (registration_state_eps, ==, expected_registration_state_eps);
    g_assert_cmpuint (registration_state_5gs, ==, expected_registration_state_5gs);
    g_assert_cmpuint (lac, ==, expected_lac);
    g_assert_cmpuint (tac, ==, expected_tac);
    g_assert_cmpuint (cid, ==, expected_cid);
    g_assert_cmpstr (operator_id, ==, expected_operator_id);
    g_assert_cmpuint (act, ==, expected_act);
}

static void
test_registration_state_from_qmi_system_info_2g_searching (void)
{
    static const guint8 data[] = {
        0x01, 0x19, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x0D, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g_idle (void)
{
    static const guint8 data[] = {
        0x01, 0x19, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x0D, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g_cs_home (void)
{
    static const guint8 data[] = {
        0x01, 0x3A, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x2E, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00, /* [ service_status = 'available' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x17: GSM System Info */
        0x17, /* tlv type */
        0x1E, 0x00, /* tlv size: 30 bytes */
        /* [ domain_valid = 'yes' domain = 'cs' service_capability_valid = 'yes' service_capability = 'cs'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'yes' lac = '7' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' egprs_support_valid = 'no' egprs_support = 'no' dtm_support_valid = 'no',
         * dtm_support = 'no'] */
        0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x07, 0x00, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x00, 0x00, 0x00, 0x00
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,    /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         7, /* lac */
                                                         0, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_GSM);
}

static void
test_registration_state_from_qmi_system_info_2g3g_searching (void)
{
    static const guint8 data[] = {
        0x01, 0x1F, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x13, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g3g_idle (void)
{
    static const guint8 data[] = {
        0x01, 0x1F, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x13, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g3g_ps_home (void)
{
    static const guint8 data[] = {
        0x01, 0x43, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x37, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00, /* [ service_status = 'available' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x18: WCDMA System Info */
        0x18, /* tlv type */
        0x21, 0x00, /* tlv size: 33 bytes */
        /* [ domain_valid = 'yes' domain = 'ps' service_capability_valid = 'yes' service_capability = 'ps'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'yes' lac = '7' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' hs_call_status_valid = 'no' hs_call_stauts = '' hs_service_valid = 'yes',
         * primary_scrambling_code_valid = 'yes' primary_scrambling_code = '255'] */
        0x01, 0x02, 0x01, 0x02, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x07, 0x00, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x01, 0x00, 0x01, 0x00, 0x01, 0xFF,
        0x00
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* cs (from GSM) */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,      /* ps (from UMTS) */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* 5gs */
                                                         7, /* lac */
                                                         0, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UMTS);
}

static void
test_registration_state_from_qmi_system_info_2g3g_csps_home (void)
{
    static const guint8 data[] = {
        0x01, 0x43, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x37, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00, /* [ service_status = 'available' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x18: WCDMA System Info */
        0x18, /* tlv type */
        0x21, 0x00, /* tlv size: 33 bytes */
        /* [ domain_valid = 'yes' domain = 'cs-ps' service_capability_valid = 'yes' service_capability = 'cs-ps'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'yes' lac = '7' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' hs_call_status_valid = 'no' hs_call_stauts = '' hs_service_valid = 'yes',
         * primary_scrambling_code_valid = 'yes' primary_scrambling_code = '255'] */
        0x01, 0x03, 0x01, 0x03, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x07, 0x00, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x01, 0x00, 0x01, 0x00, 0x01, 0xFF,
        0x00
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,    /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,    /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         7, /* lac */
                                                         0, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UMTS);
}

static void
test_registration_state_from_qmi_system_info_2g3g4g_searching (void)
{
    static const guint8 data[] = {
        0x01, 0x25, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x19, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x14: LTE Service Status */
        0x14, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,   /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g3g4g_idle (void)
{
    static const guint8 data[] = {
        0x01, 0x25, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x19, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x14: LTE Service Status */
        0x14, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x04, 0x00, 0x00, /* [ service_status = 'power-save' true_service_status = 'none' preferred_data_path = 'no' ] */
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_IDLE,    /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         0, /* lac */
                                                         0, /* tac */
                                                         0, /* cid */
                                                         NULL, /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
}

static void
test_registration_state_from_qmi_system_info_2g3g4g_eps_home (void)
{
    static const guint8 data[] = {
        0x01, 0x45, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x39, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x12: GSM Service Status*/
        0x12, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x13: WCDMA Service Status */
        0x13, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x00, 0x00, 0x00, /* [ service_status = 'none' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x14: LTE Service Status */
        0x14, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00, /* [ service_status = 'available' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x19: LTE System Info */
        0x19, /* tlv type */
        0x1D, 0x00, /* tlv size: 29 bytes */
        /* [ domain_valid = 'yes' domain = 'ps' service_capability_valid = 'yes' service_capability = 'ps'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'no' lac = '65535' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' tac_valid = 'yes' tac = '7' ] */
        0x01, 0x02, 0x01, 0x02, 0x01, 0x00, 0x01, 0x00,
        0x00, 0xFF, 0xFF, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x01, 0x07, 0x00,
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         0, /* lac */
                                                         7, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_LTE);
}

static void
test_registration_state_from_qmi_system_info_4g_eps_home (void)
{
    static const guint8 data[] = {
        0x01, 0x39, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x2D, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x14: LTE Service Status */
        0x14, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00, /* [ service_status = 'available' true_service_status = 'none' preferred_data_path = 'no' ] */
        /* TLV 0x19: LTE System Info */
        0x19, /* tlv type */
        0x1D, 0x00, /* tlv size: 29 bytes */
        /* [ domain_valid = 'yes' domain = 'ps' service_capability_valid = 'yes' service_capability = 'ps'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'no' lac = '65535' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' tac_valid = 'yes' tac = '7' ] */
        0x01, 0x02, 0x01, 0x02, 0x01, 0x00, 0x01, 0x00,
        0x00, 0xFF, 0xFF, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x01, 0x07, 0x00,
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,    /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* 5gs */
                                                         0, /* lac */
                                                         7, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_LTE);
}

static void
test_registration_state_from_qmi_system_info_5g_5gs_home (void)
{
    static const guint8 data[] = {
        0x01, 0x39, 0x00, 0x80, /* marker (1 byte), qmux length (2 bytes), qmux flags (1 byte) */
        0x03, /* service: NAS */
        0x02, /* client id */
        0x02, /* service flags */
        0x01, 0x00, /* transaction */
        0x4D, 0x00, /* message id */
        0x2D, 0x00, /* all tlvs length */
        /* TLV 0x02: Result */
        0x02, /* tlv type */
        0x04, 0x00, /* tlv size */
        0x00, 0x00, 0x00, 0x00,
        /* TLV 0x4A: 5GNR System Info */
        0x4A, /* tlv type */
        0x03, 0x00, /* tlv size */
        0x02, 0x00, 0x00,
        /* TLV 0x4B: 5GNR System Info */
        0x4B, /* tlv type */
        0x1D, 0x00, /* tlv size: 29 bytes */
        /* [ domain_valid = 'yes' domain = 'ps' service_capability_valid = 'yes' service_capability = 'ps'
         * roaming_status_valid = 'yes' roaming_status = 'off' forbidden_valid = 'yes' forbidden = 'no'
         * lac_valid = 'no' lac = '65535' cid_valid = 'yes' cid = '105217' registration_reject_info_valid = 'no'
         * registration_reject_domain = 'none' registration_reject_cause = 'none' network_id_valid = 'yes'
         * mcc = '901' mnc = '701' tac_valid = 'yes' tac = '7' ] */
        0x01, 0x02, 0x01, 0x02, 0x01, 0x00, 0x01, 0x00,
        0x00, 0xFF, 0xFF, 0x01, 0x01, 0x9B, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x39, 0x30, 0x31, 0x37,
        0x30, 0xFF, 0x01, 0x07, 0x00,
    };

    common_test_registration_state_from_qmi_system_info (data, G_N_ELEMENTS (data),
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* cs */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* ps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, /* eps */
                                                         MM_MODEM_3GPP_REGISTRATION_STATE_HOME,    /* 5gs */
                                                         0, /* lac */
                                                         7, /* tac */
                                                         0x019B01, /* cid */
                                                         "90170", /* operator id */
                                                         MM_MODEM_ACCESS_TECHNOLOGY_5GNR);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/qmi/current-capabilities/UML290",                   test_current_capabilities_uml290);
    g_test_add_func ("/MM/qmi/supported-capabilities/UML290",                 test_supported_capabilities_uml290);
    g_test_add_func ("/MM/qmi/supported-modes/UML290/cdma-evdo-gsm-umts-lte", test_supported_modes_uml290_cdma_evdo_gsm_umts_lte);
    g_test_add_func ("/MM/qmi/supported-modes/UML290/cdma-evdo-lte",          test_supported_modes_uml290_cdma_evdo_lte);
    g_test_add_func ("/MM/qmi/supported-modes/UML290/gsm-umts-lte",           test_supported_modes_uml290_gsm_umts_lte);
    g_test_add_func ("/MM/qmi/supported-modes/UML290/lte",                    test_supported_modes_uml290_lte);

    g_test_add_func ("/MM/qmi/current-capabilities/ADU960S",                  test_current_capabilities_adu960s);
    g_test_add_func ("/MM/qmi/supported-capabilities/ADU960S",                test_supported_capabilities_adu960s);
    g_test_add_func ("/MM/qmi/supported-modes/ADU960S",                       test_supported_modes_adu960s);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi1k/GSM",               test_current_capabilities_gobi1k_gsm);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi1k/GSM",             test_supported_capabilities_gobi1k_gsm);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi1k/GSM",                    test_supported_modes_gobi1k_gsm);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi1k/CDMA",              test_current_capabilities_gobi1k_cdma);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi1k/CDMA",            test_supported_capabilities_gobi1k_cdma);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi1k/CDMA",                   test_supported_modes_gobi1k_cdma);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi2k/GSM",               test_current_capabilities_gobi2k_gsm);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi2k/GSM",             test_supported_capabilities_gobi2k_gsm);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi2k/GSM",                    test_supported_modes_gobi2k_gsm);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi2k/CDMA",              test_current_capabilities_gobi2k_cdma);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi2k/CDMA",            test_supported_capabilities_gobi2k_cdma);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi2k/CDMA",                   test_supported_modes_gobi2k_cdma);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi3k/GSM",               test_current_capabilities_gobi3k_gsm);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi3k/GSM",             test_supported_capabilities_gobi3k_gsm);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi3k/GSM",                    test_supported_modes_gobi3k_gsm);

    g_test_add_func ("/MM/qmi/current-capabilities/Gobi3k/CDMA",              test_current_capabilities_gobi3k_cdma);
    g_test_add_func ("/MM/qmi/supported-capabilities/Gobi3k/CDMA",            test_supported_capabilities_gobi3k_cdma);
    g_test_add_func ("/MM/qmi/supported-modes/Gobi3k/CDMA",                   test_supported_modes_gobi3k_cdma);

    g_test_add_func ("/MM/qmi/current-capabilities/generic/nr5g-multimode",   test_current_capabilities_generic_nr5g_multimode);
    g_test_add_func ("/MM/qmi/supported-capabilities/generic/nr5g-multimode", test_supported_capabilities_generic_nr5g_multimode);
    g_test_add_func ("/MM/qmi/supported-modes/generic/nr5g-multimode",        test_supported_modes_generic_nr5g_multimode);

    g_test_add_func ("/MM/qmi/current-capabilities/generic/nr5g-only",        test_current_capabilities_generic_nr5g_only);
    g_test_add_func ("/MM/qmi/supported-capabilities/generic/nr5g-only",      test_supported_capabilities_generic_nr5g_only);
    g_test_add_func ("/MM/qmi/supported-modes/generic/nr5g-only",             test_supported_modes_generic_nr5g_only);

    g_test_add_func ("/MM/qmi/current-capabilities/generic/nr5g-lte",         test_current_capabilities_generic_nr5g_lte);
    g_test_add_func ("/MM/qmi/supported-capabilities/generic/nr5g-lte",       test_supported_capabilities_generic_nr5g_lte);
    g_test_add_func ("/MM/qmi/supported-modes/generic/nr5g-lte",              test_supported_modes_generic_nr5g_lte);

    g_test_add_func ("/MM/qmi/current-capabilities/generic/nr5g-lte-umts",    test_current_capabilities_generic_nr5g_lte_umts);
    g_test_add_func ("/MM/qmi/supported-capabilities/generic/nr5g-lte-umts",  test_supported_capabilities_generic_nr5g_lte_umts);
    g_test_add_func ("/MM/qmi/supported-modes/generic/nr5g-lte-umts",         test_supported_modes_generic_nr5g_lte_umts);

    g_test_add_func ("/MM/qmi/current-capabilities/generic/nr5g-lte-evdo",    test_current_capabilities_generic_nr5g_lte_evdo);
    g_test_add_func ("/MM/qmi/supported-capabilities/generic/nr5g-lte-evdo",  test_supported_capabilities_generic_nr5g_lte_evdo);
    g_test_add_func ("/MM/qmi/supported-modes/generic/nr5g-lte-evdo",         test_supported_modes_generic_nr5g_lte_evdo);

    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g/searching",     test_registration_state_from_qmi_system_info_2g_searching);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g/idle",          test_registration_state_from_qmi_system_info_2g_idle);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g/cs-home",       test_registration_state_from_qmi_system_info_2g_cs_home);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g/searching",   test_registration_state_from_qmi_system_info_2g3g_searching);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g/idle",        test_registration_state_from_qmi_system_info_2g3g_idle);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g/ps-home",     test_registration_state_from_qmi_system_info_2g3g_ps_home);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g/csps-home",   test_registration_state_from_qmi_system_info_2g3g_csps_home);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g4g/searching", test_registration_state_from_qmi_system_info_2g3g4g_searching);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g4g/idle",      test_registration_state_from_qmi_system_info_2g3g4g_idle);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/2g3g4g/eps-home",  test_registration_state_from_qmi_system_info_2g3g4g_eps_home);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/4g/eps-home",      test_registration_state_from_qmi_system_info_4g_eps_home);
    g_test_add_func ("/MM/qmi/registration-state-from-system-info/5g/5gs-home",      test_registration_state_from_qmi_system_info_5g_5gs_home);

    return g_test_run ();
}
