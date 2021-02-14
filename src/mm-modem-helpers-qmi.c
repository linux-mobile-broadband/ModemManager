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
 * Copyright (C) 2012-2018 Google, Inc.
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-modem-helpers-qmi.h"
#include "mm-modem-helpers.h"
#include "mm-enums-types.h"
#include "mm-log-object.h"

/*****************************************************************************/

MMModemCapability
mm_modem_capability_from_qmi_radio_interface (QmiDmsRadioInterface network,
                                              gpointer             log_object)
{
    switch (network) {
    case QMI_DMS_RADIO_INTERFACE_CDMA20001X:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_EVDO:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_GSM:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_LTE:
        return MM_MODEM_CAPABILITY_LTE;
    case QMI_DMS_RADIO_INTERFACE_5GNR:
        return MM_MODEM_CAPABILITY_5GNR;
    default:
        mm_obj_warn (log_object, "unhandled QMI radio interface '%u'", (guint)network);
        return MM_MODEM_CAPABILITY_NONE;
    }
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_qmi_radio_interface (QmiDmsRadioInterface network,
                                        gpointer             log_object)
{
    switch (network) {
    case QMI_DMS_RADIO_INTERFACE_CDMA20001X:
        return MM_MODEM_MODE_2G;
    case QMI_DMS_RADIO_INTERFACE_EVDO:
        return MM_MODEM_MODE_3G;
    case QMI_DMS_RADIO_INTERFACE_GSM:
        return MM_MODEM_MODE_2G;
    case QMI_DMS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_MODE_3G;
    case QMI_DMS_RADIO_INTERFACE_LTE:
        return MM_MODEM_MODE_4G;
    case QMI_DMS_RADIO_INTERFACE_5GNR:
        return MM_MODEM_MODE_5G;
    default:
        mm_obj_warn (log_object, "unhandled QMI radio interface '%u'", (guint)network);
        return MM_MODEM_MODE_NONE;
    }
}

/*****************************************************************************/

/* pin1 TRUE for PIN1, FALSE for PIN2 */
MMModemLock
mm_modem_lock_from_qmi_uim_pin_status (QmiDmsUimPinStatus status,
                                       gboolean pin1)
{
    switch (status) {
    case QMI_DMS_UIM_PIN_STATUS_NOT_INITIALIZED:
        return MM_MODEM_LOCK_UNKNOWN;
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_NOT_VERIFIED:
        return pin1 ? MM_MODEM_LOCK_SIM_PIN : MM_MODEM_LOCK_SIM_PIN2;
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_VERIFIED:
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_DISABLED:
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_BLOCKED:
        return pin1 ? MM_MODEM_LOCK_SIM_PUK : MM_MODEM_LOCK_SIM_PUK2;
    case QMI_DMS_UIM_PIN_STATUS_PERMANENTLY_BLOCKED:
        return MM_MODEM_LOCK_UNKNOWN;
    case QMI_DMS_UIM_PIN_STATUS_UNBLOCKED:
        /* This state is possibly given when after an Unblock() operation has been performed.
         * We'll assume the PIN is verified after this. */
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_CHANGED:
        /* This state is possibly given when after an ChangePin() operation has been performed.
         * We'll assume the PIN is verified after this. */
        return MM_MODEM_LOCK_NONE;
    default:
        return MM_MODEM_LOCK_UNKNOWN;
    }
}

/*****************************************************************************/

gboolean
mm_pin_enabled_from_qmi_uim_pin_status (QmiDmsUimPinStatus status)
{
    switch (status) {
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_NOT_VERIFIED:
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_VERIFIED:
    case QMI_DMS_UIM_PIN_STATUS_BLOCKED:
    case QMI_DMS_UIM_PIN_STATUS_PERMANENTLY_BLOCKED:
    case QMI_DMS_UIM_PIN_STATUS_UNBLOCKED:
    case QMI_DMS_UIM_PIN_STATUS_CHANGED:
        /* assume the PIN to be enabled then */
        return TRUE;

    case QMI_DMS_UIM_PIN_STATUS_DISABLED:
    case QMI_DMS_UIM_PIN_STATUS_NOT_INITIALIZED:
        /* assume the PIN to be disabled then */
        return FALSE;

    default:
        /* by default assume disabled */
        return FALSE;
    }
}

/*****************************************************************************/

QmiDmsUimFacility
mm_3gpp_facility_to_qmi_uim_facility (MMModem3gppFacility mm)
{
    switch (mm) {
    case MM_MODEM_3GPP_FACILITY_PH_SIM:
        /* Not really sure about this one; it may be PH_FSIM? */
        return QMI_DMS_UIM_FACILITY_PF;
    case MM_MODEM_3GPP_FACILITY_NET_PERS:
        return QMI_DMS_UIM_FACILITY_PN;
    case MM_MODEM_3GPP_FACILITY_NET_SUB_PERS:
        return QMI_DMS_UIM_FACILITY_PU;
    case MM_MODEM_3GPP_FACILITY_PROVIDER_PERS:
        return QMI_DMS_UIM_FACILITY_PP;
    case MM_MODEM_3GPP_FACILITY_CORP_PERS:
        return QMI_DMS_UIM_FACILITY_PC;
    case MM_MODEM_3GPP_FACILITY_NONE:
    case MM_MODEM_3GPP_FACILITY_SIM:
    case MM_MODEM_3GPP_FACILITY_FIXED_DIALING:
    case MM_MODEM_3GPP_FACILITY_PH_FSIM:
    default:
        /* Never try to ask for a facility we cannot translate */
        g_assert_not_reached ();
    }
}

/*****************************************************************************/

typedef struct {
    QmiDmsBandCapability qmi_band;
    MMModemBand mm_band;
} DmsBandsMap;

static const DmsBandsMap dms_bands_map [] = {
    /* CDMA bands */
    {
        (QMI_DMS_BAND_CAPABILITY_BC_0_A_SYSTEM | QMI_DMS_BAND_CAPABILITY_BC_0_B_SYSTEM),
        MM_MODEM_BAND_CDMA_BC0
    },
    { QMI_DMS_BAND_CAPABILITY_BC_1_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC1  },
    { QMI_DMS_BAND_CAPABILITY_BC_2,            MM_MODEM_BAND_CDMA_BC2  },
    { QMI_DMS_BAND_CAPABILITY_BC_3_A_SYSTEM,   MM_MODEM_BAND_CDMA_BC3  },
    { QMI_DMS_BAND_CAPABILITY_BC_4_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC4  },
    { QMI_DMS_BAND_CAPABILITY_BC_5_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC5  },
    { QMI_DMS_BAND_CAPABILITY_BC_6,            MM_MODEM_BAND_CDMA_BC6  },
    { QMI_DMS_BAND_CAPABILITY_BC_7,            MM_MODEM_BAND_CDMA_BC7  },
    { QMI_DMS_BAND_CAPABILITY_BC_8,            MM_MODEM_BAND_CDMA_BC8  },
    { QMI_DMS_BAND_CAPABILITY_BC_9,            MM_MODEM_BAND_CDMA_BC9  },
    { QMI_DMS_BAND_CAPABILITY_BC_10,           MM_MODEM_BAND_CDMA_BC10 },
    { QMI_DMS_BAND_CAPABILITY_BC_11,           MM_MODEM_BAND_CDMA_BC11 },
    { QMI_DMS_BAND_CAPABILITY_BC_12,           MM_MODEM_BAND_CDMA_BC12 },
    { QMI_DMS_BAND_CAPABILITY_BC_14,           MM_MODEM_BAND_CDMA_BC14 },
    { QMI_DMS_BAND_CAPABILITY_BC_15,           MM_MODEM_BAND_CDMA_BC15 },
    { QMI_DMS_BAND_CAPABILITY_BC_16,           MM_MODEM_BAND_CDMA_BC16 },
    { QMI_DMS_BAND_CAPABILITY_BC_17,           MM_MODEM_BAND_CDMA_BC17 },
    { QMI_DMS_BAND_CAPABILITY_BC_18,           MM_MODEM_BAND_CDMA_BC18 },
    { QMI_DMS_BAND_CAPABILITY_BC_19,           MM_MODEM_BAND_CDMA_BC19 },

    /* GSM bands */
    { QMI_DMS_BAND_CAPABILITY_GSM_DCS_1800,     MM_MODEM_BAND_DCS  },
    {
        (QMI_DMS_BAND_CAPABILITY_GSM_900_PRIMARY | QMI_DMS_BAND_CAPABILITY_GSM_900_EXTENDED),
        MM_MODEM_BAND_EGSM
    },
    { QMI_DMS_BAND_CAPABILITY_GSM_PCS_1900,     MM_MODEM_BAND_PCS  },
    { QMI_DMS_BAND_CAPABILITY_GSM_850,          MM_MODEM_BAND_G850 },
    { QMI_DMS_BAND_CAPABILITY_GSM_450,          MM_MODEM_BAND_G450 },
    { QMI_DMS_BAND_CAPABILITY_GSM_480,          MM_MODEM_BAND_G480 },
    { QMI_DMS_BAND_CAPABILITY_GSM_750,          MM_MODEM_BAND_G750 },

    /* UMTS/WCDMA bands */
    { QMI_DMS_BAND_CAPABILITY_WCDMA_2100,       MM_MODEM_BAND_UTRAN_1  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_DCS_1800,   MM_MODEM_BAND_UTRAN_3  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_PCS_1900,   MM_MODEM_BAND_UTRAN_2  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_1700_US,    MM_MODEM_BAND_UTRAN_4  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_800,        MM_MODEM_BAND_UTRAN_6  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_850_US,     MM_MODEM_BAND_UTRAN_5  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_900,        MM_MODEM_BAND_UTRAN_8  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_1700_JAPAN, MM_MODEM_BAND_UTRAN_9  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_2600,       MM_MODEM_BAND_UTRAN_7  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_1500,       MM_MODEM_BAND_UTRAN_11 },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_850_JAPAN,  MM_MODEM_BAND_UTRAN_19 },
};

