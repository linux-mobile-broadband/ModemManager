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

/******************************************************************************/

static gchar *
normalize_error_string (const gchar *str)
{
    gchar *buf = NULL;
    guint  i;
    guint  j;

    /* Normalize the error code by stripping whitespace and odd characters */
    buf = g_strdup (str);
    for (i = 0, j = 0; str[i]; i++) {
        if (isalnum (str[i]))
            buf[j++] = tolower (str[i]);
    }
    buf[j] = '\0';

    return buf;
}

/******************************************************************************/
/* Connection errors */

/* Human friendly messages for each error type */
static const gchar *connection_error_messages[] = {
    [MM_CONNECTION_ERROR_UNKNOWN]     = "Unknown",
    [MM_CONNECTION_ERROR_NO_CARRIER]  = "No carrier",
    [MM_CONNECTION_ERROR_NO_DIALTONE] = "No dialtone",
    [MM_CONNECTION_ERROR_BUSY]        = "Busy",
    [MM_CONNECTION_ERROR_NO_ANSWER]   = "No answer",
};

GError *
mm_connection_error_for_code (MMConnectionError code,
                              gpointer          log_object)
{
    if (code < G_N_ELEMENTS (connection_error_messages)) {
        const gchar *error_message;

        error_message = connection_error_messages[code];
        if (error_message)
            return g_error_new_literal (MM_CONNECTION_ERROR, code, error_message);
    }

    /* Not found? Then, default to 'no carrier' */
    mm_obj_dbg (log_object, "unknown connection error: %u", code);
    return g_error_new (MM_CONNECTION_ERROR,
                        MM_CONNECTION_ERROR_NO_CARRIER,
                        "Unknown connection error: %u", code);
}

/******************************************************************************/
/* Mobile equipment errors */

/* Human friendly messages for each error type */
static const gchar *me_error_messages[] = {
    [MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE]                                  = "Phone failure",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION]                                  = "No connection to phone",
    [MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED]                                  = "Phone-adaptor link reserved",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED]                                    = "Operation not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED]                                  = "Operation not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN]                                     = "PH-SIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN]                                    = "PH-FSIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK]                                    = "PH-FSIM PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED]                               = "SIM not inserted",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN]                                        = "SIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK]                                        = "SIM PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE]                                    = "SIM failure",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY]                                       = "SIM busy",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG]                                      = "SIM wrong",
    [MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD]                             = "Incorrect password",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2]                                       = "SIM PIN2 required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2]                                       = "SIM PUK2 required",
    [MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL]                                    = "Memory full",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX]                                  = "Invalid index",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND]                                      = "Not found",
    [MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE]                                 = "Memory failure",
    [MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG]                                  = "Text string too long",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS]                                  = "Invalid characters in text string",
    [MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG]                           = "Dial string too long",
    [MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID]                            = "Invalid characters in dial string",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK]                                     = "No network service",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT]                                = "Network timeout",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED]                            = "Network not allowed - emergency calls only",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN]                                    = "Network personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK]                                    = "Network personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN]                             = "Network subset personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK]                             = "Network subset personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN]                                    = "Service provider personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK]                                    = "Service provider personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN]                                       = "Corporate personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK]                                       = "Corporate personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED]                            = "Hidden key required",
    [MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED]                       = "EAP method not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS]                           = "Incorrect parameters",
    [MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN]                                        = "Unknown error",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_HLR]                       = "IMSI unknown in HLR",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS]                                = "Illegal MS",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_VLR]                       = "IMSI unknown in VLR",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME]                                = "Illegal ME",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED]                       = "GPRS services not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_AND_NON_GPRS_SERVICES_NOT_ALLOWED]         = "GPRS and non-GPRS services not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED]                          = "PLMN not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED]                      = "Location area not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED]                       = "Roaming not allowed in this location area",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_NO_CELLS_IN_LOCATION_AREA]                 = "No cells in location area",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_NETWORK_FAILURE]                           = "Network failure",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONGESTION]                                = "Congestion",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_NOT_AUTHORIZED_FOR_CSG]                    = "Not authorized for CSG",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_INSUFFICIENT_RESOURCES]                    = "Insufficient resources",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_MISSING_OR_UNKNOWN_APN]                    = "Missing or unknown APN",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_ADDRESS_OR_TYPE]               = "Unknown PDP address or type",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_USER_AUTHENTICATION_FAILED]                = "User authentication failed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_BY_GGSN_OR_GW]         = "Activation rejected by GGSN or GW",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_UNSPECIFIED]           = "Activation rejected (unspecified)",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED]              = "Service option not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED]             = "Requested service option not subscribed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER]               = "Service option temporarily out of order",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_FEATURE_NOT_SUPPORTED]                     = "Feature not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERROR_IN_TFT_OPERATION]           = "Semantic error in TFT operation",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_TFT_OPERATION]        = "Syntactical error in TFT operation",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_CONTEXT]                       = "Unknown PDP context",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERRORS_IN_PACKET_FILTER]          = "Semantic error in packet filter",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_PACKET_FILTER]        = "Syntactical error in packet filter",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED] = "PDP context without TFT already activated",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN]                                   = "Unspecified GPRS error",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE]                          = "PDP authentication failure",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS]                      = "Invalid mobile class",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY] = "Last PDN disconnection not allowed (legacy)",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED]        = "Last PDN disconnection not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTICALLY_INCORRECT_MESSAGE]            = "Semantically incorrect message",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_MANDATORY_IE_ERROR]                        = "Mandatory IE error",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_IE_NOT_IMPLEMENTED]                        = "IE not implemented",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONDITIONAL_IE_ERROR]                      = "Conditional IE error",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNSPECIFIED_PROTOCOL_ERROR]                = "Unspecified protocol error",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_OPERATOR_DETERMINED_BARRING]               = "Operator determined barring",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED]    = "Maximum number of PDP contexts reached",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUESTED_APN_NOT_SUPPORTED]               = "Requested APN not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUEST_REJECTED_BCM_VIOLATION]            = "Rejected BCM violation",
};

