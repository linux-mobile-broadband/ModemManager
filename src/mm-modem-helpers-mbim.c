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