static void
dms_add_qmi_bands (GArray               *mm_bands,
                   QmiDmsBandCapability  qmi_bands,
                   gpointer              log_object)
{
    static QmiDmsBandCapability qmi_bands_expected = 0;
    QmiDmsBandCapability not_expected;
    guint i;

    g_assert (mm_bands != NULL);

    /* Build mask of expected bands only once */
    if (G_UNLIKELY (qmi_bands_expected == 0)) {
        for (i = 0; i < G_N_ELEMENTS (dms_bands_map); i++) {
            qmi_bands_expected |= dms_bands_map[i].qmi_band;
        }
    }

    /* Log about the bands that cannot be represented in ModemManager */
    not_expected = ((qmi_bands_expected ^ qmi_bands) & qmi_bands);
    if (not_expected) {
        g_autofree gchar *aux = NULL;

        aux = qmi_dms_band_capability_build_string_from_mask (not_expected);
        mm_obj_dbg (log_object, "cannot add the following bands: '%s'", aux);
    }

    /* And add the expected ones */
    for (i = 0; i < G_N_ELEMENTS (dms_bands_map); i++) {
        if (qmi_bands & dms_bands_map[i].qmi_band)
            g_array_append_val (mm_bands, dms_bands_map[i].mm_band);
    }
}

typedef struct {
    QmiDmsLteBandCapability qmi_band;
    MMModemBand mm_band;
} DmsLteBandsMap;

static const DmsLteBandsMap dms_lte_bands_map [] = {
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_1  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_2  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_3  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_4  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_5,  MM_MODEM_BAND_EUTRAN_5  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_6,  MM_MODEM_BAND_EUTRAN_6  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_7  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_8  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_9,  MM_MODEM_BAND_EUTRAN_9  },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_10, MM_MODEM_BAND_EUTRAN_10 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_11, MM_MODEM_BAND_EUTRAN_11 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_12, MM_MODEM_BAND_EUTRAN_12 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_13, MM_MODEM_BAND_EUTRAN_13 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_14, MM_MODEM_BAND_EUTRAN_14 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_17, MM_MODEM_BAND_EUTRAN_17 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_18, MM_MODEM_BAND_EUTRAN_18 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_19, MM_MODEM_BAND_EUTRAN_19 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_20, MM_MODEM_BAND_EUTRAN_20 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_21, MM_MODEM_BAND_EUTRAN_21 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_24, MM_MODEM_BAND_EUTRAN_24 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_25, MM_MODEM_BAND_EUTRAN_25 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_26, MM_MODEM_BAND_EUTRAN_26 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_27, MM_MODEM_BAND_EUTRAN_27 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_28, MM_MODEM_BAND_EUTRAN_28 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_29, MM_MODEM_BAND_EUTRAN_29 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_30, MM_MODEM_BAND_EUTRAN_30 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_31, MM_MODEM_BAND_EUTRAN_31 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_32, MM_MODEM_BAND_EUTRAN_32 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_33, MM_MODEM_BAND_EUTRAN_33 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_34, MM_MODEM_BAND_EUTRAN_34 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_35, MM_MODEM_BAND_EUTRAN_35 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_36, MM_MODEM_BAND_EUTRAN_36 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_37, MM_MODEM_BAND_EUTRAN_37 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_38, MM_MODEM_BAND_EUTRAN_38 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_39, MM_MODEM_BAND_EUTRAN_39 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_40, MM_MODEM_BAND_EUTRAN_40 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_41, MM_MODEM_BAND_EUTRAN_41 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_42, MM_MODEM_BAND_EUTRAN_42 },
    { QMI_DMS_LTE_BAND_CAPABILITY_EUTRAN_43, MM_MODEM_BAND_EUTRAN_43 }
};

static void
dms_add_qmi_lte_bands (GArray *mm_bands,
                       QmiDmsLteBandCapability qmi_bands)
{
    /* All QMI LTE bands have a counterpart in ModemManager, no need to check
     * for unexpected ones */
    guint i;

    g_assert (mm_bands != NULL);

    for (i = 0; i < G_N_ELEMENTS (dms_lte_bands_map); i++) {
        if (qmi_bands & dms_lte_bands_map[i].qmi_band)
            g_array_append_val (mm_bands, dms_lte_bands_map[i].mm_band);
    }
}

static void
dms_add_extended_qmi_lte_bands (GArray   *mm_bands,
                                GArray   *extended_qmi_bands,
                                gpointer  log_object)
{
    guint i;

    g_assert (mm_bands != NULL);

    if (!extended_qmi_bands)
        return;

    for (i = 0; i < extended_qmi_bands->len; i++) {
        guint16 val;

        val = g_array_index (extended_qmi_bands, guint16, i);

        /* MM_MODEM_BAND_EUTRAN_1 = 31,
         * ...
         * MM_MODEM_BAND_EUTRAN_71 = 101
         */
        if (val < 1 || val > 71)
            mm_obj_dbg (log_object, "unexpected LTE band supported by module: EUTRAN %u", val);
        else {
            MMModemBand band;

            band = (MMModemBand)(val + MM_MODEM_BAND_EUTRAN_1 - 1);
            g_array_append_val (mm_bands, band);
        }
    }
}

GArray *
mm_modem_bands_from_qmi_band_capabilities (QmiDmsBandCapability     qmi_bands,
                                           QmiDmsLteBandCapability  qmi_lte_bands,
                                           GArray                  *extended_qmi_lte_bands,
                                           gpointer                 log_object)
{
    GArray *mm_bands;

    mm_bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
    dms_add_qmi_bands (mm_bands, qmi_bands, log_object);

    if (extended_qmi_lte_bands)
        dms_add_extended_qmi_lte_bands (mm_bands, extended_qmi_lte_bands, log_object);
    else
        dms_add_qmi_lte_bands (mm_bands, qmi_lte_bands);

    return mm_bands;
}

/*****************************************************************************/

typedef struct {
    QmiNasBandPreference qmi_band;
    MMModemBand mm_band;
} NasBandsMap;

static const NasBandsMap nas_bands_map [] = {
    /* CDMA bands */
    {
        (QMI_NAS_BAND_PREFERENCE_BC_0_A_SYSTEM | QMI_NAS_BAND_PREFERENCE_BC_0_B_SYSTEM),
        MM_MODEM_BAND_CDMA_BC0
    },
    { QMI_NAS_BAND_PREFERENCE_BC_1_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC1  },
    { QMI_NAS_BAND_PREFERENCE_BC_2,            MM_MODEM_BAND_CDMA_BC2  },
    { QMI_NAS_BAND_PREFERENCE_BC_3_A_SYSTEM,   MM_MODEM_BAND_CDMA_BC3  },
    { QMI_NAS_BAND_PREFERENCE_BC_4_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC4  },
    { QMI_NAS_BAND_PREFERENCE_BC_5_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC5  },
    { QMI_NAS_BAND_PREFERENCE_BC_6,            MM_MODEM_BAND_CDMA_BC6  },
    { QMI_NAS_BAND_PREFERENCE_BC_7,            MM_MODEM_BAND_CDMA_BC7  },
    { QMI_NAS_BAND_PREFERENCE_BC_8,            MM_MODEM_BAND_CDMA_BC8  },
    { QMI_NAS_BAND_PREFERENCE_BC_9,            MM_MODEM_BAND_CDMA_BC9  },
    { QMI_NAS_BAND_PREFERENCE_BC_10,           MM_MODEM_BAND_CDMA_BC10 },
    { QMI_NAS_BAND_PREFERENCE_BC_11,           MM_MODEM_BAND_CDMA_BC11 },
    { QMI_NAS_BAND_PREFERENCE_BC_12,           MM_MODEM_BAND_CDMA_BC12 },
    { QMI_NAS_BAND_PREFERENCE_BC_14,           MM_MODEM_BAND_CDMA_BC14 },
    { QMI_NAS_BAND_PREFERENCE_BC_15,           MM_MODEM_BAND_CDMA_BC15 },
    { QMI_NAS_BAND_PREFERENCE_BC_16,           MM_MODEM_BAND_CDMA_BC16 },
    { QMI_NAS_BAND_PREFERENCE_BC_17,           MM_MODEM_BAND_CDMA_BC17 },
    { QMI_NAS_BAND_PREFERENCE_BC_18,           MM_MODEM_BAND_CDMA_BC18 },
    { QMI_NAS_BAND_PREFERENCE_BC_19,           MM_MODEM_BAND_CDMA_BC19 },

    /* GSM bands */
    { QMI_NAS_BAND_PREFERENCE_GSM_DCS_1800,     MM_MODEM_BAND_DCS  },
    {
        (QMI_NAS_BAND_PREFERENCE_GSM_900_PRIMARY | QMI_NAS_BAND_PREFERENCE_GSM_900_EXTENDED),
        MM_MODEM_BAND_EGSM
    },
    { QMI_NAS_BAND_PREFERENCE_GSM_PCS_1900,     MM_MODEM_BAND_PCS  },
    { QMI_NAS_BAND_PREFERENCE_GSM_850,          MM_MODEM_BAND_G850 },
    { QMI_NAS_BAND_PREFERENCE_GSM_450,          MM_MODEM_BAND_G450 },
    { QMI_NAS_BAND_PREFERENCE_GSM_480,          MM_MODEM_BAND_G480 },
    { QMI_NAS_BAND_PREFERENCE_GSM_750,          MM_MODEM_BAND_G750 },

    /* UMTS/WCDMA bands */
    { QMI_NAS_BAND_PREFERENCE_WCDMA_2100,       MM_MODEM_BAND_UTRAN_1  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_DCS_1800,   MM_MODEM_BAND_UTRAN_3  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_PCS_1900,   MM_MODEM_BAND_UTRAN_2  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_1700_US,    MM_MODEM_BAND_UTRAN_4  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_800,        MM_MODEM_BAND_UTRAN_6  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_850_US,     MM_MODEM_BAND_UTRAN_5  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_900,        MM_MODEM_BAND_UTRAN_8  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_1700_JAPAN, MM_MODEM_BAND_UTRAN_9  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_2600,       MM_MODEM_BAND_UTRAN_7  },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_1500,       MM_MODEM_BAND_UTRAN_11 },
    { QMI_NAS_BAND_PREFERENCE_WCDMA_850_JAPAN,  MM_MODEM_BAND_UTRAN_19 },
};

