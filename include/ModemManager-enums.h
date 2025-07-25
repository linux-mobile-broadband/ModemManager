/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * ModemManager Interface Specification
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc.
 */

#ifndef _MODEMMANAGER_ENUMS_H_
#define _MODEMMANAGER_ENUMS_H_

#if !defined (__MODEM_MANAGER_H_INSIDE__)
#error "Only <ModemManager.h> can be included directly."
#endif

/**
 * SECTION:mm-enums
 * @short_description: Common enumerations and types in the API.
 *
 * This section defines enumerations and types that are used in the
 * ModemManager interface.
 **/

/**
 * MMModemCapability:
 * @MM_MODEM_CAPABILITY_NONE: Modem has no capabilities.
 * @MM_MODEM_CAPABILITY_POTS: Modem supports the analog wired telephone network (ie 56k dialup) and does not have wireless/cellular capabilities.
 * @MM_MODEM_CAPABILITY_CDMA_EVDO: Modem supports at least one of CDMA 1xRTT, EVDO revision 0, EVDO revision A, or EVDO revision B.
 * @MM_MODEM_CAPABILITY_GSM_UMTS: Modem supports at least one of GSM, GPRS, EDGE, UMTS, HSDPA, HSUPA, or HSPA+ packet switched data capability.
 * @MM_MODEM_CAPABILITY_LTE: Modem has LTE data capability.
 * @MM_MODEM_CAPABILITY_IRIDIUM: Modem has Iridium capabilities.
 * @MM_MODEM_CAPABILITY_5GNR: Modem has 5GNR capabilities. Since 1.14.
 * @MM_MODEM_CAPABILITY_TDS: Modem has TDS capabilities. Since 1.20.
 * @MM_MODEM_CAPABILITY_ANY: Mask specifying all capabilities.
 *
 * Flags describing one or more of the general access technology families that a
 * modem supports.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_capability >*/
    MM_MODEM_CAPABILITY_NONE         = 0,
    MM_MODEM_CAPABILITY_POTS         = 1 << 0,
    MM_MODEM_CAPABILITY_CDMA_EVDO    = 1 << 1,
    MM_MODEM_CAPABILITY_GSM_UMTS     = 1 << 2,
    MM_MODEM_CAPABILITY_LTE          = 1 << 3,
    /* MM_MODEM_CAPABILITY_LTE_ADVANCED deprecated */
    MM_MODEM_CAPABILITY_IRIDIUM      = 1 << 5,
    MM_MODEM_CAPABILITY_5GNR         = 1 << 6,
    MM_MODEM_CAPABILITY_TDS          = 1 << 7,
    MM_MODEM_CAPABILITY_ANY          = 0xFFFFFFFF
} MMModemCapability;

/**
 * MMModemLock:
 * @MM_MODEM_LOCK_UNKNOWN: Lock reason unknown.
 * @MM_MODEM_LOCK_NONE: Modem is unlocked.
 * @MM_MODEM_LOCK_SIM_PIN: SIM requires the PIN code.
 * @MM_MODEM_LOCK_SIM_PIN2: SIM requires the PIN2 code.
 * @MM_MODEM_LOCK_SIM_PUK: SIM requires the PUK code.
 * @MM_MODEM_LOCK_SIM_PUK2: SIM requires the PUK2 code.
 * @MM_MODEM_LOCK_PH_SP_PIN: Modem requires the service provider PIN code.
 * @MM_MODEM_LOCK_PH_SP_PUK: Modem requires the service provider PUK code.
 * @MM_MODEM_LOCK_PH_NET_PIN: Modem requires the network PIN code.
 * @MM_MODEM_LOCK_PH_NET_PUK: Modem requires the network PUK code.
 * @MM_MODEM_LOCK_PH_SIM_PIN: Modem requires the PIN code.
 * @MM_MODEM_LOCK_PH_CORP_PIN: Modem requires the corporate PIN code.
 * @MM_MODEM_LOCK_PH_CORP_PUK: Modem requires the corporate PUK code.
 * @MM_MODEM_LOCK_PH_FSIM_PIN: Modem requires the PH-FSIM PIN code.
 * @MM_MODEM_LOCK_PH_FSIM_PUK: Modem requires the PH-FSIM PUK code.
 * @MM_MODEM_LOCK_PH_NETSUB_PIN: Modem requires the network subset PIN code.
 * @MM_MODEM_LOCK_PH_NETSUB_PUK: Modem requires the network subset PUK code.
 *
 * Enumeration of possible lock reasons.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_lock >*/
    MM_MODEM_LOCK_UNKNOWN        = 0,
    MM_MODEM_LOCK_NONE           = 1,
    MM_MODEM_LOCK_SIM_PIN        = 2,
    MM_MODEM_LOCK_SIM_PIN2       = 3,
    MM_MODEM_LOCK_SIM_PUK        = 4,
    MM_MODEM_LOCK_SIM_PUK2       = 5,
    MM_MODEM_LOCK_PH_SP_PIN      = 6,
    MM_MODEM_LOCK_PH_SP_PUK      = 7,
    MM_MODEM_LOCK_PH_NET_PIN     = 8,
    MM_MODEM_LOCK_PH_NET_PUK     = 9,
    MM_MODEM_LOCK_PH_SIM_PIN     = 10,
    MM_MODEM_LOCK_PH_CORP_PIN    = 11,
    MM_MODEM_LOCK_PH_CORP_PUK    = 12,
    MM_MODEM_LOCK_PH_FSIM_PIN    = 13,
    MM_MODEM_LOCK_PH_FSIM_PUK    = 14,
    MM_MODEM_LOCK_PH_NETSUB_PIN  = 15,
    MM_MODEM_LOCK_PH_NETSUB_PUK  = 16
} MMModemLock;

/**
 * MMModemState:
 * @MM_MODEM_STATE_FAILED: The modem is unusable.
 * @MM_MODEM_STATE_UNKNOWN: State unknown or not reportable.
 * @MM_MODEM_STATE_INITIALIZING: The modem is currently being initialized.
 * @MM_MODEM_STATE_LOCKED: The modem needs to be unlocked.
 * @MM_MODEM_STATE_DISABLED: The modem is not enabled and is powered down.
 * @MM_MODEM_STATE_DISABLING: The modem is currently transitioning to the @MM_MODEM_STATE_DISABLED state.
 * @MM_MODEM_STATE_ENABLING: The modem is currently transitioning to the @MM_MODEM_STATE_ENABLED state.
 * @MM_MODEM_STATE_ENABLED: The modem is enabled and powered on but not registered with a network provider and not available for data connections.
 * @MM_MODEM_STATE_SEARCHING: The modem is searching for a network provider to register with.
 * @MM_MODEM_STATE_REGISTERED: The modem is registered with a network provider, and data connections and messaging may be available for use.
 * @MM_MODEM_STATE_DISCONNECTING: The modem is disconnecting and deactivating the last active packet data bearer. This state will not be entered if more than one packet data bearer is active and one of the active bearers is deactivated.
 * @MM_MODEM_STATE_CONNECTING: The modem is activating and connecting the first packet data bearer. Subsequent bearer activations when another bearer is already active do not cause this state to be entered.
 * @MM_MODEM_STATE_CONNECTED: One or more packet data bearers is active and connected.
 *
 * Enumeration of possible modem states.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_state >*/
    MM_MODEM_STATE_FAILED        = -1,
    MM_MODEM_STATE_UNKNOWN       = 0,
    MM_MODEM_STATE_INITIALIZING  = 1,
    MM_MODEM_STATE_LOCKED        = 2,
    MM_MODEM_STATE_DISABLED      = 3,
    MM_MODEM_STATE_DISABLING     = 4,
    MM_MODEM_STATE_ENABLING      = 5,
    MM_MODEM_STATE_ENABLED       = 6,
    MM_MODEM_STATE_SEARCHING     = 7,
    MM_MODEM_STATE_REGISTERED    = 8,
    MM_MODEM_STATE_DISCONNECTING = 9,
    MM_MODEM_STATE_CONNECTING    = 10,
    MM_MODEM_STATE_CONNECTED     = 11
} MMModemState;

/**
 * MMModemStateFailedReason:
 * @MM_MODEM_STATE_FAILED_REASON_NONE: No error.
 * @MM_MODEM_STATE_FAILED_REASON_UNKNOWN: Unknown error.
 * @MM_MODEM_STATE_FAILED_REASON_SIM_MISSING: SIM is required but missing.
 * @MM_MODEM_STATE_FAILED_REASON_SIM_ERROR: SIM is available, but unusable (e.g. permanently locked).
 * @MM_MODEM_STATE_FAILED_REASON_UNKNOWN_CAPABILITIES: Unknown modem capabilities. Since 1.20.
 * @MM_MODEM_STATE_FAILED_REASON_ESIM_WITHOUT_PROFILES: eSIM is not initialized. Since 1.20.
 *
 * Enumeration of possible errors when the modem is in @MM_MODEM_STATE_FAILED.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_state_failed_reason >*/
    MM_MODEM_STATE_FAILED_REASON_NONE                  = 0,
    MM_MODEM_STATE_FAILED_REASON_UNKNOWN               = 1,
    MM_MODEM_STATE_FAILED_REASON_SIM_MISSING           = 2,
    MM_MODEM_STATE_FAILED_REASON_SIM_ERROR             = 3,
    MM_MODEM_STATE_FAILED_REASON_UNKNOWN_CAPABILITIES  = 4,
    MM_MODEM_STATE_FAILED_REASON_ESIM_WITHOUT_PROFILES = 5,
} MMModemStateFailedReason;

/**
 * MMModemPowerState:
 * @MM_MODEM_POWER_STATE_UNKNOWN: Unknown power state.
 * @MM_MODEM_POWER_STATE_OFF: Off.
 * @MM_MODEM_POWER_STATE_LOW: Low-power mode.
 * @MM_MODEM_POWER_STATE_ON: Full power mode.
 *
 * Power state of the modem.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_power_state >*/
    MM_MODEM_POWER_STATE_UNKNOWN = 0,
    MM_MODEM_POWER_STATE_OFF     = 1,
    MM_MODEM_POWER_STATE_LOW     = 2,
    MM_MODEM_POWER_STATE_ON      = 3
} MMModemPowerState;

/**
 * MMModemStateChangeReason:
 * @MM_MODEM_STATE_CHANGE_REASON_UNKNOWN: Reason unknown or not reportable.
 * @MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED: State change was requested by an interface user.
 * @MM_MODEM_STATE_CHANGE_REASON_SUSPEND: State change was caused by a system suspend.
 * @MM_MODEM_STATE_CHANGE_REASON_FAILURE: State change was caused by an unrecoverable error.
 *
 * Enumeration of possible reasons to have changed the modem state.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_state_change_reason >*/
    MM_MODEM_STATE_CHANGE_REASON_UNKNOWN        = 0,
    MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED = 1,
    MM_MODEM_STATE_CHANGE_REASON_SUSPEND        = 2,
    MM_MODEM_STATE_CHANGE_REASON_FAILURE        = 3,
} MMModemStateChangeReason;

/**
 * MMModemAccessTechnology:
 * @MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN: The access technology used is unknown.
 * @MM_MODEM_ACCESS_TECHNOLOGY_POTS: Analog wireline telephone.
 * @MM_MODEM_ACCESS_TECHNOLOGY_GSM: GSM.
 * @MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT: Compact GSM.
 * @MM_MODEM_ACCESS_TECHNOLOGY_GPRS: GPRS.
 * @MM_MODEM_ACCESS_TECHNOLOGY_EDGE: EDGE (ETSI 27.007: "GSM w/EGPRS").
 * @MM_MODEM_ACCESS_TECHNOLOGY_UMTS: UMTS (ETSI 27.007: "UTRAN").
 * @MM_MODEM_ACCESS_TECHNOLOGY_HSDPA: HSDPA (ETSI 27.007: "UTRAN w/HSDPA").
 * @MM_MODEM_ACCESS_TECHNOLOGY_HSUPA: HSUPA (ETSI 27.007: "UTRAN w/HSUPA").
 * @MM_MODEM_ACCESS_TECHNOLOGY_HSPA: HSPA (ETSI 27.007: "UTRAN w/HSDPA and HSUPA").
 * @MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS: HSPA+ (ETSI 27.007: "UTRAN w/HSPA+").
 * @MM_MODEM_ACCESS_TECHNOLOGY_1XRTT: CDMA2000 1xRTT.
 * @MM_MODEM_ACCESS_TECHNOLOGY_EVDO0: CDMA2000 EVDO revision 0.
 * @MM_MODEM_ACCESS_TECHNOLOGY_EVDOA: CDMA2000 EVDO revision A.
 * @MM_MODEM_ACCESS_TECHNOLOGY_EVDOB: CDMA2000 EVDO revision B.
 * @MM_MODEM_ACCESS_TECHNOLOGY_LTE: LTE (ETSI 27.007: "E-UTRAN")
 * @MM_MODEM_ACCESS_TECHNOLOGY_5GNR: 5GNR (ETSI 27.007: "NG-RAN"). Since 1.14.
 * @MM_MODEM_ACCESS_TECHNOLOGY_LTE_CAT_M: Cat-M (ETSI 23.401: LTE Category M1/M2). Since 1.20.
 * @MM_MODEM_ACCESS_TECHNOLOGY_LTE_NB_IOT: NB IoT (ETSI 23.401: LTE Category NB1/NB2). Since 1.20.
 * @MM_MODEM_ACCESS_TECHNOLOGY_ANY: Mask specifying all access technologies.
 *
 * Describes various access technologies that a device uses when registered with
 * or connected to a network.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_access_technology >*/
    MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN     = 0,
    MM_MODEM_ACCESS_TECHNOLOGY_POTS        = 1 << 0,
    MM_MODEM_ACCESS_TECHNOLOGY_GSM         = 1 << 1,
    MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT = 1 << 2,
    MM_MODEM_ACCESS_TECHNOLOGY_GPRS        = 1 << 3,
    MM_MODEM_ACCESS_TECHNOLOGY_EDGE        = 1 << 4,
    MM_MODEM_ACCESS_TECHNOLOGY_UMTS        = 1 << 5,
    MM_MODEM_ACCESS_TECHNOLOGY_HSDPA       = 1 << 6,
    MM_MODEM_ACCESS_TECHNOLOGY_HSUPA       = 1 << 7,
    MM_MODEM_ACCESS_TECHNOLOGY_HSPA        = 1 << 8,
    MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS   = 1 << 9,
    MM_MODEM_ACCESS_TECHNOLOGY_1XRTT       = 1 << 10,
    MM_MODEM_ACCESS_TECHNOLOGY_EVDO0       = 1 << 11,
    MM_MODEM_ACCESS_TECHNOLOGY_EVDOA       = 1 << 12,
    MM_MODEM_ACCESS_TECHNOLOGY_EVDOB       = 1 << 13,
    MM_MODEM_ACCESS_TECHNOLOGY_LTE         = 1 << 14,
    MM_MODEM_ACCESS_TECHNOLOGY_5GNR        = 1 << 15,
    MM_MODEM_ACCESS_TECHNOLOGY_LTE_CAT_M   = 1 << 16,
    MM_MODEM_ACCESS_TECHNOLOGY_LTE_NB_IOT  = 1 << 17,
    MM_MODEM_ACCESS_TECHNOLOGY_ANY         = 0xFFFFFFFF,
} MMModemAccessTechnology;

