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
#include "mm-enums-types.h"
#include "mm-errors-types.h"
#include "mm-log.h"

/*****************************************************************************/

MMModemLock
mm_modem_lock_from_mbim_pin_type (MbimPinType pin_type)
{
    switch (pin_type) {
    case MBIM_PIN_TYPE_CUSTOM:
        break;
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
    }

    return MM_MODEM_LOCK_UNKNOWN;
}

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

GError *
mm_mobile_equipment_error_from_mbim_nw_error (MbimNwError nw_error)
{
    switch (nw_error) {
    case MBIM_NW_ERROR_IMSI_UNKNOWN_IN_HLR:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_HLR,
                            "IMSI unknown in HLR");
    case MBIM_NW_ERROR_IMSI_UNKNOWN_IN_VLR:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_VLR,
                            "IMSI unknown in VLR");
    case MBIM_NW_ERROR_ILLEGAL_ME:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME,
                            "Illegal ME");
    case MBIM_NW_ERROR_GPRS_NOT_ALLOWED:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED,
                            "GPRS not allowed");
    case MBIM_NW_ERROR_GPRS_AND_NON_GPRS_NOT_ALLOWED:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED,
                            "GPRS and non-GPRS not allowed");
    case MBIM_NW_ERROR_PLMN_NOT_ALLOWED:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED,
                            "PLMN not allowed");
    case MBIM_NW_ERROR_LOCATION_AREA_NOT_ALLOWED:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED,
                            "Location area not allowed");
    case MBIM_NW_ERROR_ROAMING_NOT_ALLOWED_IN_LOCATION_AREA:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED,
                            "Roaming not allowed in location area");
    case MBIM_NW_ERROR_GPRS_NOT_ALLOWED_IN_PLMN:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED,
                            "GPRS not allowed in PLMN");
    case MBIM_NW_ERROR_NO_CELLS_IN_LOCATION_AREA:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_NO_CELLS_IN_LOCATION_AREA,
                            "No cells in location area");
    case MBIM_NW_ERROR_NETWORK_FAILURE:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_NETWORK_FAILURE,
                            "Network failure");
    case MBIM_NW_ERROR_CONGESTION:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONGESTION,
                            "Congestion");
    default:
        return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                            MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN,
                            "Unknown error (%u)",
                            nw_error);
    }
}