static void
nas_add_qmi_bands (GArray               *mm_bands,
                   QmiNasBandPreference  qmi_bands,
                   gpointer              log_object)
{
    static QmiNasBandPreference qmi_bands_expected = 0;
    QmiNasBandPreference not_expected;
    guint i;

    g_assert (mm_bands != NULL);

    /* Build mask of expected bands only once */
    if (G_UNLIKELY (qmi_bands_expected == 0)) {
        for (i = 0; i < G_N_ELEMENTS (nas_bands_map); i++) {
            qmi_bands_expected |= nas_bands_map[i].qmi_band;
        }
    }

    /* Log about the bands that cannot be represented in ModemManager */
    not_expected = ((qmi_bands_expected ^ qmi_bands) & qmi_bands);
    if (not_expected) {
        g_autofree gchar *aux = NULL;

        aux = qmi_nas_band_preference_build_string_from_mask (not_expected);
        mm_obj_dbg (log_object, "cannot add the following bands: '%s'", aux);
    }

    /* And add the expected ones */
    for (i = 0; i < G_N_ELEMENTS (nas_bands_map); i++) {
        if (qmi_bands & nas_bands_map[i].qmi_band)
            g_array_append_val (mm_bands, nas_bands_map[i].mm_band);
    }
}

typedef struct {
    QmiNasLteBandPreference qmi_band;
    MMModemBand mm_band;
} NasLteBandsMap;

static const NasLteBandsMap nas_lte_bands_map [] = {
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_1  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_2  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_3  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_4  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_5,  MM_MODEM_BAND_EUTRAN_5  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_6,  MM_MODEM_BAND_EUTRAN_6  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_7  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_8  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_9,  MM_MODEM_BAND_EUTRAN_9  },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_10, MM_MODEM_BAND_EUTRAN_10 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_11, MM_MODEM_BAND_EUTRAN_11 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_12, MM_MODEM_BAND_EUTRAN_12 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_13, MM_MODEM_BAND_EUTRAN_13 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_14, MM_MODEM_BAND_EUTRAN_14 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_17, MM_MODEM_BAND_EUTRAN_17 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_18, MM_MODEM_BAND_EUTRAN_18 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_19, MM_MODEM_BAND_EUTRAN_19 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_20, MM_MODEM_BAND_EUTRAN_20 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_21, MM_MODEM_BAND_EUTRAN_21 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_24, MM_MODEM_BAND_EUTRAN_24 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_25, MM_MODEM_BAND_EUTRAN_25 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_26, MM_MODEM_BAND_EUTRAN_26 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_27, MM_MODEM_BAND_EUTRAN_27 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_28, MM_MODEM_BAND_EUTRAN_28 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_29, MM_MODEM_BAND_EUTRAN_29 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_30, MM_MODEM_BAND_EUTRAN_30 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_31, MM_MODEM_BAND_EUTRAN_31 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_32, MM_MODEM_BAND_EUTRAN_32 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_33, MM_MODEM_BAND_EUTRAN_33 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_34, MM_MODEM_BAND_EUTRAN_34 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_35, MM_MODEM_BAND_EUTRAN_35 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_36, MM_MODEM_BAND_EUTRAN_36 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_37, MM_MODEM_BAND_EUTRAN_37 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_38, MM_MODEM_BAND_EUTRAN_38 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_39, MM_MODEM_BAND_EUTRAN_39 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_40, MM_MODEM_BAND_EUTRAN_40 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_41, MM_MODEM_BAND_EUTRAN_41 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_42, MM_MODEM_BAND_EUTRAN_42 },
    { QMI_NAS_LTE_BAND_PREFERENCE_EUTRAN_43, MM_MODEM_BAND_EUTRAN_43 }
};

static void
nas_add_qmi_lte_bands (GArray *mm_bands,
                       QmiNasLteBandPreference qmi_bands)
{
    /* All QMI LTE bands have a counterpart in ModemManager, no need to check
     * for unexpected ones */
    guint i;

    g_assert (mm_bands != NULL);

    for (i = 0; i < G_N_ELEMENTS (nas_lte_bands_map); i++) {
        if (qmi_bands & nas_lte_bands_map[i].qmi_band)
            g_array_append_val (mm_bands, nas_lte_bands_map[i].mm_band);
    }
}

static void
nas_add_extended_qmi_lte_bands (GArray        *mm_bands,
                                const guint64 *extended_qmi_lte_bands,
                                guint          extended_qmi_lte_bands_size,
                                gpointer       log_object)
{
    guint i;

    g_assert (mm_bands != NULL);

    for (i = 0; i < extended_qmi_lte_bands_size; i++) {
        guint j;

        for (j = 0; j < 64; j++) {
            guint val;

            if (!(extended_qmi_lte_bands[i] & (((guint64) 1) << j)))
                continue;

            val = 1 + j + (i * 64);

            /* MM_MODEM_BAND_EUTRAN_1 = 31,
             * ...
             * MM_MODEM_BAND_EUTRAN_71 = 101
             */
            if (val < 1 || val > 71)
                mm_obj_dbg (log_object, "unexpected LTE band supported by module: EUTRAN %u", val);
            else {
                MMModemBand band;

                band = (val + MM_MODEM_BAND_EUTRAN_1 - 1);
                g_array_append_val (mm_bands, band);
            }
        }
    }
}

GArray *
mm_modem_bands_from_qmi_band_preference (QmiNasBandPreference     qmi_bands,
                                         QmiNasLteBandPreference  qmi_lte_bands,
                                         const guint64           *extended_qmi_lte_bands,
                                         guint                    extended_qmi_lte_bands_size,
                                         gpointer                 log_object)
{
    GArray *mm_bands;

    mm_bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
    nas_add_qmi_bands (mm_bands, qmi_bands, log_object);

    if (extended_qmi_lte_bands && extended_qmi_lte_bands_size)
        nas_add_extended_qmi_lte_bands (mm_bands, extended_qmi_lte_bands, extended_qmi_lte_bands_size, log_object);
    else
        nas_add_qmi_lte_bands (mm_bands, qmi_lte_bands);

    return mm_bands;
}

void
mm_modem_bands_to_qmi_band_preference (GArray                  *mm_bands,
                                       QmiNasBandPreference    *qmi_bands,
                                       QmiNasLteBandPreference *qmi_lte_bands,
                                       guint64                 *extended_qmi_lte_bands,
                                       guint                    extended_qmi_lte_bands_size,
                                       gpointer                 log_object)
{
    guint i;

    *qmi_bands = 0;
    *qmi_lte_bands = 0;
    if (extended_qmi_lte_bands)
        memset (extended_qmi_lte_bands, 0, extended_qmi_lte_bands_size * sizeof (guint64));

    for (i = 0; i < mm_bands->len; i++) {
        MMModemBand band;

        band = g_array_index (mm_bands, MMModemBand, i);

        if (band >= MM_MODEM_BAND_EUTRAN_1 && band <= MM_MODEM_BAND_EUTRAN_71) {
            if (extended_qmi_lte_bands && extended_qmi_lte_bands_size) {
                /* Add extended LTE band preference */
                guint val;
                guint j;
                guint k;

                /* it's really (band - MM_MODEM_BAND_EUTRAN_1 +1 -1), because
                 * we want EUTRAN1 in index 0 */
                val = band - MM_MODEM_BAND_EUTRAN_1;
                j = val / 64;
                g_assert (j < extended_qmi_lte_bands_size);
                k = val % 64;

                extended_qmi_lte_bands[j] |= ((guint64)1 << k);
            } else {
                /* Add LTE band preference */
                guint j;

                for (j = 0; j < G_N_ELEMENTS (nas_lte_bands_map); j++) {
                    if (nas_lte_bands_map[j].mm_band == band) {
                        *qmi_lte_bands |= nas_lte_bands_map[j].qmi_band;
                        break;
                    }
                }

                if (j == G_N_ELEMENTS (nas_lte_bands_map))
                    mm_obj_dbg (log_object, "cannot add the following LTE band: '%s'",
                                mm_modem_band_get_string (band));
            }
        } else {
            /* Add non-LTE band preference */
            guint j;

            for (j = 0; j < G_N_ELEMENTS (nas_bands_map); j++) {
                if (nas_bands_map[j].mm_band == band) {
                    *qmi_bands |= nas_bands_map[j].qmi_band;
                    break;
                }
            }

            if (j == G_N_ELEMENTS (nas_bands_map))
                mm_obj_dbg (log_object, "cannot add the following band: '%s'",
                            mm_modem_band_get_string (band));
        }
    }
}

/*****************************************************************************/

typedef struct {
    QmiNasActiveBand qmi_band;
    MMModemBand mm_band;
} ActiveBandsMap;

