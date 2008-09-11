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
			ENUM_ENTRY (MM_SERIAL_OPEN_FAILED,      "SerialOpenFailed"),
			ENUM_ENTRY (MM_SERIAL_SEND_FAILED,      "SerialSendfailed"),
			ENUM_ENTRY (MM_SERIAL_RESPONSE_TIMEOUT, "SerialResponseTimeout"),
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
            ENUM_ENTRY (MM_MODEM_ERROR_GENERAL,                 "Generial"),
			ENUM_ENTRY (MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED, "OperationNotSupported"),
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
            ENUM_ENTRY (MM_MOBILE_ERROR_EAP_NOT_SUPPORTED,         "EAPMethodNotSupported"),
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
            ENUM_ENTRY (MM_MOBILE_ERROR_GPRS_INVALID_CLASS,        "GprsInvalidclass"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MMMobileError", values);
        g_print ("Is enum? %d %d\n", etype, G_TYPE_IS_ENUM (etype));
	}

	return etype;
}

GError *
mm_mobile_error_for_code (int error_code)
{
    const char *msg;

    switch (error_code) {
    case MM_MOBILE_ERROR_SIM_PIN:
        msg = "PIN code required";
        break;

    case MM_MOBILE_ERROR_PHONE_FAILURE:
    case MM_MOBILE_ERROR_NO_CONNECTION:
    case MM_MOBILE_ERROR_LINK_RESERVED:
    case MM_MOBILE_ERROR_NOT_ALLOWED:
    case MM_MOBILE_ERROR_NOT_SUPPORTED:
    case MM_MOBILE_ERROR_PH_SIM_PIN:
    case MM_MOBILE_ERROR_PH_FSIM_PIN:
    case MM_MOBILE_ERROR_PH_FSIM_PUK:
    case MM_MOBILE_ERROR_SIM_NOT_INSERTED:
    case MM_MOBILE_ERROR_SIM_PUK:
    case MM_MOBILE_ERROR_SIM_FAILURE:
    case MM_MOBILE_ERROR_SIM_BUSY:
    case MM_MOBILE_ERROR_SIM_WRONG:
    case MM_MOBILE_ERROR_WRONG_PASSWORD:
    case MM_MOBILE_ERROR_SIM_PIN2:
    case MM_MOBILE_ERROR_SIM_PUK2:
    case MM_MOBILE_ERROR_MEMORY_FULL:
    case MM_MOBILE_ERROR_INVALID_INDEX:
    case MM_MOBILE_ERROR_NOT_FOUND:
    case MM_MOBILE_ERROR_MEMORY_FAILURE:
    case MM_MOBILE_ERROR_TEXT_TOO_LONG:
    case MM_MOBILE_ERROR_INVALID_CHARS:
    case MM_MOBILE_ERROR_DIAL_STRING_TOO_LONG:
    case MM_MOBILE_ERROR_DIAL_STRING_INVALID:
    case MM_MOBILE_ERROR_NO_NETWORK:
    case MM_MOBILE_ERROR_NETWORK_TIMEOUT:
    case MM_MOBILE_ERROR_NETWORK_NOT_ALLOWED:
    case MM_MOBILE_ERROR_NETWORK_PIN:
    case MM_MOBILE_ERROR_NETWORK_PUK:
    case MM_MOBILE_ERROR_NETWORK_SUBSET_PIN:
    case MM_MOBILE_ERROR_NETWORK_SUBSET_PUK:
    case MM_MOBILE_ERROR_SERVICE_PIN:
    case MM_MOBILE_ERROR_SERVICE_PUK:
    case MM_MOBILE_ERROR_CORP_PIN:
    case MM_MOBILE_ERROR_CORP_PUK:
    case MM_MOBILE_ERROR_HIDDEN_KEY:
    case MM_MOBILE_ERROR_EAP_NOT_SUPPORTED:
    case MM_MOBILE_ERROR_INCORRECT_PARAMS:
    case MM_MOBILE_ERROR_UNKNOWN:
    case MM_MOBILE_ERROR_GPRS_ILLEGAL_MS:
    case MM_MOBILE_ERROR_GPRS_ILLEGAL_ME:
    case MM_MOBILE_ERROR_GPRS_SERVICE_NOT_ALLOWED:
    case MM_MOBILE_ERROR_GPRS_PLMN_NOT_ALLOWED:
    case MM_MOBILE_ERROR_GPRS_LOCATION_NOT_ALLOWED:
    case MM_MOBILE_ERROR_GPRS_ROAMING_NOT_ALLOWED:
    case MM_MOBILE_ERROR_GPRS_OPTION_NOT_SUPPORTED:
    case MM_MOBILE_ERROR_GPRS_NOT_SUBSCRIBED:
    case MM_MOBILE_ERROR_GPRS_OUT_OF_ORDER:
    case MM_MOBILE_ERROR_GPRS_PDP_AUTH_FAILURE:
    case MM_MOBILE_ERROR_GPRS_UNKNOWN:
    case MM_MOBILE_ERROR_GPRS_INVALID_CLASS:
        /* FIXME */
        msg = "Error";
        break;
    default:
        g_warning ("Invalid error code");
        error_code = MM_MOBILE_ERROR_UNKNOWN;
        msg = "Unknown error";
    }

    return g_error_new_literal (MM_MOBILE_ERROR, error_code, msg);
}
