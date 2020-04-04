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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include "mm-error-helpers.h"
#include "mm-log.h"

#include <ctype.h>

typedef struct {
    guint code;
    const gchar *error;  /* lowercase, and stripped of special chars and whitespace */
    const gchar *message;
} ErrorTable;

/* --- Connection errors --- */

GError *
mm_connection_error_for_code (MMConnectionError code,
                              gpointer          log_object)
{
    const gchar *msg;

    switch (code) {
    case MM_CONNECTION_ERROR_UNKNOWN:
        msg = "Unknown";
        break;
    case MM_CONNECTION_ERROR_NO_CARRIER:
        msg = "No carrier";
        break;
    case MM_CONNECTION_ERROR_NO_DIALTONE:
        msg = "No dialtone";
        break;
    case MM_CONNECTION_ERROR_BUSY:
        msg = "Busy";
        break;
    case MM_CONNECTION_ERROR_NO_ANSWER:
        msg = "No answer";
        break;

    default:
        mm_obj_dbg (log_object, "invalid connection error code: %u", code);
        /* uhm... make something up (yes, ok, lie!). */
        code = MM_CONNECTION_ERROR_NO_CARRIER;
        msg = "No carrier";
    }

    return g_error_new_literal (MM_CONNECTION_ERROR, code, msg);
}

/* --- Mobile equipment errors --- */