/* All generic ME errors should be < 255, as those are the only reserved ones in the 3GPP spec */
G_STATIC_ASSERT (G_N_ELEMENTS (me_error_messages) <= 256);

GError *
mm_mobile_equipment_error_for_code (MMMobileEquipmentError code,
                                    gpointer               log_object)
{
    if (code < G_N_ELEMENTS (me_error_messages)) {
        const gchar *error_message;

        error_message = me_error_messages[code];
        if (error_message)
            return g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR, code, error_message);
    }

    /* Not found? Then, default */
    mm_obj_dbg (log_object, "unknown mobile equipment error: %u", code);
    return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
                        "Unknown mobile equipment error: %u", code);
}

/******************************************************************************/
/* Message errors
 *
 * The message errors are all >300, so we define a common offset for all error
 * types.
 */

#define MM_MESSAGE_ERROR_COMMON_OFFSET 300

/* Human friendly messages for each error type */
static const gchar *msg_error_messages[] = {
    [MM_MESSAGE_ERROR_ME_FAILURE - MM_MESSAGE_ERROR_COMMON_OFFSET]             = "ME failure",
    [MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED - MM_MESSAGE_ERROR_COMMON_OFFSET]   = "SMS service reserved",
    [MM_MESSAGE_ERROR_NOT_ALLOWED - MM_MESSAGE_ERROR_COMMON_OFFSET]            = "Operation not allowed",
    [MM_MESSAGE_ERROR_NOT_SUPPORTED - MM_MESSAGE_ERROR_COMMON_OFFSET]          = "Operation not supported",
    [MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER - MM_MESSAGE_ERROR_COMMON_OFFSET]  = "Invalid PDU mode parameter",
    [MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER - MM_MESSAGE_ERROR_COMMON_OFFSET] = "Invalid text mode parameter",
    [MM_MESSAGE_ERROR_SIM_NOT_INSERTED - MM_MESSAGE_ERROR_COMMON_OFFSET]       = "SIM not inserted",
    [MM_MESSAGE_ERROR_SIM_PIN - MM_MESSAGE_ERROR_COMMON_OFFSET]                = "SIM PIN required",
    [MM_MESSAGE_ERROR_PH_SIM_PIN - MM_MESSAGE_ERROR_COMMON_OFFSET]             = "PH-SIM PIN required",
    [MM_MESSAGE_ERROR_SIM_FAILURE - MM_MESSAGE_ERROR_COMMON_OFFSET]            = "SIM failure",
    [MM_MESSAGE_ERROR_SIM_BUSY - MM_MESSAGE_ERROR_COMMON_OFFSET]               = "SIM busy",
    [MM_MESSAGE_ERROR_SIM_WRONG - MM_MESSAGE_ERROR_COMMON_OFFSET]              = "SIM wrong",
    [MM_MESSAGE_ERROR_SIM_PUK - MM_MESSAGE_ERROR_COMMON_OFFSET]                = "SIM PUK required",
    [MM_MESSAGE_ERROR_SIM_PIN2 - MM_MESSAGE_ERROR_COMMON_OFFSET]               = "SIM PIN2 required",
    [MM_MESSAGE_ERROR_SIM_PUK2 - MM_MESSAGE_ERROR_COMMON_OFFSET]               = "SIM PUK2 required",
    [MM_MESSAGE_ERROR_MEMORY_FAILURE - MM_MESSAGE_ERROR_COMMON_OFFSET]         = "Memory failure",
    [MM_MESSAGE_ERROR_INVALID_INDEX - MM_MESSAGE_ERROR_COMMON_OFFSET]          = "Invalid index",
    [MM_MESSAGE_ERROR_MEMORY_FULL - MM_MESSAGE_ERROR_COMMON_OFFSET]            = "Memory full",
    [MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN - MM_MESSAGE_ERROR_COMMON_OFFSET]   = "SMSC address unknown",
    [MM_MESSAGE_ERROR_NO_NETWORK - MM_MESSAGE_ERROR_COMMON_OFFSET]             = "No network",
    [MM_MESSAGE_ERROR_NETWORK_TIMEOUT - MM_MESSAGE_ERROR_COMMON_OFFSET]        = "Network timeout",
    [MM_MESSAGE_ERROR_NO_CNMA_ACK_EXPECTED - MM_MESSAGE_ERROR_COMMON_OFFSET]   = "No CNMA acknowledgement expected",
    [MM_MESSAGE_ERROR_UNKNOWN - MM_MESSAGE_ERROR_COMMON_OFFSET]                = "Unknown",
};

