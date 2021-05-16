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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-modem-helpers-mbim.h"
#include "mm-modem-helpers.h"
#include "mm-enums-types.h"
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

    if ((caps_data_class & MBIM_DATA_CLASS_CUSTOM) && caps_custom_data_class) {
        /* e.g. Gosuncn GM800 reports MBIM custom data class "5G/TDS" */
        if (strstr (caps_custom_data_class, "5G"))
            mask |= MM_MODEM_CAPABILITY_5GNR;
    }

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
    case MBIM_PIN_TYPE_SUBSIDY_PIN: /* TODO: Update MM lock list? */
        break;
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
    switch (state) {
    case MBIM_PROVIDER_STATE_HOME:
    case MBIM_PROVIDER_STATE_PREFERRED:
    case MBIM_PROVIDER_STATE_VISIBLE:
    case MBIM_PROVIDER_STATE_PREFERRED_MULTICARRIER:
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE;
    case MBIM_PROVIDER_STATE_REGISTERED:
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT;
    case MBIM_PROVIDER_STATE_FORBIDDEN:
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN;
    case MBIM_PROVIDER_STATE_UNKNOWN:
    default:
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
    }
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
    [MBIM_NW_ERROR_UNKNOWN] = MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
    /* known unmapped errors */
    /* MBIM_NW_ERROR_MAC_FAILURE */
    /* MBIM_NW_ERROR_SYNCH_FAILURE */
};

GError *
mm_mobile_equipment_error_from_mbim_nw_error (MbimNwError nw_error,
                                              gpointer    log_object)
{
    MMMobileEquipmentError  error_code;
    const gchar            *msg;

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
        case MBIM_CONTEXT_TYPE_PURCHASE:
            return MM_BEARER_APN_TYPE_MANAGEMENT;
        case MBIM_CONTEXT_TYPE_IMS:
            return MM_BEARER_APN_TYPE_IMS;
        case MBIM_CONTEXT_TYPE_MMS:
            return MM_BEARER_APN_TYPE_MMS;
        case MBIM_CONTEXT_TYPE_INVALID:
        case MBIM_CONTEXT_TYPE_NONE:
        case MBIM_CONTEXT_TYPE_LOCAL:
        case MBIM_CONTEXT_TYPE_VIDEO_SHARE:
            /* some types unused right now */
        default:
            return MM_BEARER_APN_TYPE_NONE;
    }
}

MbimContextType
mm_bearer_apn_type_to_mbim_context_type (MMBearerApnType   apn_type,
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
    if (apn_type &MM_BEARER_APN_TYPE_MANAGEMENT)
        return MBIM_CONTEXT_TYPE_PURCHASE;
    if (apn_type & MM_BEARER_APN_TYPE_VOICE)
        return MBIM_CONTEXT_TYPE_VOICE;
    if (apn_type & MM_BEARER_APN_TYPE_PRIVATE)
        return MBIM_CONTEXT_TYPE_VPN;

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
