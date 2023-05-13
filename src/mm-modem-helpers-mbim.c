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
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-modem-helpers-mbim.h"
#include "mm-modem-helpers.h"
#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-errors-types.h"
#include "mm-error-helpers.h"
#include "mm-log-object.h"

#include <string.h>

/*****************************************************************************/

MMModemCapability
mm_modem_capability_from_mbim_device_caps (MbimCellularClass  caps_cellular_class,
                                           MbimDataClass      caps_data_class,
                                           const gchar       *caps_custom_data_class)
{
    MMModemCapability mask = 0;

    if (caps_cellular_class & MBIM_CELLULAR_CLASS_GSM)
        mask |= MM_MODEM_CAPABILITY_GSM_UMTS;

#if 0  /* Disable until we add MBIM CDMA support */
    if (caps_cellular_class & MBIM_CELLULAR_CLASS_CDMA)
        mask |= MM_MODEM_CAPABILITY_CDMA_EVDO;
#endif

    if (caps_data_class & MBIM_DATA_CLASS_LTE)
        mask |= MM_MODEM_CAPABILITY_LTE;

    /* e.g. Gosuncn GM800 reports MBIM custom data class "5G/TDS" */
    if ((caps_data_class & MBIM_DATA_CLASS_CUSTOM) && caps_custom_data_class) {
        if (strstr (caps_custom_data_class, "5G"))
            mask |= MM_MODEM_CAPABILITY_5GNR;
    }

    /* Support for devices with Microsoft extensions */
    if (caps_data_class & (MBIM_DATA_CLASS_5G_NSA | MBIM_DATA_CLASS_5G_SA))
        mask |= MM_MODEM_CAPABILITY_5GNR;

    return mask;
}

/*****************************************************************************/

MMModemLock
mm_modem_lock_from_mbim_pin_type (MbimPinType pin_type)
{
    switch (pin_type) {
    case MBIM_PIN_TYPE_PIN1:
        return MM_MODEM_LOCK_SIM_PIN;
    case MBIM_PIN_TYPE_PIN2:
        return MM_MODEM_LOCK_SIM_PIN2;
    case MBIM_PIN_TYPE_DEVICE_SIM_PIN:
        return MM_MODEM_LOCK_PH_SIM_PIN;
    case MBIM_PIN_TYPE_DEVICE_FIRST_SIM_PIN:
        return MM_MODEM_LOCK_PH_FSIM_PIN;
    case MBIM_PIN_TYPE_NETWORK_PIN:
        return MM_MODEM_LOCK_PH_NET_PIN;
    case MBIM_PIN_TYPE_NETWORK_SUBSET_PIN:
        return MM_MODEM_LOCK_PH_NETSUB_PIN;
    case MBIM_PIN_TYPE_SERVICE_PROVIDER_PIN:
        return MM_MODEM_LOCK_PH_SP_PIN;
    case MBIM_PIN_TYPE_CORPORATE_PIN:
        return MM_MODEM_LOCK_PH_CORP_PIN;
    case MBIM_PIN_TYPE_PUK1:
        return MM_MODEM_LOCK_SIM_PUK;
    case MBIM_PIN_TYPE_PUK2:
        return MM_MODEM_LOCK_SIM_PUK2;
    case MBIM_PIN_TYPE_DEVICE_FIRST_SIM_PUK:
        return MM_MODEM_LOCK_PH_FSIM_PUK;
    case MBIM_PIN_TYPE_NETWORK_PUK:
        return MM_MODEM_LOCK_PH_NET_PUK;
    case MBIM_PIN_TYPE_NETWORK_SUBSET_PUK:
        return MM_MODEM_LOCK_PH_NETSUB_PIN;
    case MBIM_PIN_TYPE_SERVICE_PROVIDER_PUK:
        return MM_MODEM_LOCK_PH_SP_PIN;
    case MBIM_PIN_TYPE_CORPORATE_PUK:
        return MM_MODEM_LOCK_PH_CORP_PUK;
    case MBIM_PIN_TYPE_SUBSIDY_PIN:
    case MBIM_PIN_TYPE_ADM:
    case MBIM_PIN_TYPE_NEV:
    case MBIM_PIN_TYPE_UNKNOWN:
    case MBIM_PIN_TYPE_CUSTOM:
    default:
        break;
    }

    return MM_MODEM_LOCK_UNKNOWN;
}

/*****************************************************************************/

