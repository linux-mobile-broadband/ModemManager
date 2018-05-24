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
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef _MODEMMANAGER_ERRORS_H_
#define _MODEMMANAGER_ERRORS_H_

#if !defined (__MODEM_MANAGER_H_INSIDE__)
#error "Only <ModemManager.h> can be included directly."
#endif

#include <ModemManager-names.h>

/**
 * SECTION:mm-errors
 * @short_description: Common errors in the API.
 *
 * This section defines errors that may be reported when using methods from the
 * ModemManager interface.
 **/

#define MM_CORE_ERROR_DBUS_PREFIX             MM_DBUS_ERROR_PREFIX ".Core"
#define MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".MobileEquipment"
#define MM_CONNECTION_ERROR_DBUS_PREFIX       MM_DBUS_ERROR_PREFIX ".Connection"
#define MM_SERIAL_ERROR_DBUS_PREFIX           MM_DBUS_ERROR_PREFIX ".Serial"
#define MM_MESSAGE_ERROR_DBUS_PREFIX          MM_DBUS_ERROR_PREFIX ".Message"
#define MM_CDMA_ACTIVATION_ERROR_DBUS_PREFIX  MM_DBUS_ERROR_PREFIX ".CdmaActivation"

/**
 * MMCoreError:
 * @MM_CORE_ERROR_FAILED: Operation failed.
 * @MM_CORE_ERROR_CANCELLED: Operation was cancelled.
 * @MM_CORE_ERROR_ABORTED: Operation was aborted.
 * @MM_CORE_ERROR_UNSUPPORTED: Operation is not supported.
 * @MM_CORE_ERROR_NO_PLUGINS: Cannot operate without valid plugins.
 * @MM_CORE_ERROR_UNAUTHORIZED: Authorization is required to perform the operation.
 * @MM_CORE_ERROR_INVALID_ARGS: Invalid arguments given.
 * @MM_CORE_ERROR_IN_PROGRESS: Operation is already in progress.
 * @MM_CORE_ERROR_WRONG_STATE: Operation cannot be executed in the current state.
 * @MM_CORE_ERROR_CONNECTED: Operation cannot be executed while being connected.
 * @MM_CORE_ERROR_TOO_MANY: Too many items.
 * @MM_CORE_ERROR_NOT_FOUND: Item not found.
 * @MM_CORE_ERROR_RETRY: Operation cannot yet be performed, retry later.
 * @MM_CORE_ERROR_EXISTS: Item already exists.
 *
 * Common errors that may be reported by ModemManager.
 */
typedef enum { /*< underscore_name=mm_core_error >*/
    MM_CORE_ERROR_FAILED       = 0,  /*< nick=Failed       >*/
    MM_CORE_ERROR_CANCELLED    = 1,  /*< nick=Cancelled    >*/
    MM_CORE_ERROR_ABORTED      = 2,  /*< nick=Aborted      >*/
    MM_CORE_ERROR_UNSUPPORTED  = 3,  /*< nick=Unsupported  >*/
    MM_CORE_ERROR_NO_PLUGINS   = 4,  /*< nick=NoPlugins    >*/
    MM_CORE_ERROR_UNAUTHORIZED = 5,  /*< nick=Unauthorized >*/
    MM_CORE_ERROR_INVALID_ARGS = 6,  /*< nick=InvalidArgs  >*/
    MM_CORE_ERROR_IN_PROGRESS  = 7,  /*< nick=InProgress   >*/
    MM_CORE_ERROR_WRONG_STATE  = 8,  /*< nick=WrongState   >*/
    MM_CORE_ERROR_CONNECTED    = 9,  /*< nick=Connected    >*/
    MM_CORE_ERROR_TOO_MANY     = 10, /*< nick=TooMany      >*/
    MM_CORE_ERROR_NOT_FOUND    = 11, /*< nick=NotFound     >*/
    MM_CORE_ERROR_RETRY        = 12, /*< nick=Retry        >*/
    MM_CORE_ERROR_EXISTS       = 13, /*< nick=Exists       >*/
} MMCoreError;