static const ActiveBandsMap active_bands_map [] = {
    /* CDMA bands */
    { QMI_NAS_ACTIVE_BAND_BC_0,  MM_MODEM_BAND_CDMA_BC0  },
    { QMI_NAS_ACTIVE_BAND_BC_1,  MM_MODEM_BAND_CDMA_BC1  },
    { QMI_NAS_ACTIVE_BAND_BC_2,  MM_MODEM_BAND_CDMA_BC2  },
    { QMI_NAS_ACTIVE_BAND_BC_3,  MM_MODEM_BAND_CDMA_BC3  },
    { QMI_NAS_ACTIVE_BAND_BC_4,  MM_MODEM_BAND_CDMA_BC4  },
    { QMI_NAS_ACTIVE_BAND_BC_5,  MM_MODEM_BAND_CDMA_BC5  },
    { QMI_NAS_ACTIVE_BAND_BC_6,  MM_MODEM_BAND_CDMA_BC6  },
    { QMI_NAS_ACTIVE_BAND_BC_7,  MM_MODEM_BAND_CDMA_BC7  },
    { QMI_NAS_ACTIVE_BAND_BC_8,  MM_MODEM_BAND_CDMA_BC8  },
    { QMI_NAS_ACTIVE_BAND_BC_9,  MM_MODEM_BAND_CDMA_BC9  },
    { QMI_NAS_ACTIVE_BAND_BC_10, MM_MODEM_BAND_CDMA_BC10 },
    { QMI_NAS_ACTIVE_BAND_BC_11, MM_MODEM_BAND_CDMA_BC11 },
    { QMI_NAS_ACTIVE_BAND_BC_12, MM_MODEM_BAND_CDMA_BC12 },
    { QMI_NAS_ACTIVE_BAND_BC_13, MM_MODEM_BAND_CDMA_BC13 },
    { QMI_NAS_ACTIVE_BAND_BC_14, MM_MODEM_BAND_CDMA_BC14 },
    { QMI_NAS_ACTIVE_BAND_BC_15, MM_MODEM_BAND_CDMA_BC15 },
    { QMI_NAS_ACTIVE_BAND_BC_16, MM_MODEM_BAND_CDMA_BC16 },
    { QMI_NAS_ACTIVE_BAND_BC_17, MM_MODEM_BAND_CDMA_BC17 },
    { QMI_NAS_ACTIVE_BAND_BC_18, MM_MODEM_BAND_CDMA_BC18 },
    { QMI_NAS_ACTIVE_BAND_BC_19, MM_MODEM_BAND_CDMA_BC19 },

    /* GSM bands */
    { QMI_NAS_ACTIVE_BAND_GSM_850,          MM_MODEM_BAND_G850 },
    { QMI_NAS_ACTIVE_BAND_GSM_900_EXTENDED, MM_MODEM_BAND_EGSM },
    { QMI_NAS_ACTIVE_BAND_GSM_DCS_1800,     MM_MODEM_BAND_DCS  },
    { QMI_NAS_ACTIVE_BAND_GSM_PCS_1900,     MM_MODEM_BAND_PCS  },
    { QMI_NAS_ACTIVE_BAND_GSM_450,          MM_MODEM_BAND_G450 },
    { QMI_NAS_ACTIVE_BAND_GSM_480,          MM_MODEM_BAND_G480 },
    { QMI_NAS_ACTIVE_BAND_GSM_750,          MM_MODEM_BAND_G750 },

    /* WCDMA bands */
    { QMI_NAS_ACTIVE_BAND_WCDMA_2100,       MM_MODEM_BAND_UTRAN_1  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_PCS_1900,   MM_MODEM_BAND_UTRAN_2  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_DCS_1800,   MM_MODEM_BAND_UTRAN_3  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_1700_US,    MM_MODEM_BAND_UTRAN_4  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_850,        MM_MODEM_BAND_UTRAN_5  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_800,        MM_MODEM_BAND_UTRAN_6  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_2600,       MM_MODEM_BAND_UTRAN_7  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_900,        MM_MODEM_BAND_UTRAN_8  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_1700_JAPAN, MM_MODEM_BAND_UTRAN_9  },
    { QMI_NAS_ACTIVE_BAND_WCDMA_1500_JAPAN, MM_MODEM_BAND_UTRAN_11 },
    { QMI_NAS_ACTIVE_BAND_WCDMA_850_JAPAN,  MM_MODEM_BAND_UTRAN_19 },

    /* LTE bands */
    { QMI_NAS_ACTIVE_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_1  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_2  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_3  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_4  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_5,  MM_MODEM_BAND_EUTRAN_5  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_6,  MM_MODEM_BAND_EUTRAN_6  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_7  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_8  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_9,  MM_MODEM_BAND_EUTRAN_9  },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_10, MM_MODEM_BAND_EUTRAN_10 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_11 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_12 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_13 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_14, MM_MODEM_BAND_EUTRAN_14 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_17 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_18 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_19 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_20 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_21, MM_MODEM_BAND_EUTRAN_21 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_24, MM_MODEM_BAND_EUTRAN_24 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_25, MM_MODEM_BAND_EUTRAN_25 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_33, MM_MODEM_BAND_EUTRAN_33 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_34, MM_MODEM_BAND_EUTRAN_34 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_35, MM_MODEM_BAND_EUTRAN_35 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_36, MM_MODEM_BAND_EUTRAN_36 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_37, MM_MODEM_BAND_EUTRAN_37 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_38, MM_MODEM_BAND_EUTRAN_38 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_39 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_40 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_41 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_42, MM_MODEM_BAND_EUTRAN_42 },
    { QMI_NAS_ACTIVE_BAND_EUTRAN_43, MM_MODEM_BAND_EUTRAN_43 }
};

static void
add_active_bands (GArray *mm_bands,
                  QmiNasActiveBand qmi_bands)
{
    guint i;

    g_assert (mm_bands != NULL);

    for (i = 0; i < G_N_ELEMENTS (active_bands_map); i++) {
        if (qmi_bands == active_bands_map[i].qmi_band) {
            guint j;

            /* Avoid adding duplicate band entries */
            for (j = 0; j < mm_bands->len; j++) {
                if (g_array_index (mm_bands, MMModemBand, j) == active_bands_map[i].mm_band)
                    return;
            }

            g_array_append_val (mm_bands, active_bands_map[i].mm_band);
            return;
        }
    }
}

GArray *
mm_modem_bands_from_qmi_rf_band_information_array (GArray *info_array)
{
    GArray *mm_bands;

    mm_bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    if (info_array) {
        guint i;

        for (i = 0; i < info_array->len; i++) {
            QmiMessageNasGetRfBandInformationOutputListElement *item;

            item = &g_array_index (info_array, QmiMessageNasGetRfBandInformationOutputListElement, i);
            add_active_bands (mm_bands, item->active_band_class);
        }
    }

    return mm_bands;
}

/*****************************************************************************/

MMModemAccessTechnology
mm_modem_access_technology_from_qmi_radio_interface (QmiNasRadioInterface interface)
{
    switch (interface) {
    case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case QMI_NAS_RADIO_INTERFACE_GSM:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case QMI_NAS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case QMI_NAS_RADIO_INTERFACE_LTE:
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    case QMI_NAS_RADIO_INTERFACE_5GNR:
        return MM_MODEM_ACCESS_TECHNOLOGY_5GNR;
    case QMI_NAS_RADIO_INTERFACE_UNKNOWN:
    case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
    case QMI_NAS_RADIO_INTERFACE_AMPS:
    case QMI_NAS_RADIO_INTERFACE_NONE:
    default:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
}

/*****************************************************************************/

MMModemAccessTechnology
mm_modem_access_technologies_from_qmi_radio_interface_array (GArray *radio_interfaces)
{
    MMModemAccessTechnology access_technology = 0;
    guint i;

    for (i = 0; i < radio_interfaces->len; i++) {
        QmiNasRadioInterface iface;

        iface = g_array_index (radio_interfaces, QmiNasRadioInterface, i);
        access_technology |= mm_modem_access_technology_from_qmi_radio_interface (iface);
    }

    return access_technology;
}

/*****************************************************************************/

MMModemAccessTechnology
mm_modem_access_technology_from_qmi_data_capability (QmiNasDataCapability cap)
{
    switch (cap) {
    case QMI_NAS_DATA_CAPABILITY_GPRS:
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case QMI_NAS_DATA_CAPABILITY_EDGE:
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    case QMI_NAS_DATA_CAPABILITY_HSDPA:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    case QMI_NAS_DATA_CAPABILITY_HSUPA:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    case QMI_NAS_DATA_CAPABILITY_WCDMA:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case QMI_NAS_DATA_CAPABILITY_CDMA:
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case QMI_NAS_DATA_CAPABILITY_EVDO_REV_0:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case QMI_NAS_DATA_CAPABILITY_EVDO_REV_A:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    case QMI_NAS_DATA_CAPABILITY_GSM:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case QMI_NAS_DATA_CAPABILITY_EVDO_REV_B:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;
    case QMI_NAS_DATA_CAPABILITY_LTE:
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    case QMI_NAS_DATA_CAPABILITY_HSDPA_PLUS:
    case QMI_NAS_DATA_CAPABILITY_DC_HSDPA_PLUS:
        return MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS;
    case QMI_NAS_DATA_CAPABILITY_NONE:
    default:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
}

/*****************************************************************************/

MMModemAccessTechnology
mm_modem_access_technologies_from_qmi_data_capability_array (GArray *data_capabilities)
{
    MMModemAccessTechnology access_technology = 0;
    guint i;

    for (i = 0; i < data_capabilities->len; i++) {
        QmiNasDataCapability cap;

        cap = g_array_index (data_capabilities, QmiNasDataCapability, i);
        access_technology |= mm_modem_access_technology_from_qmi_data_capability (cap);
    }

    return access_technology;
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_qmi_nas_radio_interface (QmiNasRadioInterface iface)
{
    switch (iface) {
        case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
        case QMI_NAS_RADIO_INTERFACE_GSM:
            return MM_MODEM_MODE_2G;
        case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
        case QMI_NAS_RADIO_INTERFACE_UMTS:
            return MM_MODEM_MODE_3G;
        case QMI_NAS_RADIO_INTERFACE_LTE:
            return MM_MODEM_MODE_4G;
        case QMI_NAS_RADIO_INTERFACE_5GNR:
            return MM_MODEM_MODE_5G;
        case QMI_NAS_RADIO_INTERFACE_NONE:
        case QMI_NAS_RADIO_INTERFACE_AMPS:
        case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
        case QMI_NAS_RADIO_INTERFACE_UNKNOWN:
        default:
            return MM_MODEM_MODE_NONE;
    }
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_qmi_radio_technology_preference (QmiNasRadioTechnologyPreference qmi)
{
    MMModemMode mode = MM_MODEM_MODE_NONE;

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2) {
        /* Ignore AMPS, we really don't report CS mode in QMI modems */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA)
            mode |= MM_MODEM_MODE_2G; /* CDMA */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR)
            mode |= MM_MODEM_MODE_3G; /* EV-DO */
    }

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP) {
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM)
            mode |= MM_MODEM_MODE_2G; /* GSM */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA)
            mode |= MM_MODEM_MODE_3G; /* WCDMA */
    }

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE)
        mode |= MM_MODEM_MODE_4G;

    return mode;
}

QmiNasRadioTechnologyPreference
mm_modem_mode_to_qmi_radio_technology_preference (MMModemMode mode,
                                                  gboolean is_cdma)
{
    QmiNasRadioTechnologyPreference pref = 0;

    if (is_cdma) {
        pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2;
        if (mode & MM_MODEM_MODE_2G)
            pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA; /* CDMA */
        if (mode & MM_MODEM_MODE_3G)
            pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR; /* EV-DO */
    } else {
        pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP;
        if (mode & MM_MODEM_MODE_2G)
            pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM; /* GSM */
        if (mode & MM_MODEM_MODE_3G)
            pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA; /* WCDMA */
    }

    if (mode & MM_MODEM_MODE_4G)
        pref |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE;

    return pref;
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_qmi_rat_mode_preference (QmiNasRatModePreference qmi)
{
    MMModemMode mode = MM_MODEM_MODE_NONE;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X)
        mode |= MM_MODEM_MODE_2G;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO)
        mode |= MM_MODEM_MODE_3G;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_GSM)
        mode |= MM_MODEM_MODE_2G;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_UMTS)
        mode |= MM_MODEM_MODE_3G;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_LTE)
        mode |= MM_MODEM_MODE_4G;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_5GNR)
        mode |= MM_MODEM_MODE_5G;

    return mode;
}

