/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "mm-modem-error.h"

GQuark
mm_modem_error_quark (void)
{
	static GQuark ret = 0;

	if (ret == 0)
		ret = g_quark_from_static_string ("mm_modem_error");

	return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
mm_modem_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (MM_MODEM_ERROR_GENERAL, "GeneralError"),
            ENUM_ENTRY (MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED, "OperationNotSupported"),
			ENUM_ENTRY (MM_MODEM_ERROR_PIN_NEEDED, "PINNeeded"),
			ENUM_ENTRY (MM_MODEM_ERROR_PUK_NEEDED, "PUKNeeded"),
			ENUM_ENTRY (MM_MODEM_ERROR_INVALID_SECRET, "InvalidSecret"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MMModemError", values);
	}

	return etype;
}