/**
 * MMMobileEquipmentError:
 * @MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE: Phone failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION: No connection to phone.
 * @MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED: Phone-adaptor link reserved.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED: Operation not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED: Operation not supported.
 * @MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN: PH-SIM PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN: PH-FSIM PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK: PH-FSIM PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED: SIM not inserted.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN: SIM PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK: SIM PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE: SIM failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY: SIM busy.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG: SIM wrong.
 * @MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD: Incorrect password.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2: SIM PIN2 required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2: SIM PUK2 required.
 * @MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL: Memory full.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX: Invalid index.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND: Not found.
 * @MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE: Memory failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG: Text string too long.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS: Invalid characters in text string.
 * @MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG: Dial string too long.
 * @MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID: Invalid characters in dial string.
 * @MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK: No network service.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT: Network timeout.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED: Network not allowed - Emergency calls only.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN: Network personalisation PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK: Network personalisation PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN: Network subset personalisation PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK: Network subset personalisation PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN: Service provider personalisation PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK: Service provider personalisation PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN: Corporate personalisation PIN required.
 * @MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK: Corporate personalisation PUK required.
 * @MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED: Hidden key required. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED: EAP method not supported. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS: Incorrect parameters. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN: Unknown.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_HLR: IMSI unknown in HLR.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_VLR: IMSI unknown in VLR.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS: Illegal MS.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME: Illegal ME.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED: GPRS service not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_AND_NON_GPRS_SERVICES_NOT_ALLOWED: GPRS and non-GPRS services not allowed. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED: PLMN not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED: Location area not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED: Roaming not allowed in this location area.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_NO_CELLS_IN_LOCATION_AREA: No cells in this location area.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_NETWORK_FAILURE: Network failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONGESTION: Congestion.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_NOT_AUTHORIZED_FOR_CSG: GPRS not authorized for CSG. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_INSUFFICIENT_RESOURCES: Insufficient resources.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_MISSING_OR_UNKNOWN_APN: Missing or unknown APN.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_ADDRESS_OR_TYPE: Unknown PDP address or type. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_USER_AUTHENTICATION_FAILED: User authentication failed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_BY_GGSN_OR_GW: Activation rejected by GGSN or gateway. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_UNSPECIFIED: Activation rejected (reason unspecified). Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED: Service option not supported.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED: Requested service option not subscribed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER: Service option temporarily out of order.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_FEATURE_NOT_SUPPORTED: Feature not supported. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERROR_IN_TFT_OPERATION: Semantic error in TFT operation. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_TFT_OPERATION: Syntactical error in TFT operation. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_CONTEXT: Unknown PDP context. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERRORS_IN_PACKET_FILTER: Semantic errors in packet filter. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_PACKET_FILTER: Syntactical error in packet filter. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED: PDP context witout TFT already activated. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN: Unspecified GPRS error.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE: PDP authentication failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS: Invalid mobile class.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED: Last PDN disconnection not allowed. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTICALLY_INCORRECT_MESSAGE: Semantically incorrect message. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_MANDATORY_IE_ERROR: Mandatory IE error. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_IE_NOT_IMPLEMENTED: IE not implemented. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONDITIONAL_IE_ERROR: Conditional IE error. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNSPECIFIED_PROTOCOL_ERROR: Unspecified protocol error. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_OPERATOR_DETERMINED_BARRING: Operator determined barring. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED: Maximum number of PDP contexts reached. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUESTED_APN_NOT_SUPPORTED: Requested APN not supported. Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUEST_REJECTED_BCM_VIOLATION: Request rejected (BCM violation). Since: 1.8.
 *
 * Enumeration of Mobile Equipment errors, as defined in 3GPP TS 07.07 version 7.8.0.
 */