static ErrorTable me_errors[] = {
    { MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE,                                  "phonefailure",                              "Phone failure" },
    { MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION,                                  "noconnectiontophone",                       "No connection to phone" },
    { MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED,                                  "phoneadapterlinkreserved",                  "Phone-adaptor link reserved" },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED,                                    "operationnotallowed",                       "Operation not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,                                  "operationnotsupported",                     "Operation not supported" },
    { MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN,                                     "phsimpinrequired",                          "PH-SIM PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN,                                    "phfsimpinrequired",                         "PH-FSIM PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK,                                    "phfsimpukrequired",                         "PH-FSIM PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,                               "simnotinserted",                            "SIM not inserted" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN,                                        "simpinrequired",                            "SIM PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,                                        "simpukrequired",                            "SIM PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,                                    "simfailure",                                "SIM failure" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY,                                       "simbusy",                                   "SIM busy" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,                                      "simwrong",                                  "SIM wrong" },
    { MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD,                             "incorrectpassword",                         "Incorrect password" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2,                                       "simpin2required",                           "SIM PIN2 required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2,                                       "simpuk2required",                           "SIM PUK2 required" },
    { MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL,                                    "memoryfull",                                "Memory full" },
    { MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX,                                  "invalidindex",                              "Invalid index" },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND,                                      "notfound",                                  "Not found" },
    { MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE,                                 "memoryfailure",                             "Memory failure" },
    { MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG,                                  "textstringtoolong",                         "Text string too long" },
    { MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS,                                  "invalidcharactersintextstring",             "Invalid characters in text string" },
    { MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG,                           "dialstringtoolong",                         "Dial string too long" },
    { MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID,                            "invalidcharactersindialstring",             "Invalid characters in dial string" },
    { MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK,                                     "nonetworkservice",                          "No network service" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,                                "networktimeout",                            "Network timeout" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED,                            "networknotallowedemergencycallsonly",       "Network not allowed - emergency calls only" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN,                                    "networkpersonalizationpinrequired",         "Network personalization PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK,                                    "networkpersonalizationpukrequired",         "Network personalization PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN,                             "networksubsetpersonalizationpinrequired",   "Network subset personalization PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK,                             "networksubsetpersonalizationpukrequired",   "Network subset personalization PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN,                                    "serviceproviderpersonalizationpinrequired", "Service provider personalization PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK,                                    "serviceproviderpersonalizationpukrequired", "Service provider personalization PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN,                                       "corporatepersonalizationpinrequired",       "Corporate personalization PIN required" },
    { MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK,                                       "corporatepersonalizationpukrequired",       "Corporate personalization PUK required" },
    { MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED,                            "hiddenkeyrequired",                         "Hidden key required" },
    { MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED,                       "eapmethodnotsupported",                     "EAP method not supported" },
    { MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS,                           "incorrectparameters",                       "Incorrect parameters" },
    { MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,                                        "unknownerror",                              "Unknown error" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_HLR,                       "imsiunknowninhlr",                          "IMSI unknown in HLR" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS,                                "illegalms",                                 "Illegal MS" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_VLR,                       "imsiunknowninvlr",                          "IMSI unknown in VLR" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME,                                "illegalme",                                 "Illegal ME" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED,                       "gprsservicesnotallowed",                    "GPRS services not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_AND_NON_GPRS_SERVICES_NOT_ALLOWED,         "gprsandnongprsservicesnotallowed",          "GPRS and non-GPRS services not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED,                          "plmnnotallowed",                            "PLMN not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED,                      "locationareanotallowed",                    "Location area not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED,                       "roamingnotallowedinthislocationarea",       "Roaming not allowed in this location area" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_NO_CELLS_IN_LOCATION_AREA,                 "nocellsinlocationarea",                     "No cells in location area" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_NETWORK_FAILURE,                           "networkfailure",                            "Network failure" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONGESTION,                                "congestion",                                "Congestion" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_NOT_AUTHORIZED_FOR_CSG,                    "notauthorizedforcsg",                       "Not authorized for CSG" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_INSUFFICIENT_RESOURCES,                    "insufficientresources",                     "Insufficient resources" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_MISSING_OR_UNKNOWN_APN,                    "missingorunknownapn",                       "Missing or unknown APN" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_ADDRESS_OR_TYPE,               "unknownpdpaddressortype",                   "Unknown PDP address or type" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_USER_AUTHENTICATION_FAILED,                "userauthenticationfailed",                  "User authentication failed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_BY_GGSN_OR_GW,         "activationrejectedbyggsnorgw",              "Activation rejected by GGSN or GW" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_UNSPECIFIED,           "actovationrejectedunspecified",             "Activation rejected (unspecified)" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED,              "serviceoperationnotsupported",              "Service option not supported" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED,             "requestedserviceoptionnotsubscribed",       "Requested service option not subscribed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER,               "serviceoptiontemporarilyoutoforder",        "Service option temporarily out of order" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_FEATURE_NOT_SUPPORTED,                     "featurenotsupported",                       "Feature not supported" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERROR_IN_TFT_OPERATION,           "semanticerrorintftoperation",               "Semantic error in TFT operation" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_TFT_OPERATION,        "syntacticalerrorintftoperation",            "Syntactical error in TFT operation" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_CONTEXT,                       "unknownpdpcontext",                         "Unknown PDP context" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERRORS_IN_PACKET_FILTER,          "semanticerrorsinpacketfilter",              "Semantic error in packet filter" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_PACKET_FILTER,        "syntacticalerrorinpacketfilter",            "Syntactical error in packet filter" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED, "pdpcontextwithouttftalreadyactivated",      "PDP context without TFT already activated" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN,                                   "unspecifiedgprserror",                      "Unspecified GPRS error" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE,                          "pdpauthenticationfailure",                  "PDP authentication failure" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS,                      "invalidmobileclass",                        "Invalid mobile class" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY, "lastpdndisconnectionnotallowedlegacy",      "Last PDN disconnection not allowed (legacy)" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED,        "lastpdndisconnectionnotallowed",            "Last PDN disconnection not allowed" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTICALLY_INCORRECT_MESSAGE,            "semanticallyincorrectmessage",              "Semantically incorrect message" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_MANDATORY_IE_ERROR,                        "mandatoryieerror",                          "Mandatory IE error" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_IE_NOT_IMPLEMENTED,                        "ienotimplemented",                          "IE not implemented" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONDITIONAL_IE_ERROR,                      "conditionalieerror",                        "Conditional IE error" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNSPECIFIED_PROTOCOL_ERROR,                "unspecifiedprotocolerror",                  "Unspecified protocol error" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_OPERATOR_DETERMINED_BARRING,               "operatordeterminedbarring",                 "Operator determined barring" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED,    "maximumnumberofpdpcontextsreached",         "Maximum number of PDP contexts reached" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUESTED_APN_NOT_SUPPORTED,               "requestedapnnotsupported",                  "Requested APN not supported" },
    { MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUEST_REJECTED_BCM_VIOLATION,            "rejectedbcmviolation",                      "Rejected BCM violation" },
};

GError *
mm_mobile_equipment_error_for_code (MMMobileEquipmentError code,
                                    gpointer               log_object)
{
    guint i;

    /* Look for the code */
    for (i = 0; i < G_N_ELEMENTS (me_errors); i++) {
        if (me_errors[i].code == code)
            return g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                        code,
                                        me_errors[i].message);
    }

    /* Not found? Then, default */
    mm_obj_dbg (log_object, "invalid mobile equipment error code: %u", (guint)code);
    return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
                        "Unknown error");
}

GError *
mm_mobile_equipment_error_for_string (const gchar *str,
                                      gpointer     log_object)
{
    MMMobileEquipmentError code = MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN;
    const gchar *msg = NULL;
    gchar *buf;
    guint i;
    guint j;

    g_return_val_if_fail (str != NULL, NULL);

    /* Normalize the error code by stripping whitespace and odd characters */
    buf = g_strdup (str);
    for (i = 0, j = 0; str[i]; i++) {
        if (isalnum (str[i]))
            buf[j++] = tolower (str[i]);
    }
    buf[j] = '\0';

    /* Look for the string */
    for (i = 0; i < G_N_ELEMENTS (me_errors); i++) {
        if (g_str_equal (me_errors[i].error, buf)) {
            code = me_errors[i].code;
            msg = me_errors[i].message;
            break;
        }
    }

    /* Not found? Then, default */
    if (!msg) {
        mm_obj_dbg (log_object, "invalid mobile equipment error string: '%s' (%s)", str, buf);
        code = MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    g_free (buf);
    return g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR, code, msg);
}