/**
 * MMModemMode:
 * @MM_MODEM_MODE_NONE: None.
 * @MM_MODEM_MODE_CS: CSD, GSM, and other circuit-switched technologies.
 * @MM_MODEM_MODE_2G: GPRS, EDGE.
 * @MM_MODEM_MODE_3G: UMTS, HSxPA.
 * @MM_MODEM_MODE_4G: LTE.
 * @MM_MODEM_MODE_5G: 5GNR. Since 1.14.
 * @MM_MODEM_MODE_ANY: Any mode can be used (only this value allowed for POTS modems).
 *
 * Bitfield to indicate which access modes are supported, allowed or
 * preferred in a given device.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_mode >*/
    MM_MODEM_MODE_NONE = 0,
    MM_MODEM_MODE_CS   = 1 << 0,
    MM_MODEM_MODE_2G   = 1 << 1,
    MM_MODEM_MODE_3G   = 1 << 2,
    MM_MODEM_MODE_4G   = 1 << 3,
    MM_MODEM_MODE_5G   = 1 << 4,
    MM_MODEM_MODE_ANY  = 0xFFFFFFFF
} MMModemMode;

/**
 * MMModemBand:
 * @MM_MODEM_BAND_UNKNOWN: Unknown or invalid band.
 * @MM_MODEM_BAND_EGSM: GSM/GPRS/EDGE 900 MHz.
 * @MM_MODEM_BAND_DCS: GSM/GPRS/EDGE 1800 MHz.
 * @MM_MODEM_BAND_PCS: GSM/GPRS/EDGE 1900 MHz.
 * @MM_MODEM_BAND_G850: GSM/GPRS/EDGE 850 MHz.
 * @MM_MODEM_BAND_G450: GSM/GPRS/EDGE 450 MHz.
 * @MM_MODEM_BAND_G480: GSM/GPRS/EDGE 480 MHz.
 * @MM_MODEM_BAND_G750: GSM/GPRS/EDGE 750 MHz.
 * @MM_MODEM_BAND_G380: GSM/GPRS/EDGE 380 MHz.
 * @MM_MODEM_BAND_G410: GSM/GPRS/EDGE 410 MHz.
 * @MM_MODEM_BAND_G710: GSM/GPRS/EDGE 710 MHz.
 * @MM_MODEM_BAND_G810: GSM/GPRS/EDGE 810 MHz.
 * @MM_MODEM_BAND_UTRAN_1: UMTS 2100 MHz (IMT, UTRAN band 1). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_2: UMTS 1900 MHz (PCS A-F, UTRAN band 2). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_3: UMTS 1800 MHz (DCS, UTRAN band 3). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_4: UMTS 1700 MHz (AWS A-F, UTRAN band 4). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_5: UMTS 850 MHz (CLR, UTRAN band 5). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_6: UMTS 800 MHz (UTRAN band 6). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_7: UMTS 2600 MHz (IMT-E, UTRAN band 7). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_8: UMTS 900 MHz (E-GSM, UTRAN band 8). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_9: UMTS 1700 MHz (UTRAN band 9). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_10: UMTS 1700 MHz (EAWS A-G, UTRAN band 10). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_11: UMTS 1500 MHz (LPDC, UTRAN band 11). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_12: UMTS 700 MHz (LSMH A/B/C, UTRAN band 12). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_13: UMTS 700 MHz (USMH C, UTRAN band 13). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_14: UMTS 700 MHz (USMH D, UTRAN band 14). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_19: UMTS 800 MHz (UTRAN band 19). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_20: UMTS 800 MHz (EUDD, UTRAN band 20). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_21: UMTS 1500 MHz (UPDC, UTRAN band 21). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_22: UMTS 3500 MHz (UTRAN band 22). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_25: UMTS 1900 MHz (EPCS A-G, UTRAN band 25). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_26: UMTS 850 MHz (ECLR, UTRAN band 26). Since 1.8.
 * @MM_MODEM_BAND_UTRAN_32: UMTS 1500 MHz (L-band, UTRAN band 32). Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_1: E-UTRAN band 1. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_2: E-UTRAN band 2. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_3: E-UTRAN band 3. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_4: E-UTRAN band 4. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_5: E-UTRAN band 5. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_6: E-UTRAN band 6. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_7: E-UTRAN band 7. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_8: E-UTRAN band 8. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_9: E-UTRAN band 9. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_10: E-UTRAN band 10. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_11: E-UTRAN band 11. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_12: E-UTRAN band 12. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_13: E-UTRAN band 13. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_14: E-UTRAN band 14. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_17: E-UTRAN band 17. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_18: E-UTRAN band 18. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_19: E-UTRAN band 19. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_20: E-UTRAN band 20. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_21: E-UTRAN band 21. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_22: E-UTRAN band 22. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_23: E-UTRAN band 23. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_24: E-UTRAN band 24. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_25: E-UTRAN band 25. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_26: E-UTRAN band 26. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_27: E-UTRAN band 27. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_28: E-UTRAN band 28. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_29: E-UTRAN band 29. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_30: E-UTRAN band 30. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_31: E-UTRAN band 31. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_32: E-UTRAN band 32. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_33: E-UTRAN band 33. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_34: E-UTRAN band 34. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_35: E-UTRAN band 35. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_36: E-UTRAN band 36. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_37: E-UTRAN band 37. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_38: E-UTRAN band 38. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_39: E-UTRAN band 39. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_40: E-UTRAN band 40. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_41: E-UTRAN band 41. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_42: E-UTRAN band 42. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_43: E-UTRAN band 43. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_44: E-UTRAN band 44. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_45: E-UTRAN band 45. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_46: E-UTRAN band 46. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_47: E-UTRAN band 47. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_48: E-UTRAN band 48. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_49: E-UTRAN band 49. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_50: E-UTRAN band 50. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_51: E-UTRAN band 51. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_52: E-UTRAN band 52. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_53: E-UTRAN band 53. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_54: E-UTRAN band 54. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_55: E-UTRAN band 55. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_56: E-UTRAN band 56. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_57: E-UTRAN band 57. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_58: E-UTRAN band 58. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_59: E-UTRAN band 59. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_60: E-UTRAN band 60. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_61: E-UTRAN band 61. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_62: E-UTRAN band 62. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_63: E-UTRAN band 63. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_64: E-UTRAN band 64. Since 1.10.
 * @MM_MODEM_BAND_EUTRAN_65: E-UTRAN band 65. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_66: E-UTRAN band 66. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_67: E-UTRAN band 67. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_68: E-UTRAN band 68. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_69: E-UTRAN band 69. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_70: E-UTRAN band 70. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_71: E-UTRAN band 71. Since 1.8.
 * @MM_MODEM_BAND_EUTRAN_85: E-UTRAN band 85. Since 1.20.
 * @MM_MODEM_BAND_EUTRAN_106: E-UTRAN band 106. Since 1.26.
 * @MM_MODEM_BAND_CDMA_BC0: CDMA Band Class 0 (US Cellular 850MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC1: CDMA Band Class 1 (US PCS 1900MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC2: CDMA Band Class 2 (UK TACS 900MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC3: CDMA Band Class 3 (Japanese TACS). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC4: CDMA Band Class 4 (Korean PCS). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC5: CDMA Band Class 5 (NMT 450MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC6: CDMA Band Class 6 (IMT2000 2100MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC7: CDMA Band Class 7 (Cellular 700MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC8: CDMA Band Class 8 (1800MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC9: CDMA Band Class 9 (900MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC10: CDMA Band Class 10 (US Secondary 800). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC11: CDMA Band Class 11 (European PAMR 400MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC12: CDMA Band Class 12 (PAMR 800MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC13: CDMA Band Class 13 (IMT2000 2500MHz Expansion). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC14: CDMA Band Class 14 (More US PCS 1900MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC15: CDMA Band Class 15 (AWS 1700MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC16: CDMA Band Class 16 (US 2500MHz). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC17: CDMA Band Class 17 (US 2500MHz Forward Link Only). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC18: CDMA Band Class 18 (US 700MHz Public Safety). Since 1.8.
 * @MM_MODEM_BAND_CDMA_BC19: CDMA Band Class 19 (US Lower 700MHz). Since 1.8.
 * @MM_MODEM_BAND_ANY: For certain operations, allow the modem to select a band automatically.
 * @MM_MODEM_BAND_NGRAN_1: NGRAN band 1. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_2: NGRAN band 2. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_3: NGRAN band 3. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_5: NGRAN band 5. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_7: NGRAN band 7. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_8: NGRAN band 8. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_12: NGRAN band 12. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_13: NGRAN band 13. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_14: NGRAN band 14. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_18: NGRAN band 18. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_20: NGRAN band 20. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_25: NGRAN band 25. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_26: NGRAN band 26. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_28: NGRAN band 28. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_29: NGRAN band 29. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_30: NGRAN band 30. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_34: NGRAN band 34. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_38: NGRAN band 38. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_39: NGRAN band 39. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_40: NGRAN band 40. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_41: NGRAN band 41. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_48: NGRAN band 48. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_50: NGRAN band 50. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_51: NGRAN band 51. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_53: NGRAN band 53. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_65: NGRAN band 65. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_66: NGRAN band 66. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_67: NGRAN band 67. Since 1.24.
 * @MM_MODEM_BAND_NGRAN_70: NGRAN band 70. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_71: NGRAN band 71. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_74: NGRAN band 74. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_75: NGRAN band 75. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_76: NGRAN band 76. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_77: NGRAN band 77. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_78: NGRAN band 78. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_79: NGRAN band 79. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_80: NGRAN band 80. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_81: NGRAN band 81. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_82: NGRAN band 82. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_83: NGRAN band 83. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_84: NGRAN band 84. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_86: NGRAN band 86. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_89: NGRAN band 89. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_90: NGRAN band 90. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_91: NGRAN band 91. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_92: NGRAN band 92. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_93: NGRAN band 93. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_94: NGRAN band 94. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_95: NGRAN band 95. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_257: NGRAN band 257. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_258: NGRAN band 258. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_260: NGRAN band 260. Since 1.20.
 * @MM_MODEM_BAND_NGRAN_261: NGRAN band 261. Since 1.20.
 *
 * Radio bands supported by the device when connecting to a mobile network.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_band >*/
    MM_MODEM_BAND_UNKNOWN = 0,
    /* GSM/UMTS bands */
    MM_MODEM_BAND_EGSM    = 1,
    MM_MODEM_BAND_DCS     = 2,
    MM_MODEM_BAND_PCS     = 3,
    MM_MODEM_BAND_G850    = 4,
    MM_MODEM_BAND_UTRAN_1 = 5,
    MM_MODEM_BAND_UTRAN_3 = 6,
    MM_MODEM_BAND_UTRAN_4 = 7,
    MM_MODEM_BAND_UTRAN_6 = 8,
    MM_MODEM_BAND_UTRAN_5 = 9,
    MM_MODEM_BAND_UTRAN_8 = 10,
    MM_MODEM_BAND_UTRAN_9 = 11,
    MM_MODEM_BAND_UTRAN_2 = 12,
    MM_MODEM_BAND_UTRAN_7 = 13,
    MM_MODEM_BAND_G450    = 14,
    MM_MODEM_BAND_G480    = 15,
    MM_MODEM_BAND_G750    = 16,
    MM_MODEM_BAND_G380    = 17,
    MM_MODEM_BAND_G410    = 18,
    MM_MODEM_BAND_G710    = 19,
    MM_MODEM_BAND_G810    = 20,
    /* LTE bands */
    MM_MODEM_BAND_EUTRAN_1  = 31,
    MM_MODEM_BAND_EUTRAN_2  = 32,
    MM_MODEM_BAND_EUTRAN_3  = 33,
    MM_MODEM_BAND_EUTRAN_4  = 34,
    MM_MODEM_BAND_EUTRAN_5  = 35,
    MM_MODEM_BAND_EUTRAN_6  = 36,
    MM_MODEM_BAND_EUTRAN_7  = 37,
    MM_MODEM_BAND_EUTRAN_8  = 38,
    MM_MODEM_BAND_EUTRAN_9  = 39,
    MM_MODEM_BAND_EUTRAN_10 = 40,
    MM_MODEM_BAND_EUTRAN_11 = 41,
    MM_MODEM_BAND_EUTRAN_12 = 42,
    MM_MODEM_BAND_EUTRAN_13 = 43,
    MM_MODEM_BAND_EUTRAN_14 = 44,
    MM_MODEM_BAND_EUTRAN_17 = 47,
    MM_MODEM_BAND_EUTRAN_18 = 48,
    MM_MODEM_BAND_EUTRAN_19 = 49,
    MM_MODEM_BAND_EUTRAN_20 = 50,
    MM_MODEM_BAND_EUTRAN_21 = 51,
    MM_MODEM_BAND_EUTRAN_22 = 52,
    MM_MODEM_BAND_EUTRAN_23 = 53,
    MM_MODEM_BAND_EUTRAN_24 = 54,
    MM_MODEM_BAND_EUTRAN_25 = 55,
    MM_MODEM_BAND_EUTRAN_26 = 56,
    MM_MODEM_BAND_EUTRAN_27 = 57,
    MM_MODEM_BAND_EUTRAN_28 = 58,
    MM_MODEM_BAND_EUTRAN_29 = 59,
    MM_MODEM_BAND_EUTRAN_30 = 60,
    MM_MODEM_BAND_EUTRAN_31 = 61,
    MM_MODEM_BAND_EUTRAN_32 = 62,
    MM_MODEM_BAND_EUTRAN_33 = 63,
    MM_MODEM_BAND_EUTRAN_34 = 64,
    MM_MODEM_BAND_EUTRAN_35 = 65,
    MM_MODEM_BAND_EUTRAN_36 = 66,
    MM_MODEM_BAND_EUTRAN_37 = 67,
    MM_MODEM_BAND_EUTRAN_38 = 68,
    MM_MODEM_BAND_EUTRAN_39 = 69,
    MM_MODEM_BAND_EUTRAN_40 = 70,
    MM_MODEM_BAND_EUTRAN_41 = 71,
    MM_MODEM_BAND_EUTRAN_42 = 72,
    MM_MODEM_BAND_EUTRAN_43 = 73,
    MM_MODEM_BAND_EUTRAN_44 = 74,
    MM_MODEM_BAND_EUTRAN_45 = 75,
    MM_MODEM_BAND_EUTRAN_46 = 76,
    MM_MODEM_BAND_EUTRAN_47 = 77,
    MM_MODEM_BAND_EUTRAN_48 = 78,
    MM_MODEM_BAND_EUTRAN_49 = 79,
    MM_MODEM_BAND_EUTRAN_50 = 80,
    MM_MODEM_BAND_EUTRAN_51 = 81,
    MM_MODEM_BAND_EUTRAN_52 = 82,
    MM_MODEM_BAND_EUTRAN_53 = 83,
    MM_MODEM_BAND_EUTRAN_54 = 84,
    MM_MODEM_BAND_EUTRAN_55 = 85,
    MM_MODEM_BAND_EUTRAN_56 = 86,
    MM_MODEM_BAND_EUTRAN_57 = 87,
    MM_MODEM_BAND_EUTRAN_58 = 88,
    MM_MODEM_BAND_EUTRAN_59 = 89,
    MM_MODEM_BAND_EUTRAN_60 = 90,
    MM_MODEM_BAND_EUTRAN_61 = 91,
    MM_MODEM_BAND_EUTRAN_62 = 92,
    MM_MODEM_BAND_EUTRAN_63 = 93,
    MM_MODEM_BAND_EUTRAN_64 = 94,
    MM_MODEM_BAND_EUTRAN_65 = 95,
    MM_MODEM_BAND_EUTRAN_66 = 96,
    MM_MODEM_BAND_EUTRAN_67 = 97,
    MM_MODEM_BAND_EUTRAN_68 = 98,
    MM_MODEM_BAND_EUTRAN_69 = 99,
    MM_MODEM_BAND_EUTRAN_70 = 100,
    MM_MODEM_BAND_EUTRAN_71 = 101,
    MM_MODEM_BAND_EUTRAN_85 = 115,
    /* CDMA Band Classes (see 3GPP2 C.S0057-C) */
    MM_MODEM_BAND_CDMA_BC0  = 128,
    MM_MODEM_BAND_CDMA_BC1  = 129,
    MM_MODEM_BAND_CDMA_BC2  = 130,
    MM_MODEM_BAND_CDMA_BC3  = 131,
    MM_MODEM_BAND_CDMA_BC4  = 132,
    MM_MODEM_BAND_CDMA_BC5  = 134,
    MM_MODEM_BAND_CDMA_BC6  = 135,
    MM_MODEM_BAND_CDMA_BC7  = 136,
    MM_MODEM_BAND_CDMA_BC8  = 137,
    MM_MODEM_BAND_CDMA_BC9  = 138,
    MM_MODEM_BAND_CDMA_BC10 = 139,
    MM_MODEM_BAND_CDMA_BC11 = 140,
    MM_MODEM_BAND_CDMA_BC12 = 141,
    MM_MODEM_BAND_CDMA_BC13 = 142,
    MM_MODEM_BAND_CDMA_BC14 = 143,
    MM_MODEM_BAND_CDMA_BC15 = 144,
    MM_MODEM_BAND_CDMA_BC16 = 145,
    MM_MODEM_BAND_CDMA_BC17 = 146,
    MM_MODEM_BAND_CDMA_BC18 = 147,
    MM_MODEM_BAND_CDMA_BC19 = 148,
    /* Additional UMTS bands:
     *  15-18 reserved
     *  23-24 reserved
     *  27-31 reserved
     */
    MM_MODEM_BAND_UTRAN_10 = 210,
    MM_MODEM_BAND_UTRAN_11 = 211,
    MM_MODEM_BAND_UTRAN_12 = 212,
    MM_MODEM_BAND_UTRAN_13 = 213,
    MM_MODEM_BAND_UTRAN_14 = 214,
    MM_MODEM_BAND_UTRAN_19 = 219,
    MM_MODEM_BAND_UTRAN_20 = 220,
    MM_MODEM_BAND_UTRAN_21 = 221,
    MM_MODEM_BAND_UTRAN_22 = 222,
    MM_MODEM_BAND_UTRAN_25 = 225,
    MM_MODEM_BAND_UTRAN_26 = 226,
    MM_MODEM_BAND_UTRAN_32 = 232,
    /* All/Any */
    MM_MODEM_BAND_ANY = 256,
    /* NR5G bands */
    MM_MODEM_BAND_NGRAN_1 = 301,
    MM_MODEM_BAND_NGRAN_2 = 302,
    MM_MODEM_BAND_NGRAN_3 = 303,
    MM_MODEM_BAND_NGRAN_5 = 305,
    MM_MODEM_BAND_NGRAN_7 = 307,
    MM_MODEM_BAND_NGRAN_8 = 308,
    MM_MODEM_BAND_NGRAN_12 = 312,
    MM_MODEM_BAND_NGRAN_13 = 313,
    MM_MODEM_BAND_NGRAN_14 = 314,
    MM_MODEM_BAND_NGRAN_18 = 318,
    MM_MODEM_BAND_NGRAN_20 = 320,
    MM_MODEM_BAND_NGRAN_25 = 325,
    MM_MODEM_BAND_NGRAN_26 = 326,
    MM_MODEM_BAND_NGRAN_28 = 328,
    MM_MODEM_BAND_NGRAN_29 = 329,
    MM_MODEM_BAND_NGRAN_30 = 330,
    MM_MODEM_BAND_NGRAN_34 = 334,
    MM_MODEM_BAND_NGRAN_38 = 338,
    MM_MODEM_BAND_NGRAN_39 = 339,
    MM_MODEM_BAND_NGRAN_40 = 340,
    MM_MODEM_BAND_NGRAN_41 = 341,
    MM_MODEM_BAND_NGRAN_48 = 348,
    MM_MODEM_BAND_NGRAN_50 = 350,
    MM_MODEM_BAND_NGRAN_51 = 351,
    MM_MODEM_BAND_NGRAN_53 = 353,
    MM_MODEM_BAND_NGRAN_65 = 365,
    MM_MODEM_BAND_NGRAN_66 = 366,
    MM_MODEM_BAND_NGRAN_67 = 367,
    MM_MODEM_BAND_NGRAN_70 = 370,
    MM_MODEM_BAND_NGRAN_71 = 371,
    MM_MODEM_BAND_NGRAN_74 = 374,
    MM_MODEM_BAND_NGRAN_75 = 375,
    MM_MODEM_BAND_NGRAN_76 = 376,
    MM_MODEM_BAND_NGRAN_77 = 377,
    MM_MODEM_BAND_NGRAN_78 = 378,
    MM_MODEM_BAND_NGRAN_79 = 379,
    MM_MODEM_BAND_NGRAN_80 = 380,
    MM_MODEM_BAND_NGRAN_81 = 381,
    MM_MODEM_BAND_NGRAN_82 = 382,
    MM_MODEM_BAND_NGRAN_83 = 383,
    MM_MODEM_BAND_NGRAN_84 = 384,
    MM_MODEM_BAND_NGRAN_86 = 386,
    MM_MODEM_BAND_NGRAN_89 = 389,
    MM_MODEM_BAND_NGRAN_90 = 390,
    MM_MODEM_BAND_NGRAN_91 = 391,
    MM_MODEM_BAND_NGRAN_92 = 392,
    MM_MODEM_BAND_NGRAN_93 = 393,
    MM_MODEM_BAND_NGRAN_94 = 394,
    MM_MODEM_BAND_NGRAN_95 = 395,
    MM_MODEM_BAND_NGRAN_257 = 557,
    MM_MODEM_BAND_NGRAN_258 = 558,
    MM_MODEM_BAND_NGRAN_260 = 560,
    MM_MODEM_BAND_NGRAN_261 = 561,
    /* LTE bands > 97 follow EUTRAN numbering + 1000 as offset */
    MM_MODEM_BAND_EUTRAN_106 = 1136
} MMModemBand;