QmiNasRatModePreference
mm_modem_mode_to_qmi_rat_mode_preference (MMModemMode mode,
                                          gboolean is_cdma,
                                          gboolean is_3gpp)
{
    QmiNasRatModePreference pref = 0;

    if (is_cdma) {
        if (mode & MM_MODEM_MODE_2G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X;

        if (mode & MM_MODEM_MODE_3G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO;
    }

    if (is_3gpp) {
        if (mode & MM_MODEM_MODE_2G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_GSM;

        if (mode & MM_MODEM_MODE_3G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_UMTS;

        if (mode & MM_MODEM_MODE_4G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_LTE;

        if (mode & MM_MODEM_MODE_5G)
            pref |= QMI_NAS_RAT_MODE_PREFERENCE_5GNR;
    }

    return pref;
}

/*****************************************************************************/

MMModemCapability
mm_modem_capability_from_qmi_rat_mode_preference (QmiNasRatModePreference qmi)
{
    MMModemCapability caps = MM_MODEM_CAPABILITY_NONE;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X)
        caps |= MM_MODEM_CAPABILITY_CDMA_EVDO;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO)
        caps |= MM_MODEM_CAPABILITY_CDMA_EVDO;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_GSM)
        caps |= MM_MODEM_CAPABILITY_GSM_UMTS;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_UMTS)
        caps |= MM_MODEM_CAPABILITY_GSM_UMTS;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_LTE)
        caps |= MM_MODEM_CAPABILITY_LTE;

    if (qmi & QMI_NAS_RAT_MODE_PREFERENCE_5GNR)
        caps |= MM_MODEM_CAPABILITY_5GNR;

    return caps;
}

QmiNasRatModePreference
mm_modem_capability_to_qmi_rat_mode_preference (MMModemCapability caps)
{
    QmiNasRatModePreference qmi = 0;

    if (caps & MM_MODEM_CAPABILITY_CDMA_EVDO) {
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X;
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO;
    }

    if (caps & MM_MODEM_CAPABILITY_GSM_UMTS) {
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_GSM;
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_UMTS;
    }

    if (caps & MM_MODEM_CAPABILITY_LTE)
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_LTE;

    if (caps & MM_MODEM_CAPABILITY_5GNR)
        qmi |= QMI_NAS_RAT_MODE_PREFERENCE_5GNR;

    return qmi;
}

/*****************************************************************************/

GArray *
mm_modem_capability_to_qmi_acquisition_order_preference (MMModemCapability caps)
{
    GArray               *array;
    QmiNasRadioInterface  value;

    array = g_array_new (FALSE, FALSE, sizeof (QmiNasRadioInterface));

    if (caps & MM_MODEM_CAPABILITY_5GNR) {
        value = QMI_NAS_RADIO_INTERFACE_5GNR;
        g_array_append_val (array, value);
    }

    if (caps & MM_MODEM_CAPABILITY_LTE) {
        value = QMI_NAS_RADIO_INTERFACE_LTE;
        g_array_append_val (array, value);
    }

    if (caps & MM_MODEM_CAPABILITY_GSM_UMTS) {
        value = QMI_NAS_RADIO_INTERFACE_UMTS;
        g_array_append_val (array, value);
        value = QMI_NAS_RADIO_INTERFACE_GSM;
        g_array_append_val (array, value);
    }

    if (caps & MM_MODEM_CAPABILITY_CDMA_EVDO) {
        value = QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO;
        g_array_append_val (array, value);
        value = QMI_NAS_RADIO_INTERFACE_CDMA_1X;
        g_array_append_val (array, value);
    }

    return array;
}

static gboolean
radio_interface_array_contains (GArray               *array,
                                QmiNasRadioInterface  act)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        QmiNasRadioInterface value;

        value = g_array_index (array, QmiNasRadioInterface, i);
        if (value == act)
            return TRUE;
    }
    return FALSE;
}

static void
radio_interface_array_add_missing (GArray *array,
                                   GArray *all)
{
    guint i;

    for (i = 0; i < all->len; i++) {
        QmiNasRadioInterface value;

        value = g_array_index (all, QmiNasRadioInterface, i);
        if (!radio_interface_array_contains (array, value))
            g_array_append_val (array, value);
    }
}

GArray *
mm_modem_mode_to_qmi_acquisition_order_preference (MMModemMode  allowed,
                                                   MMModemMode  preferred,
                                                   GArray      *all)
{
    GArray               *array;
    QmiNasRadioInterface  preferred_radio = QMI_NAS_RADIO_INTERFACE_UNKNOWN;
    QmiNasRadioInterface  value;

    array = g_array_sized_new (FALSE, FALSE, sizeof (QmiNasRadioInterface), all->len);

#define PROCESS_ALLOWED_PREFERRED_MODE(MODE,RADIO)                      \
    if ((allowed & MODE) && (radio_interface_array_contains (all, RADIO))) { \
        if ((preferred == MODE) && (preferred_radio == QMI_NAS_RADIO_INTERFACE_UNKNOWN)) \
            preferred_radio = RADIO;                                    \
        else {                                                          \
            value = RADIO;                                              \
            g_array_append_val (array, value);                          \
        }                                                               \
    }

    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_5G, QMI_NAS_RADIO_INTERFACE_5GNR);
    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_4G, QMI_NAS_RADIO_INTERFACE_LTE);
    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_3G, QMI_NAS_RADIO_INTERFACE_UMTS);
    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_3G, QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO);
    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_2G, QMI_NAS_RADIO_INTERFACE_GSM);
    PROCESS_ALLOWED_PREFERRED_MODE (MM_MODEM_MODE_2G, QMI_NAS_RADIO_INTERFACE_CDMA_1X);

#undef PROCESS_ALLOWED_PREFERRED_MODE

    if (preferred_radio != QMI_NAS_RADIO_INTERFACE_UNKNOWN)
        g_array_prepend_val (array, preferred_radio);

    /* the acquisition order preference is a TLV that must ALWAYS contain the
     * same list of QmiNasRadioInterface values, just with a different order. */
    radio_interface_array_add_missing (array, all);
    g_assert_cmpuint (array->len, ==, all->len);

    return array;
}

/*****************************************************************************/

MMModemCapability
mm_modem_capability_from_qmi_radio_technology_preference (QmiNasRadioTechnologyPreference qmi)
{
    MMModemCapability caps = MM_MODEM_CAPABILITY_NONE;

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2) {
        /* Skip AMPS */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA)
            caps |= MM_MODEM_CAPABILITY_CDMA_EVDO; /* CDMA */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR)
            caps |= MM_MODEM_CAPABILITY_CDMA_EVDO; /* EV-DO */
    }

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP) {
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM)
            caps |= MM_MODEM_CAPABILITY_GSM_UMTS; /* GSM */
        if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA)
            caps |= MM_MODEM_CAPABILITY_GSM_UMTS; /* WCDMA */
    }

    if (qmi & QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE)
        caps |= MM_MODEM_CAPABILITY_LTE;

    /* NOTE: no 5GNR defined in Technology Preference */

    return caps;
}

QmiNasRadioTechnologyPreference
mm_modem_capability_to_qmi_radio_technology_preference (MMModemCapability caps)
{
    QmiNasRadioTechnologyPreference qmi = 0;

    if (caps & MM_MODEM_CAPABILITY_GSM_UMTS) {
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP;
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AMPS_OR_GSM;
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA;
    }

    if (caps & MM_MODEM_CAPABILITY_CDMA_EVDO) {
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_3GPP2;
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_CDMA_OR_WCDMA;
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_HDR;
    }

    if (caps & MM_MODEM_CAPABILITY_LTE)
        qmi |= QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_LTE;

    return qmi;
}

/*****************************************************************************/

#define ALL_3GPP2_BANDS                         \
    (QMI_NAS_BAND_PREFERENCE_BC_0_A_SYSTEM   |  \
     QMI_NAS_BAND_PREFERENCE_BC_0_B_SYSTEM   |  \
     QMI_NAS_BAND_PREFERENCE_BC_1_ALL_BLOCKS |  \
     QMI_NAS_BAND_PREFERENCE_BC_2            |  \
     QMI_NAS_BAND_PREFERENCE_BC_3_A_SYSTEM   |  \
     QMI_NAS_BAND_PREFERENCE_BC_4_ALL_BLOCKS |  \
     QMI_NAS_BAND_PREFERENCE_BC_5_ALL_BLOCKS |  \
     QMI_NAS_BAND_PREFERENCE_BC_6            |  \
     QMI_NAS_BAND_PREFERENCE_BC_7            |  \
     QMI_NAS_BAND_PREFERENCE_BC_8            |  \
     QMI_NAS_BAND_PREFERENCE_BC_9            |  \
     QMI_NAS_BAND_PREFERENCE_BC_10           |  \
     QMI_NAS_BAND_PREFERENCE_BC_11           |  \
     QMI_NAS_BAND_PREFERENCE_BC_12           |  \
     QMI_NAS_BAND_PREFERENCE_BC_14           |  \
     QMI_NAS_BAND_PREFERENCE_BC_15           |  \
     QMI_NAS_BAND_PREFERENCE_BC_16           |  \
     QMI_NAS_BAND_PREFERENCE_BC_17           |  \
     QMI_NAS_BAND_PREFERENCE_BC_18           |  \
     QMI_NAS_BAND_PREFERENCE_BC_19)

#define ALL_3GPP_BANDS                          \
    (QMI_NAS_BAND_PREFERENCE_GSM_DCS_1800     | \
     QMI_NAS_BAND_PREFERENCE_GSM_900_EXTENDED | \
     QMI_NAS_BAND_PREFERENCE_GSM_900_PRIMARY  | \
     QMI_NAS_BAND_PREFERENCE_GSM_450          | \
     QMI_NAS_BAND_PREFERENCE_GSM_480          | \
     QMI_NAS_BAND_PREFERENCE_GSM_750          | \
     QMI_NAS_BAND_PREFERENCE_GSM_850          | \
     QMI_NAS_BAND_PREFERENCE_GSM_900_RAILWAYS | \
     QMI_NAS_BAND_PREFERENCE_GSM_PCS_1900     | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_2100       | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_PCS_1900   | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_DCS_1800   | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_1700_US    | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_850_US     | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_800        | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_2600       | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_900        | \
     QMI_NAS_BAND_PREFERENCE_WCDMA_1700_JAPAN)