/* All generic message errors should be <= 500 (500-common=200), as those are the only reserved ones in the 3GPP spec */
G_STATIC_ASSERT (G_N_ELEMENTS (msg_error_messages) <= 201);

GError *
mm_message_error_for_code (MMMessageError code,
                           gpointer       log_object)
{
    if ((code >= MM_MESSAGE_ERROR_COMMON_OFFSET) && ((code - MM_MESSAGE_ERROR_COMMON_OFFSET) < G_N_ELEMENTS (msg_error_messages))) {
        const gchar *error_message;

        error_message = msg_error_messages[code - MM_MESSAGE_ERROR_COMMON_OFFSET];
        if (error_message)
            return g_error_new_literal (MM_MESSAGE_ERROR, code, error_message);
    }

    /* Not found? Then, default */
    mm_obj_dbg (log_object, "unknown message error: %u", code);
    return g_error_new (MM_MESSAGE_ERROR,
                        MM_MESSAGE_ERROR_UNKNOWN,
                        "Unknown message error: %u", code);
}

/******************************************************************************/
/* Mobile equipment and message errors as string
 *
 * The strings given here must be all lowercase, and stripped of special chars
 * and whitespaces.
 *
 * Not all errors are included, only the most generic ones.
 */

typedef struct {
    guint        error_code;
    const gchar *error_string;
} MeErrorString;