MMModem3gppRegistrationState
mm_modem_3gpp_registration_state_from_mbim_register_state (MbimRegisterState state)
{
    switch (state) {
    case MBIM_REGISTER_STATE_DEREGISTERED:
        return MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;
    case MBIM_REGISTER_STATE_SEARCHING:
        return MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;
    case MBIM_REGISTER_STATE_HOME:
        return MM_MODEM_3GPP_REGISTRATION_STATE_HOME;
    case MBIM_REGISTER_STATE_ROAMING:
    case MBIM_REGISTER_STATE_PARTNER:
        return MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING;
    case MBIM_REGISTER_STATE_DENIED:
        return MM_MODEM_3GPP_REGISTRATION_STATE_DENIED;
    case MBIM_REGISTER_STATE_UNKNOWN:
    default:
        return MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

MMModem3gppPacketServiceState
mm_modem_3gpp_packet_service_state_from_mbim_packet_service_state (MbimPacketServiceState state)
{
    switch (state) {
    case MBIM_PACKET_SERVICE_STATE_ATTACHED:
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED;
    case MBIM_PACKET_SERVICE_STATE_ATTACHING:
    case MBIM_PACKET_SERVICE_STATE_DETACHING:
    case MBIM_PACKET_SERVICE_STATE_DETACHED:
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED;
    case MBIM_PACKET_SERVICE_STATE_UNKNOWN:
    default:
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;
    }
}

/*****************************************************************************/

MMModemMode
mm_modem_mode_from_mbim_data_class (MbimDataClass  data_class,
                                    const gchar   *caps_custom_data_class)
{
    MMModemMode mask = MM_MODEM_MODE_NONE;

    /* 3GPP... */
    if (data_class & (MBIM_DATA_CLASS_GPRS |
                      MBIM_DATA_CLASS_EDGE))
        mask |= MM_MODEM_MODE_2G;
    if (data_class & (MBIM_DATA_CLASS_UMTS  |
                      MBIM_DATA_CLASS_HSDPA |
                      MBIM_DATA_CLASS_HSUPA))
        mask |= MM_MODEM_MODE_3G;
    if (data_class & MBIM_DATA_CLASS_LTE)
        mask |= MM_MODEM_MODE_4G;
    if (data_class & (MBIM_DATA_CLASS_5G_NSA |
                      MBIM_DATA_CLASS_5G_SA))
        mask |= MM_MODEM_MODE_5G;
    /* Some modems (e.g. Telit FN990) reports MBIM custom data class "5G/TDS" */
    if ((data_class & MBIM_DATA_CLASS_CUSTOM) && caps_custom_data_class) {
        if (strstr (caps_custom_data_class, "5G"))
            mask |= MM_MODEM_MODE_5G;
    }

    /* 3GPP2... */
    if (data_class & MBIM_DATA_CLASS_1XRTT)
        mask |= MM_MODEM_MODE_2G;
    if (data_class & (MBIM_DATA_CLASS_1XEVDO |
                      MBIM_DATA_CLASS_1XEVDO_REVA |
                      MBIM_DATA_CLASS_1XEVDV |
                      MBIM_DATA_CLASS_3XRTT |
                      MBIM_DATA_CLASS_1XEVDO_REVB))
        mask |= MM_MODEM_MODE_3G;
    if (data_class & MBIM_DATA_CLASS_UMB)
        mask |= MM_MODEM_MODE_4G;

    return mask;
}

MbimDataClass
mm_mbim_data_class_from_modem_mode (MMModemMode modem_mode,
                                    gboolean    is_3gpp,
                                    gboolean    is_cdma)
{
    MbimDataClass mask = 0;

    /* 3GPP... */
    if (is_3gpp) {
        if (modem_mode & MM_MODEM_MODE_2G)
            mask |= (MBIM_DATA_CLASS_GPRS |
                     MBIM_DATA_CLASS_EDGE);
        if (modem_mode & MM_MODEM_MODE_3G)
            mask |= (MBIM_DATA_CLASS_UMTS |
                     MBIM_DATA_CLASS_HSDPA |
                     MBIM_DATA_CLASS_HSUPA);
        if (modem_mode & MM_MODEM_MODE_4G)
            mask |= MBIM_DATA_CLASS_LTE;
        if (modem_mode & MM_MODEM_MODE_5G)
            mask |= (MBIM_DATA_CLASS_5G_NSA |
                     MBIM_DATA_CLASS_5G_SA);
    }

    /* 3GPP2... */
    if (is_cdma) {
        if (modem_mode & MM_MODEM_MODE_2G)
            mask |= MBIM_DATA_CLASS_1XRTT;
        if (modem_mode & MM_MODEM_MODE_3G)
            mask |= (MBIM_DATA_CLASS_1XEVDO |
                     MBIM_DATA_CLASS_1XEVDO_REVA |
                     MBIM_DATA_CLASS_1XEVDV |
                     MBIM_DATA_CLASS_3XRTT |
                     MBIM_DATA_CLASS_1XEVDO_REVB);
        if (modem_mode & MM_MODEM_MODE_4G)
            mask |= MBIM_DATA_CLASS_UMB;
    }

    return mask;
}

MbimDataClass
mm_mbim_data_class_from_mbim_data_class_v3_and_subclass (MbimDataClassV3  data_class_v3,
                                                         MbimDataSubclass data_subclass)
{
    MbimDataClass data_class;

    data_class = data_class_v3 & ~(MBIM_DATA_CLASS_5G_NSA | MBIM_DATA_CLASS_5G_SA);
    if (data_class_v3 & MBIM_DATA_CLASS_V3_5G) {
        if (data_subclass & MBIM_DATA_SUBCLASS_5G_NR)
            data_class |= MBIM_DATA_CLASS_5G_SA;
        else if (data_subclass & (MBIM_DATA_SUBCLASS_5G_ENDC |
                                  MBIM_DATA_SUBCLASS_5G_NEDC |
                                  MBIM_DATA_SUBCLASS_5G_NGENDC))
            data_class |= (MBIM_DATA_CLASS_5G_NSA | MBIM_DATA_CLASS_LTE);
        else if (data_subclass & MBIM_DATA_SUBCLASS_5G_ELTE)
            data_class |= MBIM_DATA_CLASS_LTE;
    }

    return data_class;
}

MMModemAccessTechnology
mm_modem_access_technology_from_mbim_data_class (MbimDataClass data_class)
{
    MMModemAccessTechnology mask = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    if (data_class & MBIM_DATA_CLASS_GPRS)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    if (data_class & MBIM_DATA_CLASS_EDGE)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    if (data_class & MBIM_DATA_CLASS_UMTS)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    if (data_class & MBIM_DATA_CLASS_HSDPA)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
    if (data_class & MBIM_DATA_CLASS_HSUPA)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
    if (data_class & MBIM_DATA_CLASS_LTE)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    if (data_class & MBIM_DATA_CLASS_5G_NSA)
        mask |= (MM_MODEM_ACCESS_TECHNOLOGY_LTE | MM_MODEM_ACCESS_TECHNOLOGY_5GNR);
    if (data_class & MBIM_DATA_CLASS_5G_SA)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_5GNR;

    if (data_class & MBIM_DATA_CLASS_1XRTT)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    if (data_class & MBIM_DATA_CLASS_1XEVDO)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    if (data_class & MBIM_DATA_CLASS_1XEVDO_REVA)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOA;
    if (data_class & MBIM_DATA_CLASS_1XEVDO_REVB)
        mask |= MM_MODEM_ACCESS_TECHNOLOGY_EVDOB;

    /* Skip:
     *  MBIM_DATA_CLASS_1XEVDV
     *  MBIM_DATA_CLASS_3XRTT
     *  MBIM_DATA_CLASS_UMB
     *  MBIM_DATA_CLASS_CUSTOM
     */

    return mask;
}

/*****************************************************************************/

MMModem3gppNetworkAvailability
mm_modem_3gpp_network_availability_from_mbim_provider_state (MbimProviderState state)
{
    /* MbimProviderState is a bitmask!
     *
     * We don't explicitly process MBIM_PROVIDER_STATE_PREFERRED,
     * MBIM_PROVIDER_STATE_PREFERRED_MULTICARRIER or MBIM_PROVIDER_STATE_HOME,
     * so we don't report at MM level the type of operator it is (home,
     * preferred or non-preferred), just its availability.
     */
    if (state & MBIM_PROVIDER_STATE_REGISTERED)
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT;

    if (state & MBIM_PROVIDER_STATE_FORBIDDEN)
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN;

    if (state & MBIM_PROVIDER_STATE_VISIBLE)
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE;

    return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
}

/*****************************************************************************/

GList *
mm_3gpp_network_info_list_from_mbim_providers (const MbimProvider *const *providers, guint n_providers)
{
    GList *info_list = NULL;
    guint i;

    g_return_val_if_fail (providers != NULL, NULL);

    for (i = 0; i < n_providers; i++) {
        MM3gppNetworkInfo *info;

        info = g_new0 (MM3gppNetworkInfo, 1);
        info->status = mm_modem_3gpp_network_availability_from_mbim_provider_state (providers[i]->provider_state);
        info->operator_long = g_strdup (providers[i]->provider_name);
        info->operator_short = g_strdup (providers[i]->provider_name);
        info->operator_code = g_strdup (providers[i]->provider_id);
        info->access_tech = mm_modem_access_technology_from_mbim_data_class (providers[i]->cellular_class);

        info_list = g_list_append (info_list, info);
    }

    return info_list;
}

/*****************************************************************************/

MbimPinType
mbim_pin_type_from_mm_modem_3gpp_facility (MMModem3gppFacility facility)
{
    switch (facility) {
    case MM_MODEM_3GPP_FACILITY_NET_PERS:
        return MBIM_PIN_TYPE_NETWORK_PIN;
    case MM_MODEM_3GPP_FACILITY_NET_SUB_PERS:
        return MBIM_PIN_TYPE_NETWORK_SUBSET_PIN;
    case MM_MODEM_3GPP_FACILITY_PROVIDER_PERS:
        return MBIM_PIN_TYPE_SERVICE_PROVIDER_PIN;
    case MM_MODEM_3GPP_FACILITY_CORP_PERS:
        return MBIM_PIN_TYPE_CORPORATE_PIN;
    case MM_MODEM_3GPP_FACILITY_SIM:
        return MBIM_PIN_TYPE_PIN1;
    case MM_MODEM_3GPP_FACILITY_FIXED_DIALING:
        return MBIM_PIN_TYPE_PIN2;
    case MM_MODEM_3GPP_FACILITY_PH_SIM:
        return MBIM_PIN_TYPE_DEVICE_SIM_PIN;
    case MM_MODEM_3GPP_FACILITY_PH_FSIM:
        return MBIM_PIN_TYPE_DEVICE_FIRST_SIM_PIN;
    case MM_MODEM_3GPP_FACILITY_NONE:
    default:
        return MBIM_PIN_TYPE_UNKNOWN;
    }
}

/*****************************************************************************/

MMModem3gppFacility
mm_modem_3gpp_facility_from_mbim_pin_type (MbimPinType pin_type)
{
    switch (pin_type) {
    case MBIM_PIN_TYPE_PIN1:
    case MBIM_PIN_TYPE_PUK1:
        return MM_MODEM_3GPP_FACILITY_SIM;
    case MBIM_PIN_TYPE_PIN2:
    case MBIM_PIN_TYPE_PUK2:
        return MM_MODEM_3GPP_FACILITY_FIXED_DIALING;
    case MBIM_PIN_TYPE_DEVICE_SIM_PIN:
        return MM_MODEM_3GPP_FACILITY_PH_SIM;
    case MBIM_PIN_TYPE_DEVICE_FIRST_SIM_PIN:
    case MBIM_PIN_TYPE_DEVICE_FIRST_SIM_PUK:
        return MM_MODEM_3GPP_FACILITY_PH_FSIM;
    case MBIM_PIN_TYPE_NETWORK_PIN:
    case MBIM_PIN_TYPE_NETWORK_PUK:
        return MM_MODEM_3GPP_FACILITY_NET_PERS;
    case MBIM_PIN_TYPE_NETWORK_SUBSET_PIN:
    case MBIM_PIN_TYPE_NETWORK_SUBSET_PUK:
        return MM_MODEM_3GPP_FACILITY_NET_SUB_PERS;
    case MBIM_PIN_TYPE_SERVICE_PROVIDER_PIN:
    case MBIM_PIN_TYPE_SERVICE_PROVIDER_PUK:
        return MM_MODEM_3GPP_FACILITY_PROVIDER_PERS;
    case MBIM_PIN_TYPE_CORPORATE_PIN:
    case MBIM_PIN_TYPE_CORPORATE_PUK:
        return MM_MODEM_3GPP_FACILITY_CORP_PERS;
    case MBIM_PIN_TYPE_SUBSIDY_PIN:
    case MBIM_PIN_TYPE_ADM:
    case MBIM_PIN_TYPE_NEV:
    case MBIM_PIN_TYPE_UNKNOWN:
    case MBIM_PIN_TYPE_CUSTOM:
    default:
        return MM_MODEM_3GPP_FACILITY_NONE;
    }
}

/*****************************************************************************/


static const MMMobileEquipmentError mbim_nw_errors[] = {
    [MBIM_NW_ERROR_IMSI_UNKNOWN_IN_HLR] = MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_HSS,
    [MBIM_NW_ERROR_ILLEGAL_MS] = MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_UE,
    [MBIM_NW_ERROR_IMSI_UNKNOWN_IN_VLR] = MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_VLR,
    [MBIM_NW_ERROR_ILLEGAL_ME] = MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_ME,
    [MBIM_NW_ERROR_GPRS_NOT_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED,
    [MBIM_NW_ERROR_GPRS_AND_NON_GPRS_NOT_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_PS_AND_NON_PS_SERVICES_NOT_ALLOWED,
    [MBIM_NW_ERROR_PLMN_NOT_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_PLMN_NOT_ALLOWED,
    [MBIM_NW_ERROR_LOCATION_AREA_NOT_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_AREA_NOT_ALLOWED,
    [MBIM_NW_ERROR_ROAMING_NOT_ALLOWED_IN_LOCATION_AREA] = MM_MOBILE_EQUIPMENT_ERROR_ROAMING_NOT_ALLOWED_IN_AREA,
    [MBIM_NW_ERROR_GPRS_NOT_ALLOWED_IN_PLMN] = MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED_IN_PLMN,
    [MBIM_NW_ERROR_NO_CELLS_IN_LOCATION_AREA] = MM_MOBILE_EQUIPMENT_ERROR_NO_CELLS_IN_AREA,
    [MBIM_NW_ERROR_NETWORK_FAILURE] = MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ATTACH,
    [MBIM_NW_ERROR_CONGESTION] = MM_MOBILE_EQUIPMENT_ERROR_CONGESTION,
    [MBIM_NW_ERROR_GSM_AUTHENTICATION_UNACCEPTABLE] = MM_MOBILE_EQUIPMENT_ERROR_USER_AUTHENTICATION_FAILED,
    [MBIM_NW_ERROR_NOT_AUTHORIZED_FOR_CSG] = MM_MOBILE_EQUIPMENT_ERROR_NOT_AUTHORIZED_FOR_CSG,
    [MBIM_NW_ERROR_INSUFFICIENT_RESOURCES] = MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES,
    [MBIM_NW_ERROR_MISSING_OR_UNKNOWN_APN] = MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_APN,
    [MBIM_NW_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE] = MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE,
    [MBIM_NW_ERROR_USER_AUTHENTICATION_FAILED] = MM_MOBILE_EQUIPMENT_ERROR_USER_AUTHENTICATION_FAILED,
    [MBIM_NW_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW] = MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW,
    [MBIM_NW_ERROR_ACTIVATION_REJECTED_UNSPECIFIED] = MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_UNSPECIFIED,
    [MBIM_NW_ERROR_SERVICE_OPTION_NOT_SUPPORTED] = MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUPPORTED,
    [MBIM_NW_ERROR_REQUESTED_SERVICE_OPTION_NOT_SUBSCRIBED] = MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUBSCRIBED,
    [MBIM_NW_ERROR_SERVICE_OPTION_TEMPORARILY_OUT_OF_ORDER] = MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_OUT_OF_ORDER,
    [MBIM_NW_ERROR_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED] = MM_MOBILE_EQUIPMENT_ERROR_MAXIMUM_NUMBER_OF_BEARERS_REACHED,
    [MBIM_NW_ERROR_REQUESTED_APN_NOT_SUPPORTED_IN_CURRENT_RAT_AND_PLMN] = MM_MOBILE_EQUIPMENT_ERROR_REQUESTED_APN_NOT_SUPPORTED,
    [MBIM_NW_ERROR_SEMANTICALLY_INCORRECT_MESSAGE] = MM_MOBILE_EQUIPMENT_ERROR_SEMANTICALLY_INCORRECT_MESSAGE,
    [MBIM_NW_ERROR_PROTOCOL_ERROR_UNSPECIFIED] = MM_MOBILE_EQUIPMENT_ERROR_UNSPECIFIED_PROTOCOL_ERROR,
    [MBIM_NW_ERROR_IMEI_NOT_ACCEPTED] = MM_MOBILE_EQUIPMENT_ERROR_IMEI_NOT_ACCEPTED,
    [MBIM_NW_ERROR_MS_IDENTITY_NOT_DERIVED_BY_NETWORK] = MM_MOBILE_EQUIPMENT_ERROR_UE_IDENTITY_NOT_DERIVED_FROM_NETWORK,
    [MBIM_NW_ERROR_IMPLICITLY_DETACHED] = MM_MOBILE_EQUIPMENT_ERROR_IMPLICITLY_DETACHED,
    [MBIM_NW_ERROR_MSC_TEMPORARILY_NOT_REACHABLE] = MM_MOBILE_EQUIPMENT_ERROR_MSC_TEMPORARILY_NOT_REACHABLE,
    [MBIM_NW_ERROR_NO_PDP_CONTEXT_ACTIVATED] = MM_MOBILE_EQUIPMENT_ERROR_NO_BEARER_ACTIVATED,
    [MBIM_NW_ERROR_PDP_TYPE_IPV4_ONLY_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_IPV4_ONLY_ALLOWED,
    [MBIM_NW_ERROR_PDP_TYPE_IPV6_ONLY_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_IPV6_ONLY_ALLOWED,
    [MBIM_NW_ERROR_INVALID_MANDATORY_INFORMATION] = MM_MOBILE_EQUIPMENT_ERROR_INVALID_MANDATORY_INFORMATION,
    [MBIM_NW_ERROR_MESSAGE_TYPE_NON_EXISTENT_OR_NOT_IMPLEMENTED] = MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_IMPLEMENTED,
    [MBIM_NW_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE] = MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE,
    [MBIM_NW_ERROR_INFORMATION_ELEMENT_NON_EXISTENT_OR_NOT_IMPLEMENTED] = MM_MOBILE_EQUIPMENT_ERROR_IE_NOT_IMPLEMENTED,
    [MBIM_NW_ERROR_CONDITIONAL_IE_ERROR] = MM_MOBILE_EQUIPMENT_ERROR_CONDITIONAL_IE_ERROR,
    [MBIM_NW_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE] = MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE,
    [MBIM_NW_ERROR_APN_RESTRICTION_VALUE_INCOMPATIBLE_WITH_ACTIVE_PDP_CONTEXT] = MM_MOBILE_EQUIPMENT_ERROR_APN_RESTRICTION_INCOMPATIBLE,
    [MBIM_NW_ERROR_MULTIPLE_ACCESSES_TO_A_PDN_CONNECTION_NOT_ALLOWED] = MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_ACCESS_TO_PDN_CONNECTION_NOT_ALLOWED,
    [MBIM_NW_ERROR_NONE] = MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
    /* known unmapped errors */
    /* MBIM_NW_ERROR_MAC_FAILURE */
    /* MBIM_NW_ERROR_SYNCH_FAILURE */
};

GError *
mm_mobile_equipment_error_from_mbim_nw_error (MbimNwError nw_error,
                                              gpointer    log_object)
{
    const gchar            *msg;

    if (nw_error < G_N_ELEMENTS (mbim_nw_errors)) {
        MMMobileEquipmentError  error_code;

        /* convert to mobile equipment error */
        error_code = mbim_nw_errors[nw_error];
        if (error_code)
            return mm_mobile_equipment_error_for_code (error_code, log_object);

        /* provide a nicer error message on unmapped errors */
        msg = mbim_nw_error_get_string (nw_error);
        if (msg)
            return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                                MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
                                "Unsupported error (%u): %s",
                                nw_error, msg);
    }

    /* fallback */
    return g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
                                "Unknown error");
}

/*****************************************************************************/

MMBearerAllowedAuth
mm_bearer_allowed_auth_from_mbim_auth_protocol (MbimAuthProtocol auth_protocol)
{
    switch (auth_protocol) {
    case MBIM_AUTH_PROTOCOL_NONE:
        return MM_BEARER_ALLOWED_AUTH_NONE;
    case MBIM_AUTH_PROTOCOL_PAP:
        return MM_BEARER_ALLOWED_AUTH_PAP;
    case MBIM_AUTH_PROTOCOL_CHAP:
        return MM_BEARER_ALLOWED_AUTH_CHAP;
    case MBIM_AUTH_PROTOCOL_MSCHAPV2:
        return MM_BEARER_ALLOWED_AUTH_MSCHAPV2;
    default:
        return MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    }
}

MbimAuthProtocol
mm_bearer_allowed_auth_to_mbim_auth_protocol (MMBearerAllowedAuth   bearer_auth,
                                              gpointer              log_object,
                                              GError              **error)
{
    gchar *str;

    /* NOTE: the input is a BITMASK, so we try to find a "best match" */

    if (bearer_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
        mm_obj_dbg (log_object, "using default (CHAP) authentication method");
        return MBIM_AUTH_PROTOCOL_CHAP;
    }
    if (bearer_auth & MM_BEARER_ALLOWED_AUTH_CHAP)
        return MBIM_AUTH_PROTOCOL_CHAP;
    if (bearer_auth & MM_BEARER_ALLOWED_AUTH_PAP)
        return MBIM_AUTH_PROTOCOL_PAP;
    if (bearer_auth & MM_BEARER_ALLOWED_AUTH_MSCHAPV2)
        return MBIM_AUTH_PROTOCOL_MSCHAPV2;
    if (bearer_auth & MM_BEARER_ALLOWED_AUTH_NONE)
        return MBIM_AUTH_PROTOCOL_NONE;

    str = mm_bearer_allowed_auth_build_string_from_mask (bearer_auth);
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_UNSUPPORTED,
                 "Unsupported authentication methods (%s)",
                 str);
    g_free (str);
    return MBIM_AUTH_PROTOCOL_NONE;
}

/*****************************************************************************/

MMBearerApnType
mm_bearer_apn_type_from_mbim_context_type (MbimContextType context_type)
{
    switch (context_type) {
        case MBIM_CONTEXT_TYPE_INTERNET:
            return MM_BEARER_APN_TYPE_DEFAULT;
        case MBIM_CONTEXT_TYPE_VPN:
            return MM_BEARER_APN_TYPE_PRIVATE;
        case MBIM_CONTEXT_TYPE_VOICE:
            return MM_BEARER_APN_TYPE_VOICE;
        case MBIM_CONTEXT_TYPE_VIDEO_SHARE:
            return MM_BEARER_APN_TYPE_VIDEO_SHARE;
        case MBIM_CONTEXT_TYPE_PURCHASE:
            return MM_BEARER_APN_TYPE_PURCHASE;
        case MBIM_CONTEXT_TYPE_IMS:
            return MM_BEARER_APN_TYPE_IMS;
        case MBIM_CONTEXT_TYPE_MMS:
            return MM_BEARER_APN_TYPE_MMS;
        case MBIM_CONTEXT_TYPE_LOCAL:
            return MM_BEARER_APN_TYPE_LOCAL;
        case MBIM_CONTEXT_TYPE_ADMIN:
            return MM_BEARER_APN_TYPE_MANAGEMENT;
        case MBIM_CONTEXT_TYPE_APP:
            return MM_BEARER_APN_TYPE_APP;
        case MBIM_CONTEXT_TYPE_XCAP:
            return MM_BEARER_APN_TYPE_XCAP;
        case MBIM_CONTEXT_TYPE_TETHERING:
            return MM_BEARER_APN_TYPE_TETHERING;
        case MBIM_CONTEXT_TYPE_EMERGENCY_CALLING:
            return MM_BEARER_APN_TYPE_EMERGENCY;
            /* some types unused right now */
        case MBIM_CONTEXT_TYPE_INVALID:
        case MBIM_CONTEXT_TYPE_NONE:
        default:
            return MM_BEARER_APN_TYPE_NONE;
    }
}

MbimContextType
mm_bearer_apn_type_to_mbim_context_type (MMBearerApnType   apn_type,
                                         gboolean          mbim_extensions_supported,
                                         gpointer          log_object,
                                         GError          **error)
{
    g_autofree gchar *str = NULL;

    /* NOTE: the input is a BITMASK, so we try to find a "best match" */

    if (apn_type == MM_BEARER_APN_TYPE_NONE) {
        mm_obj_dbg (log_object, "using default (internet) APN type");
        return MBIM_CONTEXT_TYPE_INTERNET;
    }

    if (apn_type & MM_BEARER_APN_TYPE_DEFAULT)
        return MBIM_CONTEXT_TYPE_INTERNET;
    if (apn_type & MM_BEARER_APN_TYPE_IMS)
        return MBIM_CONTEXT_TYPE_IMS;
    if (apn_type & MM_BEARER_APN_TYPE_MMS)
        return MBIM_CONTEXT_TYPE_MMS;
    if (apn_type & MM_BEARER_APN_TYPE_VOICE)
        return MBIM_CONTEXT_TYPE_VOICE;
    if (apn_type & MM_BEARER_APN_TYPE_PRIVATE)
        return MBIM_CONTEXT_TYPE_VPN;
    if (apn_type & MM_BEARER_APN_TYPE_PURCHASE)
        return MBIM_CONTEXT_TYPE_PURCHASE;
    if (apn_type & MM_BEARER_APN_TYPE_VIDEO_SHARE)
        return MBIM_CONTEXT_TYPE_VIDEO_SHARE;
    if (apn_type & MM_BEARER_APN_TYPE_LOCAL)
        return MBIM_CONTEXT_TYPE_LOCAL;

    if (mbim_extensions_supported) {
        if (apn_type & MM_BEARER_APN_TYPE_MANAGEMENT)
            return MBIM_CONTEXT_TYPE_ADMIN;
        if (apn_type & MM_BEARER_APN_TYPE_APP)
            return MBIM_CONTEXT_TYPE_APP;
        if (apn_type & MM_BEARER_APN_TYPE_XCAP)
            return MBIM_CONTEXT_TYPE_XCAP;
        if (apn_type & MM_BEARER_APN_TYPE_TETHERING)
            return MBIM_CONTEXT_TYPE_TETHERING;
        if (apn_type & MM_BEARER_APN_TYPE_EMERGENCY)
            return MBIM_CONTEXT_TYPE_EMERGENCY_CALLING;
    } else {
        if ((apn_type & MM_BEARER_APN_TYPE_MANAGEMENT) ||
            (apn_type & MM_BEARER_APN_TYPE_APP)        ||
            (apn_type & MM_BEARER_APN_TYPE_XCAP)       ||
            (apn_type & MM_BEARER_APN_TYPE_TETHERING)  ||
            (apn_type & MM_BEARER_APN_TYPE_EMERGENCY)) {
            mm_obj_dbg (log_object,
                        "MS extensions unsupported: "
                        "fallback to using default (internet) APN type");
            return MBIM_CONTEXT_TYPE_INTERNET;
        }
    }

    str = mm_bearer_apn_type_build_string_from_mask (apn_type);
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_UNSUPPORTED,
                 "Unsupported APN types (%s)",
                 str);
    return MBIM_CONTEXT_TYPE_NONE;
}

/*****************************************************************************/

MMBearerIpFamily
mm_bearer_ip_family_from_mbim_context_ip_type (MbimContextIpType ip_type)
{
    switch (ip_type) {
    case MBIM_CONTEXT_IP_TYPE_IPV4:
        return MM_BEARER_IP_FAMILY_IPV4;
    case MBIM_CONTEXT_IP_TYPE_IPV6:
        return MM_BEARER_IP_FAMILY_IPV6;
    case MBIM_CONTEXT_IP_TYPE_IPV4V6:
        return MM_BEARER_IP_FAMILY_IPV4V6;
    case MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6:
        return MM_BEARER_IP_FAMILY_IPV4 | MM_BEARER_IP_FAMILY_IPV6;
    case MBIM_CONTEXT_IP_TYPE_DEFAULT:
    default:
        return MM_BEARER_IP_FAMILY_NONE;
    }
}

MbimContextIpType
mm_bearer_ip_family_to_mbim_context_ip_type (MMBearerIpFamily   ip_family,
                                             GError           **error)
{
    gchar *str;

    /* NOTE: the input is a BITMASK, so we try to find a "best match" */

    switch ((guint)ip_family) {
    case MM_BEARER_IP_FAMILY_IPV4:
        return MBIM_CONTEXT_IP_TYPE_IPV4;
    case MM_BEARER_IP_FAMILY_IPV6:
        return MBIM_CONTEXT_IP_TYPE_IPV6;
    case  MM_BEARER_IP_FAMILY_IPV4V6:
        return MBIM_CONTEXT_IP_TYPE_IPV4V6;
    case (MM_BEARER_IP_FAMILY_IPV4 | MM_BEARER_IP_FAMILY_IPV6):
        return MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6;
    case MM_BEARER_IP_FAMILY_NONE:
    case MM_BEARER_IP_FAMILY_ANY:
        /* A valid default IP family should have been specified */
        g_assert_not_reached ();
    default:
        break;
    }

    str = mm_bearer_ip_family_build_string_from_mask (ip_family);
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_UNSUPPORTED,
                 "Unsupported IP type configuration: '%s'",
                 str);
    g_free (str);
    return MBIM_CONTEXT_IP_TYPE_DEFAULT;
}

/*****************************************************************************/

gboolean
mm_bearer_roaming_allowance_to_mbim_context_roaming_control (MMBearerRoamingAllowance    mask,
                                                             gpointer                    log_object,
                                                             MbimContextRoamingControl  *out_value,
                                                             GError                    **error)
{
    if (mask == MM_BEARER_ROAMING_ALLOWANCE_NONE) {
        mm_obj_dbg (log_object, "using default (all) roaming allowance");
        *out_value = MBIM_CONTEXT_ROAMING_CONTROL_ALLOW_ALL;
    } else if (mask == MM_BEARER_ROAMING_ALLOWANCE_HOME)
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_HOME_ONLY;
    else if (mask == MM_BEARER_ROAMING_ALLOWANCE_PARTNER)
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_PARTNER_ONLY;
    else if (mask == MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER)
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_NON_PARTNER_ONLY;
    else if (mask == (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_PARTNER))
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_HOME_AND_PARTNER;
    else if (mask == (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER))
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_HOME_AND_NON_PARTNER;
    else if (mask == (MM_BEARER_ROAMING_ALLOWANCE_PARTNER | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER))
        *out_value =MBIM_CONTEXT_ROAMING_CONTROL_PARTNER_AND_NON_PARTNER;
    else if (mask == (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_PARTNER | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER))
        *out_value = MBIM_CONTEXT_ROAMING_CONTROL_ALLOW_ALL;
    else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported roaming allowance mask: 0x%x", mask);
        return FALSE;
    }

    return TRUE;
}

MMBearerRoamingAllowance
mm_bearer_roaming_allowance_from_mbim_context_roaming_control (MbimContextRoamingControl   value,
                                                               GError                    **error)
{
    switch (value) {
    case MBIM_CONTEXT_ROAMING_CONTROL_HOME_ONLY:
        return MM_BEARER_ROAMING_ALLOWANCE_HOME;
    case MBIM_CONTEXT_ROAMING_CONTROL_PARTNER_ONLY:
        return MM_BEARER_ROAMING_ALLOWANCE_PARTNER;
    case MBIM_CONTEXT_ROAMING_CONTROL_NON_PARTNER_ONLY:
        return MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER;
    case MBIM_CONTEXT_ROAMING_CONTROL_HOME_AND_PARTNER:
        return (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_PARTNER);
    case MBIM_CONTEXT_ROAMING_CONTROL_HOME_AND_NON_PARTNER:
        return (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER);
    case MBIM_CONTEXT_ROAMING_CONTROL_PARTNER_AND_NON_PARTNER:
        return (MM_BEARER_ROAMING_ALLOWANCE_PARTNER | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER);
    case MBIM_CONTEXT_ROAMING_CONTROL_ALLOW_ALL:
        return (MM_BEARER_ROAMING_ALLOWANCE_HOME | MM_BEARER_ROAMING_ALLOWANCE_PARTNER | MM_BEARER_ROAMING_ALLOWANCE_NON_PARTNER);
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported roaming control value: 0x%x", value);
        return MM_BEARER_ROAMING_ALLOWANCE_NONE;
    }
}

/*****************************************************************************/

gboolean
mm_bearer_access_type_preference_to_mbim_context_media_type (MMBearerAccessTypePreference   value,
                                                             gpointer                       log_object,
                                                             MbimContextMediaType          *out_value,
                                                             GError                       **error)
{
    switch (value) {
    case MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE:
        mm_obj_dbg (log_object, "using default (cellular only) context media type");
        *out_value = MBIM_CONTEXT_MEDIA_TYPE_CELLULAR_ONLY;
        return TRUE;
    case MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_ONLY:
        *out_value = MBIM_CONTEXT_MEDIA_TYPE_CELLULAR_ONLY;
        return TRUE;
    case MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_PREFERRED:
        *out_value = MBIM_CONTEXT_MEDIA_TYPE_ALL;
        return TRUE;
    case MM_BEARER_ACCESS_TYPE_PREFERENCE_NON_3GPP_ONLY:
        *out_value = MBIM_CONTEXT_MEDIA_TYPE_WIFI_ONLY;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported roaming control value: 0x%x", value);
        return FALSE;
    }
}

gboolean
mm_bearer_access_type_preference_from_mbim_context_media_type (MbimContextMediaType           value,
                                                               MMBearerAccessTypePreference  *out_value,
                                                               GError                       **error)
{
    switch (value) {
    case MBIM_CONTEXT_MEDIA_TYPE_CELLULAR_ONLY:
        *out_value =  MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_ONLY;
        return TRUE;
    case MBIM_CONTEXT_MEDIA_TYPE_WIFI_ONLY:
        *out_value = MM_BEARER_ACCESS_TYPE_PREFERENCE_NON_3GPP_ONLY;
        return TRUE;
    case MBIM_CONTEXT_MEDIA_TYPE_ALL:
        *out_value = MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_PREFERRED;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported roaming control value: 0x%x", value);
        return FALSE;
    }
}

/*****************************************************************************/

gboolean
mm_boolean_from_mbim_context_state (MbimContextState   value,
                                    gboolean          *out_value,
                                    GError           **error)
{
    switch (value) {
    case MBIM_CONTEXT_STATE_DISABLED:
        *out_value = FALSE;
        return TRUE;
    case MBIM_CONTEXT_STATE_ENABLED:
        *out_value = TRUE;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported context state value: 0x%x", value);
        return FALSE;
    }

}

MbimContextState
mm_boolean_to_mbim_context_state (gboolean value)
{
    return (value ? MBIM_CONTEXT_STATE_ENABLED: MBIM_CONTEXT_STATE_DISABLED);
}

/*****************************************************************************/

MMBearerProfileSource
mm_bearer_profile_source_from_mbim_context_source (MbimContextSource   value,
                                                   GError            **error)
{
    switch (value) {
    case MBIM_CONTEXT_SOURCE_ADMIN:
        return MM_BEARER_PROFILE_SOURCE_ADMIN;
    case MBIM_CONTEXT_SOURCE_USER:
        return MM_BEARER_PROFILE_SOURCE_USER;
    case MBIM_CONTEXT_SOURCE_OPERATOR:
        return MM_BEARER_PROFILE_SOURCE_OPERATOR;
    case MBIM_CONTEXT_SOURCE_MODEM:
        return MM_BEARER_PROFILE_SOURCE_MODEM;
    case MBIM_CONTEXT_SOURCE_DEVICE:
        return MM_BEARER_PROFILE_SOURCE_DEVICE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported context source value: 0x%x", value);
        return MM_BEARER_PROFILE_SOURCE_UNKNOWN;
    }
}

gboolean
mm_bearer_profile_source_to_mbim_context_source (MMBearerProfileSource   value,
                                                 gpointer                log_object,
                                                 MbimContextSource      *out_value,
                                                 GError                **error)
{
    switch (value) {
    case MM_BEARER_PROFILE_SOURCE_UNKNOWN:
        mm_obj_dbg (log_object, "using default (admin) context source");
        *out_value = MBIM_CONTEXT_SOURCE_ADMIN;
        return TRUE;
    case MM_BEARER_PROFILE_SOURCE_ADMIN:
        *out_value = MBIM_CONTEXT_SOURCE_ADMIN;
        return TRUE;
    case MM_BEARER_PROFILE_SOURCE_USER:
        *out_value = MBIM_CONTEXT_SOURCE_USER;
        return TRUE;
    case MM_BEARER_PROFILE_SOURCE_OPERATOR:
        *out_value = MBIM_CONTEXT_SOURCE_OPERATOR;
        return TRUE;
    case MM_BEARER_PROFILE_SOURCE_MODEM:
        *out_value = MBIM_CONTEXT_SOURCE_MODEM;
        return TRUE;
    case MM_BEARER_PROFILE_SOURCE_DEVICE:
        *out_value = MBIM_CONTEXT_SOURCE_DEVICE;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Unsupported profile source value: 0x%x", value);
        return FALSE;
    }
}

/*****************************************************************************/

/* index in the array is the code point (8 possible values), and the actual
 * value is the lower limit of the error rate range. */
static const gdouble bit_error_rate_ranges[] =   { 0.00, 0.20, 0.40, 0.80, 1.60, 3.20, 6.40, 12.80 };
static const gdouble frame_error_rate_ranges[] = { 0.00, 0.01, 0.10, 0.50, 1.00, 2.00, 4.00,  8.00 };

gboolean
mm_signal_error_rate_percentage_from_coded_value (guint      coded_value,
                                                  gdouble   *out_percentage,
                                                  gboolean   is_gsm,
                                                  GError   **error)
{
    if ((is_gsm && (coded_value >= G_N_ELEMENTS (bit_error_rate_ranges))) ||
        (!is_gsm && (coded_value >= G_N_ELEMENTS (frame_error_rate_ranges)))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "error rate coded value out of range: %u", coded_value);
        return FALSE;
    }

    *out_percentage = (is_gsm ? bit_error_rate_ranges[coded_value] : frame_error_rate_ranges[coded_value]);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_signal_rssi_from_coded_value (guint      coded_value,
                                 gdouble   *out_rssi,
                                 GError   **error)
{
    /* expected values between 0 and 31 */
    if (coded_value > 31) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "rssi coded value out of range: %u", coded_value);
        return FALSE;
    }

    *out_rssi = (gdouble)coded_value - 113;
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_signal_rsrp_from_coded_value (guint     coded_value,
                                 gdouble  *out_rsrp,
                                 GError  **error)
{
    /* expected values between 0 and 126 */
    if (coded_value > 126) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "rsrp coded value out of range: %u", coded_value);
        return FALSE;
    }

    *out_rsrp = (gdouble)coded_value - 156;
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_signal_snr_from_coded_value (guint     coded_value,
                                gdouble  *out_snr,
                                GError  **error)
{
    /* expected values between 0 and 126 */
    if (coded_value > 127) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "snr coded value out of range: %u", coded_value);
        return FALSE;
    }

    *out_snr = ((gdouble)coded_value)/2 - 23;
    return TRUE;
}