/**
 * MMModemPortType:
 * @MM_MODEM_PORT_TYPE_UNKNOWN: Unknown.
 * @MM_MODEM_PORT_TYPE_NET: Net port.
 * @MM_MODEM_PORT_TYPE_AT: AT port.
 * @MM_MODEM_PORT_TYPE_QCDM: QCDM port.
 * @MM_MODEM_PORT_TYPE_GPS: GPS port.
 * @MM_MODEM_PORT_TYPE_QMI: QMI port.
 * @MM_MODEM_PORT_TYPE_MBIM: MBIM port.
 * @MM_MODEM_PORT_TYPE_AUDIO: Audio port. Since 1.12.
 * @MM_MODEM_PORT_TYPE_IGNORED: Ignored port. Since 1.16.
 * @MM_MODEM_PORT_TYPE_XMMRPC: Control protocol for Intel XMM modems. Since 1.24.
 *
 * Type of modem port.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_port_type >*/
    MM_MODEM_PORT_TYPE_UNKNOWN = 1,
    MM_MODEM_PORT_TYPE_NET     = 2,
    MM_MODEM_PORT_TYPE_AT      = 3,
    MM_MODEM_PORT_TYPE_QCDM    = 4,
    MM_MODEM_PORT_TYPE_GPS     = 5,
    MM_MODEM_PORT_TYPE_QMI     = 6,
    MM_MODEM_PORT_TYPE_MBIM    = 7,
    MM_MODEM_PORT_TYPE_AUDIO   = 8,
    MM_MODEM_PORT_TYPE_IGNORED = 9,
    MM_MODEM_PORT_TYPE_XMMRPC  = 10,
} MMModemPortType;

/**
 * MMCellType:
 * @MM_CELL_TYPE_UNKNOWN: Unknown.
 * @MM_CELL_TYPE_CDMA: CDMA cell.
 * @MM_CELL_TYPE_GSM: GSM cell.
 * @MM_CELL_TYPE_UMTS: UMTS cell.
 * @MM_CELL_TYPE_TDSCDMA: TD-SCDMA cell.
 * @MM_CELL_TYPE_LTE: LTE cell.
 * @MM_CELL_TYPE_5GNR: 5GNR cell.
 *
 * Type of cell information reported.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_cell_type >*/
    MM_CELL_TYPE_UNKNOWN = 0,
    MM_CELL_TYPE_CDMA    = 1,
    MM_CELL_TYPE_GSM     = 2,
    MM_CELL_TYPE_UMTS    = 3,
    MM_CELL_TYPE_TDSCDMA = 4,
    MM_CELL_TYPE_LTE     = 5,
    MM_CELL_TYPE_5GNR    = 6,
} MMCellType;

/**
 * MMSmsPduType:
 * @MM_SMS_PDU_TYPE_UNKNOWN: Unknown type.
 * @MM_SMS_PDU_TYPE_DELIVER: 3GPP Mobile-Terminated (MT) message.
 * @MM_SMS_PDU_TYPE_SUBMIT: 3GPP Mobile-Originated (MO) message.
 * @MM_SMS_PDU_TYPE_STATUS_REPORT: 3GPP status report (MT).
 * @MM_SMS_PDU_TYPE_CDMA_DELIVER: 3GPP2 Mobile-Terminated (MT) message. Since 1.2.
 * @MM_SMS_PDU_TYPE_CDMA_SUBMIT: 3GPP2 Mobile-Originated (MO) message. Since 1.2.
 * @MM_SMS_PDU_TYPE_CDMA_CANCELLATION: 3GPP2 Cancellation (MO) message. Since 1.2.
 * @MM_SMS_PDU_TYPE_CDMA_DELIVERY_ACKNOWLEDGEMENT: 3GPP2 Delivery Acknowledgement (MT) message. Since 1.2.
 * @MM_SMS_PDU_TYPE_CDMA_USER_ACKNOWLEDGEMENT: 3GPP2 User Acknowledgement (MT or MO) message. Since 1.2.
 * @MM_SMS_PDU_TYPE_CDMA_READ_ACKNOWLEDGEMENT: 3GPP2 Read Acknowledgement (MT or MO) message. Since 1.2.
 *
 * Type of PDUs used in the SMS.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_sms_pdu_type >*/
    MM_SMS_PDU_TYPE_UNKNOWN = 0,
    MM_SMS_PDU_TYPE_DELIVER       = 1,
    MM_SMS_PDU_TYPE_SUBMIT        = 2,
    MM_SMS_PDU_TYPE_STATUS_REPORT = 3,
    MM_SMS_PDU_TYPE_CDMA_DELIVER                  = 32,
    MM_SMS_PDU_TYPE_CDMA_SUBMIT                   = 33,
    MM_SMS_PDU_TYPE_CDMA_CANCELLATION             = 34,
    MM_SMS_PDU_TYPE_CDMA_DELIVERY_ACKNOWLEDGEMENT = 35,
    MM_SMS_PDU_TYPE_CDMA_USER_ACKNOWLEDGEMENT     = 36,
    MM_SMS_PDU_TYPE_CDMA_READ_ACKNOWLEDGEMENT     = 37,
} MMSmsPduType;

/**
 * MMSmsState:
 * @MM_SMS_STATE_UNKNOWN: State unknown or not reportable.
 * @MM_SMS_STATE_STORED: The message has been neither received nor yet sent.
 * @MM_SMS_STATE_RECEIVING: The message is being received but is not yet complete.
 * @MM_SMS_STATE_RECEIVED: The message has been completely received.
 * @MM_SMS_STATE_SENDING: The message is queued for delivery.
 * @MM_SMS_STATE_SENT: The message was successfully sent.
 *
 * State of a given SMS.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_sms_state >*/
    MM_SMS_STATE_UNKNOWN   = 0,
    MM_SMS_STATE_STORED    = 1,
    MM_SMS_STATE_RECEIVING = 2,
    MM_SMS_STATE_RECEIVED  = 3,
    MM_SMS_STATE_SENDING   = 4,
    MM_SMS_STATE_SENT      = 5,
} MMSmsState;

