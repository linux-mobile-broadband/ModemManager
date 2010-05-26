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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MODEM_ERROR_H
#define MM_MODEM_ERROR_H

#include <glib-object.h>

enum {
    MM_SERIAL_ERROR_OPEN_FAILED = 0,
    MM_SERIAL_ERROR_SEND_FAILED = 1,
    MM_SERIAL_ERROR_RESPONSE_TIMEOUT = 2,
    MM_SERIAL_ERROR_OPEN_FAILED_NO_DEVICE = 3,
    MM_SERIAL_ERROR_FLASH_FAILED = 4,
    MM_SERIAL_ERROR_NOT_OPEN = 5,
};

#define MM_SERIAL_ERROR (mm_serial_error_quark ())
#define MM_TYPE_SERIAL_ERROR (mm_serial_error_get_type ())

GQuark mm_serial_error_quark    (void);
GType  mm_serial_error_get_type (void);


enum {
    MM_MODEM_ERROR_GENERAL = 0,
    MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED = 1,
    MM_MODEM_ERROR_CONNECTED = 2,
    MM_MODEM_ERROR_DISCONNECTED = 3,
    MM_MODEM_ERROR_OPERATION_IN_PROGRESS = 4,
    MM_MODEM_ERROR_REMOVED = 5,
    MM_MODEM_ERROR_AUTHORIZATION_REQUIRED = 6,
    MM_MODEM_ERROR_UNSUPPORTED_CHARSET = 7
};

#define MM_MODEM_ERROR (mm_modem_error_quark ())
#define MM_TYPE_MODEM_ERROR (mm_modem_error_get_type ())

GQuark mm_modem_error_quark    (void);
GType  mm_modem_error_get_type (void);


enum {
    MM_MODEM_CONNECT_ERROR_NO_CARRIER = 3,
    MM_MODEM_CONNECT_ERROR_NO_DIALTONE = 6,
    MM_MODEM_CONNECT_ERROR_BUSY = 7,
    MM_MODEM_CONNECT_ERROR_NO_ANSWER = 8,
};

#define MM_MODEM_CONNECT_ERROR (mm_modem_connect_error_quark ())
#define MM_TYPE_MODEM_CONNECT_ERROR (mm_modem_connect_error_get_type ())

GQuark  mm_modem_connect_error_quark    (void);
GType   mm_modem_connect_error_get_type (void);
GError *mm_modem_connect_error_for_code (int error_code);


/* 3GPP TS 07.07 version 7.8.0 Release 1998 (page 90) ETSI TS 100 916 V7.8.0 (2003-03) */
enum {
    MM_MOBILE_ERROR_PHONE_FAILURE = 0,
    MM_MOBILE_ERROR_NO_CONNECTION = 1,
    MM_MOBILE_ERROR_LINK_RESERVED = 2,
    MM_MOBILE_ERROR_NOT_ALLOWED = 3,
    MM_MOBILE_ERROR_NOT_SUPPORTED = 4,
    MM_MOBILE_ERROR_PH_SIM_PIN = 5,
    MM_MOBILE_ERROR_PH_FSIM_PIN = 6,
    MM_MOBILE_ERROR_PH_FSIM_PUK = 7,
    MM_MOBILE_ERROR_SIM_NOT_INSERTED = 10,
    MM_MOBILE_ERROR_SIM_PIN = 11,
    MM_MOBILE_ERROR_SIM_PUK = 12,
    MM_MOBILE_ERROR_SIM_FAILURE = 13,
    MM_MOBILE_ERROR_SIM_BUSY = 14,
    MM_MOBILE_ERROR_SIM_WRONG = 15,
    MM_MOBILE_ERROR_WRONG_PASSWORD = 16,
    MM_MOBILE_ERROR_SIM_PIN2 = 17,
    MM_MOBILE_ERROR_SIM_PUK2 = 18,
    MM_MOBILE_ERROR_MEMORY_FULL = 20,
    MM_MOBILE_ERROR_INVALID_INDEX = 21,
    MM_MOBILE_ERROR_NOT_FOUND = 22,
    MM_MOBILE_ERROR_MEMORY_FAILURE = 23,
    MM_MOBILE_ERROR_TEXT_TOO_LONG = 24,
    MM_MOBILE_ERROR_INVALID_CHARS = 25,
    MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG = 26,
    MM_MOBILE_ERROR_DIAL_STRING_INVALID = 27,
    MM_MOBILE_ERROR_NO_NETWORK = 30,
    MM_MOBILE_ERROR_NETWORK_TIMEOUT = 31,
    MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED = 32,
    MM_MOBILE_ERROR_NETWORK_PIN = 40,
    MM_MOBILE_ERROR_NETWORK_PUK = 41,
    MM_MOBILE_ERROR_NETWORK_SUBSET_PIN = 42,
    MM_MOBILE_ERROR_NETWORK_SUBSET_PUK = 43,
    MM_MOBILE_ERROR_SERVICE_PIN = 44,
    MM_MOBILE_ERROR_SERVICE_PUK = 45,
    MM_MOBILE_ERROR_CORP_PIN = 46,
    MM_MOBILE_ERROR_CORP_PUK = 47,
    MM_MOBILE_ERROR_HIDDEN_KEY = 48,
    MM_MOBILE_ERROR_EAP_NOT_SUPPORTED = 49,
    MM_MOBILE_ERROR_INCORRECT_PARAMS = 50,
    MM_MOBILE_ERROR_UNKNOWN = 100,

    MM_MOBILE_ERROR_GPRS_ILLEGAL_MS = 103,
    MM_MOBILE_ERROR_GPRS_ILLEGAL_ME = 106,
    MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED = 107,
    MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED = 111,
    MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED = 112,
    MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED = 113,
    MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED = 132,
    MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED = 133,
    MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER = 134,
    MM_MOBILE_ERROR_GPRS_UNKNOWN = 148,
    MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE = 149,
    MM_MOBILE_ERROR_GPRS_INVALID_CLASS = 150
};


#define MM_MOBILE_ERROR (mm_mobile_error_quark ())
#define MM_TYPE_MOBILE_ERROR (mm_mobile_error_get_type ())

GQuark  mm_mobile_error_quark    (void);
GType   mm_mobile_error_get_type (void);
GError *mm_mobile_error_for_code (int error_code);
GError *mm_mobile_error_for_string (const char *str);

#endif /* MM_MODEM_ERROR_H */
