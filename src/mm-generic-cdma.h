/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_GENERIC_CDMA_H
#define MM_GENERIC_CDMA_H

#include "mm-modem.h"
#include "mm-modem-base.h"

#define MM_TYPE_GENERIC_CDMA            (mm_generic_cdma_get_type ())
#define MM_GENERIC_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GENERIC_CDMA, MMGenericCdma))
#define MM_GENERIC_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))
#define MM_IS_GENERIC_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GENERIC_CDMA))
#define MM_IS_GENERIC_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GENERIC_CDMA))
#define MM_GENERIC_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GENERIC_CDMA, MMGenericCdmaClass))

typedef struct {
    MMModemBase parent;
} MMGenericCdma;

typedef struct {
    MMModemBaseClass parent;
} MMGenericCdmaClass;

GType mm_generic_cdma_get_type (void);

MMModem *mm_generic_cdma_new (const char *device,
                              const char *driver,
                              const char *plugin);

#endif /* MM_GENERIC_CDMA_H */