/* --- Message errors --- */

static ErrorTable msg_errors[] = {
    { MM_MESSAGE_ERROR_ME_FAILURE,             "mefailure",             "ME failure" },
    { MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED,   "smsservicereserved",    "SMS service reserved" },
    { MM_MESSAGE_ERROR_NOT_ALLOWED,            "operationnotallowed",   "Operation not allowed" },
    { MM_MESSAGE_ERROR_NOT_SUPPORTED,          "operationnotsupported", "Operation not supported" },
    { MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,  "invalidpduparameter",   "Invalid PDU mode parameter" },
    { MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER, "invalidtextparameter",  "Invalid text mode parameter" },
    { MM_MESSAGE_ERROR_SIM_NOT_INSERTED,       "simnotinserted",        "SIM not inserted" },
    { MM_MESSAGE_ERROR_SIM_PIN,                "simpinrequired",        "SIM PIN required" },
    { MM_MESSAGE_ERROR_PH_SIM_PIN,             "phsimpinrequired",      "PH-SIM PIN required" },
    { MM_MESSAGE_ERROR_SIM_FAILURE,            "simfailure",            "SIM failure" },
    { MM_MESSAGE_ERROR_SIM_BUSY,               "simbusy",               "SIM busy" },
    { MM_MESSAGE_ERROR_SIM_WRONG,              "simwrong",              "SIM wrong" },
    { MM_MESSAGE_ERROR_SIM_PUK,                "simpukrequired",        "SIM PUK required" },
    { MM_MESSAGE_ERROR_SIM_PIN2,               "simpin2required",       "SIM PIN2 required" },
    { MM_MESSAGE_ERROR_SIM_PUK2,               "simpuk2required",       "SIM PUK2 required" },
    { MM_MESSAGE_ERROR_MEMORY_FAILURE,         "memoryfailure",         "Memory failure" },
    { MM_MESSAGE_ERROR_INVALID_INDEX,          "invalidindex",          "Invalid index" },
    { MM_MESSAGE_ERROR_MEMORY_FULL,            "memoryfull",            "Memory full" },
    { MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN,   "smscaddressunknown",    "SMSC address unknown" },
    { MM_MESSAGE_ERROR_NO_NETWORK,             "nonetwork",             "No network" },
    { MM_MESSAGE_ERROR_NETWORK_TIMEOUT,        "networktimeout",        "Network timeout" },
    { MM_MESSAGE_ERROR_NO_CNMA_ACK_EXPECTED,   "nocnmaackexpected",     "No CNMA acknowledgement expected" },
    { MM_MESSAGE_ERROR_UNKNOWN,                "unknown",               "Unknown" }
};

GError *
mm_message_error_for_code (MMMessageError code,
                           gpointer       log_object)
{
    guint i;

    /* Look for the code */
    for (i = 0; i < G_N_ELEMENTS (msg_errors); i++) {
        if (msg_errors[i].code == code)
            return g_error_new_literal (MM_MESSAGE_ERROR,
                                        code,
                                        msg_errors[i].message);
    }

    /* Not found? Then, default */
    mm_obj_dbg (log_object, "invalid message error code: %u", (guint)code);
    return g_error_new (MM_MESSAGE_ERROR,
                        MM_MESSAGE_ERROR_UNKNOWN,
                        "Unknown error");
}

GError *
mm_message_error_for_string (const gchar *str,
                             gpointer     log_object)
{
    MMMessageError code = MM_MESSAGE_ERROR_UNKNOWN;
    const gchar *msg = NULL;
    gchar *buf;
    guint i;
    guint j;

    g_return_val_if_fail (str != NULL, NULL);

    /* Normalize the error code by stripping whitespace and odd characters */
    buf = g_strdup (str);
    for (i = 0, j = 0; str[i]; i++) {
        if (isalnum (str[i]))
            buf[j++] = tolower (str[i]);
    }
    buf[j] = '\0';

    /* Look for the string */
    for (i = 0; i < G_N_ELEMENTS (msg_errors); i++) {
        if (g_str_equal (msg_errors[i].error, buf)) {
            code = msg_errors[i].code;
            msg = msg_errors[i].message;
            break;
        }
    }

    /* Not found? Then, default */
    if (!msg) {
        mm_obj_dbg (log_object, "invalid message error string: '%s' (%s)", str, buf);
        code = MM_MESSAGE_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    g_free (buf);
    return g_error_new_literal (MM_MESSAGE_ERROR, code, msg);
}