/*****************************************************************************/

MMModem3gppMicoMode
mm_modem_3gpp_mico_mode_from_mbim_mico_mode (MbimMicoMode mico_mode)
{
    switch (mico_mode) {
    case MBIM_MICO_MODE_DISABLED:
        return MM_MODEM_3GPP_MICO_MODE_DISABLED;
    case MBIM_MICO_MODE_ENABLED:
        return MM_MODEM_3GPP_MICO_MODE_ENABLED;
    case MBIM_MICO_MODE_UNSUPPORTED:
        return MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED;
    case MBIM_MICO_MODE_DEFAULT:
        /* default expected only in set requests */
    default:
        return MM_MODEM_3GPP_MICO_MODE_UNKNOWN;
    }
}

MbimMicoMode
mm_modem_3gpp_mico_mode_to_mbim_mico_mode (MMModem3gppMicoMode mico_mode)
{
    switch (mico_mode) {
    case MM_MODEM_3GPP_MICO_MODE_DISABLED:
        return MBIM_MICO_MODE_DISABLED;
    case MM_MODEM_3GPP_MICO_MODE_ENABLED:
        return MBIM_MICO_MODE_ENABLED;
    case MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED:
        return MBIM_MICO_MODE_UNSUPPORTED;
    case MM_MODEM_3GPP_MICO_MODE_UNKNOWN:
    default:
        return MBIM_MICO_MODE_DEFAULT;
    }
}

