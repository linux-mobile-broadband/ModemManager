/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_NOVATEL_CDMA_H
#define MM_MODEM_NOVATEL_CDMA_H

#include "mm-generic-cdma.h"

#define MM_TYPE_MODEM_NOVATEL_CDMA            (mm_modem_novatel_cdma_get_type ())
#define MM_MODEM_NOVATEL_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_NOVATEL_CDMA, MMModemNovatelCdma))
#define MM_MODEM_NOVATEL_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_NOVATEL_CDMA, MMModemNovatelCdmaClass))
#define MM_IS_MODEM_NOVATEL_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_NOVATEL_CDMA))
#define MM_IS_MODEM_NOVATEL_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_NOVATEL_CDMA))
#define MM_MODEM_NOVATEL_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_NOVATEL_CDMA, MMModemNovatelCdmaClass))

typedef struct {
    MMGenericCdma parent;
} MMModemNovatelCdma;

typedef struct {
    MMGenericCdmaClass parent;
} MMModemNovatelCdmaClass;

GType mm_modem_novatel_cdma_get_type (void);

MMModem *mm_modem_novatel_cdma_new (const char *data_device,
                                    const char *driver);

#endif /* MM_MODEM_NOVATEL_CDMA_H */
