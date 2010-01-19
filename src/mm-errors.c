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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include "mm-errors.h"

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GQuark
mm_serial_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("mm_serial_error");

    return ret;
}

GType
mm_serial_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (MM_SERIAL_OPEN_FAILED,      "SerialOpenFailed"),
            ENUM_ENTRY (MM_SERIAL_SEND_FAILED,      "SerialSendfailed"),
            ENUM_ENTRY (MM_SERIAL_RESPONSE_TIMEOUT, "SerialResponseTimeout"),
            ENUM_ENTRY (MM_SERIAL_OPEN_FAILED_NO_DEVICE, "SerialOpenFailedNoDevice"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("MMSerialError", values);
    }

    return etype;
}

GQuark
mm_modem_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("mm_modem_error");

    return ret;
}

GType
mm_modem_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (MM_MODEM_ERROR_GENERAL,                 "General"),
            ENUM_ENTRY (MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED, "OperationNotSupported"),
            ENUM_ENTRY (MM_MODEM_ERROR_CONNECTED,               "Connected"),
            ENUM_ENTRY (MM_MODEM_ERROR_DISCONNECTED,            "Disconnected"),
            ENUM_ENTRY (MM_MODEM_ERROR_OPERATION_IN_PROGRESS,   "OperationInProgress"),
            ENUM_ENTRY (MM_MODEM_ERROR_REMOVED,                 "Removed"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("MMModemError", values);
    }

    return etype;
}

GQuark
mm_modem_connect_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("mm_modem_connect_error");

    return ret;
}

GType
mm_modem_connect_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_CARRIER,  "NoCarrier"),
            ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_DIALTONE, "NoDialtone"),
            ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_BUSY,        "Busy"),
            ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_ANSWER,   "NoAnswer"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("MMModemConnectError", values);
    }

    return etype;
}

GError *
mm_modem_connect_error_for_code (int error_code)
{
    const char *msg;

    switch (error_code) {
    case MM_MODEM_CONNECT_ERROR_NO_CARRIER:
        msg = "No carrier";
        break;
    case MM_MODEM_CONNECT_ERROR_NO_DIALTONE:
        msg = "No dialtone";
        break;
    case MM_MODEM_CONNECT_ERROR_BUSY:
        msg = "Busy";
        break;
    case MM_MODEM_CONNECT_ERROR_NO_ANSWER:
        msg = "No answer";
        break;

    default:
        g_warning ("Invalid error code");
        /* uhm... make something up (yes, ok, lie!). */
        error_code = MM_MODEM_CONNECT_ERROR_NO_CARRIER;
        msg = "No carrier";
    }

    return g_error_new_literal (MM_MODEM_CONNECT_ERROR, error_code, msg);
}


GQuark
mm_mobile_error_quark (void)
{
    static GQuark ret = 0;

    if (ret == 0)
        ret = g_quark_from_static_string ("mm_mobile_error");

    return ret;
}