MMModem3gppDrxCycle
mm_modem_3gpp_drx_cycle_from_mbim_drx_cycle (MbimDrxCycle drx_cycle)
{
    switch (drx_cycle) {
    case MBIM_DRX_CYCLE_NOT_SUPPORTED:
        return MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED;
    case MBIM_DRX_CYCLE_32:
        return MM_MODEM_3GPP_DRX_CYCLE_32;
    case MBIM_DRX_CYCLE_64:
        return MM_MODEM_3GPP_DRX_CYCLE_64;
    case MBIM_DRX_CYCLE_128:
        return MM_MODEM_3GPP_DRX_CYCLE_128;
    case MBIM_DRX_CYCLE_256:
        return MM_MODEM_3GPP_DRX_CYCLE_256;
    case MBIM_DRX_CYCLE_NOT_SPECIFIED:
    default:
        return MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN;
    }
}

MbimDrxCycle
mm_modem_3gpp_drx_cycle_to_mbim_drx_cycle (MMModem3gppDrxCycle drx_cycle)
{
    switch (drx_cycle) {
    case MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED:
        return MBIM_DRX_CYCLE_NOT_SUPPORTED;
    case MM_MODEM_3GPP_DRX_CYCLE_32:
        return MBIM_DRX_CYCLE_32;
    case MM_MODEM_3GPP_DRX_CYCLE_64:
        return MBIM_DRX_CYCLE_64;
    case MM_MODEM_3GPP_DRX_CYCLE_128:
        return MBIM_DRX_CYCLE_128;
    case MM_MODEM_3GPP_DRX_CYCLE_256:
        return MBIM_DRX_CYCLE_256;
    case MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN:
    default:
        return MBIM_DRX_CYCLE_NOT_SPECIFIED;
    }
}