typedef enum { /*< underscore_name=mm_mobile_equipment_error >*/
    /* General errors */
    MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE            = 0,   /*< nick=PhoneFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION            = 1,   /*< nick=NoConnection >*/
    MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED            = 2,   /*< nick=LinkReserved >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED              = 3,   /*< nick=NotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED            = 4,   /*< nick=NotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN               = 5,   /*< nick=PhSimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN              = 6,   /*< nick=PhFsimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK              = 7,   /*< nick=PhFsimPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED         = 10,  /*< nick=SimNotInserted >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN                  = 11,  /*< nick=SimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK                  = 12,  /*< nick=SimPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE              = 13,  /*< nick=SimFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY                 = 14,  /*< nick=SimBusy >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG                = 15,  /*< nick=SimWrong >*/
    MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD       = 16,  /*< nick=IncorrectPassword >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2                 = 17,  /*< nick=SimPin2 >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2                 = 18,  /*< nick=SimPuk2 >*/
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL              = 20,  /*< nick=MemoryFull >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX            = 21,  /*< nick=InvalidIndex >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND                = 22,  /*< nick=NotFound >*/
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE           = 23,  /*< nick=MemoryFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG            = 24,  /*< nick=TextTooLong >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS            = 25,  /*< nick=InvalidChars >*/
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG     = 26,  /*< nick=DialStringTooLong >*/
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID      = 27,  /*< nick=DialStringInvalid >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK               = 30,  /*< nick=NoNetwork >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT          = 31,  /*< nick=NetworkTimeout >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED      = 32,  /*< nick=NetworkNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN              = 40,  /*< nick=NetworkPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK              = 41,  /*< nick=NetworkPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN       = 42,  /*< nick=NetworkSubsetPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK       = 43,  /*< nick=NetworkSubsetPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN              = 44,  /*< nick=ServicePin >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK              = 45,  /*< nick=ServicePuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN                 = 46,  /*< nick=CorpPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK                 = 47,  /*< nick=CorpPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED      = 48,  /*< nick=HiddenKeyRequired >*/
    MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED = 49, /*< nick=EapMethodNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS     = 50,  /*< nick=IncorrectParameters >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN                  = 100, /*< nick=Unknown >*/
    /* GPRS related errors */
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_HLR                       = 102, /*< nick=GprsImsiUnknownInHlr >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS                                = 103, /*< nick=GprsIllegalMs >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_IMSI_UNKNOWN_IN_VLR                       = 104, /*< nick=GprsImsiUnknownInVlr >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME                                = 106, /*< nick=GprsIllegalMe >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED                       = 107, /*< nick=GprsServiceNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_AND_NON_GPRS_SERVICES_NOT_ALLOWED         = 108, /*< nick=GprsAndNonGprsServicesNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED                          = 111, /*< nick=GprsPlmnNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED                      = 112, /*< nick=GprsLocationNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED                       = 113, /*< nick=GprsRomaingNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_NO_CELLS_IN_LOCATION_AREA                 = 115, /*< nick=GprsNoCellsInLocationArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_NETWORK_FAILURE                           = 117, /*< nick=GprsNetworkFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONGESTION                                = 122, /*< nick=GprsCongestion >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_NOT_AUTHORIZED_FOR_CSG                    = 125, /*< nick=NotAuthorizedForCsg >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_INSUFFICIENT_RESOURCES                    = 126, /*< nick=GprsInsufficientResources >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_MISSING_OR_UNKNOWN_APN                    = 127, /*< nick=GprsMissingOrUnknownApn >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_ADDRESS_OR_TYPE               = 128, /*< nick=GprsUnknownPdpAddressOrType >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_USER_AUTHENTICATION_FAILED                = 129, /*< nick=GprsUserAuthenticationFailed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_BY_GGSN_OR_GW         = 130, /*< nick=GprsActivationRejectedByGgsnOrGw >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ACTIVATION_REJECTED_UNSPECIFIED           = 131, /*< nick=GprsActivationRejectedUnspecified >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED              = 132, /*< nick=GprsServiceOptionNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED             = 133, /*< nick=GprsServiceOptionNotSubscribed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER               = 134, /*< nick=GprsServiceOptionOutOfOrder >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_FEATURE_NOT_SUPPORTED                     = 140, /*< nick=GprsFeatureNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERROR_IN_TFT_OPERATION           = 141, /*< nick=GprsSemanticErrorInTftOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_TFT_OPERATION        = 142, /*< nick=GprsSyntacticalErrorInTftOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN_PDP_CONTEXT                       = 143, /*< nick=GprsUnknownPdpContext >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTIC_ERRORS_IN_PACKET_FILTER          = 144, /*< nick=GprsSemanticErrorsInPacketFilter >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SYNTACTICAL_ERROR_IN_PACKET_FILTER        = 145, /*< nick=GprsSyntacticalErrorsInPacketFilter >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED = 146, /*< nick=GprsPdpContextWithoutTftAlreadyActivated >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN                                   = 148, /*< nick=GprsUnknown >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE                          = 149, /*< nick=GprsPdpAuthFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS                      = 150, /*< nick=GprsInvalidMobileClass >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_LAST_PDN_DISCONNECTION_NOT_ALLOWED        = 171, /*< nick=GprsLastPdnDisconnectionNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SEMANTICALLY_INCORRECT_MESSAGE            = 172, /*< nick=GprsSemanticallyIncorrectMessage >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_MANDATORY_IE_ERROR                        = 173, /*< nick=GprsMandatoryIeError >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_IE_NOT_IMPLEMENTED                        = 174, /*< nick=GprsIeNotImplemented >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_CONDITIONAL_IE_ERROR                      = 175, /*< nick=GprsConditionalIeError >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNSPECIFIED_PROTOCOL_ERROR                = 176, /*< nick=GprsUnspecifiedProtocolError >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_OPERATOR_DETERMINED_BARRING               = 177, /*< nick=GprsOperatorDeterminedBarring >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_MAXIMUM_NUMBER_OF_PDP_CONTEXTS_REACHED    = 178, /*< nick=GprsMaximumNumberOfPdpContextsReached >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUESTED_APN_NOT_SUPPORTED               = 179, /*< nick=GprsRequestedApnNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_REQUEST_REJECTED_BCM_VIOLATION            = 180, /*< nick=GprsRequestRejectedBcmViolation >*/
} MMMobileEquipmentError;

