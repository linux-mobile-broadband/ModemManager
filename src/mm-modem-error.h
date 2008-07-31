/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_ERROR_H
#define MM_MODEM_ERROR_H

#include <glib-object.h>

enum {
	MM_MODEM_ERROR_GENERAL = 0,
    MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
	MM_MODEM_ERROR_PIN_NEEDED,
	MM_MODEM_ERROR_PUK_NEEDED,
	MM_MODEM_ERROR_INVALID_SECRET
};

#define MM_MODEM_ERROR (mm_modem_error_quark ())
#define MM_TYPE_MODEM_ERROR (mm_modem_error_get_type ())

GQuark mm_modem_error_quark    (void);
GType  mm_modem_error_get_type (void);

#endif /* MM_MODEM_ERROR_H */