/*****************************************************************************/

MMSmsState
mm_sms_state_from_mbim_message_status (MbimSmsStatus status)
{
    switch (status) {
    case MBIM_SMS_STATUS_NEW:
        return MM_SMS_STATE_RECEIVED;
    case MBIM_SMS_STATUS_OLD:
        return MM_SMS_STATE_RECEIVED;
    case MBIM_SMS_STATUS_DRAFT:
        return MM_SMS_STATE_STORED;
    case MBIM_SMS_STATUS_SENT:
        return MM_SMS_STATE_SENT;
    default:
        break;
    }

    return MM_SMS_STATE_UNKNOWN;
}

/*****************************************************************************/

guint
mm_signal_quality_from_mbim_signal_state (guint                 rssi,
                                          MbimRsrpSnrInfoArray *rsrp_snr,
                                          guint32               rsrp_snr_count,
                                          gpointer              log_object)
{
    guint quality;

    /* When MBIMEx is enabled we may get RSSI unset, but per access technology
     * RSRP available. When more than one access technology in use (e.g. 4G+5G in
     * 5G NSA), take the highest RSRP value reported. */
    if (rssi == 99 && rsrp_snr && rsrp_snr_count) {
        guint i;
        gint  max_rsrp = G_MININT;

        for (i = 0; i < rsrp_snr_count; i++) {
            MbimRsrpSnrInfo  *info;

            info = rsrp_snr[i];
            /* scale the value to dBm */
            if (info->rsrp < 127) {
                gint rsrp;

                rsrp = -157 + info->rsrp;
                if (rsrp > max_rsrp)
                    max_rsrp = rsrp;
            }
        }
        quality = MM_RSRP_TO_QUALITY (max_rsrp);
        mm_obj_dbg (log_object, "signal state update: %ddBm --> %u%%", max_rsrp, quality);
    } else {
        /* Normalize the quality. 99 means unknown, we default it to 0 */
        quality = MM_CLAMP_HIGH (rssi == 99 ? 0 : rssi, 31) * 100 / 31;
        mm_obj_dbg (log_object, "signal state update: %u --> %u%%", rssi, quality);
    }

    return quality;
}

