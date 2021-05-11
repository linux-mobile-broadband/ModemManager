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
    [MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE]                                     = "Phone failure",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION]                                     = "No connection to phone",
    [MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED]                                     = "Phone-adaptor link reserved",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED]                                       = "Operation not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED]                                     = "Operation not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN]                                        = "PH-SIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN]                                       = "PH-FSIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK]                                       = "PH-FSIM PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED]                                  = "SIM not inserted",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN]                                           = "SIM PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK]                                           = "SIM PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE]                                       = "SIM failure",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY]                                          = "SIM busy",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG]                                         = "SIM wrong",
    [MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD]                                = "Incorrect password",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2]                                          = "SIM PIN2 required",
    [MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2]                                          = "SIM PUK2 required",
    [MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL]                                       = "Memory full",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX]                                     = "Invalid index",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND]                                         = "Not found",
    [MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE]                                    = "Memory failure",
    [MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG]                                     = "Text string too long",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS]                                     = "Invalid characters in text string",
    [MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG]                              = "Dial string too long",
    [MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID]                               = "Invalid characters in dial string",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK]                                        = "No network service",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT]                                   = "Network timeout",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED]                               = "Network not allowed - emergency calls only",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN]                                       = "Network personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK]                                       = "Network personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN]                                = "Network subset personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK]                                = "Network subset personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN]                                       = "Service provider personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK]                                       = "Service provider personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN]                                          = "Corporate personalization PIN required",
    [MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK]                                          = "Corporate personalization PUK required",
    [MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED]                               = "Hidden key required",
    [MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED]                          = "EAP method not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS]                              = "Incorrect parameters",
    [MM_MOBILE_EQUIPMENT_ERROR_COMMAND_DISABLED]                                  = "Command disabled",
    [MM_MOBILE_EQUIPMENT_ERROR_COMMAND_ABORTED]                                   = "Command aborted",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_ATTACHED_RESTRICTED]                           = "Not attached] restricted",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_EMERGENCY_ONLY]                        = "Not allowed] emergency only",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_RESTRICTED]                            = "Not allowed] restricted",
    [MM_MOBILE_EQUIPMENT_ERROR_FIXED_DIAL_NUMBER_ONLY]                            = "Fixed dial number only",
    [MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_OUT_OF_SERVICE]                        = "Temporarily out of service",
    [MM_MOBILE_EQUIPMENT_ERROR_LANGUAGE_OR_ALPHABET_NOT_SUPPORTED]                = "Language or alphabet not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_UNEXPECTED_DATA_VALUE]                             = "Unexpected data value",
    [MM_MOBILE_EQUIPMENT_ERROR_SYSTEM_FAILURE]                                    = "System failure",
    [MM_MOBILE_EQUIPMENT_ERROR_DATA_MISSING]                                      = "Data missing",
    [MM_MOBILE_EQUIPMENT_ERROR_CALL_BARRED]                                       = "Call barred",
    [MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_WAITING_INDICATION_SUBSCRIPTION_FAILURE]   = "Message waiting indication subscription failure",
    [MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN]                                           = "Unknown error",
    [MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_HSS]                               = "IMSI unknown in HLR/HSS",
    [MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_UE]                                        = "Illegal MS/UE",
    [MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_VLR]                               = "IMSI unknown in VLR",
    [MM_MOBILE_EQUIPMENT_ERROR_IMEI_NOT_ACCEPTED]                                 = "IMEI not accepted",
    [MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_ME]                                        = "Illegal ME",
    [MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED]                           = "PS services not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_PS_AND_NON_PS_SERVICES_NOT_ALLOWED]                = "PS and non-PS services not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_UE_IDENTITY_NOT_DERIVED_FROM_NETWORK]              = "UE identity not derived from network",
    [MM_MOBILE_EQUIPMENT_ERROR_IMPLICITLY_DETACHED]                               = "Implicitly detached",
    [MM_MOBILE_EQUIPMENT_ERROR_PLMN_NOT_ALLOWED]                                  = "PLMN not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_AREA_NOT_ALLOWED]                                  = "Location/tracking area not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_ROAMING_NOT_ALLOWED_IN_AREA]                       = "Roaming not allowed in this location/tracking area",
    [MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED_IN_PLMN]                   = "PS services not allowed in PLMN",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_CELLS_IN_AREA]                                  = "No cells in location/tracking area",
    [MM_MOBILE_EQUIPMENT_ERROR_MSC_TEMPORARILY_NOT_REACHABLE]                     = "MSC temporarily not reachable",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ATTACH]                            = "Network failure (attach)",
    [MM_MOBILE_EQUIPMENT_ERROR_CS_DOMAIN_UNAVAILABLE]                             = "CS domain unavailable",
    [MM_MOBILE_EQUIPMENT_ERROR_ESM_FAILURE]                                       = "ESM failure",
    [MM_MOBILE_EQUIPMENT_ERROR_CONGESTION]                                        = "Congestion",
    [MM_MOBILE_EQUIPMENT_ERROR_MBMS_BEARER_CAPABILITIES_INSUFFICIENT_FOR_SERVICE] = "MBMS bearer capabilities insufficient for service",
    [MM_MOBILE_EQUIPMENT_ERROR_NOT_AUTHORIZED_FOR_CSG]                            = "Not authorized for CSG",
    [MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES]                            = "Insufficient resources",
    [MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_APN]                            = "Missing or unknown APN",
    [MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE]                       = "Unknown PDP address or type",
    [MM_MOBILE_EQUIPMENT_ERROR_USER_AUTHENTICATION_FAILED]                        = "User authentication failed",
    [MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW]                 = "Activation rejected by GGSN or GW",
    [MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_UNSPECIFIED]                   = "Activation rejected (unspecified)",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUPPORTED]                      = "Service option not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUBSCRIBED]                     = "Requested service option not subscribed",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_OUT_OF_ORDER]                       = "Service option temporarily out of order",
    [MM_MOBILE_EQUIPMENT_ERROR_NSAPI_OR_PTI_ALREADY_IN_USE]                       = "NSAPI/PTI already in use",
    [MM_MOBILE_EQUIPMENT_ERROR_REGULAR_DEACTIVATION]                              = "Regular deactivation",
    [MM_MOBILE_EQUIPMENT_ERROR_QOS_NOT_ACCEPTED]                                  = "QoS not accepted",
    [MM_MOBILE_EQUIPMENT_ERROR_CALL_CANNOT_BE_IDENTIFIED]                         = "Call cannot be identified",
    [MM_MOBILE_EQUIPMENT_ERROR_CS_SERVICE_TEMPORARILY_UNAVAILABLE]                = "CS service temporarily unavailable",
    [MM_MOBILE_EQUIPMENT_ERROR_FEATURE_NOT_SUPPORTED]                             = "Feature not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_TFT_OPERATION]                   = "Semantic error in TFT operation",
    [MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_TFT_OPERATION]                = "Syntactical error in TFT operation",
    [MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_CONTEXT]                               = "Unknown PDP context",
    [MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERRORS_IN_PACKET_FILTER]                  = "Semantic error in packet filter",
    [MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_PACKET_FILTER]                = "Syntactical error in packet filter",
    [MM_MOBILE_EQUIPMENT_ERROR_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED]         = "PDP context without TFT already activated",
    [MM_MOBILE_EQUIPMENT_ERROR_MULTICAST_GROUP_MEMBERSHIP_TIMEOUT]                = "Multicast group membership timeout",
    [MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN]                                      = "Unspecified GPRS error",
    [MM_MOBILE_EQUIPMENT_ERROR_PDP_AUTH_FAILURE]                                  = "PDP authentication failure",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_MOBILE_CLASS]                              = "Invalid mobile class",
    [MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY]         = "Last PDN disconnection not allowed (legacy)",
    [MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED]                = "Last PDN disconnection not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_SEMANTICALLY_INCORRECT_MESSAGE]                    = "Semantically incorrect message",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_MANDATORY_INFORMATION]                     = "Invalid mandatory information",
    [MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_IMPLEMENTED]                      = "Message type not implemented",
    [MM_MOBILE_EQUIPMENT_ERROR_CONDITIONAL_IE_ERROR]                              = "Conditional IE error",
    [MM_MOBILE_EQUIPMENT_ERROR_UNSPECIFIED_PROTOCOL_ERROR]                        = "Unspecified protocol error",
    [MM_MOBILE_EQUIPMENT_ERROR_OPERATOR_DETERMINED_BARRING]                       = "Operator determined barring",
    [MM_MOBILE_EQUIPMENT_ERROR_MAXIMUM_NUMBER_OF_BEARERS_REACHED]                 = "Maximum number of PDP/bearer contexts reached",
    [MM_MOBILE_EQUIPMENT_ERROR_REQUESTED_APN_NOT_SUPPORTED]                       = "Requested APN not supported",
    [MM_MOBILE_EQUIPMENT_ERROR_REQUEST_REJECTED_BCM_VIOLATION]                    = "Rejected BCM violation",
    [MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_QCI_OR_5QI_VALUE]                      = "Unsupported QCI/5QI value",
    [MM_MOBILE_EQUIPMENT_ERROR_USER_DATA_VIA_CONTROL_PLANE_CONGESTED]             = "User data via control plane congested",
    [MM_MOBILE_EQUIPMENT_ERROR_SMS_PROVIDED_VIA_GPRS_IN_ROUTING_AREA]             = "SMS provided via GPRS in routing area",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_PTI_VALUE]                                 = "Invalid PTI value",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_BEARER_ACTIVATED]                               = "No bearer activated",
    [MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE]        = "Message not compatible with protocol state",
    [MM_MOBILE_EQUIPMENT_ERROR_RECOVERY_ON_TIMER_EXPIRY]                          = "Recovery on timer expiry",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_TRANSACTION_ID_VALUE]                      = "Invalid transaction ID value",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_AUTHORIZED_IN_PLMN]             = "Service option not authorized in PLMN",
    [MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ACTIVATION]                        = "Network failure (activation)",
    [MM_MOBILE_EQUIPMENT_ERROR_REACTIVATION_REQUESTED]                            = "Reactivation requested",
    [MM_MOBILE_EQUIPMENT_ERROR_IPV4_ONLY_ALLOWED]                                 = "IPv4 only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_IPV6_ONLY_ALLOWED]                                 = "IPv6 only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_SINGLE_ADDRESS_BEARERS_ONLY_ALLOWED]               = "Single address bearers only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_COLLISION_WITH_NETWORK_INITIATED_REQUEST]          = "Collision with network initiated request",
    [MM_MOBILE_EQUIPMENT_ERROR_IPV4V6_ONLY_ALLOWED]                               = "IPv4v6 only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_NON_IP_ONLY_ALLOWED]                               = "Non-IP only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_BEARER_HANDLING_UNSUPPORTED]                       = "Bearer handling unsupported",
    [MM_MOBILE_EQUIPMENT_ERROR_APN_RESTRICTION_INCOMPATIBLE]                      = "APN restriction incompatible",
    [MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_ACCESS_TO_PDN_CONNECTION_NOT_ALLOWED]     = "Multiple access to PDN connection not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_ESM_INFORMATION_NOT_RECEIVED]                      = "ESM information not received",
    [MM_MOBILE_EQUIPMENT_ERROR_PDN_CONNECTION_NONEXISTENT]                        = "PDN connection nonexistent",
    [MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_PDN_CONNECTION_SAME_APN_NOT_ALLOWED]      = "Multiple PDN connection to same APN not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_SEVERE_NETWORK_FAILURE]                            = "Severe network failure",
    [MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE_AND_DNN]          = "Insufficient resources for slice and DNN",
    [MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_SSC_MODE]                              = "Unsupported SSC mode",
    [MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE]                  = "Insufficient resources for slice",
    [MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE]   = "Message type not compatible with protocol state",
    [MM_MOBILE_EQUIPMENT_ERROR_IE_NOT_IMPLEMENTED]                                = "IE not implemented",
    [MM_MOBILE_EQUIPMENT_ERROR_N1_MODE_NOT_ALLOWED]                               = "N1 mode not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_RESTRICTED_SERVICE_AREA]                           = "Restricted service area",
    [MM_MOBILE_EQUIPMENT_ERROR_LADN_UNAVAILABLE]                                  = "LADN unavailable",
    [MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_DNN_IN_SLICE]                   = "Missing or unknown DNN in slice",
    [MM_MOBILE_EQUIPMENT_ERROR_NGKSI_ALREADY_IN_USE]                              = "ngKSI already in use",
    [MM_MOBILE_EQUIPMENT_ERROR_PAYLOAD_NOT_FORWARDED]                             = "Payload not forwarded",
    [MM_MOBILE_EQUIPMENT_ERROR_NON_3GPP_ACCESS_TO_5GCN_NOT_ALLOWED]               = "Non-3GPP access to 5GCN not allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_SERVING_NETWORK_NOT_AUTHORIZED]                    = "Serving network not authorized",
    [MM_MOBILE_EQUIPMENT_ERROR_DNN_NOT_SUPPORTED_IN_SLICE]                        = "DNN not supported in slice",
    [MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_USER_PLANE_RESOURCES_FOR_PDU_SESSION] = "Insufficient user plane resources for PDU session",
    [MM_MOBILE_EQUIPMENT_ERROR_OUT_OF_LADN_SERVICE_AREA]                          = "Out of LADN service area",
    [MM_MOBILE_EQUIPMENT_ERROR_PTI_MISMATCH]                                      = "PTI mismatch",
    [MM_MOBILE_EQUIPMENT_ERROR_MAX_DATA_RATE_FOR_USER_PLANE_INTEGRITY_TOO_LOW]    = "Max data rate for user plane integrity too low",
    [MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_QOS_OPERATION]                   = "Semantic error in QoS operation",
    [MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_QOS_OPERATION]                = "Syntactical error in QoS operation",
    [MM_MOBILE_EQUIPMENT_ERROR_INVALID_MAPPED_EPS_BEARER_IDENTITY]                = "Invalid mapped EPS bearer identity",
    [MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_5GCN_REQUIRED]                      = "Redirection to 5GCN required",
    [MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_EPC_REQUIRED]                       = "Redirection to EPC required",
    [MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_UNAUTHORIZED_FOR_SNPN]                 = "Temporarily unauthorized for SNPN",
    [MM_MOBILE_EQUIPMENT_ERROR_PERMANENTLY_UNAUTHORIZED_FOR_SNPN]                 = "Permanently unauthorized for SNPN",
    [MM_MOBILE_EQUIPMENT_ERROR_ETHERNET_ONLY_ALLOWED]                             = "Ethernet only allowed",
    [MM_MOBILE_EQUIPMENT_ERROR_UNAUTHORIZED_FOR_CAG]                              = "Unauthorized for CAG",
    [MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK_SLICES_AVAILABLE]                       = "No network slices available",
    [MM_MOBILE_EQUIPMENT_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED]                  = "Wireline access area not allowed",
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
