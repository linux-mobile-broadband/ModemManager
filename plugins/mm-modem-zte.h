/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_ZTE_H
#define MM_MODEM_ZTE_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_ZTE            (mm_modem_zte_get_type ())
#define MM_MODEM_ZTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_ZTE, MMModemZte))
#define MM_MODEM_ZTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_ZTE, MMModemZteClass))
#define MM_IS_MODEM_ZTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_ZTE))
#define MM_IS_MODEM_ZTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_ZTE))
#define MM_MODEM_ZTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_ZTE, MMModemZteClass))

typedef struct {
    MMGenericGsm parent;
} MMModemZte;

typedef struct {
    MMGenericGsmClass parent;
} MMModemZteClass;

GType mm_modem_zte_get_type (void);

MMModem *mm_modem_zte_new (const char *data_device,
                           const char *driver);

#endif /* MM_MODEM_ZTE_H */
