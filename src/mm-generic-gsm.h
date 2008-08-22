/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_GENERIC_GSM_H
#define MM_GENERIC_GSM_H

#include "mm-gsm-modem.h"
#include "mm-serial.h"

#define MM_TYPE_GENERIC_GSM			(mm_generic_gsm_get_type ())
#define MM_GENERIC_GSM(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_GSM, MMGenericGsm))
#define MM_GENERIC_GSM_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))
#define MM_IS_GENERIC_GSM(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_GSM))
#define MM_IS_GENERIC_GSM_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_GSM))
#define MM_GENERIC_GSM_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_GSM, MMGenericGsmClass))

typedef struct {
    MMSerial parent;
} MMGenericGsm;

typedef struct {
    MMSerialClass parent;
} MMGenericGsmClass;

GType mm_generic_gsm_get_type (void);

MMModem *mm_generic_gsm_new (const char *serial_device,
                             const char *driver);

guint32 mm_generic_gsm_get_cid (MMGenericGsm *modem);
void mm_generic_gsm_set_reg_status (MMGenericGsm *modem,
                                    MMGsmModemRegStatus status);

void mm_generic_gsm_set_operator (MMGenericGsm *modem,
                                  const char *code,
                                  const char *name);

#endif /* MM_GENERIC_GSM_H */
