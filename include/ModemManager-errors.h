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

/**
 * MM_CORE_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMCoreError errors.
 *
 * Since: 1.0
 */
#define MM_CORE_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".Core"

/**
 * MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMMobileEquipmentError errors.
 *
 * Since: 1.0
 */
#define MM_MOBILE_EQUIPMENT_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".MobileEquipment"

/**
 * MM_CONNECTION_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMConnectionError errors.
 *
 * Since: 1.0
 */
#define MM_CONNECTION_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".Connection"

/**
 * MM_SERIAL_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMSerialError errors.
 *
 * Since: 1.0
 */
#define MM_SERIAL_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".Serial"

/**
 * MM_MESSAGE_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMMessageError errors.
 *
 * Since: 1.0
 */
#define MM_MESSAGE_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".Message"

/**
 * MM_CDMA_ACTIVATION_ERROR_DBUS_PREFIX:
 *
 * DBus prefix for #MMCdmaActivationError errors.
 *
 * Since: 1.0
 */
#define MM_CDMA_ACTIVATION_ERROR_DBUS_PREFIX MM_DBUS_ERROR_PREFIX ".CdmaActivation"

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
 *
 * Since: 1.0
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
 * @MM_MOBILE_EQUIPMENT_ERROR_COMMAND_DISABLED: Command implemented but currently disabled. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_COMMAND_ABORTED: Command aborted by user. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_ATTACHED_RESTRICTED: Not attached to network due to MT functionality restrictions. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_EMERGENCY_ONLY: Modem not allowed, MT restricted to emergency calls only. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_RESTRICTED: Operation not allowed because of MT functionality restrictions. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_FIXED_DIAL_NUMBER_ONLY: Fixed dial number only allowed; called number is not a fixed dial number. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_OUT_OF_SERVICE: Temporarily out of service due to other MT usage. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_LANGUAGE_OR_ALPHABET_NOT_SUPPORTED: Language or alphabet not supported. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNEXPECTED_DATA_VALUE: Unexpected data value. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SYSTEM_FAILURE: System failure. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_DATA_MISSING: Data missing. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CALL_BARRED: Call barred. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_WAITING_INDICATION_SUBSCRIPTION_FAILURE: Message waiting indication subscription failure. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN: Unknown.
 * @MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_HSS: IMSI unknown in HLR (CS, GPRS, UMTS); IMSI unknown in HSS (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_UE: Illegal MS (CS, GPRS, UMTS); Illegal UE (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_VLR: IMSI unknown in VLR. Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IMEI_NOT_ACCEPTED: IMEI not accepted (CS, GPRS, UMTS, EPS); PEI not accepted (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_ME: Illegal ME (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED: GPRS services not allowed (CS, GPRS, UMTS); EPS services not allowed (EPS); 5GS services not allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PS_AND_NON_PS_SERVICES_NOT_ALLOWED: GPRS and non-GPRS services not allowed (CS, GPRS, UMTS); EPS and non-EPS services not allowed (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UE_IDENTITY_NOT_DERIVED_FROM_NETWORK: MS identity cannot be derived from network (CS, GPRS, UMTS; UE identity cannot be derived from network (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IMPLICITLY_DETACHED: Implicitly detached (CS, GPRS, UMTS, EPS); implicitly degistered (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PLMN_NOT_ALLOWED: PLMN not allowed (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_AREA_NOT_ALLOWED: Location area not allowed (CS, GPRS, UMTS); Tracking area not allowed (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ROAMING_NOT_ALLOWED_IN_AREA: Roaming not allowed in this location area (CS, GPRS, UMTS); Roaming not allowed in this tracking area (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED_IN_PLMN: GPRS services not allowed in this PLMN (CS, GPRS, UMTS); EPS services not allowed in this PLMN (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NO_CELLS_IN_AREA: No suitable cells in this location area (CS, GPRS, UMTS); no suitable cells in this tracking area (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MSC_TEMPORARILY_NOT_REACHABLE: MSC temporarily not reachable (CS, GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ATTACH: Network failure during attach (CS, GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CS_DOMAIN_UNAVAILABLE: CS domain not available (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ESM_FAILURE: ESM failure (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CONGESTION: Congestion (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MBMS_BEARER_CAPABILITIES_INSUFFICIENT_FOR_SERVICE: MBMS bearer capabilities insufficient for service (GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NOT_AUTHORIZED_FOR_CSG: Not authorized for this CSG (CS, GPRS, UMTS, EPS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES: Insufficient resources (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_APN: Missing or unknown APN (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE: Unknown PDP address or PDP type (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_USER_AUTHENTICATION_FAILED: User authentication or authorization failed (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW: Activation rejected by GGSN, Serving GW or PDN GW (GPRS, UMTS); activation rejected by Serving GW or PDN GW (EPS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_UNSPECIFIED: Activation rejected, unspecified (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUPPORTED: Service option not supported (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUBSCRIBED: Requested service option not subscribed (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_OUT_OF_ORDER: Service option temporarily out of order (CS, GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NSAPI_OR_PTI_ALREADY_IN_USE: NSAPI out of order (GPRS, UMTS); PTI out of order (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REGULAR_DEACTIVATION: Regular deactivation (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_QOS_NOT_ACCEPTED: EPS Qos not accepted (EPS); 5GS QoS not accepted (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CALL_CANNOT_BE_IDENTIFIED: Call cannot be identified (CS, GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CS_SERVICE_TEMPORARILY_UNAVAILABLE: CS service temporarily unavailable (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_FEATURE_NOT_SUPPORTED: Feature not supported (GPRS, UMTS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_TFT_OPERATION: Semantic error in TFT operation (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_TFT_OPERATION: Syntactical error in TFT operation (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_CONTEXT: Unknown PDP context (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERRORS_IN_PACKET_FILTER: Semantic errors in packet filter (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_PACKET_FILTER: Syntactical error in packet filter (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED: PDP context without TFT already activated (GPRS, UMTS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MULTICAST_GROUP_MEMBERSHIP_TIMEOUT: Multicast group membership timeout (GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN: Unspecified GPRS error (CS, GPRS, UMTS).
 * @MM_MOBILE_EQUIPMENT_ERROR_PDP_AUTH_FAILURE: PDP authentication failure (GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_MOBILE_CLASS: Invalid mobile class (CS, GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY: Last PDN disconnection not allowed, legacy value defined before 3GPP Rel-11 (EPS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED: Last PDN disconnection not allowed (EPS). Since: 1.8.
 * @MM_MOBILE_EQUIPMENT_ERROR_SEMANTICALLY_INCORRECT_MESSAGE: Semantically incorrect message (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_MANDATORY_INFORMATION: Invalid mandatory information (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_IMPLEMENTED: Message type non-existent or not implemented (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_CONDITIONAL_IE_ERROR: Conditional IE error (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNSPECIFIED_PROTOCOL_ERROR: Unspecified protocol error (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_OPERATOR_DETERMINED_BARRING: Operator determined barring (GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MAXIMUM_NUMBER_OF_BEARERS_REACHED: Maximum number of PDP contexts reached (GPRS, UMTS); maximum number of EPS bearers reached (EPS); maximum number of PDU sessions reached (5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REQUESTED_APN_NOT_SUPPORTED: Requested APN not supported in current RAT and PLMN combination (GPRS, UMTS, EPS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REQUEST_REJECTED_BCM_VIOLATION: Request rejected, bearer control mode violation (GPRS, UMTS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_QCI_OR_5QI_VALUE: Unsupported QCI value (EPS); unsupported 5QI value (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_USER_DATA_VIA_CONTROL_PLANE_CONGESTED: User data transmission via control plane is congested (GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SMS_PROVIDED_VIA_GPRS_IN_ROUTING_AREA: SMS provided via GPRS in routing area (CS, GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_PTI_VALUE: Invalid PTI value (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NO_BEARER_ACTIVATED: No PDP context activated (CS, GPRS, UMTS); no bearer context activated (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE: Message not compatible with protocol state (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_RECOVERY_ON_TIMER_EXPIRY: Recovery on timer expiry (CS, GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_TRANSACTION_ID_VALUE: Invalid transaction identifier value (GPRS, UMTS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_AUTHORIZED_IN_PLMN: Requested service option is not authorized in this PLMN (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ACTIVATION: Network failure during context activation (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REACTIVATION_REQUESTED: Reactivation requested (GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IPV4_ONLY_ALLOWED: PDP type IPv4 only allowed (GPRS, UMTS); PDN type IPv4 only allowed (EPS); PDU session type IPv4 only allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IPV6_ONLY_ALLOWED: PDP type IPv6 only allowed (GPRS, UMTS); PDN type IPv6 only allowed (EPS); PDU session type IPv6 only allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SINGLE_ADDRESS_BEARERS_ONLY_ALLOWED: Single address bearers only allowed (GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_COLLISION_WITH_NETWORK_INITIATED_REQUEST: Collision with network initiated request (GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IPV4V6_ONLY_ALLOWED: PDP type IPv4v6 only allowed (GPRS, UMTS); PDN type IPv4v6 only allowed (EPS); PDU session type IPv4v6 only allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NON_IP_ONLY_ALLOWED: PDP type non-IP only allowed (GPRS, UMTS); PDN type non-IP only allowed (EPS); PDU session type unstructured only allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_BEARER_HANDLING_UNSUPPORTED: Bearer handling not supported (GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_APN_RESTRICTION_INCOMPATIBLE: APN restriction value incompatible with active PDP context (GPRS, UMTS); APN restriction value incompatible with active EPS bearer context (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_ACCESS_TO_PDN_CONNECTION_NOT_ALLOWED: Multiple accesses to PDN connection not allowed (GPRS, UMTS, EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ESM_INFORMATION_NOT_RECEIVED: ESM information not received (EPS).Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PDN_CONNECTION_NONEXISTENT: PDN connection does not exist (EPS); PDU session does not exist (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_PDN_CONNECTION_SAME_APN_NOT_ALLOWED: Multiple PDN connections for a given APN not allowed (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SEVERE_NETWORK_FAILURE: Severe network failure (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE_AND_DNN: Insufficient resources for specific slice and DNN (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_SSC_MODE: Not supported SSC mode (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE: Insufficient resources for specific slice (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE: Message type not compatible with protocol state (CS, GPRS, UMTS, EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_IE_NOT_IMPLEMENTED: Information element non-existent or not implemented (CS, GPRS, UMTS, EPS, 5GS). Since: 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_N1_MODE_NOT_ALLOWED: N1 mode not allowed. (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_RESTRICTED_SERVICE_AREA: Restricted service area (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_LADN_UNAVAILABLE: LADN not available (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_DNN_IN_SLICE: Missing or unknown DNN in a slice (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NGKSI_ALREADY_IN_USE: ngKSI already in use (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PAYLOAD_NOT_FORWARDED: Payload was not forwarded (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NON_3GPP_ACCESS_TO_5GCN_NOT_ALLOWED: Non-3GPP access to 5GCN not allowed (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SERVING_NETWORK_NOT_AUTHORIZED: Serving network not authorized (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_DNN_NOT_SUPPORTED_IN_SLICE: DNN not supported or not subscribed in the slice (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_USER_PLANE_RESOURCES_FOR_PDU_SESSION: Insufficient user plane resources for PDU session (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_OUT_OF_LADN_SERVICE_AREA: Out of LADN service area (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PTI_MISMATCH: PTI mismatch (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_MAX_DATA_RATE_FOR_USER_PLANE_INTEGRITY_TOO_LOW: Maximum data rate per UE for user-plane integrity protection is too low (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_QOS_OPERATION: Semantic error in QoS operation (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_QOS_OPERATION: Semantic error in QoS operation (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_INVALID_MAPPED_EPS_BEARER_IDENTITY: Invalid mapped EPS bearer identity (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_5GCN_REQUIRED: Redirection to 5GCN required (EPS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_EPC_REQUIRED: Redirection to EPC required (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_UNAUTHORIZED_FOR_SNPN: Temporarily not authorized for this SNPN (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_PERMANENTLY_UNAUTHORIZED_FOR_SNPN: Permanently not authorized for this SNPN (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_ETHERNET_ONLY_ALLOWED: PDN type Ethernet only allowed (EPS, 5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_UNAUTHORIZED_FOR_CAG: Not authorized for this CAG or authorized for CAG cells only (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK_SLICES_AVAILABLE: No network slices available (5GS). Since 1.18.
 * @MM_MOBILE_EQUIPMENT_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED: Wireline access area not allowed (5GS). Since 1.18.
 *
 * Enumeration of Mobile Equipment errors, as defined in 3GPP TS 27.007 v17.1.0,
 * section 9.2 (Mobile termination error result code +CME ERROR).
 *
 * Since: 1.0
 */
typedef enum { /*< underscore_name=mm_mobile_equipment_error >*/
    MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE                                     = 0,   /*< nick=PhoneFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION                                     = 1,   /*< nick=NoConnection >*/
    MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED                                     = 2,   /*< nick=LinkReserved >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED                                       = 3,   /*< nick=NotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED                                     = 4,   /*< nick=NotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN                                        = 5,   /*< nick=PhSimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN                                       = 6,   /*< nick=PhFsimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK                                       = 7,   /*< nick=PhFsimPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED                                  = 10,  /*< nick=SimNotInserted >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN                                           = 11,  /*< nick=SimPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK                                           = 12,  /*< nick=SimPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE                                       = 13,  /*< nick=SimFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY                                          = 14,  /*< nick=SimBusy >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG                                         = 15,  /*< nick=SimWrong >*/
    MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD                                = 16,  /*< nick=IncorrectPassword >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2                                          = 17,  /*< nick=SimPin2 >*/
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2                                          = 18,  /*< nick=SimPuk2 >*/
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL                                       = 20,  /*< nick=MemoryFull >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX                                     = 21,  /*< nick=InvalidIndex >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND                                         = 22,  /*< nick=NotFound >*/
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE                                    = 23,  /*< nick=MemoryFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG                                     = 24,  /*< nick=TextTooLong >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS                                     = 25,  /*< nick=InvalidChars >*/
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG                              = 26,  /*< nick=DialStringTooLong >*/
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID                               = 27,  /*< nick=DialStringInvalid >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK                                        = 30,  /*< nick=NoNetwork >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT                                   = 31,  /*< nick=NetworkTimeout >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED                               = 32,  /*< nick=NetworkNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN                                       = 40,  /*< nick=NetworkPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK                                       = 41,  /*< nick=NetworkPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN                                = 42,  /*< nick=NetworkSubsetPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK                                = 43,  /*< nick=NetworkSubsetPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN                                       = 44,  /*< nick=ServicePin >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK                                       = 45,  /*< nick=ServicePuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN                                          = 46,  /*< nick=CorpPin >*/
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK                                          = 47,  /*< nick=CorpPuk >*/
    MM_MOBILE_EQUIPMENT_ERROR_HIDDEN_KEY_REQUIRED                               = 48,  /*< nick=HiddenKeyRequired >*/
    MM_MOBILE_EQUIPMENT_ERROR_EAP_METHOD_NOT_SUPPORTED                          = 49,  /*< nick=EapMethodNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS                              = 50,  /*< nick=IncorrectParameters >*/
    MM_MOBILE_EQUIPMENT_ERROR_COMMAND_DISABLED                                  = 51, /*< nick=CommandDisabled >*/
    MM_MOBILE_EQUIPMENT_ERROR_COMMAND_ABORTED                                   = 52, /*< nick=CommandAborted >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ATTACHED_RESTRICTED                           = 53, /*< nick=NotAttachedRestricted >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_EMERGENCY_ONLY                        = 54, /*< nick=NotAllowedEmergencyOnly >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED_RESTRICTED                            = 55, /*< nick=NotAllowedRestricted >*/
    MM_MOBILE_EQUIPMENT_ERROR_FIXED_DIAL_NUMBER_ONLY                            = 56, /*< nick=FixedDialNumberOnly >*/
    MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_OUT_OF_SERVICE                        = 57, /*< nick=TemporarilyOutOfService >*/
    MM_MOBILE_EQUIPMENT_ERROR_LANGUAGE_OR_ALPHABET_NOT_SUPPORTED                = 58, /*< nick=LanguageOrAlphabetNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNEXPECTED_DATA_VALUE                             = 59, /*< nick=UnexpectedDataValue >*/
    MM_MOBILE_EQUIPMENT_ERROR_SYSTEM_FAILURE                                    = 60, /*< nick=SystemFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_DATA_MISSING                                      = 61, /*< nick=DataMissing >*/
    MM_MOBILE_EQUIPMENT_ERROR_CALL_BARRED                                       = 62, /*< nick=CallBarred >*/
    MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_WAITING_INDICATION_SUBSCRIPTION_FAILURE   = 63, /*< nick=MessageWaitingIndicationSubscriptionFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN                                           = 100, /*< nick=Unknown >*/
    MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_HSS                               = 102, /*< nick=ImsiUnknownInHss >*/
    MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_UE                                        = 103, /*< nick=IllegalUe >*/
    MM_MOBILE_EQUIPMENT_ERROR_IMSI_UNKNOWN_IN_VLR                               = 104, /*< nick=ImsiUnknownInVlr >*/
    MM_MOBILE_EQUIPMENT_ERROR_IMEI_NOT_ACCEPTED                                 = 105, /*< nick=ImeiNotAccepted >*/
    MM_MOBILE_EQUIPMENT_ERROR_ILLEGAL_ME                                        = 106, /*< nick=IllegalMe >*/
    MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED                           = 107, /*< nick=PsServicesNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_PS_AND_NON_PS_SERVICES_NOT_ALLOWED                = 108, /*< nick=PsAndNonPsServicesNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_UE_IDENTITY_NOT_DERIVED_FROM_NETWORK              = 109, /*< nick=UeIdentityNotDerivedFromNetwork >*/
    MM_MOBILE_EQUIPMENT_ERROR_IMPLICITLY_DETACHED                               = 110, /*< nick=ImplicitlyDetached >*/
    MM_MOBILE_EQUIPMENT_ERROR_PLMN_NOT_ALLOWED                                  = 111, /*< nick=PlmnNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_AREA_NOT_ALLOWED                                  = 112, /*< nick=AreaNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_ROAMING_NOT_ALLOWED_IN_AREA                       = 113, /*< nick=RoamingNotAllowedInArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_PS_SERVICES_NOT_ALLOWED_IN_PLMN                   = 114, /*< nick=PsServicesNotAllowedInPlmn >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_CELLS_IN_AREA                                  = 115, /*< nick=NoCellsInArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_MSC_TEMPORARILY_NOT_REACHABLE                     = 116, /*< nick=MscTemporarilyNotReachable >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ATTACH                            = 117, /*< nick=NetworkFailureAttach >*/
    MM_MOBILE_EQUIPMENT_ERROR_CS_DOMAIN_UNAVAILABLE                             = 118, /*< nick=CsDomainUnavailable >*/
    MM_MOBILE_EQUIPMENT_ERROR_ESM_FAILURE                                       = 119, /*< nick=EsmFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_CONGESTION                                        = 122, /*< nick=Congestion >*/
    MM_MOBILE_EQUIPMENT_ERROR_MBMS_BEARER_CAPABILITIES_INSUFFICIENT_FOR_SERVICE = 124, /*< nick=MbmsBearerCapabilitiesInsufficientForService >*/
    MM_MOBILE_EQUIPMENT_ERROR_NOT_AUTHORIZED_FOR_CSG                            = 125, /*< nick=NotAuthorizedForCsg >*/
    MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES                            = 126, /*< nick=InsufficientResources >*/
    MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_APN                            = 127, /*< nick=MissingOrUnknownApn >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_ADDRESS_OR_TYPE                       = 128, /*< nick=UnknownPdpAddressOrType >*/
    MM_MOBILE_EQUIPMENT_ERROR_USER_AUTHENTICATION_FAILED                        = 129, /*< nick=UserAuthenticationFailed >*/
    MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_BY_GGSN_OR_GW                 = 130, /*< nick=ActivationRejectedByGgsnOrGw >*/
    MM_MOBILE_EQUIPMENT_ERROR_ACTIVATION_REJECTED_UNSPECIFIED                   = 131, /*< nick=ActivationRejectedUnspecified >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUPPORTED                      = 132, /*< nick=ServiceOptionNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_SUBSCRIBED                     = 133, /*< nick=ServiceOptionNotSubscribed >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_OUT_OF_ORDER                       = 134, /*< nick=ServiceOptionOutOfOrder >*/
    MM_MOBILE_EQUIPMENT_ERROR_NSAPI_OR_PTI_ALREADY_IN_USE                       = 135, /*< nick=NsapiOrPtiAlreadyInUse >*/
    MM_MOBILE_EQUIPMENT_ERROR_REGULAR_DEACTIVATION                              = 136, /*< nick=RegularDeactivation >*/
    MM_MOBILE_EQUIPMENT_ERROR_QOS_NOT_ACCEPTED                                  = 137, /*< nick=QosNotAccepted >*/
    MM_MOBILE_EQUIPMENT_ERROR_CALL_CANNOT_BE_IDENTIFIED                         = 138, /*< nick=CallCannotBeIdentified >*/
    MM_MOBILE_EQUIPMENT_ERROR_CS_SERVICE_TEMPORARILY_UNAVAILABLE                = 139, /*< nick=CsServiceTemporarilyUnavailable >*/
    MM_MOBILE_EQUIPMENT_ERROR_FEATURE_NOT_SUPPORTED                             = 140, /*< nick=FeatureNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_TFT_OPERATION                   = 141, /*< nick=SemanticErrorInTftOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_TFT_OPERATION                = 142, /*< nick=SyntacticalErrorInTftOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN_PDP_CONTEXT                               = 143, /*< nick=UnknownPdpContext >*/
    MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERRORS_IN_PACKET_FILTER                  = 144, /*< nick=SemanticErrorsInPacketFilter >*/
    MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_PACKET_FILTER                = 145, /*< nick=SyntacticalErrorsInPacketFilter >*/
    MM_MOBILE_EQUIPMENT_ERROR_PDP_CONTEXT_WITHOUT_TFT_ALREADY_ACTIVATED         = 146, /*< nick=PdpContextWithoutTftAlreadyActivated >*/
    MM_MOBILE_EQUIPMENT_ERROR_MULTICAST_GROUP_MEMBERSHIP_TIMEOUT                = 147, /*< nick=MulticastGroupMembershipTimeout >*/
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN                                      = 148, /*< nick=GprsUnknown >*/
    MM_MOBILE_EQUIPMENT_ERROR_PDP_AUTH_FAILURE                                  = 149, /*< nick=PdpAuthFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_MOBILE_CLASS                              = 150, /*< nick=InvalidMobileClass >*/
    MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED_LEGACY         = 151, /*< nick=LastPdnDisconnectionNotAllowedLegacy >*/
    MM_MOBILE_EQUIPMENT_ERROR_LAST_PDN_DISCONNECTION_NOT_ALLOWED                = 171, /*< nick=LastPdnDisconnectionNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_SEMANTICALLY_INCORRECT_MESSAGE                    = 172, /*< nick=SemanticallyIncorrectMessage >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_MANDATORY_INFORMATION                     = 173, /*< nick=InvalidMandatoryInformation >*/
    MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_IMPLEMENTED                      = 174, /*< nick=MessageTypeNotImplemented >*/
    MM_MOBILE_EQUIPMENT_ERROR_CONDITIONAL_IE_ERROR                              = 175, /*< nick=ConditionalIeError >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNSPECIFIED_PROTOCOL_ERROR                        = 176, /*< nick=UnspecifiedProtocolError >*/
    MM_MOBILE_EQUIPMENT_ERROR_OPERATOR_DETERMINED_BARRING                       = 177, /*< nick=OperatorDeterminedBarring >*/
    MM_MOBILE_EQUIPMENT_ERROR_MAXIMUM_NUMBER_OF_BEARERS_REACHED                 = 178, /*< nick=MaximumNumberOfBearersReached >*/
    MM_MOBILE_EQUIPMENT_ERROR_REQUESTED_APN_NOT_SUPPORTED                       = 179, /*< nick=RequestedApnNotSupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_REQUEST_REJECTED_BCM_VIOLATION                    = 180, /*< nick=RequestRejectedBcmViolation >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_QCI_OR_5QI_VALUE                      = 181, /*< nick=UnsupportedQciOr5qiValue >*/
    MM_MOBILE_EQUIPMENT_ERROR_USER_DATA_VIA_CONTROL_PLANE_CONGESTED             = 182, /*< nick=UserDataViaControlPlaneCongested >*/
    MM_MOBILE_EQUIPMENT_ERROR_SMS_PROVIDED_VIA_GPRS_IN_ROUTING_AREA             = 183, /*< nick=SmsProvidedViaGprsInRoutingArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_PTI_VALUE                                 = 184, /*< nick=InvalidPtiValue >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_BEARER_ACTIVATED                               = 185, /*< nick=NoBearerActivated >*/
    MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE        = 186, /*< nick=MessageNotCompatibleWithProtocolState >*/
    MM_MOBILE_EQUIPMENT_ERROR_RECOVERY_ON_TIMER_EXPIRY                          = 187, /*< nick=RecoveryOnTimerExpiry >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_TRANSACTION_ID_VALUE                      = 188, /*< nick=InvalidTransactionIdValue >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_OPTION_NOT_AUTHORIZED_IN_PLMN             = 189, /*< nick=ServiceOptionNotAuthorizedInPlmn >*/
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_FAILURE_ACTIVATION                        = 190, /*< nick=NetworkFailureActivation >*/
    MM_MOBILE_EQUIPMENT_ERROR_REACTIVATION_REQUESTED                            = 191, /*< nick=ReactivationRequested >*/
    MM_MOBILE_EQUIPMENT_ERROR_IPV4_ONLY_ALLOWED                                 = 192, /*< nick=Ipv4OnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_IPV6_ONLY_ALLOWED                                 = 193, /*< nick=Ipv6OnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_SINGLE_ADDRESS_BEARERS_ONLY_ALLOWED               = 194, /*< nick=SingleAddressBearersOnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_COLLISION_WITH_NETWORK_INITIATED_REQUEST          = 195, /*< nick=CollisionWithNetworkInitiatedRequest >*/
    MM_MOBILE_EQUIPMENT_ERROR_IPV4V6_ONLY_ALLOWED                               = 196, /*< nick=Ipv4v6OnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_NON_IP_ONLY_ALLOWED                               = 197, /*< nick=NonIpOnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_BEARER_HANDLING_UNSUPPORTED                       = 198, /*< nick=BearerHandlingUnsupported >*/
    MM_MOBILE_EQUIPMENT_ERROR_APN_RESTRICTION_INCOMPATIBLE                      = 199, /*< nick=ApnRestrictionIncompatible >*/
    MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_ACCESS_TO_PDN_CONNECTION_NOT_ALLOWED     = 200, /*< nick=MultipleAccessToPdnConnectionNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_ESM_INFORMATION_NOT_RECEIVED                      = 201, /*< nick=EsmInformationNotReceived >*/
    MM_MOBILE_EQUIPMENT_ERROR_PDN_CONNECTION_NONEXISTENT                        = 202, /*< nick=PdnConnectionNonexistent >*/
    MM_MOBILE_EQUIPMENT_ERROR_MULTIPLE_PDN_CONNECTION_SAME_APN_NOT_ALLOWED      = 203, /*< nick=MultiplePdnConnectionSameApnNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_SEVERE_NETWORK_FAILURE                            = 204, /*< nick=SevereNetworkFailure >*/
    MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE_AND_DNN          = 205, /*< nick=InsufficientResourcesForSliceAndDnn >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNSUPPORTED_SSC_MODE                              = 206, /*< nick=UnsupportedSscMode >*/
    MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_RESOURCES_FOR_SLICE                  = 207, /*< nick=InsufficientResourcesForSlice >*/
    MM_MOBILE_EQUIPMENT_ERROR_MESSAGE_TYPE_NOT_COMPATIBLE_WITH_PROTOCOL_STATE   = 208, /*< nick=MessageTypeNotCompatibleWithProtocolState >*/
    MM_MOBILE_EQUIPMENT_ERROR_IE_NOT_IMPLEMENTED                                = 209, /*< nick=IeNotImplemented >*/
    MM_MOBILE_EQUIPMENT_ERROR_N1_MODE_NOT_ALLOWED                               = 210, /*< nick=N1ModeNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_RESTRICTED_SERVICE_AREA                           = 211, /*< nick=RestrictedServiceArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_LADN_UNAVAILABLE                                  = 212, /*< nick=LadnUnavailable >*/
    MM_MOBILE_EQUIPMENT_ERROR_MISSING_OR_UNKNOWN_DNN_IN_SLICE                   = 213, /*< nick=MissingOrUnknownDnnInSlice >*/
    MM_MOBILE_EQUIPMENT_ERROR_NGKSI_ALREADY_IN_USE                              = 214, /*< nick=NkgsiAlreadyInUse >*/
    MM_MOBILE_EQUIPMENT_ERROR_PAYLOAD_NOT_FORWARDED                             = 215, /*< nick=PayloadNotForwarded >*/
    MM_MOBILE_EQUIPMENT_ERROR_NON_3GPP_ACCESS_TO_5GCN_NOT_ALLOWED               = 216, /*< nick=Non3gppAccessTo5gcnNotAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_SERVING_NETWORK_NOT_AUTHORIZED                    = 217, /*< nick=ServingNetworkNotAuthorized >*/
    MM_MOBILE_EQUIPMENT_ERROR_DNN_NOT_SUPPORTED_IN_SLICE                        = 218, /*< nick=DnnNotSupportedInSlice >*/
    MM_MOBILE_EQUIPMENT_ERROR_INSUFFICIENT_USER_PLANE_RESOURCES_FOR_PDU_SESSION = 219, /*< nick=InsufficientUserPlaneResourcesForPduSession >*/
    MM_MOBILE_EQUIPMENT_ERROR_OUT_OF_LADN_SERVICE_AREA                          = 220, /*< nick=OutOfLadnServiceArea >*/
    MM_MOBILE_EQUIPMENT_ERROR_PTI_MISMATCH                                      = 221, /*< nick=PtiMismatch >*/
    MM_MOBILE_EQUIPMENT_ERROR_MAX_DATA_RATE_FOR_USER_PLANE_INTEGRITY_TOO_LOW    = 222, /*< nick=MaxDataRateForUserPlaneIntegrityTooLow >*/
    MM_MOBILE_EQUIPMENT_ERROR_SEMANTIC_ERROR_IN_QOS_OPERATION                   = 223, /*< nick=SemanticErrorInQosOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_SYNTACTICAL_ERROR_IN_QOS_OPERATION                = 224, /*< nick=SyntacticalErrorInQosOperation >*/
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_MAPPED_EPS_BEARER_IDENTITY                = 225, /*< nick=InvalidMappedEpsBearerIdentity >*/
    MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_5GCN_REQUIRED                      = 226, /*< nick=RedirectionTo5gcnRequired >*/
    MM_MOBILE_EQUIPMENT_ERROR_REDIRECTION_TO_EPC_REQUIRED                       = 227, /*< nick=RedirectionToEpcRequired >*/
    MM_MOBILE_EQUIPMENT_ERROR_TEMPORARILY_UNAUTHORIZED_FOR_SNPN                 = 228, /*< nick=TemporarilyUnauthorizedForSnpn >*/
    MM_MOBILE_EQUIPMENT_ERROR_PERMANENTLY_UNAUTHORIZED_FOR_SNPN                 = 229, /*< nick=PermanentlyUnauthorizedForSnpn >*/
    MM_MOBILE_EQUIPMENT_ERROR_ETHERNET_ONLY_ALLOWED                             = 230, /*< nick=EthernetOnlyAllowed >*/
    MM_MOBILE_EQUIPMENT_ERROR_UNAUTHORIZED_FOR_CAG                              = 231, /*< nick=UnauthorizedForCag >*/
    MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK_SLICES_AVAILABLE                       = 232, /*< nick=NoNetworkSlicesAvailable >*/
    MM_MOBILE_EQUIPMENT_ERROR_WIRELINE_ACCESS_AREA_NOT_ALLOWED                  = 233, /*< nick=WirelineAccessAreaNotAllowed >*/
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
 *
 * Since: 1.0
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
 * @MM_SERIAL_ERROR_FRAME_NOT_FOUND: The serial port reported that the frame marker wasn't found (e.g. for QCDM). Since 1.6.
 *
 * Serial errors that may be reported by ModemManager.
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
