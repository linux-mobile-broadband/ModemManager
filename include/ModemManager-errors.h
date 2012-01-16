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

/**
 * SECTION:mm-errors
 * @short_description: Common errors in the API.
 *
 * This section defines errors that may be reported when using methods from the
 * ModemManager interface.
 **/

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
 *
 * Common errors that may be reported by ModemManager.
 */
typedef enum { /*< underscore_name=mm_core_error >*/
    MM_CORE_ERROR_FAILED       = 0,
    MM_CORE_ERROR_CANCELLED    = 1,
    MM_CORE_ERROR_ABORTED      = 2,
    MM_CORE_ERROR_UNSUPPORTED  = 3,
    MM_CORE_ERROR_NO_PLUGINS   = 4,
    MM_CORE_ERROR_UNAUTHORIZED = 5,
    MM_CORE_ERROR_INVALID_ARGS = 6,
    MM_CORE_ERROR_IN_PROGRESS  = 7,
    MM_CORE_ERROR_WRONG_STATE  = 8,
    MM_CORE_ERROR_CONNECTED    = 9,
    MM_CORE_ERROR_TOO_MANY     = 10,
    MM_CORE_ERROR_NOT_FOUND    = 11,
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
 * @MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN: Unknown.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS: Illegal MS.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME: Illegal ME.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED: GPRS service not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED: PLMN not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED: Location area not allowed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED: Roaming not allowed in this location area.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED: Service option not supported.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED: Requested service option not subscribed.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER: Service option temporarily out of order.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN: Unspecified GPRS error.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE: PDP authentication failure.
 * @MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS: Invalid mobile class.
 *
 * Enumeration of Mobile Equipment errors, as defined in 3GPP TS 07.07 version 7.8.0.
 */
typedef enum { /*< underscore_name=mm_mobile_equipment_error >*/
    /* General errors */
    MM_MOBILE_EQUIPMENT_ERROR_PHONE_FAILURE        = 0,
    MM_MOBILE_EQUIPMENT_ERROR_NO_CONNECTION        = 1,
    MM_MOBILE_EQUIPMENT_ERROR_LINK_RESERVED        = 2,
    MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED          = 3,
    MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED        = 4,
    MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN           = 5,
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN          = 6,
    MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK          = 7,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED     = 10,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN              = 11,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK              = 12,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE          = 13,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY             = 14,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG            = 15,
    MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD   = 16,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2             = 17,
    MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2             = 18,
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL          = 20,
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX        = 21,
    MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND            = 22,
    MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FAILURE       = 23,
    MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG        = 24,
    MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS        = 25,
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_TOO_LONG = 26,
    MM_MOBILE_EQUIPMENT_ERROR_DIAL_STRING_INVALID  = 27,
    MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK           = 30,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT      = 31,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED  = 32,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN          = 40,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK          = 41,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN   = 42,
    MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK   = 43,
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN          = 44,
    MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK          = 45,
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN             = 46,
    MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK             = 47,
    MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN              = 100,
    /* GPRS related errors */
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_MS                    = 103,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ILLEGAL_ME                    = 106,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_NOT_ALLOWED           = 107,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_PLMN_NOT_ALLOWED              = 111,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_LOCATION_NOT_ALLOWED          = 112,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_ROAMING_NOT_ALLOWED           = 113,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUPPORTED  = 132,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED = 133,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_OUT_OF_ORDER   = 134,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_UNKNOWN                       = 148,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_PDP_AUTH_FAILURE              = 149,
    MM_MOBILE_EQUIPMENT_ERROR_GPRS_INVALID_MOBILE_CLASS          = 150,
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
    MM_CONNECTION_ERROR_UNKNOWN     = 0,
    MM_CONNECTION_ERROR_NO_CARRIER  = 1,
    MM_CONNECTION_ERROR_NO_DIALTONE = 2,
    MM_CONNECTION_ERROR_BUSY        = 3,
    MM_CONNECTION_ERROR_NO_ANSWER   = 4,
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
 *
 * Serial errors that may be reported by ModemManager.
 */
typedef enum { /*< underscore_name=mm_serial_error >*/
    MM_SERIAL_ERROR_UNKNOWN               = 0,
    MM_SERIAL_ERROR_OPEN_FAILED           = 1,
    MM_SERIAL_ERROR_SEND_FAILED           = 2,
    MM_SERIAL_ERROR_RESPONSE_TIMEOUT      = 3,
    MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE = 4,
    MM_SERIAL_ERROR_FLASH_FAILED          = 5,
    MM_SERIAL_ERROR_NOT_OPEN              = 6,
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
    MM_MESSAGE_ERROR_ME_FAILURE             = 300,
    MM_MESSAGE_ERROR_SMS_SERVICE_RESERVED   = 301,
    MM_MESSAGE_ERROR_NOT_ALLOWED            = 302,
    MM_MESSAGE_ERROR_NOT_SUPPORTED          = 303,
    MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER  = 304,
    MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER = 305,
    MM_MESSAGE_ERROR_SIM_NOT_INSERTED       = 310,
    MM_MESSAGE_ERROR_SIM_PIN                = 311,
    MM_MESSAGE_ERROR_PH_SIM_PIN             = 312,
    MM_MESSAGE_ERROR_SIM_FAILURE            = 313,
    MM_MESSAGE_ERROR_SIM_BUSY               = 314,
    MM_MESSAGE_ERROR_SIM_WRONG              = 315,
    MM_MESSAGE_ERROR_SIM_PUK                = 316,
    MM_MESSAGE_ERROR_SIM_PIN2               = 317,
    MM_MESSAGE_ERROR_SIM_PUK2               = 318,
    MM_MESSAGE_ERROR_MEMORY_FAILURE         = 320,
    MM_MESSAGE_ERROR_INVALID_INDEX          = 321,
    MM_MESSAGE_ERROR_MEMORY_FULL            = 322,
    MM_MESSAGE_ERROR_SMSC_ADDRESS_UNKNOWN   = 330,
    MM_MESSAGE_ERROR_NO_NETWORK             = 331,
    MM_MESSAGE_ERROR_NETWORK_TIMEOUT        = 332,
    MM_MESSAGE_ERROR_NO_CNMA_ACK_EXPECTED   = 340,
    MM_MESSAGE_ERROR_UNKNOWN                = 500
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
    MM_CDMA_ACTIVATION_ERROR_NONE                           = 0,
    MM_CDMA_ACTIVATION_ERROR_UNKNOWN                        = 1,
    MM_CDMA_ACTIVATION_ERROR_ROAMING                        = 2,
    MM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE          = 3,
    MM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT              = 4,
    MM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED = 5,
    MM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED            = 6,
    MM_CDMA_ACTIVATION_ERROR_NO_SIGNAL                      = 7,
    MM_CDMA_ACTIVATION_ERROR_TIMED_OUT                      = 8,
    MM_CDMA_ACTIVATION_ERROR_START_FAILED                   = 9
} MMCdmaActivationError;

#endif /*  _MODEMMANAGER_ERRORS_H_ */