/**
 * MMConnectionError:
 * @MM_CONNECTION_ERROR_UNKNOWN: Unknown connection error.
 * @MM_CONNECTION_ERROR_NO_CARRIER: No carrier.
 * @MM_CONNECTION_ERROR_NO_DIALTONE: No dialtone.
 * @MM_CONNECTION_ERROR_BUSY: Busy.
 * @MM_CONNECTION_ERROR_NO_ANSWER: No answer.
 *
 * Connection errors that may be reported by ModemManager.
 */
typedef enum { /*< underscore_name=mm_connection_error >*/
    MM_CONNECTION_ERROR_UNKNOWN     = 0, /*< nick=Unknown    >*/
    MM_CONNECTION_ERROR_NO_CARRIER  = 1, /*< nick=NoCarrier  >*/
    MM_CONNECTION_ERROR_NO_DIALTONE = 2, /*< nick=NoDialtone >*/
    MM_CONNECTION_ERROR_BUSY        = 3, /*< nick=Busy       >*/
    MM_CONNECTION_ERROR_NO_ANSWER   = 4, /*< nick=NoAnswer   >*/
} MMConnectionError;

/**
 * MMSerialError:
 * @MM_SERIAL_ERROR_UNKNOWN: Unknown serial error.
 * @MM_SERIAL_ERROR_OPEN_FAILED: Could not open the serial device.
 * @MM_SERIAL_ERROR_SEND_FAILED: Could not write to the serial device.
 * @MM_SERIAL_ERROR_RESPONSE_TIMEOUT: A response was not received on time.
 * @MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE: Could not open the serial port, no device.
 * @MM_SERIAL_ERROR_FLASH_FAILED: Could not flash the device.
 * @MM_SERIAL_ERROR_NOT_OPEN: The serial port is not open.
 * @MM_SERIAL_ERROR_PARSE_FAILED: The serial port specific parsing failed.
 * @MM_SERIAL_ERROR_FRAME_NOT_FOUND: The serial port reported that the frame marker wasn't found (e.g. for QCDM).
 *
 * Serial errors that may be reported by ModemManager.
 */