/**
 * MMSmsDeliveryState:
 * @MM_SMS_DELIVERY_STATE_COMPLETED_RECEIVED: Delivery completed, message received by the SME.
 * @MM_SMS_DELIVERY_STATE_COMPLETED_FORWARDED_UNCONFIRMED: Forwarded by the SC to the SME but the SC is unable to confirm delivery.
 * @MM_SMS_DELIVERY_STATE_COMPLETED_REPLACED_BY_SC: Message replaced by the SC.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_CONGESTION: Temporary error, congestion.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_SME_BUSY: Temporary error, SME busy.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_NO_RESPONSE_FROM_SME: Temporary error, no response from the SME.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_SERVICE_REJECTED: Temporary error, service rejected.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_QOS_NOT_AVAILABLE: Temporary error, QoS not available.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_IN_SME: Temporary error in the SME.
 * @MM_SMS_DELIVERY_STATE_ERROR_REMOTE_PROCEDURE: Permanent remote procedure error.
 * @MM_SMS_DELIVERY_STATE_ERROR_INCOMPATIBLE_DESTINATION: Permanent error, incompatible destination.
 * @MM_SMS_DELIVERY_STATE_ERROR_CONNECTION_REJECTED: Permanent error, connection rejected by the SME.
 * @MM_SMS_DELIVERY_STATE_ERROR_NOT_OBTAINABLE: Permanent error, not obtainable.
 * @MM_SMS_DELIVERY_STATE_ERROR_QOS_NOT_AVAILABLE: Permanent error, QoS not available.
 * @MM_SMS_DELIVERY_STATE_ERROR_NO_INTERWORKING_AVAILABLE: Permanent error, no interworking available.
 * @MM_SMS_DELIVERY_STATE_ERROR_VALIDITY_PERIOD_EXPIRED: Permanent error, message validity period expired.
 * @MM_SMS_DELIVERY_STATE_ERROR_DELETED_BY_ORIGINATING_SME: Permanent error, deleted by originating SME.
 * @MM_SMS_DELIVERY_STATE_ERROR_DELETED_BY_SC_ADMINISTRATION: Permanent error, deleted by SC administration.
 * @MM_SMS_DELIVERY_STATE_ERROR_MESSAGE_DOES_NOT_EXIST: Permanent error, message does no longer exist.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_CONGESTION: Permanent error, congestion.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_SME_BUSY: Permanent error, SME busy.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_NO_RESPONSE_FROM_SME: Permanent error, no response from the SME.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_SERVICE_REJECTED: Permanent error, service rejected.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_QOS_NOT_AVAILABLE: Permanent error, QoS not available.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_IN_SME: Permanent error in SME.
 * @MM_SMS_DELIVERY_STATE_UNKNOWN: Unknown state.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_ADDRESS_VACANT: Permanent error in network, address vacant. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_ADDRESS_TRANSLATION_FAILURE: Permanent error in network, address translation failure. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_NETWORK_RESOURCE_OUTAGE: Permanent error in network, network resource outage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_NETWORK_FAILURE: Permanent error in network, network failure. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_INVALID_TELESERVICE_ID: Permanent error in network, invalid teleservice id. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_OTHER: Permanent error, other network problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_NO_PAGE_RESPONSE: Permanent error in terminal, no page response. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_BUSY: Permanent error in terminal, destination busy. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_NO_ACKNOWLEDGMENT: Permanent error in terminal, no acknowledgement. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_RESOURCE_SHORTAGE: Permanent error in terminal, destination resource shortage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_SMS_DELIVERY_POSTPONED: Permanent error in terminal, SMS delivery postponed. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_OUT_OF_SERVICE: Permanent error in terminal, destination out of service. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_NO_LONGER_AT_THIS_ADDRESS: Permanent error in terminal, destination no longer at this address. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_OTHER: Permanent error, other terminal problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_RESOURCE_SHORTAGE: Permanent error in radio interface, resource shortage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_INCOMPATIBILITY: Permanent error in radio interface, problem incompatibility. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_OTHER: Permanent error, other radio interface problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_ENCODING: Permanent error, encoding. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_ORIGINATION_DENIED: Permanent error, SMS origination denied. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_TERMINATION_DENIED: Permanent error, SMS termination denied. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SUPPLEMENTARY_SERVICE_NOT_SUPPORTED: Permanent error, supplementary service not supported. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_NOT_SUPPORTED: Permanent error, SMS not supported. Since 1.22.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_MISSING_EXPECTED_PARAMETER: Permanent error, missing expected parameter. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_MISSING_MANDATORY_PARAMETER: Permanent error, missing mandatory parameter. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_UNRECOGNIZED_PARAMETER_VALUE: Permanent error, unrecognized parameter value. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_UNEXPECTED_PARAMETER_VALUE: Permanent error, unexpected parameter value. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_USER_DATA_SIZE_ERROR: Permanent error, user data size error. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_OTHER: Permanent error, other general problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_ADDRESS_VACANT: Temporary error in network, address vacant. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_ADDRESS_TRANSLATION_FAILURE: Temporary error in network, address translation failure. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_NETWORK_RESOURCE_OUTAGE: Temporary error in network, network resource outage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_NETWORK_FAILURE: Temporary error in network, network failure. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_INVALID_TELESERVICE_ID: Temporary error in network, invalid teleservice id. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_OTHER: Temporary error, other network problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_NO_PAGE_RESPONSE: Temporary error in terminal, no page response. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_BUSY: Temporary error in terminal, destination busy. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_NO_ACKNOWLEDGMENT: Temporary error in terminal, no acknowledgement. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_RESOURCE_SHORTAGE: Temporary error in terminal, destination resource shortage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_SMS_DELIVERY_POSTPONED: Temporary error in terminal, SMS delivery postponed. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_OUT_OF_SERVICE: Temporary error in terminal, destination out of service. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_NO_LONGER_AT_THIS_ADDRESS: Temporary error in terminal, destination no longer at this address. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_OTHER: Temporary error, other terminal problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_RESOURCE_SHORTAGE: Temporary error in radio interface, resource shortage. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_INCOMPATIBILITY: Temporary error in radio interface, problem incompatibility. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_OTHER: Temporary error, other radio interface problem. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_ENCODING: Temporary error, encoding. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_ORIGINATION_DENIED: Temporary error, SMS origination denied. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_TERMINATION_DENIED: Temporary error, SMS termination denied. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SUPPLEMENTARY_SERVICE_NOT_SUPPORTED: Temporary error, supplementary service not supported. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_NOT_SUPPORTED: Temporary error, SMS not supported. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_MISSING_EXPECTED_PARAMETER: Temporary error, missing expected parameter. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_MISSING_MANDATORY_PARAMETER: Temporary error, missing mandatory parameter. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_UNRECOGNIZED_PARAMETER_VALUE: Temporary error, unrecognized parameter value. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_UNEXPECTED_PARAMETER_VALUE: Temporary error, unexpected parameter value. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_USER_DATA_SIZE_ERROR: Temporary error, user data size error. Since 1.2.
 * @MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_OTHER: Temporary error, other general problem. Since 1.2.
 *
 * Enumeration of known SMS delivery states as defined in 3GPP TS 03.40 and
 * 3GPP2 N.S0005-O, section 6.5.2.125.
 *
 * States out of the known ranges may also be valid (either reserved or SC-specific).
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_sms_delivery_state >*/
    /* --------------- 3GPP specific errors ---------------------- */

    /* Completed deliveries */
    MM_SMS_DELIVERY_STATE_COMPLETED_RECEIVED              = 0x00,
    MM_SMS_DELIVERY_STATE_COMPLETED_FORWARDED_UNCONFIRMED = 0x01,
    MM_SMS_DELIVERY_STATE_COMPLETED_REPLACED_BY_SC        = 0x02,

    /* Temporary failures */
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_CONGESTION           = 0x20,
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_SME_BUSY             = 0x21,
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_NO_RESPONSE_FROM_SME = 0x22,
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_SERVICE_REJECTED     = 0x23,
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_QOS_NOT_AVAILABLE    = 0x24,
    MM_SMS_DELIVERY_STATE_TEMPORARY_ERROR_IN_SME               = 0x25,

    /* Permanent failures */
    MM_SMS_DELIVERY_STATE_ERROR_REMOTE_PROCEDURE             = 0x40,
    MM_SMS_DELIVERY_STATE_ERROR_INCOMPATIBLE_DESTINATION     = 0x41,
    MM_SMS_DELIVERY_STATE_ERROR_CONNECTION_REJECTED          = 0x42,
    MM_SMS_DELIVERY_STATE_ERROR_NOT_OBTAINABLE               = 0x43,
    MM_SMS_DELIVERY_STATE_ERROR_QOS_NOT_AVAILABLE            = 0x44,
    MM_SMS_DELIVERY_STATE_ERROR_NO_INTERWORKING_AVAILABLE    = 0x45,
    MM_SMS_DELIVERY_STATE_ERROR_VALIDITY_PERIOD_EXPIRED      = 0x46,
    MM_SMS_DELIVERY_STATE_ERROR_DELETED_BY_ORIGINATING_SME   = 0x47,
    MM_SMS_DELIVERY_STATE_ERROR_DELETED_BY_SC_ADMINISTRATION = 0x48,
    MM_SMS_DELIVERY_STATE_ERROR_MESSAGE_DOES_NOT_EXIST       = 0x49,

    /* Temporary failures that became permanent */
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_CONGESTION           = 0x60,
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_SME_BUSY             = 0x61,
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_NO_RESPONSE_FROM_SME = 0x62,
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_SERVICE_REJECTED     = 0x63,
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_QOS_NOT_AVAILABLE    = 0x64,
    MM_SMS_DELIVERY_STATE_TEMPORARY_FATAL_ERROR_IN_SME               = 0x65,

    /* Unknown, out of any possible valid value [0x00-0xFF] */
    MM_SMS_DELIVERY_STATE_UNKNOWN = 0x100,

    /* --------------- 3GPP2 specific errors ---------------------- */

    /* Network problems */
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_ADDRESS_VACANT              = 0x200,
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_ADDRESS_TRANSLATION_FAILURE = 0x201,
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_NETWORK_RESOURCE_OUTAGE     = 0x202,
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_NETWORK_FAILURE             = 0x203,
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_INVALID_TELESERVICE_ID      = 0x204,
    MM_SMS_DELIVERY_STATE_NETWORK_PROBLEM_OTHER                       = 0x205,
    /* Terminal problems */
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_NO_PAGE_RESPONSE                      = 0x220,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_BUSY                      = 0x221,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_NO_ACKNOWLEDGMENT                     = 0x222,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_RESOURCE_SHORTAGE         = 0x223,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_SMS_DELIVERY_POSTPONED                = 0x224,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_OUT_OF_SERVICE            = 0x225,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_DESTINATION_NO_LONGER_AT_THIS_ADDRESS = 0x226,
    MM_SMS_DELIVERY_STATE_TERMINAL_PROBLEM_OTHER                                 = 0x227,
    /* Radio problems */
    MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_RESOURCE_SHORTAGE = 0x240,
    MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_INCOMPATIBILITY   = 0x241,
    MM_SMS_DELIVERY_STATE_RADIO_INTERFACE_PROBLEM_OTHER             = 0x242,
    /* General problems */
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_ENCODING                            = 0x260,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_ORIGINATION_DENIED              = 0x261,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_TERMINATION_DENIED              = 0x262,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SUPPLEMENTARY_SERVICE_NOT_SUPPORTED = 0x263,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_SMS_NOT_SUPPORTED                   = 0x264,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_MISSING_EXPECTED_PARAMETER          = 0x266,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_MISSING_MANDATORY_PARAMETER         = 0x267,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_UNRECOGNIZED_PARAMETER_VALUE        = 0x268,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_UNEXPECTED_PARAMETER_VALUE          = 0x269,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_USER_DATA_SIZE_ERROR                = 0x26A,
    MM_SMS_DELIVERY_STATE_GENERAL_PROBLEM_OTHER                               = 0x26B,

    /* Temporary network problems */
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_ADDRESS_VACANT              = 0x300,
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_ADDRESS_TRANSLATION_FAILURE = 0x301,
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_NETWORK_RESOURCE_OUTAGE     = 0x302,
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_NETWORK_FAILURE             = 0x303,
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_INVALID_TELESERVICE_ID      = 0x304,
    MM_SMS_DELIVERY_STATE_TEMPORARY_NETWORK_PROBLEM_OTHER                       = 0x305,
    /* Temporary terminal problems */
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_NO_PAGE_RESPONSE                      = 0x320,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_BUSY                      = 0x321,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_NO_ACKNOWLEDGMENT                     = 0x322,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_RESOURCE_SHORTAGE         = 0x323,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_SMS_DELIVERY_POSTPONED                = 0x324,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_OUT_OF_SERVICE            = 0x325,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_DESTINATION_NO_LONGER_AT_THIS_ADDRESS = 0x326,
    MM_SMS_DELIVERY_STATE_TEMPORARY_TERMINAL_PROBLEM_OTHER                                 = 0x327,
    /* Temporary radio problems */
    MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_RESOURCE_SHORTAGE = 0x340,
    MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_INCOMPATIBILITY   = 0x341,
    MM_SMS_DELIVERY_STATE_TEMPORARY_RADIO_INTERFACE_PROBLEM_OTHER             = 0x342,
    /* Temporary general problems */
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_ENCODING                            = 0x360,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_ORIGINATION_DENIED              = 0x361,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_TERMINATION_DENIED              = 0x362,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SUPPLEMENTARY_SERVICE_NOT_SUPPORTED = 0x363,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_SMS_NOT_SUPPORTED                   = 0x364,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_MISSING_EXPECTED_PARAMETER          = 0x366,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_MISSING_MANDATORY_PARAMETER         = 0x367,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_UNRECOGNIZED_PARAMETER_VALUE        = 0x368,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_UNEXPECTED_PARAMETER_VALUE          = 0x369,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_USER_DATA_SIZE_ERROR                = 0x36A,
    MM_SMS_DELIVERY_STATE_TEMPORARY_GENERAL_PROBLEM_OTHER                               = 0x36B,
} MMSmsDeliveryState;

/**
 * MMSmsStorage:
 * @MM_SMS_STORAGE_UNKNOWN: Storage unknown.
 * @MM_SMS_STORAGE_SM: SIM card storage area.
 * @MM_SMS_STORAGE_ME: Mobile equipment storage area.
 * @MM_SMS_STORAGE_MT: Sum of SIM and Mobile equipment storages
 * @MM_SMS_STORAGE_SR: Status report message storage area.
 * @MM_SMS_STORAGE_BM: Broadcast message storage area.
 * @MM_SMS_STORAGE_TA: Terminal adaptor message storage area.
 *
 * Storage for SMS messages.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_sms_storage >*/
    MM_SMS_STORAGE_UNKNOWN = 0,
    MM_SMS_STORAGE_SM      = 1,
    MM_SMS_STORAGE_ME      = 2,
    MM_SMS_STORAGE_MT      = 3,
    MM_SMS_STORAGE_SR      = 4,
    MM_SMS_STORAGE_BM      = 5,
    MM_SMS_STORAGE_TA      = 6,
} MMSmsStorage;

/**
 * MMSmsValidityType:
 * @MM_SMS_VALIDITY_TYPE_UNKNOWN: Validity type unknown.
 * @MM_SMS_VALIDITY_TYPE_RELATIVE: Relative validity.
 * @MM_SMS_VALIDITY_TYPE_ABSOLUTE: Absolute validity.
 * @MM_SMS_VALIDITY_TYPE_ENHANCED: Enhanced validity.
 *
 * Type of SMS validity value.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_sms_validity_type >*/
    MM_SMS_VALIDITY_TYPE_UNKNOWN  = 0,
    MM_SMS_VALIDITY_TYPE_RELATIVE = 1,
    MM_SMS_VALIDITY_TYPE_ABSOLUTE = 2,
    MM_SMS_VALIDITY_TYPE_ENHANCED = 3,
} MMSmsValidityType;

/**
 * MMSmsCdmaTeleserviceId:
 * @MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN: Unknown.
 * @MM_SMS_CDMA_TELESERVICE_ID_CMT91: IS-91 Extended Protocol Enhanced Services.
 * @MM_SMS_CDMA_TELESERVICE_ID_WPT: Wireless Paging Teleservice.
 * @MM_SMS_CDMA_TELESERVICE_ID_WMT: Wireless Messaging Teleservice.
 * @MM_SMS_CDMA_TELESERVICE_ID_VMN: Voice Mail Notification.
 * @MM_SMS_CDMA_TELESERVICE_ID_WAP: Wireless Application Protocol.
 * @MM_SMS_CDMA_TELESERVICE_ID_WEMT: Wireless Enhanced Messaging Teleservice.
 * @MM_SMS_CDMA_TELESERVICE_ID_SCPT: Service Category Programming Teleservice.
 * @MM_SMS_CDMA_TELESERVICE_ID_CATPT: Card Application Toolkit Protocol Teleservice.
 *
 * Teleservice IDs supported for CDMA SMS, as defined in 3GPP2 X.S0004-550-E
 * (section 2.256) and 3GPP2 C.S0015-B (section 3.4.3.1).
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_sms_cdma_teleservice_id >*/
    MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN = 0x0000,
    MM_SMS_CDMA_TELESERVICE_ID_CMT91   = 0x1000,
    MM_SMS_CDMA_TELESERVICE_ID_WPT     = 0x1001,
    MM_SMS_CDMA_TELESERVICE_ID_WMT     = 0x1002,
    MM_SMS_CDMA_TELESERVICE_ID_VMN     = 0x1003,
    MM_SMS_CDMA_TELESERVICE_ID_WAP     = 0x1004,
    MM_SMS_CDMA_TELESERVICE_ID_WEMT    = 0x1005,
    MM_SMS_CDMA_TELESERVICE_ID_SCPT    = 0x1006,
    MM_SMS_CDMA_TELESERVICE_ID_CATPT   = 0x1007,
} MMSmsCdmaTeleserviceId;

