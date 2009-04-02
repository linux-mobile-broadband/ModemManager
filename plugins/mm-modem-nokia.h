/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_NOKIA_H
#define MM_MODEM_NOKIA_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_NOKIA            (mm_modem_nokia_get_type ())
#define MM_MODEM_NOKIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_NOKIA, MMModemNokia))
#define MM_MODEM_NOKIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_NOKIA, MMModemNokiaClass))
#define MM_IS_MODEM_NOKIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_NOKIA))
#define MM_IS_MODEM_NOKIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_NOKIA))
#define MM_MODEM_NOKIA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_NOKIA, MMModemNokiaClass))

typedef struct {
    MMGenericGsm parent;
} MMModemNokia;

typedef struct {
    MMGenericGsmClass parent;
} MMModemNokiaClass;

GType mm_modem_nokia_get_type (void);

MMModem *mm_modem_nokia_new (const char *data_device,
                             const char *driver);

#endif /* MM_MODEM_NOKIA_H */
