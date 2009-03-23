/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_GSM_SMS_H
#define MM_MODEM_GSM_SMS_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_GSM_SMS      (mm_modem_gsm_sms_get_type ())
#define MM_MODEM_GSM_SMS(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_SMS, MMModemGsmSms))
#define MM_IS_MODEM_GSM_SMS(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_SMS))
#define MM_MODEM_GSM_SMS_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_SMS, MMModemGsmSms))

typedef struct _MMModemGsmSms MMModemGsmSms;

struct _MMModemGsmSms {
    GTypeInterface g_iface;

    /* Methods */
    void (*send) (MMModemGsmSms *modem,
                  const char *number,
                  const char *text,
                  const char *smsc,
                  guint validity,
                  guint class,
                  MMModemFn callback,
                  gpointer user_data);

    /* Signals */
    void (*sms_received) (MMModemGsmSms *self,
                          guint32 index);

};

GType mm_modem_gsm_sms_get_type (void);

void mm_modem_gsm_sms_send (MMModemGsmSms *self,
                            const char *number,
                            const char *text,
                            const char *smsc,
                            guint validity,
                            guint class,
                            MMModemFn callback,
                            gpointer user_data);

#endif /* MM_MODEM_GSM_SMS_H */