/**
 * MMSmsCdmaServiceCategory:
 * @MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN: Unknown.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_EMERGENCY_BROADCAST: Emergency broadcast.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ADMINISTRATIVE: Administrative.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_MAINTENANCE: Maintenance.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_LOCAL: General news (local).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_REGIONAL: General news (regional).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_NATIONAL: General news (national).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_INTERNATIONAL: General news (international).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_LOCAL: Business/Financial news (local).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_REGIONAL: Business/Financial news (regional).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_NATIONAL: Business/Financial news (national).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_INTERNATIONAL: Business/Financial news (international).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_LOCAL: Sports news (local).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_REGIONAL: Sports news (regional).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_NATIONAL: Sports news (national).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_INTERNATIONAL: Sports news (international).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_LOCAL: Entertainment news (local).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_REGIONAL: Entertainment news (regional).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_NATIONAL: Entertainment news (national).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_INTERNATIONAL: Entertainment news (international).
 * @MM_SMS_CDMA_SERVICE_CATEGORY_LOCAL_WEATHER: Local weather.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_TRAFFIC_REPORT: Area traffic report.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_FLIGHT_SCHEDULES: Local airport flight schedules.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_RESTAURANTS: Restaurants.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_LODGINGS: Lodgings.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_RETAIL_DIRECTORY: Retail directory.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_ADVERTISEMENTS: Advertisements.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_STOCK_QUOTES: Stock quotes.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_EMPLOYMENT: Employment.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_HOSPITALS: Medical / Health / Hospitals.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_TECHNOLOGY_NEWS: Technology news.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_MULTICATEGORY: Multi-category.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_PRESIDENTIAL_ALERT: Presidential alert.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_EXTREME_THREAT: Extreme threat.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_SEVERE_THREAT: Severe threat.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_CHILD_ABDUCTION_EMERGENCY: Child abduction emergency.
 * @MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_TEST: CMAS test.
 *
 * Service category for CDMA SMS, as defined in 3GPP2 C.R1001-D (section 9.3).
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_sms_cdma_service_category >*/
    MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN                          = 0x0000,
    MM_SMS_CDMA_SERVICE_CATEGORY_EMERGENCY_BROADCAST              = 0x0001,
    MM_SMS_CDMA_SERVICE_CATEGORY_ADMINISTRATIVE                   = 0x0002,
    MM_SMS_CDMA_SERVICE_CATEGORY_MAINTENANCE                      = 0x0003,
    MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_LOCAL               = 0x0004,
    MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_REGIONAL            = 0x0005,
    MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_NATIONAL            = 0x0006,
    MM_SMS_CDMA_SERVICE_CATEGORY_GENERAL_NEWS_INTERNATIONAL       = 0x0007,
    MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_LOCAL              = 0x0008,
    MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_REGIONAL           = 0x0009,
    MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_NATIONAL           = 0x000A,
    MM_SMS_CDMA_SERVICE_CATEGORY_BUSINESS_NEWS_INTERNATIONAL      = 0x000B,
    MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_LOCAL                = 0x000C,
    MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_REGIONAL             = 0x000D,
    MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_NATIONAL             = 0x000E,
    MM_SMS_CDMA_SERVICE_CATEGORY_SPORTS_NEWS_INTERNATIONAL        = 0x000F,
    MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_LOCAL         = 0x0010,
    MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_REGIONAL      = 0x0011,
    MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_NATIONAL      = 0x0012,
    MM_SMS_CDMA_SERVICE_CATEGORY_ENTERTAINMENT_NEWS_INTERNATIONAL = 0x0013,
    MM_SMS_CDMA_SERVICE_CATEGORY_LOCAL_WEATHER                    = 0x0014,
    MM_SMS_CDMA_SERVICE_CATEGORY_TRAFFIC_REPORT                   = 0x0015,
    MM_SMS_CDMA_SERVICE_CATEGORY_FLIGHT_SCHEDULES                 = 0x0016,
    MM_SMS_CDMA_SERVICE_CATEGORY_RESTAURANTS                      = 0x0017,
    MM_SMS_CDMA_SERVICE_CATEGORY_LODGINGS                         = 0x0018,
    MM_SMS_CDMA_SERVICE_CATEGORY_RETAIL_DIRECTORY                 = 0x0019,
    MM_SMS_CDMA_SERVICE_CATEGORY_ADVERTISEMENTS                   = 0x001A,
    MM_SMS_CDMA_SERVICE_CATEGORY_STOCK_QUOTES                     = 0x001B,
    MM_SMS_CDMA_SERVICE_CATEGORY_EMPLOYMENT                       = 0x001C,
    MM_SMS_CDMA_SERVICE_CATEGORY_HOSPITALS                        = 0x001D,
    MM_SMS_CDMA_SERVICE_CATEGORY_TECHNOLOGY_NEWS                  = 0x001E,
    MM_SMS_CDMA_SERVICE_CATEGORY_MULTICATEGORY                    = 0x001F,
    MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_PRESIDENTIAL_ALERT          = 0x1000,
    MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_EXTREME_THREAT              = 0x1001,
    MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_SEVERE_THREAT               = 0x1002,
    MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_CHILD_ABDUCTION_EMERGENCY   = 0x1003,
    MM_SMS_CDMA_SERVICE_CATEGORY_CMAS_TEST                        = 0x1004,
} MMSmsCdmaServiceCategory;

/**
 * MMCbmState:
 * @MM_CBM_STATE_UNKNOWN: State unknown or not reportable.
 * @MM_CBM_STATE_RECEIVING: The message is being received but is not yet complete.
 * @MM_CBM_STATE_RECEIVED: The message has been completely received.
 *
 * State of a given CBM.
 *
 * Since: 1.24
 */
typedef enum { /*< underscore_name=mm_cbm_state >*/
    MM_CBM_STATE_UNKNOWN   = 0,
    MM_CBM_STATE_RECEIVING = 1,
    MM_CBM_STATE_RECEIVED  = 2,
} MMCbmState;

/**
 * MMModemLocationSource:
 * @MM_MODEM_LOCATION_SOURCE_NONE: None.
 * @MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI: Location Area Code and Cell ID.
 * @MM_MODEM_LOCATION_SOURCE_GPS_RAW: GPS location given by predefined keys.
 * @MM_MODEM_LOCATION_SOURCE_GPS_NMEA: GPS location given as NMEA traces.
 * @MM_MODEM_LOCATION_SOURCE_CDMA_BS: CDMA base station position.
 * @MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED: No location given, just GPS module setup. Since 1.4.
 * @MM_MODEM_LOCATION_SOURCE_AGPS_MSA: Mobile Station Assisted A-GPS location requested. In
 *  MSA A-GPS, the position fix is computed by a server online. The modem must have a valid SIM
 *  card inserted and be enabled for this mode to be allowed. Since 1.12.
 * @MM_MODEM_LOCATION_SOURCE_AGPS_MSB: Mobile Station Based A-GPS location requested. In MSB
 *  A-GPS, the position fix is computed by the modem, but it first gathers information  from an
 *  online server to facilitate the process (e.g. ephemeris). The modem must have a valid SIM
 *  card inserted and be enabled for this mode to be allowed. Since 1.12.
 *
 * Sources of location information supported by the modem.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_location_source >*/
    MM_MODEM_LOCATION_SOURCE_NONE          = 0,
    MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI   = 1 << 0,
    MM_MODEM_LOCATION_SOURCE_GPS_RAW       = 1 << 1,
    MM_MODEM_LOCATION_SOURCE_GPS_NMEA      = 1 << 2,
    MM_MODEM_LOCATION_SOURCE_CDMA_BS       = 1 << 3,
    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED = 1 << 4,
    MM_MODEM_LOCATION_SOURCE_AGPS_MSA      = 1 << 5,
    MM_MODEM_LOCATION_SOURCE_AGPS_MSB      = 1 << 6,
#if defined (MM_COMPILATION) /*< private >*/
    MM_MODEM_LOCATION_SOURCE_FIRST = MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI, /*< skip >*/
    MM_MODEM_LOCATION_SOURCE_LAST  = MM_MODEM_LOCATION_SOURCE_AGPS_MSB,    /*< skip >*/
#endif
} MMModemLocationSource;

/**
 * MMModemLocationAssistanceDataType:
 * @MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE: None.
 * @MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_XTRA: Qualcomm gpsOneXTRA.
 *
 * Type of assistance data that may be injected to the GNSS module.
 *
 * Since: 1.10
 */
typedef enum { /*< underscore_name=mm_modem_location_assistance_data_type >*/
    MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE = 0,
    MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_XTRA = 1 << 0,
} MMModemLocationAssistanceDataType;

/**
 * MMModemContactsStorage:
 * @MM_MODEM_CONTACTS_STORAGE_UNKNOWN: Unknown location.
 * @MM_MODEM_CONTACTS_STORAGE_ME: Device's local memory.
 * @MM_MODEM_CONTACTS_STORAGE_SM: Card inserted in the device (like a SIM/RUIM).
 * @MM_MODEM_CONTACTS_STORAGE_MT: Combined device/ME and SIM/SM phonebook.
 *
 * Specifies different storage locations for contact information.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_contacts_storage >*/
    MM_MODEM_CONTACTS_STORAGE_UNKNOWN = 0,
    MM_MODEM_CONTACTS_STORAGE_ME      = 1,
    MM_MODEM_CONTACTS_STORAGE_SM      = 2,
    MM_MODEM_CONTACTS_STORAGE_MT      = 3,
} MMModemContactsStorage;

/**
 * MMBearerType:
 * @MM_BEARER_TYPE_UNKNOWN: Unknown bearer.
 * @MM_BEARER_TYPE_DEFAULT: Primary context (2G/3G) or default bearer (4G),
 * defined by the user of the API.
 * @MM_BEARER_TYPE_DEFAULT_ATTACH: The initial default bearer established
 * during LTE attach procedure, automatically connected as long as the device is
 * registered in the LTE network.
 * @MM_BEARER_TYPE_DEDICATED: Secondary context (2G/3G) or dedicated bearer
 * (4G), defined by the user of the API. These bearers use the same IP address
 * used by a primary context or default bearer and provide a dedicated flow for
 * specific traffic with different QoS settings.
 *
 * Type of context (2G/3G) or bearer (4G).
 *
 * Since: 1.10
 */
typedef enum { /*< underscore_name=mm_bearer_type >*/
    MM_BEARER_TYPE_UNKNOWN        = 0,
    MM_BEARER_TYPE_DEFAULT        = 1,
    MM_BEARER_TYPE_DEFAULT_ATTACH = 2,
    MM_BEARER_TYPE_DEDICATED      = 3,
} MMBearerType;

/**
 * MMBearerIpMethod:
 * @MM_BEARER_IP_METHOD_UNKNOWN: Unknown method.
 * @MM_BEARER_IP_METHOD_PPP: Use PPP to get IP addresses and DNS information.
 * For IPv6, use PPP to retrieve the 64-bit Interface Identifier, use the IID to
 * construct an IPv6 link-local address by following RFC 5072, and then run
 * DHCP over the PPP link to retrieve DNS settings.
 * @MM_BEARER_IP_METHOD_STATIC: Use the provided static IP configuration given
 * by the modem to configure the IP data interface.  Note that DNS servers may
 * not be provided by the network or modem firmware.
 * @MM_BEARER_IP_METHOD_DHCP: Begin DHCP or IPv6 SLAAC on the data interface to
 * obtain any necessary IP configuration details that are not already provided
 * by the IP configuration.  For IPv4 bearers DHCP should be used.  For IPv6
 * bearers SLAAC should be used, and the IP configuration may already contain
 * a link-local address that should be assigned to the interface before SLAAC
 * is started to obtain the rest of the configuration.
 *
 * Type of IP method configuration to be used in a given Bearer.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_bearer_ip_method >*/
    MM_BEARER_IP_METHOD_UNKNOWN = 0,
    MM_BEARER_IP_METHOD_PPP     = 1,
    MM_BEARER_IP_METHOD_STATIC  = 2,
    MM_BEARER_IP_METHOD_DHCP    = 3,
} MMBearerIpMethod;

/**
 * MMBearerIpFamily:
 * @MM_BEARER_IP_FAMILY_NONE: None or unknown.
 * @MM_BEARER_IP_FAMILY_IPV4: IPv4.
 * @MM_BEARER_IP_FAMILY_IPV6: IPv6.
 * @MM_BEARER_IP_FAMILY_IPV4V6: IPv4 and IPv6.
 * @MM_BEARER_IP_FAMILY_NON_IP: Non-IP Bearer. Since 1.20.
 * @MM_BEARER_IP_FAMILY_ANY: Mask specifying all IP based families.
 *
 * Type of IP family to be used in a given Bearer.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_bearer_ip_family >*/
    MM_BEARER_IP_FAMILY_NONE    = 0,
    MM_BEARER_IP_FAMILY_IPV4    = 1 << 0,
    MM_BEARER_IP_FAMILY_IPV6    = 1 << 1,
    MM_BEARER_IP_FAMILY_IPV4V6  = 1 << 2,
    MM_BEARER_IP_FAMILY_NON_IP  = 1 << 3,
    MM_BEARER_IP_FAMILY_ANY     = 0xFFFFFFFF
} MMBearerIpFamily;

/**
 * MMBearerAllowedAuth:
 * @MM_BEARER_ALLOWED_AUTH_UNKNOWN: Unknown.
 * @MM_BEARER_ALLOWED_AUTH_NONE: None.
 * @MM_BEARER_ALLOWED_AUTH_PAP: PAP.
 * @MM_BEARER_ALLOWED_AUTH_CHAP: CHAP.
 * @MM_BEARER_ALLOWED_AUTH_MSCHAP: MS-CHAP.
 * @MM_BEARER_ALLOWED_AUTH_MSCHAPV2: MS-CHAP v2.
 * @MM_BEARER_ALLOWED_AUTH_EAP: EAP.
 *
 * Allowed authentication methods when authenticating with the network.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_bearer_allowed_auth >*/
    MM_BEARER_ALLOWED_AUTH_UNKNOWN  = 0,
    /* bits 0..4 order match Ericsson device bitmap */
    MM_BEARER_ALLOWED_AUTH_NONE     = 1 << 0,
    MM_BEARER_ALLOWED_AUTH_PAP      = 1 << 1,
    MM_BEARER_ALLOWED_AUTH_CHAP     = 1 << 2,
    MM_BEARER_ALLOWED_AUTH_MSCHAP   = 1 << 3,
    MM_BEARER_ALLOWED_AUTH_MSCHAPV2 = 1 << 4,
    MM_BEARER_ALLOWED_AUTH_EAP      = 1 << 5,
} MMBearerAllowedAuth;

/**
 * MMModemCdmaRegistrationState:
 * @MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN: Registration status is unknown or the device is not registered.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED: Registered, but roaming status is unknown or cannot be provided by the device. The device may or may not be roaming.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_HOME: Currently registered on the home network.
 * @MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING: Currently registered on a roaming network.
 *
 * Registration state of a CDMA modem.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_cdma_registration_state >*/
    MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN    = 0,
    MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED = 1,
    MM_MODEM_CDMA_REGISTRATION_STATE_HOME       = 2,
    MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING    = 3,
} MMModemCdmaRegistrationState;

/**
 * MMModemCdmaActivationState:
 * @MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN: Unknown activation state.
 * @MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED: Device is not activated
 * @MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING: Device is activating
 * @MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED: Device is partially activated; carrier-specific steps required to continue.
 * @MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED: Device is ready for use.
 *
 * Activation state of a CDMA modem.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_cdma_activation_state >*/
    MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN             = 0,
    MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED       = 1,
    MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING          = 2,
    MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED = 3,
    MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED           = 4,
} MMModemCdmaActivationState;

/**
 * MMModemCdmaRmProtocol:
 * @MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN: Unknown protocol.
 * @MM_MODEM_CDMA_RM_PROTOCOL_ASYNC: Asynchronous data or fax.
 * @MM_MODEM_CDMA_RM_PROTOCOL_PACKET_RELAY: Packet data service, Relay Layer Rm interface.
 * @MM_MODEM_CDMA_RM_PROTOCOL_PACKET_NETWORK_PPP: Packet data service, Network Layer Rm interface, PPP.
 * @MM_MODEM_CDMA_RM_PROTOCOL_PACKET_NETWORK_SLIP: Packet data service, Network Layer Rm interface, SLIP.
 * @MM_MODEM_CDMA_RM_PROTOCOL_STU_III: STU-III service.
 *
 * Protocol of the Rm interface in modems with CDMA capabilities.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_cdma_rm_protocol >*/
    MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN             = 0,
    MM_MODEM_CDMA_RM_PROTOCOL_ASYNC               = 1,
    MM_MODEM_CDMA_RM_PROTOCOL_PACKET_RELAY        = 2,
    MM_MODEM_CDMA_RM_PROTOCOL_PACKET_NETWORK_PPP  = 3,
    MM_MODEM_CDMA_RM_PROTOCOL_PACKET_NETWORK_SLIP = 4,
    MM_MODEM_CDMA_RM_PROTOCOL_STU_III             = 5,
} MMModemCdmaRmProtocol;

