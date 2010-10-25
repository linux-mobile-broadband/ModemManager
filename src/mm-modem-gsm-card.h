/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MODEM_GSM_CARD_H
#define MM_MODEM_GSM_CARD_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_GSM_CARD      (mm_modem_gsm_card_get_type ())
#define MM_MODEM_GSM_CARD(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_CARD, MMModemGsmCard))
#define MM_IS_MODEM_GSM_CARD(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_CARD))
#define MM_MODEM_GSM_CARD_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_CARD, MMModemGsmCard))

#define MM_MODEM_GSM_CARD_SUPPORTED_BANDS "supported-bands"
#define MM_MODEM_GSM_CARD_SUPPORTED_MODES "supported-modes"
#define MM_MODEM_GSM_CARD_SIM_IDENTIFIER  "sim-identifier"

#define MM_MODEM_GSM_CARD_SIM_PIN "sim-pin"
#define MM_MODEM_GSM_CARD_SIM_PIN2 "sim-pin2"
#define MM_MODEM_GSM_CARD_SIM_PUK "sim-puk"
#define MM_MODEM_GSM_CARD_SIM_PUK2 "sim-puk2"

#define MM_MODEM_GSM_CARD_UNLOCK_RETRIES_NOT_SUPPORTED 999

typedef struct _MMModemGsmCard MMModemGsmCard;

struct _MMModemGsmCard {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_imei) (MMModemGsmCard *self,
                      MMModemStringFn callback,
                      gpointer user_data);

    void (*get_imsi) (MMModemGsmCard *self,
                      MMModemStringFn callback,
                      gpointer user_data);

    void (*get_unlock_retries) (MMModemGsmCard *self,
                              const char *pin_type,
                              MMModemUIntFn callback,
                              gpointer user_data);

    void (*get_operator_id) (MMModemGsmCard *self,
                             MMModemStringFn callback,
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

void mm_modem_gsm_card_get_unlock_retries (MMModemGsmCard *self,
                                           const char *pin_type,
                                           MMModemUIntFn callback,
                                           gpointer user_data);

void mm_modem_gsm_card_get_operator_id (MMModemGsmCard *self,
                                        MMModemStringFn callback,
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
