/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_HSO_H
#define MM_MODEM_HSO_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_HSO			(mm_modem_hso_get_type ())
#define MM_MODEM_HSO(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_HSO, MMModemHso))
#define MM_MODEM_HSO_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_HSO, MMModemHsoClass))
#define MM_IS_MODEM_HSO(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_HSO))
#define MM_IS_MODEM_HSO_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_HSO))
#define MM_MODEM_HSO_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_HSO, MMModemHsoClass))

typedef struct {
    MMGenericGsm parent;
} MMModemHso;

typedef struct {
    MMGenericGsmClass parent;
} MMModemHsoClass;

GType mm_modem_hso_get_type (void);

MMModem *mm_modem_hso_new (const char *serial_device,
                           const char *network_device,
                           const char *driver);

void mm_hso_modem_authenticate (MMModemHso *self,
                                const char *username,
                                const char *password,
                                MMModemFn callback,
                                gpointer user_data);

#endif /* MM_MODEM_HSO_H */