static MMSignal **
select_mbim_signal_with_data_class (MbimDataClass   data_class,
                                    MMSignal      **cdma,
                                    MMSignal      **evdo,
                                    MMSignal      **gsm,
                                    MMSignal      **umts,
                                    MMSignal      **lte,
                                    MMSignal      **nr5g)
{
    if (data_class & (MBIM_DATA_CLASS_5G_NSA |
                      MBIM_DATA_CLASS_5G_SA))
        return nr5g;
    if (data_class & (MBIM_DATA_CLASS_LTE))
        return lte;
    if (data_class & (MBIM_DATA_CLASS_UMTS |
                      MBIM_DATA_CLASS_HSDPA |
                      MBIM_DATA_CLASS_HSUPA))
        return umts;
    if (data_class & (MBIM_DATA_CLASS_GPRS |
                      MBIM_DATA_CLASS_EDGE))
        return gsm;
    if (data_class & (MBIM_DATA_CLASS_1XEVDO |
                      MBIM_DATA_CLASS_1XEVDO_REVA |
                      MBIM_DATA_CLASS_1XEVDV |
                      MBIM_DATA_CLASS_3XRTT |
                      MBIM_DATA_CLASS_1XEVDO_REVB))
        return evdo;
    if (data_class & MBIM_DATA_CLASS_1XRTT)
        return cdma;
    return NULL;
}

