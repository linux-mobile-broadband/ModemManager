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
 * Copyright (C) 2009 Novell, Inc.
 */

#ifndef MM_MODEM_SIMPLE_H
#define MM_MODEM_SIMPLE_H

#include <glib-object.h>
#include <mm-modem.h>

#define MM_TYPE_MODEM_SIMPLE      (mm_modem_simple_get_type ())
#define MM_MODEM_SIMPLE(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_SIMPLE, MMModemSimple))
#define MM_IS_MODEM_SIMPLE(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_SIMPLE))
#define MM_MODEM_SIMPLE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_SIMPLE, MMModemSimple))

typedef struct _MMModemSimple MMModemSimple;

typedef void (*MMModemSimpleGetStatusFn) (MMModemSimple *modem,
                                          GHashTable *properties,
                                          GError *error,
                                          gpointer user_data);

struct _MMModemSimple {
    GTypeInterface g_iface;

    /* Methods */
    void (*connect) (MMModemSimple *self,
                     GHashTable *properties,
                     MMModemFn callback,
                     gpointer user_data);

    void (*get_status) (MMModemSimple *self,
                        MMModemSimpleGetStatusFn callback,
                        gpointer user_data);
};

GType mm_modem_simple_get_type (void);

void mm_modem_simple_connect (MMModemSimple *self,
                              GHashTable *properties,
                              MMModemFn callback,
                              gpointer user_data);

void mm_modem_simple_get_status (MMModemSimple *self,
                                 MMModemSimpleGetStatusFn callback,
                                 gpointer user_data);

#endif /* MM_MODEM_SIMPLE_H */