/**
 * MMModem3gppRegistrationState:
 * @MM_MODEM_3GPP_REGISTRATION_STATE_IDLE: Not registered, not searching for new operator to register.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_HOME: Registered on home network.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING: Not registered, searching for new operator to register with.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_DENIED: Registration denied.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN: Unknown registration status.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING: Registered on a roaming network.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY: Registered for "SMS only", home network (applicable only when on LTE). Since 1.8.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY: Registered for "SMS only", roaming network (applicable only when on LTE). Since 1.8.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY: Emergency services only. Since 1.8.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED: Registered for "CSFB not preferred", home network (applicable only when on LTE). Since 1.8.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED: Registered for "CSFB not preferred", roaming network (applicable only when on LTE). Since 1.8.
 * @MM_MODEM_3GPP_REGISTRATION_STATE_ATTACHED_RLOS: Attached for access to Restricted Local Operator Services (applicable only when on LTE). Since 1.14.
 *
 * GSM registration code as defined in 3GPP TS 27.007.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_registration_state >*/
    MM_MODEM_3GPP_REGISTRATION_STATE_IDLE                       = 0,
    MM_MODEM_3GPP_REGISTRATION_STATE_HOME                       = 1,
    MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING                  = 2,
    MM_MODEM_3GPP_REGISTRATION_STATE_DENIED                     = 3,
    MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN                    = 4,
    MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING                    = 5,
    MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY              = 6,
    MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY           = 7,
    MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY             = 8,
    MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED    = 9,
    MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED = 10,
    MM_MODEM_3GPP_REGISTRATION_STATE_ATTACHED_RLOS              = 11,
} MMModem3gppRegistrationState;

/**
 * MMModem3gppFacility:
 * @MM_MODEM_3GPP_FACILITY_NONE: No facility.
 * @MM_MODEM_3GPP_FACILITY_SIM: SIM lock.
 * @MM_MODEM_3GPP_FACILITY_FIXED_DIALING: Fixed dialing (PIN2) SIM lock.
 * @MM_MODEM_3GPP_FACILITY_PH_SIM: Device is locked to a specific SIM.
 * @MM_MODEM_3GPP_FACILITY_PH_FSIM: Device is locked to first SIM inserted.
 * @MM_MODEM_3GPP_FACILITY_NET_PERS: Network personalization.
 * @MM_MODEM_3GPP_FACILITY_NET_SUB_PERS: Network subset personalization.
 * @MM_MODEM_3GPP_FACILITY_PROVIDER_PERS: Service provider personalization.
 * @MM_MODEM_3GPP_FACILITY_CORP_PERS: Corporate personalization.
 *
 * A bitfield describing which facilities have a lock enabled, i.e.,
 * requires a pin or unlock code. The facilities include the
 * personalizations (device locks) described in 3GPP spec TS 22.022,
 * and the PIN and PIN2 locks, which are SIM locks.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_facility >*/
    MM_MODEM_3GPP_FACILITY_NONE          = 0,
    MM_MODEM_3GPP_FACILITY_SIM           = 1 << 0,
    MM_MODEM_3GPP_FACILITY_FIXED_DIALING = 1 << 1,
    MM_MODEM_3GPP_FACILITY_PH_SIM        = 1 << 2,
    MM_MODEM_3GPP_FACILITY_PH_FSIM       = 1 << 3,
    MM_MODEM_3GPP_FACILITY_NET_PERS      = 1 << 4,
    MM_MODEM_3GPP_FACILITY_NET_SUB_PERS  = 1 << 5,
    MM_MODEM_3GPP_FACILITY_PROVIDER_PERS = 1 << 6,
    MM_MODEM_3GPP_FACILITY_CORP_PERS     = 1 << 7,
} MMModem3gppFacility;

/**
 * MMModem3gppNetworkAvailability:
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN: Unknown availability.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE: Network is available.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT: Network is the current one.
 * @MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN: Network is forbidden.
 *
 * Network availability status as defined in 3GPP TS 27.007 section 7.3.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_network_availability >*/
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN   = 0,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE = 1,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT   = 2,
    MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN = 3,
} MMModem3gppNetworkAvailability;

/**
 * MMModem3gppSubscriptionState:
 * @MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN: The subscription state is unknown.
 * @MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNPROVISIONED: The account is unprovisioned.
 * @MM_MODEM_3GPP_SUBSCRIPTION_STATE_PROVISIONED: The account is provisioned and has data available.
 * @MM_MODEM_3GPP_SUBSCRIPTION_STATE_OUT_OF_DATA: The account is provisioned but there is no data left.
 *
 * Describes the current subscription status of the SIM.  This value is only available after the
 * modem attempts to register with the network.
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_subscription_state >*/
    MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN       = 0,
    MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNPROVISIONED = 1,
    MM_MODEM_3GPP_SUBSCRIPTION_STATE_PROVISIONED   = 2,
    MM_MODEM_3GPP_SUBSCRIPTION_STATE_OUT_OF_DATA   = 3,
} MMModem3gppSubscriptionState;

/**
 * MMModem3gppUssdSessionState:
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN: Unknown state.
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE: No active session.
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE: A session is active and the mobile is waiting for a response.
 * @MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE: The network is waiting for the client's response.
 *
 * State of a USSD session.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_ussd_session_state >*/
    MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN       = 0,
    MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE          = 1,
    MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE        = 2,
    MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE = 3,
} MMModem3gppUssdSessionState;

/**
 * MMModem3gppEpsUeModeOperation:
 * @MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN: Unknown or not applicable.
 * @MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_1: PS mode 1 of operation: EPS only, voice-centric.
 * @MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_2: PS mode 2 of operation: EPS only, data-centric.
 * @MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_1: CS/PS mode 1 of operation: EPS and non-EPS, voice-centric.
 * @MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_2: CS/PS mode 2 of operation: EPS and non-EPS, data-centric.
 *
 * UE mode of operation for EPS, as per 3GPP TS 24.301.
 *
 * Since: 1.8
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_eps_ue_mode_operation >*/
    MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN = 0,
    MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_1    = 1,
    MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_2    = 2,
    MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_1  = 3,
    MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_2  = 4,
} MMModem3gppEpsUeModeOperation;

/**
 * MMFirmwareImageType:
 * @MM_FIRMWARE_IMAGE_TYPE_UNKNOWN: Unknown firmware type.
 * @MM_FIRMWARE_IMAGE_TYPE_GENERIC: Generic firmware image.
 * @MM_FIRMWARE_IMAGE_TYPE_GOBI: Firmware image of Gobi devices.
 *
 * Type of firmware image.
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_firmware_image_type >*/
    MM_FIRMWARE_IMAGE_TYPE_UNKNOWN = 0,
    MM_FIRMWARE_IMAGE_TYPE_GENERIC = 1,
    MM_FIRMWARE_IMAGE_TYPE_GOBI    = 2,
} MMFirmwareImageType;

/**
 * MMOmaFeature:
 * @MM_OMA_FEATURE_NONE: None.
 * @MM_OMA_FEATURE_DEVICE_PROVISIONING: Device provisioning service.
 * @MM_OMA_FEATURE_PRL_UPDATE: PRL update service.
 * @MM_OMA_FEATURE_HANDS_FREE_ACTIVATION: Hands free activation service.
 *
 * Features that can be enabled or disabled in the OMA device management support.
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_oma_feature >*/
    MM_OMA_FEATURE_NONE                  = 0,
    MM_OMA_FEATURE_DEVICE_PROVISIONING   = 1 << 0,
    MM_OMA_FEATURE_PRL_UPDATE            = 1 << 1,
    MM_OMA_FEATURE_HANDS_FREE_ACTIVATION = 1 << 2,
} MMOmaFeature;

/**
 * MMOmaSessionType:
 * @MM_OMA_SESSION_TYPE_UNKNOWN: Unknown session type.
 * @MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE: Client-initiated device configure.
 * @MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE: Client-initiated PRL update.
 * @MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION: Client-initiated hands free activation.
 * @MM_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE: Network-initiated device configure.
 * @MM_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE: Network-initiated PRL update.
 * @MM_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE: Device-initiated PRL update.
 * @MM_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION: Device-initiated hands free activation.
 *
 * Type of OMA device management session.
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_oma_session_type >*/
    MM_OMA_SESSION_TYPE_UNKNOWN                                = 0,
    MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE      = 10,
    MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE            = 11,
    MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION = 12,
    MM_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE     = 20,
    MM_OMA_SESSION_TYPE_NETWORK_INITIATED_PRL_UPDATE           = 21,
    MM_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE            = 30,
    MM_OMA_SESSION_TYPE_DEVICE_INITIATED_HANDS_FREE_ACTIVATION = 31,
} MMOmaSessionType;

/**
 * MMOmaSessionState:
 * @MM_OMA_SESSION_STATE_FAILED: Failed.
 * @MM_OMA_SESSION_STATE_UNKNOWN: Unknown.
 * @MM_OMA_SESSION_STATE_STARTED: Started.
 * @MM_OMA_SESSION_STATE_RETRYING: Retrying.
 * @MM_OMA_SESSION_STATE_CONNECTING: Connecting.
 * @MM_OMA_SESSION_STATE_CONNECTED: Connected.
 * @MM_OMA_SESSION_STATE_AUTHENTICATED: Authenticated.
 * @MM_OMA_SESSION_STATE_MDN_DOWNLOADED: MDN downloaded.
 * @MM_OMA_SESSION_STATE_MSID_DOWNLOADED: MSID downloaded.
 * @MM_OMA_SESSION_STATE_PRL_DOWNLOADED: PRL downloaded.
 * @MM_OMA_SESSION_STATE_MIP_PROFILE_DOWNLOADED: MIP profile downloaded.
 * @MM_OMA_SESSION_STATE_COMPLETED: Session completed.
 *
 * State of the OMA device management session.
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_oma_session_state >*/
    MM_OMA_SESSION_STATE_FAILED                 = -1,
    MM_OMA_SESSION_STATE_UNKNOWN                = 0,
    MM_OMA_SESSION_STATE_STARTED                = 1,
    MM_OMA_SESSION_STATE_RETRYING               = 2,
    MM_OMA_SESSION_STATE_CONNECTING             = 3,
    MM_OMA_SESSION_STATE_CONNECTED              = 4,
    MM_OMA_SESSION_STATE_AUTHENTICATED          = 5,
    MM_OMA_SESSION_STATE_MDN_DOWNLOADED         = 10,
    MM_OMA_SESSION_STATE_MSID_DOWNLOADED        = 11,
    MM_OMA_SESSION_STATE_PRL_DOWNLOADED         = 12,
    MM_OMA_SESSION_STATE_MIP_PROFILE_DOWNLOADED = 13,
    MM_OMA_SESSION_STATE_COMPLETED              = 20,
} MMOmaSessionState;

/**
 * MMOmaSessionStateFailedReason:
 * @MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN: No reason or unknown.
 * @MM_OMA_SESSION_STATE_FAILED_REASON_NETWORK_UNAVAILABLE: Network unavailable.
 * @MM_OMA_SESSION_STATE_FAILED_REASON_SERVER_UNAVAILABLE: Server unavailable.
 * @MM_OMA_SESSION_STATE_FAILED_REASON_AUTHENTICATION_FAILED: Authentication failed.
 * @MM_OMA_SESSION_STATE_FAILED_REASON_MAX_RETRY_EXCEEDED: Maximum retries exceeded.
 * @MM_OMA_SESSION_STATE_FAILED_REASON_SESSION_CANCELLED: Session cancelled.
 *
 * Reason of failure in the OMA device management session.
 *
 * Since: 1.2
 */
typedef enum { /*< underscore_name=mm_oma_session_state_failed_reason >*/
    MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN               = 0,
    MM_OMA_SESSION_STATE_FAILED_REASON_NETWORK_UNAVAILABLE   = 1,
    MM_OMA_SESSION_STATE_FAILED_REASON_SERVER_UNAVAILABLE    = 2,
    MM_OMA_SESSION_STATE_FAILED_REASON_AUTHENTICATION_FAILED = 3,
    MM_OMA_SESSION_STATE_FAILED_REASON_MAX_RETRY_EXCEEDED    = 4,
    MM_OMA_SESSION_STATE_FAILED_REASON_SESSION_CANCELLED     = 5
} MMOmaSessionStateFailedReason;

/**
 * MMCallState:
 * @MM_CALL_STATE_UNKNOWN: default state for a new outgoing call.
 * @MM_CALL_STATE_DIALING: outgoing call started. Wait for free channel.
 * @MM_CALL_STATE_RINGING_IN: incoming call is waiting for an answer.
 * @MM_CALL_STATE_RINGING_OUT: outgoing call attached to GSM network, waiting for an answer.
 * @MM_CALL_STATE_ACTIVE: call is active between two peers.
 * @MM_CALL_STATE_HELD: held call (by +CHLD AT command).
 * @MM_CALL_STATE_WAITING: waiting call (by +CCWA AT command).
 * @MM_CALL_STATE_TERMINATED: call is terminated.
 *
 * State of Call.
 *
 * Since: 1.6
 */
typedef enum { /*< underscore_name=mm_call_state >*/
    MM_CALL_STATE_UNKNOWN       = 0,
    MM_CALL_STATE_DIALING       = 1,
    MM_CALL_STATE_RINGING_OUT   = 2,
    MM_CALL_STATE_RINGING_IN    = 3,
    MM_CALL_STATE_ACTIVE        = 4,
    MM_CALL_STATE_HELD          = 5,
    MM_CALL_STATE_WAITING       = 6,
    MM_CALL_STATE_TERMINATED    = 7
} MMCallState;

/**
 * MMCallStateReason:
 * @MM_CALL_STATE_REASON_UNKNOWN: Default value for a new outgoing call.
 * @MM_CALL_STATE_REASON_OUTGOING_STARTED: Outgoing call is started.
 * @MM_CALL_STATE_REASON_INCOMING_NEW: Received a new incoming call.
 * @MM_CALL_STATE_REASON_ACCEPTED: Dialing or Ringing call is accepted.
 * @MM_CALL_STATE_REASON_TERMINATED: Call is correctly terminated.
 * @MM_CALL_STATE_REASON_REFUSED_OR_BUSY: Remote peer is busy or refused call.
 * @MM_CALL_STATE_REASON_ERROR: Wrong number or generic network error.
 * @MM_CALL_STATE_REASON_AUDIO_SETUP_FAILED: Error setting up audio channel. Since 1.10.
 * @MM_CALL_STATE_REASON_TRANSFERRED: Call has been transferred. Since 1.12.
 * @MM_CALL_STATE_REASON_DEFLECTED: Call has been deflected to a new number. Since 1.12.
 *
 * Reason for the state change in the call.
 *
 * Since: 1.6
 */
typedef enum { /*< underscore_name=mm_call_state_reason >*/
    MM_CALL_STATE_REASON_UNKNOWN            = 0,
    MM_CALL_STATE_REASON_OUTGOING_STARTED   = 1,
    MM_CALL_STATE_REASON_INCOMING_NEW       = 2,
    MM_CALL_STATE_REASON_ACCEPTED           = 3,
    MM_CALL_STATE_REASON_TERMINATED         = 4,
    MM_CALL_STATE_REASON_REFUSED_OR_BUSY    = 5,
    MM_CALL_STATE_REASON_ERROR              = 6,
    MM_CALL_STATE_REASON_AUDIO_SETUP_FAILED = 7,
    MM_CALL_STATE_REASON_TRANSFERRED        = 8,
    MM_CALL_STATE_REASON_DEFLECTED          = 9,
} MMCallStateReason;