static const MeErrorString me_error_strings[] = {
    { MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE,        "phonefailure"          },
    { MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION,        "noconnection"          },
    { MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED,        "linkreserved"          },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED,          "operationnotallowed"   },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,        "operationnotsupported" },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED,     "simnotinserted"        },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN,              "simpinrequired"        },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,              "simpukrequired"        },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,          "simfailure"            },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY,             "simbusy"               },
    { MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG,            "simwrong"              },
    { MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD,   "incorrectpassword"     },
    { MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND,            "notfound"              },
    { MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK,           "nonetwork"             },
    { MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,      "timeout"               },
    { MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS, "incorrectparameters"   },
};

GError *
mm_mobile_equipment_error_for_string (const gchar *str,
                                      gpointer     log_object)
{
    g_autofree gchar *buf = NULL;
    guint             i;

    g_assert (str != NULL);

    /* Look for the string */
    buf = normalize_error_string (str);
    for (i = 0; i < G_N_ELEMENTS (me_error_strings); i++) {
        if (g_str_equal (me_error_strings[i].error_string, buf))
            return mm_mobile_equipment_error_for_code ((MMMobileEquipmentError)me_error_strings[i].error_code, log_object);
    }

    /* Not found? then, default */
    mm_obj_dbg (log_object, "unknown mobile equipment error string: '%s' (%s)", str, buf);
    return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,
                        "Unknown mobile equipment error string: %s", str);
}

static const MeErrorString msg_error_strings[] = {
    { MM_MESSAGE_ERROR_ME_FAILURE,             "mefailure"             },
    { MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED,   "smsservicereserved"    },
    { MM_MESSAGE_ERROR_NOT_ALLOWED,            "operationnotallowed"   },
    { MM_MESSAGE_ERROR_NOT_SUPPORTED,          "operationnotsupported" },
    { MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,  "invalidpduparameter"   },
    { MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER, "invalidtextparameter"  },
    { MM_MESSAGE_ERROR_SIM_NOT_INSERTED,       "simnotinserted"        },
    { MM_MESSAGE_ERROR_SIM_PIN,                "simpinrequired"        },
    { MM_MESSAGE_ERROR_SIM_FAILURE,            "simfailure"            },
    { MM_MESSAGE_ERROR_SIM_BUSY,               "simbusy"               },
    { MM_MESSAGE_ERROR_SIM_WRONG,              "simwrong"              },
    { MM_MESSAGE_ERROR_SIM_PUK,                "simpukrequired"        },
    { MM_MESSAGE_ERROR_MEMORY_FAILURE,         "memoryfailure"         },
    { MM_MESSAGE_ERROR_INVALID_INDEX,          "invalidindex"          },
    { MM_MESSAGE_ERROR_MEMORY_FULL,            "memoryfull"            },
    { MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN,   "smscaddressunknown"    },
    { MM_MESSAGE_ERROR_NO_NETWORK,             "nonetwork"             },
    { MM_MESSAGE_ERROR_NETWORK_TIMEOUT,        "networktimeout"        },
};

GError *
mm_message_error_for_string (const gchar *str,
                             gpointer     log_object)
{
    g_autofree gchar *buf = NULL;
    guint             i;

    g_assert (str != NULL);

    /* Look for the string */
    buf = normalize_error_string (str);
    for (i = 0; i < G_N_ELEMENTS (msg_error_strings); i++) {
        if (g_str_equal (msg_error_strings[i].error_string, buf))
            return mm_message_error_for_code ((MMMessageError)msg_error_strings[i].error_code, log_object);
    }

    /* Not found? then, default */
    mm_obj_dbg (log_object, "unknown message error string: '%s' (%s)", str, buf);
    return g_error_new (MM_MESSAGE_ERROR,
                        MM_MESSAGE_ERROR_UNKNOWN,
                        "Unknown message error string: %s", str);
}