GType
mm_mobile_error_get_type (void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY (MM_MOBILE_ERROR_PHONE_FAILURE,             "PhoneFailure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NO_CONNECTION,             "NoConnection"),
            ENUM_ENTRY (MM_MOBILE_ERROR_LINK_RESERVED,             "LinkReserved"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_ALLOWED,               "OperationNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_SUPPORTED,             "OperationNotSupported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_SIM_PIN,                "PhSimPinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_FSIM_PIN,               "PhFSimPinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_FSIM_PUK,               "PhFSimPukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_NOT_INSERTED,          "SimNotInserted"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PIN,                   "SimPinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PUK,                   "SimPukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_FAILURE,               "SimFailure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_BUSY,                  "SimBusy"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_WRONG,                 "SimWrong"),
            ENUM_ENTRY (MM_MOBILE_ERROR_WRONG_PASSWORD,            "IncorrectPassword"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PIN2,                  "SimPin2Required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PUK2,                  "SimPuk2Required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_MEMORY_FULL,               "MemoryFull"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INVALID_INDEX,             "InvalidIndex"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_FOUND,                 "NotFound"),
            ENUM_ENTRY (MM_MOBILE_ERROR_MEMORY_FAILURE,            "MemoryFailure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_TEXT_TOO_LONG,             "TextTooLong"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INVALID_CHARS,             "InvalidChars"),
            ENUM_ENTRY (MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG,      "DialStringTooLong"),
            ENUM_ENTRY (MM_MOBILE_ERROR_DIAL_STRING_INVALID,       "InvalidDialString"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NO_NETWORK,                "NoNetwork"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_TIMEOUT,           "NetworkTimeout"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED,       "NetworkNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_PIN,               "NetworkPinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_PUK,               "NetworkPukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_SUBSET_PIN,        "NetworkSubsetPinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_SUBSET_PUK,        "NetworkSubsetPukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SERVICE_PIN,               "ServicePinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SERVICE_PUK,               "ServicePukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_CORP_PIN,                  "CorporatePinRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_CORP_PUK,                  "CorporatePukRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_HIDDEN_KEY,                "HiddenKeyRequired"),
            ENUM_ENTRY (MM_MOBILE_ERROR_EAP_NOT_SUPPORTED,         "EapMethodNotSupported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INCORRECT_PARAMS,          "IncorrectParams"),
            ENUM_ENTRY (MM_MOBILE_ERROR_UNKNOWN,                   "Unknown"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ILLEGAL_MS,           "GprsIllegalMs"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ILLEGAL_ME,           "GprsIllegalMe"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED,  "GprsServiceNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED,     "GprsPlmnNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED, "GprsLocationNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED,  "GprsRoamingNotAllowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED, "GprsOptionNotSupported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED,       "GprsNotSubscribed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER,         "GprsOutOfOrder"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE,     "GprsPdpAuthFailure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_UNKNOWN,              "GprsUnspecified"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_INVALID_CLASS,        "GprsInvalidClass"),
            { 0, 0, 0 }
        };

        etype = g_enum_register_static ("MMMobileError", values);
    }

    return etype;
}

GError *
mm_mobile_error_for_code (int error_code)
{
    const char *msg;

    switch (error_code) {
    case MM_MOBILE_ERROR_PHONE_FAILURE: msg = "Phone failure"; break;
    case MM_MOBILE_ERROR_NO_CONNECTION: msg = "No connection to phone"; break;
    case MM_MOBILE_ERROR_LINK_RESERVED: msg = "Phone-adaptor link reserved"; break;
    case MM_MOBILE_ERROR_NOT_ALLOWED: msg = "Operation not allowed"; break;
    case MM_MOBILE_ERROR_NOT_SUPPORTED: msg = "Operation not supported"; break;
    case MM_MOBILE_ERROR_PH_SIM_PIN: msg = "PH-SIM PIN required"; break;
    case MM_MOBILE_ERROR_PH_FSIM_PIN: msg = "PH-FSIM PIN required"; break;
    case MM_MOBILE_ERROR_PH_FSIM_PUK: msg = "PH-FSIM PUK required"; break;
    case MM_MOBILE_ERROR_SIM_NOT_INSERTED: msg = "SIM not inserted"; break;
    case MM_MOBILE_ERROR_SIM_PIN: msg = "SIM PIN required"; break;
    case MM_MOBILE_ERROR_SIM_PUK: msg = "SIM PUK required"; break;
    case MM_MOBILE_ERROR_SIM_FAILURE: msg = "SIM failure"; break;
    case MM_MOBILE_ERROR_SIM_BUSY: msg = "SIM busy"; break;
    case MM_MOBILE_ERROR_SIM_WRONG: msg = "SIM wrong"; break;
    case MM_MOBILE_ERROR_WRONG_PASSWORD: msg = "Incorrect password"; break;
    case MM_MOBILE_ERROR_SIM_PIN2: msg = "SIM PIN2 required"; break;
    case MM_MOBILE_ERROR_SIM_PUK2: msg = "SIM PUK2 required"; break;
    case MM_MOBILE_ERROR_MEMORY_FULL: msg = "Memory full"; break;
    case MM_MOBILE_ERROR_INVALID_INDEX: msg = "Invalid index"; break;
    case MM_MOBILE_ERROR_NOT_FOUND: msg = "Not found"; break;
    case MM_MOBILE_ERROR_MEMORY_FAILURE: msg = "Memory failure"; break;
    case MM_MOBILE_ERROR_TEXT_TOO_LONG: msg = "Text string too long"; break;
    case MM_MOBILE_ERROR_INVALID_CHARS: msg = "Invalid characters in text string"; break;
    case MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG: msg = "Dial string too long"; break;
    case MM_MOBILE_ERROR_DIAL_STRING_INVALID: msg = "Invalid characters in dial string"; break;
    case MM_MOBILE_ERROR_NO_NETWORK: msg = "No network service"; break;
    case MM_MOBILE_ERROR_NETWORK_TIMEOUT: msg = "Network timeout"; break;
    case MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED: msg = "Network not allowed - emergency calls only"; break;
    case MM_MOBILE_ERROR_NETWORK_PIN: msg = "Network personalization PIN required"; break;
    case MM_MOBILE_ERROR_NETWORK_PUK: msg = "Network personalization PUK required"; break;
    case MM_MOBILE_ERROR_NETWORK_SUBSET_PIN: msg = "Network subset personalization PIN required"; break;
    case MM_MOBILE_ERROR_NETWORK_SUBSET_PUK: msg = "Network subset personalization PUK required"; break;
    case MM_MOBILE_ERROR_SERVICE_PIN: msg = "Service provider personalization PIN required"; break;
    case MM_MOBILE_ERROR_SERVICE_PUK: msg = "Service provider personalization PUK required"; break;
    case MM_MOBILE_ERROR_CORP_PIN: msg = "Corporate personalization PIN required"; break;
    case MM_MOBILE_ERROR_CORP_PUK: msg = "Corporate personalization PUK required"; break;
    case MM_MOBILE_ERROR_HIDDEN_KEY: msg = "Hidden key required"; break;
    case MM_MOBILE_ERROR_EAP_NOT_SUPPORTED: msg = "EAP method not supported"; break;
    case MM_MOBILE_ERROR_INCORRECT_PARAMS: msg = "Incorrect parameters"; break;
    case MM_MOBILE_ERROR_UNKNOWN: msg = "Unknown error"; break;
    case MM_MOBILE_ERROR_GPRS_ILLEGAL_MS: msg = "Illegal MS"; break;
    case MM_MOBILE_ERROR_GPRS_ILLEGAL_ME: msg = "Illegal ME"; break;
    case MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED: msg = "GPRS services not allowed"; break;
    case MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED: msg = "PLMN not allowed"; break;
    case MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED: msg = "Location area not allowed"; break;
    case MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED: msg = "Roaming not allowed in this location area"; break;
    case MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED: msg = "Service option not supported"; break;
    case MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED: msg = "Requested service option not subscribed"; break;
    case MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER: msg = "Service option temporarily out of order"; break;
    case MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE: msg = "PDP authentication failure"; break;
    case MM_MOBILE_ERROR_GPRS_UNKNOWN: msg = "Unspecified GPRS error"; break;
    case MM_MOBILE_ERROR_GPRS_INVALID_CLASS: msg = "Invalid mobile class"; break;
    default:
        g_warning ("Invalid error code");
        error_code = MM_MOBILE_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    return g_error_new_literal (MM_MOBILE_ERROR, error_code, msg);
}