/**
 * MMCallDirection:
 * @MM_CALL_DIRECTION_UNKNOWN: unknown.
 * @MM_CALL_DIRECTION_INCOMING: call from network.
 * @MM_CALL_DIRECTION_OUTGOING: call to network.
 *
 * Direction of the call.
 *
 * Since: 1.6
 */
typedef enum { /*< underscore_name=mm_call_direction >*/
    MM_CALL_DIRECTION_UNKNOWN   = 0,
    MM_CALL_DIRECTION_INCOMING  = 1,
    MM_CALL_DIRECTION_OUTGOING  = 2
} MMCallDirection;

/**
 * MMModemFirmwareUpdateMethod:
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE: No method specified.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT: Device supports fastboot-based update.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC: Device supports QMI PDC based update.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU: Device supports MBIM QDU based update. Since 1.18.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE: Device supports Firehose based update. Since 1.18.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA: Device supports Sahara protocol. Usually used in combination with Firehose. Since 1.20.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_DFOTA: Device supports Quectel DFOTA based update. Since 1.24.
 * @MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL: Device supports AT^SFDL based update. Since 1.24.
 *
 * Type of firmware update method supported by the module.
 *
 * Since: 1.10
 */
typedef enum { /*< underscore_name=mm_modem_firmware_update_method >*/
    MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE          = 0,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT      = 1 << 0,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC       = 1 << 1,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU      = 1 << 2,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE      = 1 << 3,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA        = 1 << 4,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_DFOTA         = 1 << 5,
    MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL = 1 << 6,
} MMModemFirmwareUpdateMethod;

/**
 * MMBearerMultiplexSupport:
 * @MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN: Unknown.
 * @MM_BEARER_MULTIPLEX_SUPPORT_NONE: No multiplex support should be used.
 * @MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED: If available, multiplex support should be used.
 * @MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED: Multiplex support must be used or otherwise the connection attempt will fail.
 *
 * Multiplex support requested by the user.
 *
 * Since: 1.18
 */
typedef enum { /*< underscore_name=mm_bearer_multiplex_support >*/
    MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN   = 0,
    MM_BEARER_MULTIPLEX_SUPPORT_NONE      = 1,
    MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED = 2,
    MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED  = 3,
} MMBearerMultiplexSupport;

/**
 * MMBearerApnType:
 * @MM_BEARER_APN_TYPE_NONE: Unknown or unsupported.
 * @MM_BEARER_APN_TYPE_INITIAL: APN used for the initial attach procedure.
 * @MM_BEARER_APN_TYPE_DEFAULT: Default connection APN providing access to the Internet.
 * @MM_BEARER_APN_TYPE_IMS: APN providing access to IMS services.
 * @MM_BEARER_APN_TYPE_MMS: APN providing access to MMS services.
 * @MM_BEARER_APN_TYPE_MANAGEMENT: APN providing access to over-the-air device management procedures.
 * @MM_BEARER_APN_TYPE_VOICE: APN providing access to voice-over-IP services.
 * @MM_BEARER_APN_TYPE_EMERGENCY: APN providing access to emergency services.
 * @MM_BEARER_APN_TYPE_PRIVATE: APN providing access to private networks.
 * @MM_BEARER_APN_TYPE_PURCHASE: APN providing access to over-the-air activation sites. Since 1.20.
 * @MM_BEARER_APN_TYPE_VIDEO_SHARE: APN providing access to video sharing service. Since 1.20.
 * @MM_BEARER_APN_TYPE_LOCAL: APN providing access to a local connection with the device. Since 1.20.
 * @MM_BEARER_APN_TYPE_APP: APN providing access to certain applications allowed  by mobile operators. Since 1.20.
 * @MM_BEARER_APN_TYPE_XCAP: APN providing access to XCAP provisioning on IMS services. Since 1.20.
 * @MM_BEARER_APN_TYPE_TETHERING: APN providing access to mobile hotspot tethering. Since 1.20.
 *
 * Purpose of the APN used in a given Bearer.
 *
 * This information may be stored in the device configuration (e.g. if carrier
 * specific configurations have been enabled for the SIM in use), or provided
 * explicitly by the user.
 *
 * If the mask of types includes %MM_BEARER_APN_TYPE_DEFAULT, it is expected
 * that the connection manager will include a default route through the specific
 * bearer connection to the public Internet.
 *
 * For any other mask type, it is expected that the connection manager will
 * not setup a default route and will therefore require additional custom
 * routing rules to provide access to the different services. E.g. a bearer
 * connected with %MM_BEARER_APN_TYPE_MMS will probably require an explicit
 * additional route in the host to access the MMSC server at the address
 * specified by the operator. If this address relies on a domain name instead
 * of a fixed IP address, the name resolution should be performed using the
 * DNS servers specified in the corresponding bearer connection settings.
 *
 * If not explicitly specified during a connection attempt, the connection
 * manager should be free to treat it with its own logic. E.g. a good default
 * could be to treat the first connection as %MM_BEARER_APN_TYPE_DEFAULT (with
 * a default route) and any other additional connection as
 * %MM_BEARER_APN_TYPE_PRIVATE (without a default route).
 *
 * Since: 1.18
 */
typedef enum { /*< underscore_name=mm_bearer_apn_type >*/
    MM_BEARER_APN_TYPE_NONE        = 0,
    MM_BEARER_APN_TYPE_INITIAL     = 1 << 0,
    MM_BEARER_APN_TYPE_DEFAULT     = 1 << 1,
    MM_BEARER_APN_TYPE_IMS         = 1 << 2,
    MM_BEARER_APN_TYPE_MMS         = 1 << 3,
    MM_BEARER_APN_TYPE_MANAGEMENT  = 1 << 4,
    MM_BEARER_APN_TYPE_VOICE       = 1 << 5,
    MM_BEARER_APN_TYPE_EMERGENCY   = 1 << 6,
    MM_BEARER_APN_TYPE_PRIVATE     = 1 << 7,
    MM_BEARER_APN_TYPE_PURCHASE    = 1 << 8,
    MM_BEARER_APN_TYPE_VIDEO_SHARE = 1 << 9,
    MM_BEARER_APN_TYPE_LOCAL       = 1 << 10,
    MM_BEARER_APN_TYPE_APP         = 1 << 11,
    MM_BEARER_APN_TYPE_XCAP        = 1 << 12,
    MM_BEARER_APN_TYPE_TETHERING   = 1 << 13,
} MMBearerApnType;

/**
 * MMModem3gppPacketServiceState:
 * @MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN: Unknown.
 * @MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED: Detached.
 * @MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED: Attached.
 *
 * The packet domain service state.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_packet_service_state >*/
    MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN  = 0,
    MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED = 1,
    MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED = 2,
} MMModem3gppPacketServiceState;

/**
 * MMSimType:
 * @MM_SIM_TYPE_UNKNOWN: SIM type is not known.
 * @MM_SIM_TYPE_PHYSICAL: SIM is a physical SIM.
 * @MM_SIM_TYPE_ESIM: SIM is a ESIM.
 *
 * SIM type indicating whether ESIM or not
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_sim_type >*/
    MM_SIM_TYPE_UNKNOWN  = 0,
    MM_SIM_TYPE_PHYSICAL = 1,
    MM_SIM_TYPE_ESIM     = 2,
} MMSimType;

/**
 * MMSimEsimStatus:
 * @MM_SIM_ESIM_STATUS_UNKNOWN: ESIM status unknown.
 * @MM_SIM_ESIM_STATUS_NO_PROFILES: ESIM with no profiles.
 * @MM_SIM_ESIM_STATUS_WITH_PROFILES: ESIM with profiles.
 *
 * Status of the profiles for ESIM
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_sim_esim_status >*/
    MM_SIM_ESIM_STATUS_UNKNOWN       = 0,
    MM_SIM_ESIM_STATUS_NO_PROFILES   = 1,
    MM_SIM_ESIM_STATUS_WITH_PROFILES = 2,
} MMSimEsimStatus;

/**
 * MMSimRemovability:
 * @MM_SIM_REMOVABILITY_UNKNOWN: SIM removability not known.
 * @MM_SIM_REMOVABILITY_REMOVABLE: SIM is a removable SIM.
 * @MM_SIM_REMOVABILITY_NOT_REMOVABLE: SIM is not a removable SIM.
 *
 * Represents SIM removability of the current SIM.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_sim_removability >*/
    MM_SIM_REMOVABILITY_UNKNOWN       = 0,
    MM_SIM_REMOVABILITY_REMOVABLE     = 1,
    MM_SIM_REMOVABILITY_NOT_REMOVABLE = 2,
} MMSimRemovability;

/**
 * MMModem3gppMicoMode:
 * @MM_MODEM_3GPP_MICO_MODE_UNKNOWN: Unknown or not specified.
 * @MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED: Unsupported.
 * @MM_MODEM_3GPP_MICO_MODE_DISABLED: Disabled.
 * @MM_MODEM_3GPP_MICO_MODE_ENABLED: Enabled.
 *
 * Mobile Initiated Connection Only (MICO) mode.
 *
 * This is a 5G-specific registration setting.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_mico_mode >*/
    MM_MODEM_3GPP_MICO_MODE_UNKNOWN     = 0,
    MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED = 1,
    MM_MODEM_3GPP_MICO_MODE_DISABLED    = 2,
    MM_MODEM_3GPP_MICO_MODE_ENABLED     = 3,
} MMModem3gppMicoMode;

/**
 * MMModem3gppDrxCycle:
 * @MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN: Unknown or not specified.
 * @MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED: Unsupported.
 * @MM_MODEM_3GPP_DRX_CYCLE_32: DRX cycle T=32.
 * @MM_MODEM_3GPP_DRX_CYCLE_64: DRX cycle T=64.
 * @MM_MODEM_3GPP_DRX_CYCLE_128: DRX cycle T=128.
 * @MM_MODEM_3GPP_DRX_CYCLE_256: DRX cycle T=256.
 *
 * DRX cycle.
 *
 * This is a 5G-specific registration setting.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_modem_3gpp_drx_cycle >*/
    MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN     = 0,
    MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED = 1,
    MM_MODEM_3GPP_DRX_CYCLE_32          = 2,
    MM_MODEM_3GPP_DRX_CYCLE_64          = 3,
    MM_MODEM_3GPP_DRX_CYCLE_128         = 4,
    MM_MODEM_3GPP_DRX_CYCLE_256         = 5,
} MMModem3gppDrxCycle;

/**
 * MMBearerAccessTypePreference:
 * @MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE: No access type preference, or unknown.
 * @MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_ONLY: 3GPP access type only.
 * @MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_PREFERRED: All access types allowed but 3GPP preferred.
 * @MM_BEARER_ACCESS_TYPE_PREFERENCE_NON_3GPP_ONLY: Non-3GPP access type only.
 *
 * 5G network access type preference, such as 3GPP (NR, E-UTRA) or
 * non-3GPP (untrusted WiFi, trusted WiFi, wireline).
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_bearer_access_type_preference >*/
    MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE           = 0,
    MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_ONLY      = 1,
    MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_PREFERRED = 2,
    MM_BEARER_ACCESS_TYPE_PREFERENCE_NON_3GPP_ONLY  = 3,
} MMBearerAccessTypePreference;

/**
 * MMBearerRoamingAllowance:
 * @MM_BEARER_ROAMING_ALLOWANCE_NONE: No explicit roaming allowance rules.
 * @MM_BEARER_ROAMING_ALLOWANCE_HOME: Home network allowed.
 * @MM_BEARER_ROAMING_ALLOWANCE_PARTNER: Partner network allowed.
 * @MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER: Non-parter network allowed.
 *
 * Bitmask specifying roaming allowance rules for different network types.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_bearer_roaming_allowance >*/
    MM_BEARER_ROAMING_ALLOWANCE_NONE        = 0,
    MM_BEARER_ROAMING_ALLOWANCE_HOME        = 1 << 0,
    MM_BEARER_ROAMING_ALLOWANCE_PARTNER     = 1 << 1,
    MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER = 1 << 2,
} MMBearerRoamingAllowance;

/**
 * MMBearerProfileSource:
 * @MM_BEARER_PROFILE_SOURCE_UNKNOWN: Unknown.
 * @MM_BEARER_PROFILE_SOURCE_ADMIN: Profile created by an enterprise IT admin from the OS.
 * @MM_BEARER_PROFILE_SOURCE_USER: Profile created by the user.
 * @MM_BEARER_PROFILE_SOURCE_OPERATOR: Profile created by the operator through OMA-DM or similar.
 * @MM_BEARER_PROFILE_SOURCE_MODEM: Profile created by the OEM that was included with the modem firmware.
 * @MM_BEARER_PROFILE_SOURCE_DEVICE: Profile created by the OS APN database.
 *
 * Value specifying how a given context was created, mostly for informative purposes.
 *
 * Since: 1.20
 */
typedef enum { /*< underscore_name=mm_bearer_profile_source >*/
    MM_BEARER_PROFILE_SOURCE_UNKNOWN  = 0,
    MM_BEARER_PROFILE_SOURCE_ADMIN    = 1,
    MM_BEARER_PROFILE_SOURCE_USER     = 2,
    MM_BEARER_PROFILE_SOURCE_OPERATOR = 3,
    MM_BEARER_PROFILE_SOURCE_MODEM    = 4,
    MM_BEARER_PROFILE_SOURCE_DEVICE   = 5,
} MMBearerProfileSource;

/*****************************************************************************/
/* 'RF' enums */

/**
 * MMServingCellType:
 * @MM_SERVING_CELL_TYPE_UNKNOWN: Unknown.
 * @MM_SERVING_CELL_TYPE_PCELL: In LTE CA, this refers to the primary cell. In ENDC case, this is the primary cell of MCS. In 5G SA, this refers to primary cell of NR.
 * @MM_SERVING_CELL_TYPE_SCELL: In LTE CA, this refers to the secondary cell. In ENDC case, this is the secondary cell of MCS. In 5G SA, this refers to secondary cell of NR.
 * @MM_SERVING_CELL_TYPE_PSCELL: Refers to primary cell of secondary cell group (SCG).
 * @MM_SERVING_CELL_TYPE_SSCELL: Refers to secondary cell of secondary cell group (SCG).
 * @MM_SERVING_CELL_TYPE_INVALID: Indicates that the radio is off, all other values are invalid.
 *
 * Indicates the frequency information provided belongs to which serving cell.
 *
 * Since: 1.22
 */
typedef enum { /*< underscore_name=mm_serving_cell_type >*/
    MM_SERVING_CELL_TYPE_UNKNOWN  = 0,
    MM_SERVING_CELL_TYPE_PCELL    = 1,
    MM_SERVING_CELL_TYPE_SCELL    = 2,
    MM_SERVING_CELL_TYPE_PSCELL   = 3,
    MM_SERVING_CELL_TYPE_SSCELL   = 4,
    MM_SERVING_CELL_TYPE_INVALID  = 0xFFFFFFFF
} MMServingCellType;