MMModemCapability
mm_modem_capability_from_qmi_band_preference (QmiNasBandPreference qmi)
{
    MMModemCapability caps = MM_MODEM_CAPABILITY_NONE;

    if (qmi & ALL_3GPP_BANDS)
        caps |= MM_MODEM_CAPABILITY_GSM_UMTS;

    if (qmi & ALL_3GPP2_BANDS)
        caps |= MM_MODEM_CAPABILITY_CDMA_EVDO;

    return caps;
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_qmi_gsm_wcdma_acquisition_order_preference (QmiNasGsmWcdmaAcquisitionOrderPreference qmi,
                                                               gpointer                                 log_object)
{
    switch (qmi) {
    case QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_AUTOMATIC:
        return MM_MODEM_MODE_NONE;
    case QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_GSM:
        return MM_MODEM_MODE_2G;
    case QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_WCDMA:
        return MM_MODEM_MODE_3G;
    default:
        mm_obj_dbg (log_object, "unknown acquisition order preference: '%s'",
                    qmi_nas_gsm_wcdma_acquisition_order_preference_get_string (qmi));
        return MM_MODEM_MODE_NONE;
    }
}

QmiNasGsmWcdmaAcquisitionOrderPreference
mm_modem_mode_to_qmi_gsm_wcdma_acquisition_order_preference (MMModemMode mode,
                                                             gpointer    log_object)
{
    g_autofree gchar *str = NULL;

    /* mode is not a mask in this case, only a value */

    switch (mode) {
    case MM_MODEM_MODE_3G:
        return QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_WCDMA;
    case MM_MODEM_MODE_2G:
        return QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_GSM;
    case MM_MODEM_MODE_NONE:
        return QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_AUTOMATIC;
    case MM_MODEM_MODE_CS:
    case MM_MODEM_MODE_4G:
    case MM_MODEM_MODE_5G:
    case MM_MODEM_MODE_ANY:
    default:
        break;
    }

    str = mm_modem_mode_build_string_from_mask (mode);
    mm_obj_dbg (log_object, "unhandled modem mode: '%s'", str);

    return QMI_NAS_GSM_WCDMA_ACQUISITION_ORDER_PREFERENCE_AUTOMATIC;
}

/*****************************************************************************/

MMModem3gppRegistrationState
mm_modem_3gpp_registration_state_from_qmi_registration_state (QmiNasAttachState attach_state,
                                                              QmiNasRegistrationState registration_state,
                                                              gboolean roaming)
{
    if (attach_state == QMI_NAS_ATTACH_STATE_UNKNOWN)
        return MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

    if (attach_state == QMI_NAS_ATTACH_STATE_DETACHED)
        return MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;

    /* attached */

    switch (registration_state) {
    case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED:
        return MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
    case QMI_NAS_REGISTRATION_STATE_REGISTERED:
        return (roaming ? MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING : MM_MODEM_3GPP_REGISTRATION_STATE_HOME);
    case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING:
        return MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;
    case QMI_NAS_REGISTRATION_STATE_REGISTRATION_DENIED:
        return MM_MODEM_3GPP_REGISTRATION_STATE_DENIED;
    case QMI_NAS_REGISTRATION_STATE_UNKNOWN:
    default:
        return MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

MMModemCdmaRegistrationState
mm_modem_cdma_registration_state_from_qmi_registration_state (QmiNasRegistrationState registration_state)
{
    switch (registration_state) {
    case QMI_NAS_REGISTRATION_STATE_REGISTERED:
        return MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
    case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED:
    case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING:
    case QMI_NAS_REGISTRATION_STATE_REGISTRATION_DENIED:
    case QMI_NAS_REGISTRATION_STATE_UNKNOWN:
    default:
        return MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

MMModemCdmaActivationState
mm_modem_cdma_activation_state_from_qmi_activation_state (QmiDmsActivationState state)
{
    switch (state) {
    case QMI_DMS_ACTIVATION_STATE_NOT_ACTIVATED:
        return MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED;
    case QMI_DMS_ACTIVATION_STATE_ACTIVATED:
        return MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED;
    case QMI_DMS_ACTIVATION_STATE_CONNECTING:
    case QMI_DMS_ACTIVATION_STATE_CONNECTED:
    case QMI_DMS_ACTIVATION_STATE_OTASP_AUTHENTICATED:
    case QMI_DMS_ACTIVATION_STATE_OTASP_NAM:
    case QMI_DMS_ACTIVATION_STATE_OTASP_MDN:
    case QMI_DMS_ACTIVATION_STATE_OTASP_IMSI:
    case QMI_DMS_ACTIVATION_STATE_OTASP_PRL:
    case QMI_DMS_ACTIVATION_STATE_OTASP_SPC:
        return MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING;
    case QMI_DMS_ACTIVATION_STATE_OTASP_COMMITED:
        return MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED;

    default:
        return MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

QmiWmsStorageType
mm_sms_storage_to_qmi_storage_type (MMSmsStorage storage)
{
    switch (storage) {
    case MM_SMS_STORAGE_SM:
        return QMI_WMS_STORAGE_TYPE_UIM;
    case MM_SMS_STORAGE_ME:
        return QMI_WMS_STORAGE_TYPE_NV;
    case MM_SMS_STORAGE_UNKNOWN:
    case MM_SMS_STORAGE_MT:
    case MM_SMS_STORAGE_SR:
    case MM_SMS_STORAGE_BM:
    case MM_SMS_STORAGE_TA:
    default:
        return QMI_WMS_STORAGE_TYPE_NONE;
    }
}

MMSmsStorage
mm_sms_storage_from_qmi_storage_type (QmiWmsStorageType qmi_storage)
{
    switch (qmi_storage) {
    case QMI_WMS_STORAGE_TYPE_UIM:
        return MM_SMS_STORAGE_SM;
    case QMI_WMS_STORAGE_TYPE_NV:
        return MM_SMS_STORAGE_ME;
    case QMI_WMS_STORAGE_TYPE_NONE:
    default:
        return MM_SMS_STORAGE_UNKNOWN;
    }
}

/*****************************************************************************/

MMSmsState
mm_sms_state_from_qmi_message_tag (QmiWmsMessageTagType tag)
{
    switch (tag) {
    case QMI_WMS_MESSAGE_TAG_TYPE_MT_READ:
    case QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ:
        return MM_SMS_STATE_RECEIVED;
    case QMI_WMS_MESSAGE_TAG_TYPE_MO_SENT:
        return MM_SMS_STATE_SENT;
    case QMI_WMS_MESSAGE_TAG_TYPE_MO_NOT_SENT:
        return MM_SMS_STATE_STORED;
    default:
        return MM_SMS_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

QmiWdsAuthentication
mm_bearer_allowed_auth_to_qmi_authentication (MMBearerAllowedAuth auth)
{
    QmiWdsAuthentication out;

    out = QMI_WDS_AUTHENTICATION_NONE;
    if (auth & MM_BEARER_ALLOWED_AUTH_PAP)
        out |= QMI_WDS_AUTHENTICATION_PAP;
    if (auth & MM_BEARER_ALLOWED_AUTH_CHAP)
        out |= QMI_WDS_AUTHENTICATION_CHAP;

    return out;
}

MMBearerAllowedAuth
mm_bearer_allowed_auth_from_qmi_authentication (QmiWdsAuthentication auth)
{
    MMBearerAllowedAuth out = 0;

    if (auth & QMI_WDS_AUTHENTICATION_PAP)
        out |= MM_BEARER_ALLOWED_AUTH_PAP;
    if (auth & QMI_WDS_AUTHENTICATION_CHAP)
        out |= MM_BEARER_ALLOWED_AUTH_CHAP;

    return out;
}

MMBearerIpFamily
mm_bearer_ip_family_from_qmi_ip_support_type (QmiWdsIpSupportType ip_support_type)
{
    switch (ip_support_type) {
    case QMI_WDS_IP_SUPPORT_TYPE_IPV4:
        return MM_BEARER_IP_FAMILY_IPV4;
    case QMI_WDS_IP_SUPPORT_TYPE_IPV6:
        return MM_BEARER_IP_FAMILY_IPV6;
    case QMI_WDS_IP_SUPPORT_TYPE_IPV4V6:
        return MM_BEARER_IP_FAMILY_IPV4V6;
    default:
        return MM_BEARER_IP_FAMILY_NONE;
    }
}

MMBearerIpFamily
mm_bearer_ip_family_from_qmi_pdp_type (QmiWdsPdpType pdp_type)
{
    switch (pdp_type) {
    case QMI_WDS_PDP_TYPE_IPV4:
        return MM_BEARER_IP_FAMILY_IPV4;
    case QMI_WDS_PDP_TYPE_IPV6:
        return MM_BEARER_IP_FAMILY_IPV6;
    case QMI_WDS_PDP_TYPE_IPV4_OR_IPV6:
        return MM_BEARER_IP_FAMILY_IPV4V6;
    case QMI_WDS_PDP_TYPE_PPP:
    default:
        return MM_BEARER_IP_FAMILY_NONE;
    }
}

gboolean
mm_bearer_ip_family_to_qmi_pdp_type (MMBearerIpFamily  ip_family,
                                     QmiWdsPdpType    *out_pdp_type)
{
    switch (ip_family) {
    case MM_BEARER_IP_FAMILY_IPV4:
        *out_pdp_type = QMI_WDS_PDP_TYPE_IPV4;
        return TRUE;
    case MM_BEARER_IP_FAMILY_IPV6:
        *out_pdp_type =  QMI_WDS_PDP_TYPE_IPV6;
        return TRUE;
    case MM_BEARER_IP_FAMILY_IPV4V6:
        *out_pdp_type =  QMI_WDS_PDP_TYPE_IPV4_OR_IPV6;
        return TRUE;
    case MM_BEARER_IP_FAMILY_NONE:
    case MM_BEARER_IP_FAMILY_ANY:
    default:
        /* there is no valid conversion, so just return FALSE to indicate it */
        return FALSE;
    }
}

/*****************************************************************************/

/**
 * The only case where we need to apply some logic to decide what the current
 * capabilities are is when we have a multimode CDMA/EVDO+GSM/UMTS device, in
 * which case we'll check the SSP and TP current values to decide which
 * capabilities are present and which have been disabled.
 *
 * For all the other cases, the DMS capabilities are exactly the current ones,
 * as there would be no capability switching support.
 */
MMModemCapability
mm_modem_capability_from_qmi_capabilities_context (MMQmiCapabilitiesContext *ctx,
                                                   gpointer                  log_object)
{
    MMModemCapability tmp = MM_MODEM_CAPABILITY_NONE;
    g_autofree gchar *nas_ssp_mode_preference_str = NULL;
    g_autofree gchar *nas_tp_str = NULL;
    g_autofree gchar *dms_capabilities_str = NULL;
    g_autofree gchar *tmp_str = NULL;

    /* If not a multimode device, we're done */
#define MULTIMODE (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO)
    if ((ctx->dms_capabilities & MULTIMODE) != MULTIMODE)
        tmp = ctx->dms_capabilities;
    else {
        /* We have a multimode CDMA/EVDO+GSM/UMTS device, check SSP and TP */

        /* SSP logic to gather capabilities uses the Mode Preference TLV if available */
        if (ctx->nas_ssp_mode_preference_mask)
            tmp = mm_modem_capability_from_qmi_rat_mode_preference (ctx->nas_ssp_mode_preference_mask);
        /* If no value retrieved from SSP, check TP. We only process TP
         * values if not 'auto' (0). */
        else if (ctx->nas_tp_mask != QMI_NAS_RADIO_TECHNOLOGY_PREFERENCE_AUTO)
            tmp = mm_modem_capability_from_qmi_radio_technology_preference (ctx->nas_tp_mask);

        /* Final capabilities are the intersection between the Technology
         * Preference or SSP and the device's capabilities.
         * If the Technology Preference was "auto" or unknown we just fall back
         * to the Get Capabilities response.
         */
        if (tmp == MM_MODEM_CAPABILITY_NONE)
            tmp = ctx->dms_capabilities;
        else
            tmp &= ctx->dms_capabilities;
    }

    /* Log about the logic applied */
    nas_ssp_mode_preference_str = qmi_nas_rat_mode_preference_build_string_from_mask (ctx->nas_ssp_mode_preference_mask);
    nas_tp_str = qmi_nas_radio_technology_preference_build_string_from_mask (ctx->nas_tp_mask);
    dms_capabilities_str = mm_modem_capability_build_string_from_mask (ctx->dms_capabilities);
    tmp_str = mm_modem_capability_build_string_from_mask (tmp);
    mm_obj_dbg (log_object,
                "Current capabilities built: '%s'\n"
                "  SSP mode preference: '%s'\n"
                "  TP: '%s'\n"
                "  DMS Capabilities: '%s'",
                tmp_str,
                nas_ssp_mode_preference_str ? nas_ssp_mode_preference_str : "unknown",
                nas_tp_str ? nas_tp_str : "unknown",
                dms_capabilities_str);

    return tmp;
}

/*****************************************************************************/

MMOmaSessionType
mm_oma_session_type_from_qmi_oma_session_type (QmiOmaSessionType qmi_session_type)
{
    switch (qmi_session_type) {
    case QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE:
        return MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE;
    case QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE:
        return MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE;
    case QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION:
        return MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION;
    case QMI_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION:
        return MM_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION;
    case QMI_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE:
        return MM_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE;
    case QMI_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE:
        return MM_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE;
    case QMI_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE:
        return MM_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE;
    default:
        return MM_OMA_SESSION_TYPE_UNKNOWN;
    }
}

QmiOmaSessionType
mm_oma_session_type_to_qmi_oma_session_type (MMOmaSessionType mm_session_type)
{
    switch (mm_session_type) {
    case MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE:
        return QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE;
    case MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE:
        return QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE;
    case MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION:
        return QMI_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION;
    case MM_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION:
        return QMI_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION;
    case MM_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE:
        return QMI_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE;
    case MM_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE:
        return QMI_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE;
    case MM_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE:
        return QMI_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE;
    case MM_OMA_SESSION_TYPE_UNKNOWN:
    default:
        g_assert_not_reached ();
    }
}

MMOmaSessionState
mm_oma_session_state_from_qmi_oma_session_state (QmiOmaSessionState qmi_session_state)
{
    /* Note: MM_OMA_SESSION_STATE_STARTED is not a state received from the modem */

    switch (qmi_session_state) {
    case QMI_OMA_SESSION_STATE_COMPLETE_INFORMATION_UPDATED:
    case QMI_OMA_SESSION_STATE_COMPLETE_UPDATED_INFORMATION_UNAVAILABLE:
        return MM_OMA_SESSION_STATE_COMPLETED;
    case QMI_OMA_SESSION_STATE_FAILED:
        return MM_OMA_SESSION_STATE_FAILED;
    case QMI_OMA_SESSION_STATE_RETRYING:
        return MM_OMA_SESSION_STATE_RETRYING;
    case QMI_OMA_SESSION_STATE_CONNECTING:
        return MM_OMA_SESSION_STATE_CONNECTING;
    case QMI_OMA_SESSION_STATE_CONNECTED:
        return MM_OMA_SESSION_STATE_CONNECTED;
    case QMI_OMA_SESSION_STATE_AUTHENTICATED:
        return MM_OMA_SESSION_STATE_AUTHENTICATED;
    case QMI_OMA_SESSION_STATE_MDN_DOWNLOADED:
        return MM_OMA_SESSION_STATE_MDN_DOWNLOADED;
    case QMI_OMA_SESSION_STATE_MSID_DOWNLOADED:
        return MM_OMA_SESSION_STATE_MSID_DOWNLOADED;
    case QMI_OMA_SESSION_STATE_PRL_DOWNLOADED:
        return MM_OMA_SESSION_STATE_PRL_DOWNLOADED;
    case QMI_OMA_SESSION_STATE_MIP_PROFILE_DOWNLOADED:
        return MM_OMA_SESSION_STATE_MIP_PROFILE_DOWNLOADED;
    default:
        return MM_OMA_SESSION_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

MMOmaSessionStateFailedReason
mm_oma_session_state_failed_reason_from_qmi_oma_session_failed_reason (QmiOmaSessionFailedReason qmi_session_failed_reason)
{
    switch (qmi_session_failed_reason) {
    case QMI_OMA_SESSION_FAILED_REASON_UNKNOWN:
        return MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN;
    case QMI_OMA_SESSION_FAILED_REASON_NETWORK_UNAVAILABLE:
        return MM_OMA_SESSION_STATE_FAILED_REASON_NETWORK_UNAVAILABLE;
    case QMI_OMA_SESSION_FAILED_REASON_SERVER_UNAVAILABLE:
        return MM_OMA_SESSION_STATE_FAILED_REASON_SERVER_UNAVAILABLE;
    case QMI_OMA_SESSION_FAILED_REASON_AUTHENTICATION_FAILED:
        return MM_OMA_SESSION_STATE_FAILED_REASON_AUTHENTICATION_FAILED;
    case QMI_OMA_SESSION_FAILED_REASON_MAX_RETRY_EXCEEDED:
        return MM_OMA_SESSION_STATE_FAILED_REASON_MAX_RETRY_EXCEEDED;
    case QMI_OMA_SESSION_FAILED_REASON_SESSION_CANCELLED:
        return MM_OMA_SESSION_STATE_FAILED_REASON_SESSION_CANCELLED;
    default:
        return MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN;
    }
}

/*****************************************************************************/

gboolean
mm_error_from_qmi_loc_indication_status (QmiLocIndicationStatus   status,
                                         GError                 **error)
{
    switch (status) {
    case QMI_LOC_INDICATION_STATUS_SUCCESS:
        return TRUE;
    case QMI_LOC_INDICATION_STATUS_GENERAL_FAILURE:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "LOC service: general failure");
        return FALSE;
    case QMI_LOC_INDICATION_STATUS_UNSUPPORTED:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "LOC service: unsupported");
        return FALSE;
    case QMI_LOC_INDICATION_STATUS_INVALID_PARAMETER:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "LOC service: invalid parameter");
        return FALSE;
    case QMI_LOC_INDICATION_STATUS_ENGINE_BUSY:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS, "LOC service: engine busy");
        return FALSE;
    case QMI_LOC_INDICATION_STATUS_PHONE_OFFLINE:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "LOC service: phone offline");
        return FALSE;
    case QMI_LOC_INDICATION_STATUS_TIMEOUT:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED, "LOC service: timeout");
        return FALSE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "LOC service: unknown failure");
        return FALSE;
    }
}

/*****************************************************************************/
/* Convert between firmware unique ID (string) and QMI unique ID (16 bytes)
 *
 * The unique ID coming in the QMI message is a fixed-size 16 byte array, and its
 * format really depends on the manufacturer. But, if the manufacturer is nice enough
 * to use ASCII for this field, just use it ourselves as well, no need to obfuscate
 * the information we expose in our interfaces.
 *
 * We also need to do the conversion in the other way around, because when
 * selecting a new image to run we need to provide the QMI unique ID.
 */

#define EXPECTED_QMI_UNIQUE_ID_LENGTH 16

gchar *
mm_qmi_unique_id_to_firmware_unique_id (GArray  *qmi_unique_id,
                                        GError **error)
{
    guint    i;
    gboolean expect_nul_byte = FALSE;

    if (qmi_unique_id->len != EXPECTED_QMI_UNIQUE_ID_LENGTH) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "unexpected QMI unique ID length: %u (expected: %u)",
                     qmi_unique_id->len, EXPECTED_QMI_UNIQUE_ID_LENGTH);
        return NULL;
    }

    for (i = 0; i < qmi_unique_id->len; i++) {
        guint8 val;

        val = g_array_index (qmi_unique_id, guint8, i);

        /* Check for ASCII chars */
        if (g_ascii_isprint ((gchar) val)) {
            /* Halt iteration if we found an ASCII char after a NUL byte */
            if (expect_nul_byte)
                break;

            /* good char */
            continue;
        }

        /* Allow NUL bytes at the end of the array */
        if (val == '\0' && i > 0) {
            if (!expect_nul_byte)
                expect_nul_byte = TRUE;
            continue;
        }

        /* Halt iteration, not something we can build as ASCII */
        break;
    }

    if (i != qmi_unique_id->len)
        return mm_utils_bin2hexstr ((const guint8 *)qmi_unique_id->data, qmi_unique_id->len);

    return g_strndup ((const gchar *)qmi_unique_id->data, qmi_unique_id->len);
}

GArray *
mm_firmware_unique_id_to_qmi_unique_id (const gchar  *unique_id,
                                        GError      **error)
{
    guint   len;
    GArray *qmi_unique_id;

    len = strlen (unique_id);

    /* The length will be exactly EXPECTED_QMI_UNIQUE_ID_LENGTH*2 if given in HEX */
    if (len == (2 * EXPECTED_QMI_UNIQUE_ID_LENGTH)) {
        g_autofree guint8 *tmp = NULL;
        gsize              tmp_len;

        tmp_len = 0;
        tmp = mm_utils_hexstr2bin (unique_id, -1, &tmp_len, error);
        if (!tmp) {
            g_prefix_error (error, "Unexpected character found in unique id: ");
            return NULL;
        }
        g_assert (tmp_len == EXPECTED_QMI_UNIQUE_ID_LENGTH);

        qmi_unique_id = g_array_sized_new (FALSE, FALSE, sizeof (guint8), tmp_len);
        g_array_insert_vals (qmi_unique_id, 0, tmp, tmp_len);
        return qmi_unique_id;
    }

    /* The length will be EXPECTED_QMI_UNIQUE_ID_LENGTH or less if given in ASCII */
    if (len > 0 && len <= EXPECTED_QMI_UNIQUE_ID_LENGTH) {
        guint i;

        for (i = 0; i < len; i++) {
            if (!g_ascii_isprint (unique_id[i])) {
                g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Unexpected character found in unique id (not ASCII): %c", unique_id[i]);
                return NULL;
            }
        }

        qmi_unique_id = g_array_sized_new (FALSE, FALSE, sizeof (guint8), EXPECTED_QMI_UNIQUE_ID_LENGTH);
        g_array_set_size (qmi_unique_id, EXPECTED_QMI_UNIQUE_ID_LENGTH);
        memcpy (&qmi_unique_id->data[0], unique_id, len);
        if (len < EXPECTED_QMI_UNIQUE_ID_LENGTH)
            memset (&qmi_unique_id->data[len], 0, EXPECTED_QMI_UNIQUE_ID_LENGTH - len);
        return qmi_unique_id;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Unexpected unique id length: %u", len);
    return NULL;
}

/*****************************************************************************/

gboolean
mm_qmi_uim_get_card_status_output_parse (gpointer                           log_object,
                                         QmiMessageUimGetCardStatusOutput  *output,
                                         MMModemLock                       *o_lock,
                                         QmiUimPinState                    *o_pin1_state,
                                         guint                             *o_pin1_retries,
                                         guint                             *o_puk1_retries,
                                         QmiUimPinState                    *o_pin2_state,
                                         guint                             *o_pin2_retries,
                                         guint                             *o_puk2_retries,
                                         GError                           **error)
{
    QmiMessageUimGetCardStatusOutputCardStatusCardsElement                    *card;
    QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement *app;
    GArray      *cards;
    guint16      index_gw_primary = 0xFFFF;
    guint8       gw_primary_slot_i = 0;
    guint8       gw_primary_application_i = 0;
    MMModemLock  lock = MM_MODEM_LOCK_UNKNOWN;

    /* This command supports MULTIPLE cards with MULTIPLE applications each. For our
     * purposes, we're going to consider as the SIM to use the one identified as
     * 'primary GW' exclusively. We don't really support Dual Sim Dual Standby yet. */

    qmi_message_uim_get_card_status_output_get_card_status (
        output,
        &index_gw_primary,
        NULL, /* index_1x_primary */
        NULL, /* index_gw_secondary */
        NULL, /* index_1x_secondary */
        &cards,
        NULL);

    if (cards->len == 0) {
        g_set_error (error, QMI_CORE_ERROR, QMI_CORE_ERROR_FAILED,
                     "No cards reported");
        return FALSE;
    }

    /* Look for the primary GW slot and application.
     * If we don't have valid GW primary slot index and application index, assume
     * we're missing the SIM altogether */
    gw_primary_slot_i        = ((index_gw_primary & 0xFF00) >> 8);
    gw_primary_application_i = ((index_gw_primary & 0x00FF));

    if (gw_primary_slot_i == 0xFF) {
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,
                     "GW primary session index unknown");
        return FALSE;
    }
    mm_obj_dbg (log_object, "GW primary session index: %u",     gw_primary_slot_i);

    if (gw_primary_application_i == 0xFF) {
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,
                     "GW primary application index unknown");
        return FALSE;
    }
    mm_obj_dbg (log_object, "GW primary application index: %u", gw_primary_application_i);

    /* Validate slot index */
    if (gw_primary_slot_i >= cards->len) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid GW primary session index: %u",
                     gw_primary_slot_i);
        return FALSE;
    }

    /* Get card at slot */
    card = &g_array_index (cards, QmiMessageUimGetCardStatusOutputCardStatusCardsElement, gw_primary_slot_i);

    if (card->card_state == QMI_UIM_CARD_STATE_ABSENT) {
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,
                     "No card found");
        return FALSE;
    }

    if (card->card_state == QMI_UIM_CARD_STATE_ERROR) {
        const gchar *card_error;

        card_error = qmi_uim_card_error_get_string (card->error_code);
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "Card error: %s", card_error ? card_error : "unknown error");
        return FALSE;
    }

    if (card->card_state != QMI_UIM_CARD_STATE_PRESENT) {
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "Card error: unexpected card state: 0x%x", card->card_state);
        return FALSE;
    }

    /* Card is present */

    if (card->applications->len == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No applications reported in card");
        return FALSE;
    }

    /* Validate application index */
    if (gw_primary_application_i >= card->applications->len) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid GW primary application index: %u",
                     gw_primary_application_i);
        return FALSE;
    }

    app = &g_array_index (card->applications, QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElement, gw_primary_application_i);
    if ((app->type != QMI_UIM_CARD_APPLICATION_TYPE_SIM) && (app->type != QMI_UIM_CARD_APPLICATION_TYPE_USIM)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unsupported application type found in GW primary application index: %s",
                     qmi_uim_card_application_type_get_string (app->type));
        return FALSE;
    }

    /* Illegal application state is fatal, consider it as a failed SIM right
     * away and don't even attempt to retry */
    if (app->state == QMI_UIM_CARD_APPLICATION_STATE_ILLEGAL) {
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "Illegal SIM/USIM application state");
        return FALSE;
    }

    /* If card not ready yet, return RETRY error.
     * If the application state reports needing PIN/PUk, consider that ready as
     * well, and let the logic fall down to check PIN1/PIN2. */
    if (app->state != QMI_UIM_CARD_APPLICATION_STATE_READY &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PIN1_OR_UPIN_PIN_REQUIRED &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PUK1_OR_UPIN_PUK_REQUIRED &&
        app->state != QMI_UIM_CARD_APPLICATION_STATE_PIN1_BLOCKED) {
        mm_obj_dbg (log_object, "neither SIM nor USIM are ready");
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY,
                     "SIM not ready yet (retry)");
        return FALSE;
    }

    /* Report state and retries if requested to do so */
    if (o_pin1_state)
        *o_pin1_state = app->pin1_state;
    if (o_pin1_retries)
        *o_pin1_retries = app->pin1_retries;
    if (o_puk1_retries)
        *o_puk1_retries = app->puk1_retries;
    if (o_pin2_state)
        *o_pin2_state = app->pin2_state;
    if (o_pin2_retries)
        *o_pin2_retries = app->pin2_retries;
    if (o_puk2_retries)
        *o_puk2_retries = app->puk2_retries;

    /* Early bail out if lock status isn't wanted at this point, so that we
     * don't fail with an error the unlock retries check */
    if (!o_lock)
        return TRUE;

    /* Card is ready, what's the lock status? */

    /* PIN1 */
    switch (app->pin1_state) {
    case QMI_UIM_PIN_STATE_NOT_INITIALIZED:
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "SIM PIN/PUK status not known yet");
        return FALSE;

    case QMI_UIM_PIN_STATE_PERMANENTLY_BLOCKED:
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "SIM PIN/PUK permanently blocked");
        return FALSE;

    case QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED:
        lock = MM_MODEM_LOCK_SIM_PIN;
        break;

    case QMI_UIM_PIN_STATE_BLOCKED:
        lock = MM_MODEM_LOCK_SIM_PUK;
        break;

    case QMI_UIM_PIN_STATE_DISABLED:
    case QMI_UIM_PIN_STATE_ENABLED_VERIFIED:
        lock = MM_MODEM_LOCK_NONE;
        break;

    default:
        g_set_error (error,
                     MM_MOBILE_EQUIPMENT_ERROR,
                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,
                     "Unknown SIM PIN/PUK status");
        return FALSE;
    }

    /* PIN2 */
    if (lock == MM_MODEM_LOCK_NONE) {
        switch (app->pin2_state) {
        case QMI_UIM_PIN_STATE_NOT_INITIALIZED:
            mm_obj_warn (log_object, "SIM PIN2/PUK2 status not known yet");
            break;

        case QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED:
            lock = MM_MODEM_LOCK_SIM_PIN2;
            break;

        case QMI_UIM_PIN_STATE_PERMANENTLY_BLOCKED:
            mm_obj_warn (log_object, "PUK2 permanently blocked");
            /* Fall through */
        case QMI_UIM_PIN_STATE_BLOCKED:
            lock = MM_MODEM_LOCK_SIM_PUK2;
            break;

        case QMI_UIM_PIN_STATE_DISABLED:
        case QMI_UIM_PIN_STATE_ENABLED_VERIFIED:
            break;

        default:
            mm_obj_warn (log_object, "unknown SIM PIN2/PUK2 status");
            break;
        }
    }

    *o_lock = lock;
    return TRUE;
}

/*************************************************************************/
/* EID parsing */

#define EID_BYTE_LENGTH 16

gchar *
mm_qmi_uim_decode_eid (const gchar *eid, gsize eid_len)
{
    if (eid_len != EID_BYTE_LENGTH)
        return NULL;

    return mm_bcd_to_string ((const guint8 *) eid, eid_len, FALSE /* low_nybble_first */);
}
