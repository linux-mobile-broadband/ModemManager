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

#include <string.h>
#include <ctype.h>

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
            ENUM_ENTRY (MM_SERIAL_ERROR_OPEN_FAILED,           "SerialOpenFailed"),
            ENUM_ENTRY (MM_SERIAL_ERROR_SEND_FAILED,           "SerialSendfailed"),
            ENUM_ENTRY (MM_SERIAL_ERROR_RESPONSE_TIMEOUT,      "SerialResponseTimeout"),
            ENUM_ENTRY (MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE, "SerialOpenFailedNoDevice"),
            ENUM_ENTRY (MM_SERIAL_ERROR_FLASH_FAILED,          "SerialFlashFailed"),
            ENUM_ENTRY (MM_SERIAL_ERROR_NOT_OPEN,              "SerialNotOpen"),
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
            ENUM_ENTRY (MM_MODEM_ERROR_AUTHORIZATION_REQUIRED,  "AuthorizationRequired"),
            ENUM_ENTRY (MM_MODEM_ERROR_UNSUPPORTED_CHARSET,     "UnsupportedCharset"),
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

typedef struct {
    int code;
    const char *error;  /* lowercase, and stripped of special chars and whitespace */
    const char *message;
} ErrorTable;

static ErrorTable errors[] = {
    { MM_MOBILE_ERROR_PHONE_FAILURE,             "phonefailure",                              "Phone failure" },
    { MM_MOBILE_ERROR_NO_CONNECTION,             "noconnectiontophone",                       "No connection to phone" },
    { MM_MOBILE_ERROR_LINK_RESERVED,             "phoneadapterlinkreserved",                  "Phone-adaptor link reserved" },
    { MM_MOBILE_ERROR_NOT_ALLOWED,               "operationnotallowed",                       "Operation not allowed" },
    { MM_MOBILE_ERROR_NOT_SUPPORTED,             "operationnotsupported",                     "Operation not supported" },
    { MM_MOBILE_ERROR_PH_SIM_PIN,                "phsimpinrequired",                          "PH-SIM PIN required" },
    { MM_MOBILE_ERROR_PH_FSIM_PIN,               "phfsimpinrequired",                         "PH-FSIM PIN required" },
    { MM_MOBILE_ERROR_PH_FSIM_PUK,               "phfsimpukrequired",                         "PH-FSIM PUK required" },
    { MM_MOBILE_ERROR_SIM_NOT_INSERTED,          "simnotinserted",                            "SIM not inserted" },
    { MM_MOBILE_ERROR_SIM_PIN,                   "simpinrequired",                            "SIM PIN required" },
    { MM_MOBILE_ERROR_SIM_PUK,                   "simpukrequired",                            "SIM PUK required" },
    { MM_MOBILE_ERROR_SIM_FAILURE,               "simfailure",                                "SIM failure" },
    { MM_MOBILE_ERROR_SIM_BUSY,                  "simbusy",                                   "SIM busy" },
    { MM_MOBILE_ERROR_SIM_WRONG,                 "simwrong",                                  "SIM wrong" },
    { MM_MOBILE_ERROR_WRONG_PASSWORD,            "incorrectpassword",                         "Incorrect password" },
    { MM_MOBILE_ERROR_SIM_PIN2,                  "simpin2required",                           "SIM PIN2 required" },
    { MM_MOBILE_ERROR_SIM_PUK2,                  "simpuk2required",                           "SIM PUK2 required" },
    { MM_MOBILE_ERROR_MEMORY_FULL,               "memoryfull",                                "Memory full" },
    { MM_MOBILE_ERROR_INVALID_INDEX,             "invalidindex",                              "Invalid index" },
    { MM_MOBILE_ERROR_NOT_FOUND,                 "notfound",                                  "Not found" },
    { MM_MOBILE_ERROR_MEMORY_FAILURE,            "memoryfailure",                             "Memory failure" },
    { MM_MOBILE_ERROR_TEXT_TOO_LONG,             "textstringtoolong",                         "Text string too long" },
    { MM_MOBILE_ERROR_INVALID_CHARS,             "invalidcharactersintextstring",             "Invalid characters in text string" },
    { MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG,      "dialstringtoolong",                         "Dial string too long" },
    { MM_MOBILE_ERROR_DIAL_STRING_INVALID,       "invalidcharactersindialstring",             "Invalid characters in dial string" },
    { MM_MOBILE_ERROR_NO_NETWORK,                "nonetworkservice",                          "No network service" },
    { MM_MOBILE_ERROR_NETWORK_TIMEOUT,           "networktimeout",                            "Network timeout" },
    { MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED,       "networknotallowedemergencycallsonly",       "Network not allowed - emergency calls only" },
    { MM_MOBILE_ERROR_NETWORK_PIN,               "networkpersonalizationpinrequired",         "Network personalization PIN required" },
    { MM_MOBILE_ERROR_NETWORK_PUK,               "networkpersonalizationpukrequired",         "Network personalization PUK required" },
    { MM_MOBILE_ERROR_NETWORK_SUBSET_PIN,        "networksubsetpersonalizationpinrequired",   "Network subset personalization PIN required" },
    { MM_MOBILE_ERROR_NETWORK_SUBSET_PUK,        "networksubsetpersonalizationpukrequired",   "Network subset personalization PUK required" },
    { MM_MOBILE_ERROR_SERVICE_PIN,               "serviceproviderpersonalizationpinrequired", "Service provider personalization PIN required" },
    { MM_MOBILE_ERROR_SERVICE_PUK,               "serviceproviderpersonalizationpukrequired", "Service provider personalization PUK required" },
    { MM_MOBILE_ERROR_CORP_PIN,                  "corporatepersonalizationpinrequired",       "Corporate personalization PIN required" },
    { MM_MOBILE_ERROR_CORP_PUK,                  "corporatepersonalizationpukrequired",       "Corporate personalization PUK required" },
    { MM_MOBILE_ERROR_HIDDEN_KEY,                "phsimpukrequired",                          "Hidden key required" },
    { MM_MOBILE_ERROR_EAP_NOT_SUPPORTED,         "eapmethodnotsupported",                     "EAP method not supported" },
    { MM_MOBILE_ERROR_INCORRECT_PARAMS,          "incorrectparameters",                       "Incorrect parameters" },
    { MM_MOBILE_ERROR_UNKNOWN,                   "unknownerror",                              "Unknown error" },
    { MM_MOBILE_ERROR_GPRS_ILLEGAL_MS,           "illegalms",                                 "Illegal MS" },
    { MM_MOBILE_ERROR_GPRS_ILLEGAL_ME,           "illegalme",                                 "Illegal ME" },
    { MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED,  "gprsservicesnotallowed",                    "GPRS services not allowed" },
    { MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED,     "plmnnotallowed",                            "PLMN not allowed" },
    { MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED, "locationareanotallowed",                    "Location area not allowed" },
    { MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED,  "roamingnotallowedinthislocationarea",       "Roaming not allowed in this location area" },
    { MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED, "serviceoperationnotsupported",              "Service option not supported" },
    { MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED,       "requestedserviceoptionnotsubscribed",       "Requested service option not subscribed" },
    { MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER,         "serviceoptiontemporarilyoutoforder",        "Service option temporarily out of order" },
    { MM_MOBILE_ERROR_GPRS_UNKNOWN,              "unspecifiedgprserror",                      "Unspecified GPRS error" },
    { MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE,     "pdpauthenticationfailure",                  "PDP authentication failure" },
    { MM_MOBILE_ERROR_GPRS_INVALID_CLASS,        "invalidmobileclass",                        "Invalid mobile class" },
    { -1,                                        NULL,                                        NULL }
};

