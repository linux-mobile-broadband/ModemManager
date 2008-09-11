/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

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
			ENUM_ENTRY (MM_SERIAL_OPEN_FAILED, "Could not open the serial device"),
			ENUM_ENTRY (MM_SERIAL_SEND_FAILED, "Writing to serial device failed"),
			ENUM_ENTRY (MM_SERIAL_RESPONSE_TIMEOUT, "Did not receive response"),
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
            ENUM_ENTRY (MM_MODEM_ERROR_GENERAL, "Unknown error"),
			ENUM_ENTRY (MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED, "Operation not supported"),
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
			ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_CARRIER, "No carrier"),
            ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_DIALTONE, "No dialtone"),
			ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_BUSY, "Busy"),
			ENUM_ENTRY (MM_MODEM_CONNECT_ERROR_NO_ANSWER, "No answer"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MMModemConnectError", values);
	}

	return etype;
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
			ENUM_ENTRY (MM_MOBILE_ERROR_PHONE_FAILURE, "PhoneFailure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NO_CONNECTION, "No connection to phone"),
            ENUM_ENTRY (MM_MOBILE_ERROR_LINK_RESERVED, "Phone-adaptor link reserved"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_ALLOWED, "Operation not allowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_SUPPORTED, "Operation not supported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_SIM_PIN, "PH-SIM PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_FSIM_PIN, "PH-FSIM PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_PH_FSIM_PUK, "PH-FSIM PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_NOT_INSERTED, "SIM not inserted"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PIN, "SIM PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PUK, "SIM PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_FAILURE, "SIM failure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_BUSY, "SIM busy"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_WRONG, "SIM wrong"),
            ENUM_ENTRY (MM_MOBILE_ERROR_WRONG_PASSWORD, "Incorrect password"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PIN2, "SIM PIN2 required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SIM_PUK2, "SIM PUK2 required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_MEMORY_FULL, "Memory full"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INVALID_INDEX, "Invalid index"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NOT_FOUND, "Not found"),
            ENUM_ENTRY (MM_MOBILE_ERROR_MEMORY_FAILURE, "Memory failure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_TEXT_TOO_LONG, "Text string too long"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INVALID_CHARS, "Invalid charactes in text string"),
            ENUM_ENTRY (MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG, "Dial string too long"),
            ENUM_ENTRY (MM_MOBILE_ERROR_DIAL_STRING_INVALID, "Invalid charactes in dial string"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NO_NETWORK, "No network service"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_TIMEOUT, "Network timeout"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED, "Network not allowed - emergency calls only"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_PIN, "Network personalization PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_PUK, "Network personalization PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_SUBSET_PIN, "Network subset personalization PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_NETWORK_SUBSET_PUK, "Network subset personalization PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SERVICE_PIN, "Service provider personalization PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_SERVICE_PUK, "Service provider personalization PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_CORP_PIN, "Corporate personalization PIN required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_CORP_PUK, "Corporate personalization PUK required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_HIDDEN_KEY, "Hidden key required"),
            ENUM_ENTRY (MM_MOBILE_ERROR_EAP_NOT_SUPPORTED, "EAP method not supported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_INCORRECT_PARAMS, "Incorrect parameters"),
            ENUM_ENTRY (MM_MOBILE_ERROR_UNKNOWN, "Unknown"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ILLEGAL_MS, "Illegal MS"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ILLEGAL_ME, "Illegal ME"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED, "GPRS services not allowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED, "PLMN not allowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED, "Location area not allowed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED, "Roaming not allowed in this location area"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED, "Service option not supported"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED, "Requested service option not subscribed"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER, "Service option temporarily out of order"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE, "PDP authentication failure"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_UNKNOWN, "Unspecified GPRS error"),
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_INVALID_CLASS, "Invalid mobile class"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MMMobileError", values);
        g_print ("Is enum? %d %d\n", etype, G_TYPE_IS_ENUM (etype));
	}

	return etype;
}