/**
 * MMNetworkError:
 * @MM_NETWORK_ERROR_NONE: No error.
 * @MM_NETWORK_ERROR_IMSI_UNKNOWN_IN_HLR: IMSI unknown in the HLR.
 * @MM_NETWORK_ERROR_ILLEGAL_MS: Illegal MS.
 * @MM_NETWORK_ERROR_IMSI_UNKNOWN_IN_VLR: IMSI unknown in the VLR.
 * @MM_NETWORK_ERROR_IMEI_NOT_ACCEPTED: IMEI not accepted.
 * @MM_NETWORK_ERROR_ILLEGAL_ME: Illegal ME.
 * @MM_NETWORK_ERROR_GPRS_NOT_ALLOWED: GPRS not allowed.
 * @MM_NETWORK_ERROR_GPRS_AND_NON_GPRS_NOT_ALLOWED: GPRS and non-GPRS not allowed.
 * @MM_NETWORK_ERROR_MS_IDENTITY_NOT_DERIVED_BY_NETWORK: MS identity cannot be derived by the network.
 * @MM_NETWORK_ERROR_IMPLICITLY_DETACHED: Implicitly detached.
 * @MM_NETWORK_ERROR_PLMN_NOT_ALLOWED: PLMN not allowed.
 * @MM_NETWORK_ERROR_LOCATION_AREA_NOT_ALLOWED: Location area not allowed.
 * @MM_NETWORK_ERROR_ROAMING_NOT_ALLOWED_IN_LOCATION_AREA: Roaming not allowed in the location area.
 * @MM_NETWORK_ERROR_GPRS_NOT_ALLOWED_IN_PLMN: GPRS not allowed in PLMN.
 * @MM_NETWORK_ERROR_NO_CELLS_IN_LOCATION_AREA: No cells in location area.
 * @MM_NETWORK_ERROR_MSC_TEMPORARILY_NOT_REACHABLE: MSC temporarily not reachable.
 * @MM_NETWORK_ERROR_NETWORK_FAILURE: Network failure.
 * @MM_NETWORK_ERROR_MAC_FAILURE: MAC failure.
 * @MM_NETWORK_ERROR_SYNCH_FAILURE: Synch failure.
 * @MM_NETWORK_ERROR_CONGESTION: Congestion.
 * @MM_NETWORK_ERROR_GSM_AUTHENTICATION_UNACCEPTABLE: GSM authentication unacceptable.
 * @MM_NETWORK_ERROR_NOT_AUTHORIZED_FOR_CSG: Not authorized for this CSG.
 * @MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES: Insufficient resources.
 * @MM_NETWORK_ERROR_MISSING_OR_UNKNOWN_APN: Missing or unknown access point name.
 * @MM_NETWORK_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE: Unknown PDP address or PDP type.
 * @MM_NETWORK_ERROR_USER_AUTHENTICATION_FAILED: User authentication failed.
 * @MM_NETWORK_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW: Activation rejected by GGSN, Serving GW or PDN GW.
 * @MM_NETWORK_ERROR_SERVICE_OPTION_NOT_SUPPORTED: Service option not supported.
 * @MM_NETWORK_ERROR_REQUESTED_SERVICE_OPTION_NOT_SUBSCRIBED: Requested service option not subscribed.
 * @MM_NETWORK_ERROR_SERVICE_OPTION_TEMPORARILY_OUT_OF_ORDER: Service option temporarily out of order.
 * @MM_NETWORK_ERROR_NO_PDP_CONTEXT_ACTIVATED: No PDP context activated.
 * @MM_NETWORK_ERROR_SEMANTIC_ERROR_IN_THE_TFT_OPERATION: Semantic error in the TFT operation.
 * @MM_NETWORK_ERROR_SYNTACTICAL_ERROR_IN_THE_TFT_OPERATION: Syntactical error in the TFT operation.
 * @MM_NETWORK_ERROR_UNKNOWN_PDP_CONTEXT: Unknown PDP context.
 * @MM_NETWORK_ERROR_SEMANTIC_ERRORS_IN_PACKET_FILTER: Semantic errors in packet filter.
 * @MM_NETWORK_ERROR_SYNTACTICAL_ERRORS_IN_PACKET_FILTER: Syntactical errors in packet filter.
 * @MM_NETWORK_ERROR_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED: PDP context without TFT already activated.
 * @MM_NETWORK_ERROR_REQUEST_REJECTED_OR_BEARER_CONTROL_MODE_VIOLATION: Request rejected, Bearer Control Mode violation.
 * @MM_NETWORK_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED: Last PDN disconnection not allowed.
 * @MM_NETWORK_ERROR_PDP_TYPE_IPV4_ONLY_ALLOWED: PDP type IPv4 only allowed.
 * @MM_NETWORK_ERROR_PDP_TYPE_IPV6_ONLY_ALLOWED: PDP type IPv6 only allowed.
 * @MM_NETWORK_ERROR_NO_NETWORK_SLICES_AVAILABLE: No network slices available.
 * @MM_NETWORK_ERROR_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED: Maximum number of PDP contexts reached.
 * @MM_NETWORK_ERROR_REQUESTED_APN_NOT_SUPPORTED_IN_CURRENT_RAT_AND_PLMN: Requested APN not supported in current RAT and PLMN combination.
 * @MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES_FOR_SPECIFIC_SLICE_AND_DNN: Insufficient resources for specific slice and DNN.
 * @MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES_FOR_SPECIFIC_SLICE: Insufficient resources for specific slice.
 * @MM_NETWORK_ERROR_NGKSI_ALREADY_IN_USE: NgKSI already in use.
 * @MM_NETWORK_ERROR_NON_3GPP_ACCESS_TO_5GCN_NOT_ALLOWED: Non-3GPP access to 5GCN not allowed.
 * @MM_NETWORK_ERROR_SERVING_NETWORK_NOT_AUTHORIZED: Serving network not authorized.
 * @MM_NETWORK_ERROR_TEMPORARILY_NOT_AUTHORIZED_FOR_THIS_SNPN: Temporarily not authorized for this SNPN.
 * @MM_NETWORK_ERROR_PERMANENTLY_NOT_AUTHORIZED_FOR_THIS_SNPN: Permanently not authorized for this SNPN.
 * @MM_NETWORK_ERROR_NOT_AUTHORIZED_FOR_THIS_CAG_OR_AUTHORIZED_FOR_CAG_CELLS_ONLY: Not authorized for this CAG or authorized for CAG cells.
 * @MM_NETWORK_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED: Wireline access area not allowed.
 * @MM_NETWORK_ERROR_PAYLOAD_WAS_NOT_FORWARDED: Payload was not forwarded.
 * @MM_NETWORK_ERROR_DNN_NOT_SUPPORTED_OR_NOT_SUBSCRIBED_IN_THE_SLICE: DNN not supported or not subscribed in the slice.
 * @MM_NETWORK_ERROR_INSUFFICIENT_USER_PLANE_RESOURCES_FOR_THE_PDU_SESSION: Insufficient user-plane resources for the PDU session.
 * @MM_NETWORK_ERROR_SEMANTICALLY_INCORRECT_MESSAGE: Semantically incorrect message.
 * @MM_NETWORK_ERROR_INVALID_MANDATORY_INFORMATION: Invalid mandatory information.
 * @MM_NETWORK_ERROR_MESSAGE_TYPE_NON_EXISTENT_OR_NOT_IMPLEMENTED: Message type non-existent or not implemented.
 * @MM_NETWORK_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE: Message type not compatible with protocol state.
 * @MM_NETWORK_ERROR_INFORMATION_ELEMENT_NON_EXISTENT_OR_NOT_IMPLEMENTED: Information element non-existent or not implemented.
 * @MM_NETWORK_ERROR_CONDITIONAL_IE_ERROR: Conditional IE error.
 * @MM_NETWORK_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE: Message not compatible with protocol state.
 * @MM_NETWORK_ERROR_PROTOCOL_ERROR_UNSPECIFIED: Protocol error, unspecified.
 * @MM_NETWORK_ERROR_APN_RESTRICTION_VALUE_INCOMPATIBLE_WITH_ACTIVE_PDP_CONTEXT: APN restriction value incompatible with active PDP context.
 * @MM_NETWORK_ERROR_MULTIPLE_ACCESSES_TO_A_PDN_CONNECTION_NOT_ALLOWED: Multiple accesses to a PDN connection not allowed.
 *
 * Network errors reported by the network when registration is rejected.
 * Even though some error names make reference to legacy technologies (e.g. GPRS), errors are not
 * technology specific and the same type of errors are reported even for newer
 * technologies like LTE and 5G.
 * Network errors are defined in 3GPP TS 24.008 in sections 10.5.3.6 and
 * 10.5.5.14 (detailed in annex G) and in 3GPP TS 24.301 in section 9.9.3.9.
 *
 * Since: 1.24
 */
typedef enum { /*< underscore_name=mm_network_error >*/
    MM_NETWORK_ERROR_NONE                                                         = 0,
    MM_NETWORK_ERROR_IMSI_UNKNOWN_IN_HLR                                          = 2,
    MM_NETWORK_ERROR_ILLEGAL_MS                                                   = 3,
    MM_NETWORK_ERROR_IMSI_UNKNOWN_IN_VLR                                          = 4,
    MM_NETWORK_ERROR_IMEI_NOT_ACCEPTED                                            = 5,
    MM_NETWORK_ERROR_ILLEGAL_ME                                                   = 6,
    MM_NETWORK_ERROR_GPRS_NOT_ALLOWED                                             = 7,
    MM_NETWORK_ERROR_GPRS_AND_NON_GPRS_NOT_ALLOWED                                = 8,
    MM_NETWORK_ERROR_MS_IDENTITY_NOT_DERIVED_BY_NETWORK                           = 9,
    MM_NETWORK_ERROR_IMPLICITLY_DETACHED                                          = 10,
    MM_NETWORK_ERROR_PLMN_NOT_ALLOWED                                             = 11,
    MM_NETWORK_ERROR_LOCATION_AREA_NOT_ALLOWED                                    = 12,
    MM_NETWORK_ERROR_ROAMING_NOT_ALLOWED_IN_LOCATION_AREA                         = 13,
    MM_NETWORK_ERROR_GPRS_NOT_ALLOWED_IN_PLMN                                     = 14,
    MM_NETWORK_ERROR_NO_CELLS_IN_LOCATION_AREA                                    = 15,
    MM_NETWORK_ERROR_MSC_TEMPORARILY_NOT_REACHABLE                                = 16,
    MM_NETWORK_ERROR_NETWORK_FAILURE                                              = 17,
    MM_NETWORK_ERROR_CS_DOMAIN_NOT_AVAILABLE                                      = 18,
    MM_NETWORK_ERROR_ESM_FAILURE                                                  = 19,
    MM_NETWORK_ERROR_MAC_FAILURE                                                  = 20,
    MM_NETWORK_ERROR_SYNCH_FAILURE                                                = 21,
    MM_NETWORK_ERROR_CONGESTION                                                   = 22,
    MM_NETWORK_ERROR_GSM_AUTHENTICATION_UNACCEPTABLE                              = 23,
    MM_NETWORK_ERROR_NOT_AUTHORIZED_FOR_CSG                                       = 25,
    MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES                                       = 26,
    MM_NETWORK_ERROR_MISSING_OR_UNKNOWN_APN                                       = 27,
    MM_NETWORK_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE                                  = 28,
    MM_NETWORK_ERROR_USER_AUTHENTICATION_FAILED                                   = 29,
    MM_NETWORK_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW                            = 30,
    MM_NETWORK_ERROR_REDIRECTION_TO_5GCN_REQUIRED                                 = 31,
    MM_NETWORK_ERROR_SERVICE_OPTION_NOT_SUPPORTED                                 = 32,
    MM_NETWORK_ERROR_REQUESTED_SERVICE_OPTION_NOT_SUBSCRIBED                      = 33,
    MM_NETWORK_ERROR_SERVICE_OPTION_TEMPORARILY_OUT_OF_ORDER                      = 34,
    MM_NETWORK_ERROR_REQUESTED_SERVICE_OPTION_NOT_AUTHORIZED                      = 35,
    MM_NETWORK_ERROR_CALL_CANNOT_BE_IDENTIFIED                                    = 38,
    MM_NETWORK_ERROR_CS_SERVICE_TEMPORARILY_NOT_AVAILABLE                         = 39,
    MM_NETWORK_ERROR_NO_PDP_CONTEXT_ACTIVATED                                     = 40,
    MM_NETWORK_ERROR_SEMANTIC_ERROR_IN_THE_TFT_OPERATION                          = 41,
    MM_NETWORK_ERROR_SYNTACTICAL_ERROR_IN_THE_TFT_OPERATION                       = 42,
    MM_NETWORK_ERROR_UNKNOWN_PDP_CONTEXT                                          = 43,
    MM_NETWORK_ERROR_SEMANTIC_ERRORS_IN_PACKET_FILTER                             = 44,
    MM_NETWORK_ERROR_SYNTACTICAL_ERRORS_IN_PACKET_FILTER                          = 45,
    MM_NETWORK_ERROR_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED                    = 46,
    MM_NETWORK_ERROR_REQUEST_REJECTED_OR_BEARER_CONTROL_MODE_VIOLATION            = 48,
    MM_NETWORK_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED                           = 49,
    MM_NETWORK_ERROR_PDP_TYPE_IPV4_ONLY_ALLOWED                                   = 50,
    MM_NETWORK_ERROR_PDP_TYPE_IPV6_ONLY_ALLOWED                                   = 51,
    MM_NETWORK_ERROR_NO_NETWORK_SLICES_AVAILABLE                                  = 62,
    MM_NETWORK_ERROR_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED                       = 65,
    MM_NETWORK_ERROR_REQUESTED_APN_NOT_SUPPORTED_IN_CURRENT_RAT_AND_PLMN          = 66,
    MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES_FOR_SPECIFIC_SLICE_AND_DNN            = 67,
    MM_NETWORK_ERROR_INSUFFICIENT_RESOURCES_FOR_SPECIFIC_SLICE                    = 69,
    MM_NETWORK_ERROR_NGKSI_ALREADY_IN_USE                                         = 71,
    MM_NETWORK_ERROR_NON_3GPP_ACCESS_TO_5GCN_NOT_ALLOWED                          = 72,
    MM_NETWORK_ERROR_SERVING_NETWORK_NOT_AUTHORIZED                               = 73,
    MM_NETWORK_ERROR_TEMPORARILY_NOT_AUTHORIZED_FOR_THIS_SNPN                     = 74,
    MM_NETWORK_ERROR_PERMANENTLY_NOT_AUTHORIZED_FOR_THIS_SNPN                     = 75,
    MM_NETWORK_ERROR_NOT_AUTHORIZED_FOR_THIS_CAG_OR_AUTHORIZED_FOR_CAG_CELLS_ONLY = 76,
    MM_NETWORK_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED                             = 77,
    MM_NETWORK_ERROR_PAYLOAD_WAS_NOT_FORWARDED                                    = 90,
    MM_NETWORK_ERROR_DNN_NOT_SUPPORTED_OR_NOT_SUBSCRIBED_IN_THE_SLICE             = 91,
    MM_NETWORK_ERROR_INSUFFICIENT_USER_PLANE_RESOURCES_FOR_THE_PDU_SESSION        = 92,
    MM_NETWORK_ERROR_SEMANTICALLY_INCORRECT_MESSAGE                               = 95,
    MM_NETWORK_ERROR_INVALID_MANDATORY_INFORMATION                                = 96,
    MM_NETWORK_ERROR_MESSAGE_TYPE_NON_EXISTENT_OR_NOT_IMPLEMENTED                 = 97,
    MM_NETWORK_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE              = 98,
    MM_NETWORK_ERROR_INFORMATION_ELEMENT_NON_EXISTENT_OR_NOT_IMPLEMENTED          = 99,
    MM_NETWORK_ERROR_CONDITIONAL_IE_ERROR                                         = 100,
    MM_NETWORK_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE                   = 101,
    MM_NETWORK_ERROR_PROTOCOL_ERROR_UNSPECIFIED                                   = 111,
    MM_NETWORK_ERROR_APN_RESTRICTION_VALUE_INCOMPATIBLE_WITH_ACTIVE_PDP_CONTEXT   = 112,
    MM_NETWORK_ERROR_MULTIPLE_ACCESSES_TO_A_PDN_CONNECTION_NOT_ALLOWED            = 113,
    MM_NETWORK_ERROR_UNKNOWN                                                      = 114,
} MMNetworkError;

#endif /*  _MODEMMANAGER_ENUMS_H_ */