GError *
mm_mobile_error_for_code (int error_code)
{
    const char *msg = NULL;
    const ErrorTable *ptr = &errors[0];

    while (ptr->code >= 0) {
        if (ptr->code == error_code) {
            msg = ptr->message;
            break;
        }
        ptr++;
    }

    if (!msg) {
        g_warning ("Invalid error code: %d", error_code);
        error_code = MM_MOBILE_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    return g_error_new_literal (MM_MOBILE_ERROR, error_code, msg);
}

#define BUF_SIZE 100

GError *
mm_mobile_error_for_string (const char *str)
{
    int error_code = -1;
    const ErrorTable *ptr = &errors[0];
    char buf[BUF_SIZE + 1];
    const char *msg = NULL, *p = str;
    int i = 0;

    g_return_val_if_fail (str != NULL, NULL);

    /* Normalize the error code by stripping whitespace and odd characters */
    while (*p && i < BUF_SIZE) {
        if (isalnum (*p))
            buf[i++] = tolower (*p);
        p++;
    }
    buf[i] = '\0';

    while (ptr->code >= 0) {
        if (!strcmp (buf, ptr->error)) {
            error_code = ptr->code;
            msg = ptr->message;
            break;
        }
        ptr++;
    }

    if (!msg) {
        g_warning ("Invalid error code: %d", error_code);
        error_code = MM_MOBILE_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    return g_error_new_literal (MM_MOBILE_ERROR, error_code, msg);
}

