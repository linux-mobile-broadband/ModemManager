/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_H
#define MM_MODEM_H

#include <glib-object.h>

#define MM_TYPE_MODEM      (mm_modem_get_type ())
#define MM_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM, MMModem))
#define MM_IS_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM))
#define MM_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM, MMModem))

#define MM_MODEM_DATA_DEVICE "data-device"
#define MM_MODEM_DRIVER      "driver"
#define MM_MODEM_TYPE        "type"

#define MM_MODEM_TYPE_GSM  1
#define MM_MODEM_TYPE_CDMA 2

typedef enum {
    MM_MODEM_PROP_FIRST = 0x1000,

    MM_MODEM_PROP_DATA_DEVICE = MM_MODEM_PROP_FIRST,
    MM_MODEM_PROP_DRIVER,
    MM_MODEM_PROP_TYPE
} MMModemProp;

typedef struct _MMModem MMModem;

typedef void (*MMModemFn) (MMModem *modem,
                           GError *error,
                           gpointer user_data);

typedef void (*MMModemUIntFn) (MMModem *modem,
                               guint32 result,
                               GError *error,
                               gpointer user_data);

struct _MMModem {
    GTypeInterface g_iface;

    /* Methods */
    void (*enable) (MMModem *self,
                    gboolean enable,
                    MMModemFn callback,
                    gpointer user_data);

    void (*connect) (MMModem *self,
                     const char *number,
                     MMModemFn callback,
                     gpointer user_data);

    void (*disconnect) (MMModem *self,
                        MMModemFn callback,
                        gpointer user_data);
};

GType mm_modem_get_type (void);

void mm_modem_enable (MMModem *self,
                      gboolean enable,
                      MMModemFn callback,
                      gpointer user_data);

void mm_modem_connect (MMModem *self,
                       const char *number,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_disconnect (MMModem *self,
                          MMModemFn callback,
                          gpointer user_data);

#endif  /* MM_MODEM_H */