typedef enum { /*< underscore_name=mm_serial_error >*/
    MM_SERIAL_ERROR_UNKNOWN               = 0, /*< nick=Unknown            >*/
    MM_SERIAL_ERROR_OPEN_FAILED           = 1, /*< nick=OpenFailed         >*/
    MM_SERIAL_ERROR_SEND_FAILED           = 2, /*< nick=SendFailed         >*/
    MM_SERIAL_ERROR_RESPONSE_TIMEOUT      = 3, /*< nick=ResponseTimeout    >*/
    MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE = 4, /*< nick=OpenFailedNoDevice >*/
    MM_SERIAL_ERROR_FLASH_FAILED          = 5, /*< nick=FlashFailed        >*/
    MM_SERIAL_ERROR_NOT_OPEN              = 6, /*< nick=NotOpen            >*/
    MM_SERIAL_ERROR_PARSE_FAILED          = 7, /*< nick=ParseFailed        >*/
    MM_SERIAL_ERROR_FRAME_NOT_FOUND       = 8, /*< nick=FrameNotFound      >*/
} MMSerialError;

/**
 * MMMessageError:
 * @MM_MESSAGE_ERROR_ME_FAILURE: ME failure.
 * @MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED: SMS service reserved.
 * @MM_MESSAGE_ERROR_NOT_ALLOWED: Operation not allowed.
 * @MM_MESSAGE_ERROR_NOT_SUPPORTED: Operation not supported.
 * @MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER: Invalid PDU mode parameter.
 * @MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER: Invalid text mode parameter.
 * @MM_MESSAGE_ERROR_SIM_NOT_INSERTED: SIM not inserted.
 * @MM_MESSAGE_ERROR_SIM_PIN: SIM PIN required.
 * @MM_MESSAGE_ERROR_PH_SIM_PIN: PH-SIM PIN required.
 * @MM_MESSAGE_ERROR_SIM_FAILURE: SIM failure.
 * @MM_MESSAGE_ERROR_SIM_BUSY: SIM busy.
 * @MM_MESSAGE_ERROR_SIM_WRONG: SIM wrong.
 * @MM_MESSAGE_ERROR_SIM_PUK: SIM PUK required.
 * @MM_MESSAGE_ERROR_SIM_PIN2: SIM PIN2 required.
 * @MM_MESSAGE_ERROR_SIM_PUK2: SIM PUK2 required.
 * @MM_MESSAGE_ERROR_MEMORY_FAILURE: Memory failure.
 * @MM_MESSAGE_ERROR_INVALID_INDEX: Invalid index.
 * @MM_MESSAGE_ERROR_MEMORY_FULL: Memory full.
 * @MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN: SMSC address unknown.
 * @MM_MESSAGE_ERROR_NO_NETWORK: No network.
 * @MM_MESSAGE_ERROR_NETWORK_TIMEOUT: Network timeout.
 * @MM_MESSAGE_ERROR_NO_CNMA_ACK_EXPECTED: No CNMA Acknowledgement expected.
 * @MM_MESSAGE_ERROR_UNKNOWN: Unknown error.
 *
 * Enumeration of message errors, as defined in 3GPP TS 27.005 version 10 section 3.2.5.
 */