gboolean
mm_signal_from_mbim_signal_state (MbimDataClass          data_class,
                                  guint                  coded_rssi,
                                  guint                  coded_error_rate,
                                  MbimRsrpSnrInfoArray  *rsrp_snr,
                                  guint32                rsrp_snr_count,
                                  gpointer               log_object,
                                  MMSignal             **out_cdma,
                                  MMSignal             **out_evdo,
                                  MMSignal             **out_gsm,
                                  MMSignal             **out_umts,
                                  MMSignal             **out_lte,
                                  MMSignal             **out_nr5g)
{
    MMSignal **tmp;
    MMSignal **last_updated = NULL;
    guint      n_out_updated = 0;

    if (out_cdma)
        *out_cdma = NULL;
    if (out_evdo)
        *out_evdo = NULL;
    if (out_gsm)
        *out_gsm = NULL;
    if (out_umts)
        *out_umts = NULL;
    if (out_lte)
        *out_lte = NULL;
    if (out_nr5g)
        *out_nr5g = NULL;

    /* When MBIMEx v2.0 is available, we get LTE+5GNR information reported
     * in the RSRP/SNR list of items. */
    if (rsrp_snr && rsrp_snr_count) {
        guint i;

        for (i = 0; i < rsrp_snr_count; i++) {
            MbimRsrpSnrInfo *info;

            info = rsrp_snr[i];

            tmp = select_mbim_signal_with_data_class (info->system_type,
                                                      out_cdma, out_evdo,
                                                      out_gsm, out_umts, out_lte, out_nr5g);
            if (!tmp || ((info->rsrp == 0xFFFFFFFF) && (info->snr == 0xFFFFFFFF)))
                continue;

            last_updated = tmp;
            n_out_updated++;

            *tmp = mm_signal_new ();

            mm_signal_set_rsrp (*tmp, MM_SIGNAL_UNKNOWN);
            if (info->rsrp != 0xFFFFFFFF) {
                g_autoptr(GError) error = NULL;
                gdouble           rsrp;

                if (!mm_signal_rsrp_from_coded_value (info->rsrp, &rsrp, &error))
                    mm_obj_dbg (log_object, "couldn't convert RSRP coded value '%u': %s", info->rsrp, error->message);
                else
                    mm_signal_set_rsrp (*tmp, rsrp);
            }

            mm_signal_set_snr (*tmp, MM_SIGNAL_UNKNOWN);
            if (info->snr != 0xFFFFFFFF) {
                g_autoptr(GError) error = NULL;
                gdouble           snr;

                if (!mm_signal_snr_from_coded_value (info->snr, &snr, &error))
                    mm_obj_dbg (log_object, "couldn't convert SNR coded value '%u': %s", info->snr, error->message);
                else
                    mm_signal_set_snr (*tmp, snr);
            }
        }
    }

    /* The MBIM v1.0 details (RSSI, error rate) will only be set if
     * the target access technology is known without any doubt.
     * E.g. if we are in 5GNSA (4G+5G), we will only set the fields
     * if one of them has valid values. If both have valid values,
     * we'll skip updating RSSI and error rate, as we wouldn't know
     * to which of them applies. */
    if (n_out_updated > 1)
        return TRUE;

    if (n_out_updated == 0) {
        tmp = select_mbim_signal_with_data_class (data_class,
                                                  out_cdma, out_evdo,
                                                  out_gsm, out_umts, out_lte, out_nr5g);
        if (!tmp)
            return FALSE;
        *tmp = mm_signal_new ();
    } else {
        tmp = last_updated;
        g_assert (tmp && *tmp);
    }

    mm_signal_set_error_rate (*tmp, MM_SIGNAL_UNKNOWN);
    if (coded_error_rate != 99) {
        g_autoptr(GError) error = NULL;
        gdouble           error_rate;

        if (!mm_signal_error_rate_percentage_from_coded_value (coded_error_rate,
                                                               &error_rate,
                                                               data_class == (MBIM_DATA_CLASS_GPRS | MBIM_DATA_CLASS_EDGE),
                                                               &error))
            mm_obj_dbg (log_object, "couldn't convert error rate coded value '%u': %s", coded_error_rate, error->message);
        else
            mm_signal_set_error_rate (*tmp, error_rate);
    }

    mm_signal_set_rssi (*tmp, MM_SIGNAL_UNKNOWN);
    if (coded_rssi != 99) {
        g_autoptr(GError) error = NULL;
        gdouble           rssi;

        if (!mm_signal_rssi_from_coded_value (coded_rssi, &rssi, &error))
            mm_obj_dbg (log_object, "couldn't convert RSSI coded value '%u': %s", coded_rssi, error->message);
        else
            mm_signal_set_rssi (*tmp, rssi);
    }

    return TRUE;
}

