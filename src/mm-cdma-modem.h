/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_CDMA_MODEM_H
#define MM_CDMA_MODEM_H

#include <mm-modem.h>

#define MM_TYPE_CDMA_MODEM      (mm_cdma_modem_get_type ())
#define MM_CDMA_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CDMA_MODEM, MMCdmaModem))
#define MM_IS_CDMA_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CDMA_MODEM))
#define MM_CDMA_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_CDMA_MODEM, MMCdmaModem))

typedef struct _MMCdmaModem MMCdmaModem;

struct _MMCdmaModem {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_signal_quality) (MMCdmaModem *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMCdmaModem *self,
                            guint32 quality);
};

GType mm_cdma_modem_get_type (void);

void mm_cdma_modem_get_signal_quality (MMCdmaModem *self,
                                       MMModemUIntFn callback,
                                       gpointer user_data);

/* Protected */

void mm_cdma_modem_signal_quality (MMCdmaModem *self,
                                   guint32 quality);

#endif  /* MM_CDMA_MODEM_H */
