/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_GSM_CARD_H
#define MM_MODEM_GSM_CARD_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_GSM_CARD      (mm_modem_gsm_card_get_type ())
#define MM_MODEM_GSM_CARD(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_CARD, MMModemGsmCard))
#define MM_IS_MODEM_GSM_CARD(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_CARD))
#define MM_MODEM_GSM_CARD_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_CARD, MMModemGsmCard))

typedef struct _MMModemGsmCard MMModemGsmCard;

typedef void (*MMModemGsmCardInfoFn) (MMModemGsmCard *self,
                                      const char *manufacturer,
                                      const char *model,
                                      const char *version,
                                      GError *error,
                                      gpointer user_data);

struct _MMModemGsmCard {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_imei) (MMModemGsmCard *self,
                      MMModemStringFn callback,
                      gpointer user_data);

    void (*get_imsi) (MMModemGsmCard *self,
                      MMModemStringFn callback,
                      gpointer user_data);

    void (*get_info) (MMModemGsmCard *self,
                      MMModemGsmCardInfoFn callback,
                      gpointer user_data);

    void (*send_puk) (MMModemGsmCard *self,
                      const char *puk,
                      const char *pin,
                      MMModemFn callback,
                      gpointer user_data);
    
    void (*send_pin) (MMModemGsmCard *self,
                      const char *pin,
                      MMModemFn callback,
                      gpointer user_data);

    void (*enable_pin) (MMModemGsmCard *self,
                        const char *pin,
                        gboolean enabled,
                        MMModemFn callback,
                        gpointer user_data);

    void (*change_pin) (MMModemGsmCard *self,
                        const char *old_pin,
                        const char *new_pin,
                        MMModemFn callback,
                        gpointer user_data);
};

GType mm_modem_gsm_card_get_type (void);

void mm_modem_gsm_card_get_imei (MMModemGsmCard *self,
                                 MMModemStringFn callback,
                                 gpointer user_data);

void mm_modem_gsm_card_get_imsi (MMModemGsmCard *self,
                                 MMModemStringFn callback,
                                 gpointer user_data);

void mm_modem_gsm_card_get_info (MMModemGsmCard *self,
                                 MMModemGsmCardInfoFn callback,
                                 gpointer user_data);

void mm_modem_gsm_card_send_puk (MMModemGsmCard *self,
                                 const char *puk,
                                 const char *pin,
                                 MMModemFn callback,
                                 gpointer user_data);

void mm_modem_gsm_card_send_pin (MMModemGsmCard *self,
                                 const char *pin,
                                 MMModemFn callback,
                                 gpointer user_data);

void mm_modem_gsm_card_enable_pin (MMModemGsmCard *self,
                                   const char *pin,
                                   gboolean enabled,
                                   MMModemFn callback,
                                   gpointer user_data);

void mm_modem_gsm_card_change_pin (MMModemGsmCard *self,
                                   const char *old_pin,
                                   const char *new_pin,
                                   MMModemFn callback,
                                   gpointer user_data);

#endif /* MM_MODEM_GSM_CARD_H */