gboolean
mm_signal_from_atds_signal_response (guint32    rssi,
                                     guint32    rscp,
                                     guint32    ecno,
                                     guint32    rsrq,
                                     guint32    rsrp,
                                     guint32    snr,
                                     MMSignal **out_gsm,
                                     MMSignal **out_umts,
                                     MMSignal **out_lte)
{

    if (rscp <= 96) {
        *out_umts = mm_signal_new ();
        mm_signal_set_rscp (*out_umts, -120.0 + rscp);
    }

    if (ecno <= 49) {
        if (!*out_umts)
            *out_umts = mm_signal_new ();
        mm_signal_set_ecio (*out_umts, -24.0 + ((gdouble) ecno / 2));
    }

    if (rsrq <= 34) {
        *out_lte = mm_signal_new ();
        mm_signal_set_rsrq (*out_lte, -19.5 + ((gdouble) rsrq / 2));
    }

    if (rsrp <= 97) {
        if (!*out_lte)
            *out_lte = mm_signal_new ();
        mm_signal_set_rsrp (*out_lte, -140.0 + rsrp);
    }

    if (snr <= 35) {
        if (!*out_lte)
            *out_lte = mm_signal_new ();
        mm_signal_set_snr (*out_lte, -5.0 + snr);
    }

    /* RSSI may be given for all 2G, 3G or 4G so we detect to which one applies */
    if (rssi <= 31) {
        gdouble value;

        value = -113.0 + (2 * rssi);
        if (*out_lte)
            mm_signal_set_rssi (*out_lte, value);
        else if (*out_umts)
            mm_signal_set_rssi (*out_umts, value);
        else {
            *out_gsm = mm_signal_new ();
            mm_signal_set_rssi (*out_gsm, value);
        }
    }

    if (!out_gsm && !out_umts && !out_lte) {
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

void
mm_rf_info_free (MMRfInfo *rf_data)
{
    g_free (rf_data);
}

void
mm_rfim_info_list_free (GList *rfim_info_list)
{
    g_list_free_full (rfim_info_list, (GDestroyNotify) mm_rf_info_free);
}

GList *
mm_rfim_info_list_from_mbim_intel_rfim_frequency_value_array (MbimIntelRfimFrequencyValueArray *freq_info,
                                                              guint                             freq_count,
                                                              gpointer                          log_object)
{
    GList *info_list = NULL;
    guint i;

    for (i = 0; i < freq_count; i++) {
        MMRfInfo *info;

        /* If Cell info value indicates radio off, then other parameters are invalid.
         * So those data will be ignored. */
        if (freq_info[i]->serving_cell_info == MBIM_INTEL_SERVING_CELL_INFO_RADIO_OFF)
            continue;

        info = g_new0 (MMRfInfo, 1);
        info->serving_cell_type = MM_SERVING_CELL_TYPE_UNKNOWN;
        switch (freq_info[i]->serving_cell_info) {
            case MBIM_INTEL_SERVING_CELL_INFO_PCELL:
                info->serving_cell_type = MM_SERVING_CELL_TYPE_PCELL;
                break;
            case MBIM_INTEL_SERVING_CELL_INFO_SCELL:
                info->serving_cell_type = MM_SERVING_CELL_TYPE_SCELL;
                break;
            case MBIM_INTEL_SERVING_CELL_INFO_PSCELL:
                info->serving_cell_type = MM_SERVING_CELL_TYPE_PSCELL;
                break;
            case MBIM_INTEL_SERVING_CELL_INFO_SSCELL:
                info->serving_cell_type = MM_SERVING_CELL_TYPE_SSCELL;
                break;
            case MBIM_INTEL_SERVING_CELL_INFO_RADIO_OFF:
            default:
                info->serving_cell_type = MM_SERVING_CELL_TYPE_INVALID;
                break;
        }
        info->bandwidth = freq_info[i]->bandwidth;
        info->center_frequency = freq_info[i]->center_frequency;
        info_list = g_list_append (info_list, info);
    }

    return info_list;
}

typedef struct {
    guint8  band;
    gdouble fdl_low;
    guint32 n_offs_dl;
    guint32 range_dl1;
    guint32 range_dl2;
} LteDlRangeData;

static LteDlRangeData lte_dl_range_data [] = {
    { 1,  2110,      0,      0,   599 },
    { 2,  1930,    600,    600,  1199 },
    { 3,  1805,   1200,   1200,  1949 },
    { 4,  2110,   1950,   1950,  2399 },
    { 5,   869,   2400,   2400,  2649 },
    { 6,   875,   2650,   2650,  2749 },
    { 7,  2620,   2750,   2750,  3449 },
    { 8,   925,   3450,   3450,  3799 },
    { 9,  1844.9, 3800,   3800,  4149 },
    { 10, 2110,   4150,   4150,  4749 },
    { 11, 1475.9, 4750,   4750,  4949 },
    { 12,  728,   5000,   5000,  5179 },
    { 13,  746,   5180,   5180,  5279 },
    { 14,  758,   5280,   5280,  5379 },
    { 17,  734,   5730,   5730,  5849 },
    { 18,  860,   5850,   5850,  5999 },
    { 19,  875,   6000,   6000,  6149 },
    { 20,  791,   6150,   6150,  6449 },
    { 21, 1495.9, 6450,   6450,  6599 },
    { 33, 1900,   36000, 36000, 36199 },
    { 34, 2010,   36200, 36200, 36349 },
    { 35, 1850,   36350, 36350, 36949 },
    { 36, 1930,   36950, 36950, 37549 },
    { 37, 1910,   37550, 37550, 37749 },
    { 38, 2570,   37750, 37750, 38249 },
    { 39, 1880,   38250, 38250, 38649 },
    { 40, 2300,   38650, 38650, 39649 },
};

static gint
earfcn_to_band_index (guint32  earfcn,
                      gpointer log_object)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (lte_dl_range_data); i++) {
        if (lte_dl_range_data[i].range_dl1 <= earfcn && lte_dl_range_data[i].range_dl2 >= earfcn) {
            mm_obj_dbg (log_object, "found matching band index %u for earfcn %u", i, earfcn);
            return i;
        }
    }
    mm_obj_dbg (log_object, "earfcn %u not matched to any band index", earfcn);
    return -1;
}

gdouble
mm_earfcn_to_frequency (guint32  earfcn,
                        gpointer log_object)
{
    gint i;

    i = earfcn_to_band_index (earfcn, log_object);
    if (i < 0)
        return 0.0;

    return 1.0e6 * (lte_dl_range_data[i].fdl_low + 0.1 * (earfcn - lte_dl_range_data[i].n_offs_dl));
}

typedef struct {
    guint global_khz;
    guint range_offset;
    guint nrarfcn_offset;
    guint range_first;
    guint range_last;
} NrRangeData ;

static NrRangeData nr_range_data [] = {
    { 5,         0,       0,       0,  599999 },
    { 15,  3000000,  600000,  600000, 2016666 },
    { 60, 24250080, 2016667, 2016667, 3279165 },
};

static gint
nrarfcn_to_range_index (guint32  nrarfcn,
                        gpointer log_object)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (nr_range_data); i++) {
        if (nr_range_data[i].range_first <= nrarfcn &&  nr_range_data[i].range_last >= nrarfcn) {
            mm_obj_dbg (log_object, "found matching range index %u for nrarfcn %u", i, nrarfcn);
            return i;
        }
    }
    mm_obj_dbg (log_object, "nrarfcn %u not matched to any range index", nrarfcn);
    return -1;
}

gdouble
mm_nrarfcn_to_frequency (guint32  nrarfcn,
                         gpointer log_object)
{
    gint i;

    i = nrarfcn_to_range_index (nrarfcn, log_object);
    if (i < 0)
        return 0.0;

    return 1.0e3 * (nr_range_data[i].range_offset + nr_range_data[i].global_khz * (nrarfcn - nr_range_data[i].nrarfcn_offset));
}