typedef enum { /*< underscore_name=mm_message_error >*/
    /* 0 -> 127 per 3GPP TS 24.011 [6] clause E.2 */
    /* 128 -> 255 per 3GPP TS 23.040 [3] clause 9.2.3.22 */
    MM_MESSAGE_ERROR_ME_FAILURE             = 300, /*< nick=MeFailure            >*/
    MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED   = 301, /*< nick=SmsServiceReserved   >*/
    MM_MESSAGE_ERROR_NOT_ALLOWED            = 302, /*< nick=NotAllowed           >*/
    MM_MESSAGE_ERROR_NOT_SUPPORTED          = 303, /*< nick=NotSupported         >*/
    MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER  = 304, /*< nick=InvalidPduParameter  >*/
    MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER = 305, /*< nick=InvalidTextParameter >*/
    MM_MESSAGE_ERROR_SIM_NOT_INSERTED       = 310, /*< nick=SimNotInserted       >*/
    MM_MESSAGE_ERROR_SIM_PIN                = 311, /*< nick=SimPin               >*/
    MM_MESSAGE_ERROR_PH_SIM_PIN             = 312, /*< nick=PhSimPin             >*/
    MM_MESSAGE_ERROR_SIM_FAILURE            = 313, /*< nick=SimFailure           >*/
    MM_MESSAGE_ERROR_SIM_BUSY               = 314, /*< nick=SimBusy              >*/
    MM_MESSAGE_ERROR_SIM_WRONG              = 315, /*< nick=SimWrong             >*/
    MM_MESSAGE_ERROR_SIM_PUK                = 316, /*< nick=SimPuk               >*/
    MM_MESSAGE_ERROR_SIM_PIN2               = 317, /*< nick=SimPin2              >*/
    MM_MESSAGE_ERROR_SIM_PUK2               = 318, /*< nick=SimPuk2              >*/
    MM_MESSAGE_ERROR_MEMORY_FAILURE         = 320, /*< nick=MemoryFailure        >*/
    MM_MESSAGE_ERROR_INVALID_INDEX          = 321, /*< nick=InvalidIndex         >*/
    MM_MESSAGE_ERROR_MEMORY_FULL            = 322, /*< nick=MemoryFull           >*/
    MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN   = 330, /*< nick=SmscAddressUnknown   >*/
    MM_MESSAGE_ERROR_NO_NETWORK             = 331, /*< nick=NoNetwork            >*/
    MM_MESSAGE_ERROR_NETWORK_TIMEOUT        = 332, /*< nick=NetworkTimeout       >*/
    MM_MESSAGE_ERROR_NO_CNMA_ACK_EXPECTED   = 340, /*< nick=NoCnmaAckExpected    >*/
    MM_MESSAGE_ERROR_UNKNOWN                = 500  /*< nick=Unknown              >*/
} MMMessageError;

/**
 * MMCdmaActivationError:
 * @MM_CDMA_ACTIVATION_ERROR_NONE: No error.
 * @MM_CDMA_ACTIVATION_ERROR_UNKNOWN: An error occurred.
 * @MM_CDMA_ACTIVATION_ERROR_ROAMING: Device cannot activate while roaming.
 * @MM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE: Device cannot activate on this network type (eg EVDO vs 1xRTT).
 * @MM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT: Device could not connect to the network for activation.
 * @MM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED: Device could not authenticate to the network for activation.
 * @MM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED: Later stages of device provisioning failed.
 * @MM_CDMA_ACTIVATION_ERROR_NO_SIGNAL: No signal available.
 * @MM_CDMA_ACTIVATION_ERROR_TIMED_OUT: Activation timed out.
 * @MM_CDMA_ACTIVATION_ERROR_START_FAILED: API call for initial activation failed.
 *
 * CDMA Activation errors.
 */
typedef enum { /*< underscore_name=mm_cdma_activation_error >*/
    MM_CDMA_ACTIVATION_ERROR_NONE                           = 0, /*< nick=None                         >*/
    MM_CDMA_ACTIVATION_ERROR_UNKNOWN                        = 1, /*< nick=Unknown                      >*/
    MM_CDMA_ACTIVATION_ERROR_ROAMING                        = 2, /*< nick=Roaming                      >*/
    MM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE          = 3, /*< nick=WrongRadioInterface          >*/
    MM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT              = 4, /*< nick=CouldNotConnect              >*/
    MM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED = 5, /*< nick=SecurityAuthenticationFailed >*/
    MM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED            = 6, /*< nick=ProvisioningFailed           >*/
    MM_CDMA_ACTIVATION_ERROR_NO_SIGNAL                      = 7, /*< nick=NoSignal                     >*/
    MM_CDMA_ACTIVATION_ERROR_TIMED_OUT                      = 8, /*< nick=TimedOut                     >*/
    MM_CDMA_ACTIVATION_ERROR_START_FAILED                   = 9  /*< nick=StartFailed                  >*/
} MMCdmaActivationError;

#endif /*  _MODEMMANAGER_ERRORS_H_ */
