/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_NOVATEL_GSM_H
#define MM_MODEM_NOVATEL_GSM_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_NOVATEL_GSM            (mm_modem_novatel_gsm_get_type ())
#define MM_MODEM_NOVATEL_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsm))
#define MM_MODEM_NOVATEL_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsmClass))
#define MM_IS_MODEM_NOVATEL_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_NOVATEL_GSM))
#define MM_IS_MODEM_NOVATEL_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_NOVATEL_GSM))
#define MM_MODEM_NOVATEL_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_NOVATEL_GSM, MMModemNovatelGsmClass))

typedef struct {
    MMGenericGsm parent;
} MMModemNovatelGsm;

typedef struct {
    MMGenericGsmClass parent;
} MMModemNovatelGsmClass;

GType mm_modem_novatel_gsm_get_type (void);

MMModem *mm_modem_novatel_gsm_new (const char *data_device,
                                   const char *driver);

#endif /* MM_MODEM_NOVATEL_GSM_H */
