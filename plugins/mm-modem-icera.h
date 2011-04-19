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
 * Copyright (C) 2010 Red Hat, Inc.
 */

/******************************************
 * Generic utilities for Icera-based modems
 ******************************************/

#ifndef MM_MODEM_ICERA_H
#define MM_MODEM_ICERA_H

#include <glib-object.h>

#include "mm-modem-gsm.h"
#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_ICERA               (mm_modem_icera_get_type ())
#define MM_MODEM_ICERA(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_ICERA, MMModemIcera))
#define MM_IS_MODEM_ICERA(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_ICERA))
#define MM_MODEM_ICERA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_ICERA, MMModemIcera))

typedef struct _MMModemIceraPrivate MMModemIceraPrivate;

typedef struct _MMModemIcera MMModemIcera;

struct _MMModemIcera {
    GTypeInterface g_iface;

    /* Returns the implementing object's pointer to an internal
     * MMModemIceraPrivate pointer.
     */
    MMModemIceraPrivate * (*get_private) (MMModemIcera *icera);
};

GType mm_modem_icera_get_type (void);

MMModemIceraPrivate *mm_modem_icera_init_private (void);
void mm_modem_icera_dispose_private (MMModemIcera *self);

void mm_modem_icera_cleanup (MMModemIcera *self);

void mm_modem_icera_get_allowed_mode (MMModemIcera *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data);

void mm_modem_icera_set_allowed_mode (MMModemIcera *self,
                                      MMModemGsmAllowedMode mode,
                                      MMModemFn callback,
                                      gpointer user_data);

void mm_modem_icera_register_unsolicted_handlers (MMModemIcera *self,
                                                  MMAtSerialPort *port);

void mm_modem_icera_change_unsolicited_messages (MMModemIcera *self,
                                                 gboolean enabled);

void mm_modem_icera_get_access_technology (MMModemIcera *self,
                                           MMModemUIntFn callback,
                                           gpointer user_data);

void mm_modem_icera_is_icera (MMModemIcera *self,
                              MMModemUIntFn callback,
                              gpointer user_data);

void mm_modem_icera_do_disconnect (MMGenericGsm *gsm,
                                   gint cid,
                                   MMModemFn callback,
                                   gpointer user_data);

void mm_modem_icera_simple_connect (MMModemIcera *self, GHashTable *properties);

void mm_modem_icera_do_connect (MMModemIcera *self,
                                const char *number,
                                MMModemFn callback,
                                gpointer user_data);

void mm_modem_icera_get_ip4_config (MMModemIcera *self,
                                    MMModemIp4Fn callback,
                                    gpointer user_data);

#endif  /* MM_MODEM_ICERA_H */

