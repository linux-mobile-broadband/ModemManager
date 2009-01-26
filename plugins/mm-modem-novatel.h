/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_NOVATEL_H
#define MM_MODEM_NOVATEL_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_NOVATEL            (mm_modem_novatel_get_type ())
#define MM_MODEM_NOVATEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_NOVATEL, MMModemNovatel))
#define MM_MODEM_NOVATEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_NOVATEL, MMModemNovatelClass))
#define MM_IS_MODEM_NOVATEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_NOVATEL))
#define MM_IS_MODEM_NOVATEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_NOVATEL))
#define MM_MODEM_NOVATEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_NOVATEL, MMModemNovatelClass))

typedef struct {
    MMGenericGsm parent;
} MMModemNovatel;

typedef struct {
    MMGenericGsmClass parent;
} MMModemNovatelClass;

GType mm_modem_novatel_get_type (void);

MMModem *mm_modem_novatel_new (const char *data_device,
                               const char *driver);

#endif /* MM_MODEM_NOVATEL_H */
