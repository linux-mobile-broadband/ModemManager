/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_SIERRA_H
#define MM_MODEM_SIERRA_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_SIERRA            (mm_modem_sierra_get_type ())
#define MM_MODEM_SIERRA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SIERRA, MMModemSierra))
#define MM_MODEM_SIERRA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_SIERRA, MMModemSierraClass))
#define MM_IS_MODEM_SIERRA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SIERRA))
#define MM_IS_MODEM_SIERRA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_SIERRA))
#define MM_MODEM_SIERRA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_SIERRA, MMModemSierraClass))

typedef struct {
    MMGenericGsm parent;
} MMModemSierra;

typedef struct {
    MMGenericGsmClass parent;
} MMModemSierraClass;

GType mm_modem_sierra_get_type (void);

MMModem *mm_modem_sierra_new (const char *data_device,
                              const char *driver);

#endif /* MM_MODEM_SIERRA_H */
