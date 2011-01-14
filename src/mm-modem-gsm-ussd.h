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
 * Copyright (C) 2010 Guido Guenther <agx@sigxcpu.org>
 */

#ifndef MM_MODEM_GSM_USSD_H
#define MM_MODEM_GSM_USSD_H

#include <mm-modem.h>

typedef enum {
    MM_MODEM_GSM_USSD_STATE_IDLE          = 0x00000000,
    MM_MODEM_GSM_USSD_STATE_ACTIVE        = 0x00000001,
    MM_MODEM_GSM_USSD_STATE_USER_RESPONSE = 0x00000002,
} MMModemGsmUssdState;

#define MM_MODEM_GSM_USSD_STATE                "ussd-state"
#define MM_MODEM_GSM_USSD_NETWORK_NOTIFICATION "network-notification"
#define MM_MODEM_GSM_USSD_NETWORK_REQUEST      "network-request"

#define MM_TYPE_MODEM_GSM_USSD      (mm_modem_gsm_ussd_get_type ())
#define MM_MODEM_GSM_USSD(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_GSM_USSD, MMModemGsmUssd))
#define MM_IS_MODEM_GSM_USSD(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_GSM_USSD))
#define MM_MODEM_GSM_USSD_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_GSM_USSD, MMModemGsmUssd))

#define MM_MODEM_GSM_USSD_DBUS_INTERFACE "org.freedesktop.ModemManager.Modem.Gsm.Ussd"

typedef struct _MMModemGsmUssd MMModemGsmUssd;

struct _MMModemGsmUssd {
    GTypeInterface g_iface;

    /* Methods */
    void (*initiate) (MMModemGsmUssd *modem,
                      const char *command,
                      MMModemStringFn callback,
                      gpointer user_data);

    void (*respond) (MMModemGsmUssd *modem,
                     const char *command,
                     MMModemStringFn callback,
                     gpointer user_data);

    void (*cancel) (MMModemGsmUssd *modem,
                    MMModemFn callback,
                    gpointer user_data);
};

GType mm_modem_gsm_ussd_get_type (void);

void mm_modem_gsm_ussd_initiate (MMModemGsmUssd *self,
                                 const char *command,
                                 MMModemStringFn callback,
                                 gpointer user_data);

void mm_modem_gsm_ussd_respond (MMModemGsmUssd *self,
                                const char *command,
                                MMModemStringFn callback,
                                gpointer user_data);

void mm_modem_gsm_ussd_cancel (MMModemGsmUssd *self,
                               MMModemFn callback,
                               gpointer user_data);

#endif /* MM_MODEM_GSM_USSD_H */
