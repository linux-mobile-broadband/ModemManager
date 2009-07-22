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

#ifndef MM_MODEM_CDMA_H
#define MM_MODEM_CDMA_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_CDMA      (mm_modem_cdma_get_type ())
#define MM_MODEM_CDMA(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_CDMA, MMModemCdma))
#define MM_IS_MODEM_CDMA(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_CDMA))
#define MM_MODEM_CDMA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_CDMA, MMModemCdma))

typedef struct _MMModemCdma MMModemCdma;

typedef void (*MMModemCdmaServingSystemFn) (MMModemCdma *modem,
                                            guint32 class,
                                            unsigned char band,
                                            guint32 sid,
                                            GError *error,
                                            gpointer user_data);

struct _MMModemCdma {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_signal_quality) (MMModemCdma *self,
                                MMModemUIntFn callback,
                                gpointer user_data);

    void (*get_esn) (MMModemCdma *self,
                     MMModemStringFn callback,
                     gpointer user_data);

    void (*get_serving_system) (MMModemCdma *self,
                                MMModemCdmaServingSystemFn callback,
                                gpointer user_data);

    /* Signals */
    void (*signal_quality) (MMModemCdma *self,
                            guint32 quality);
};

GType mm_modem_cdma_get_type (void);

void mm_modem_cdma_get_signal_quality (MMModemCdma *self,
                                       MMModemUIntFn callback,
                                       gpointer user_data);

void mm_modem_cdma_get_esn (MMModemCdma *self,
                            MMModemStringFn callback,
                            gpointer user_data);

void mm_modem_cdma_get_serving_system (MMModemCdma *self,
                                       MMModemCdmaServingSystemFn callback,
                                       gpointer user_data);

/* Protected */

void mm_modem_cdma_signal_quality (MMModemCdma *self,
                                   guint32 quality);

#endif  /* MM_MODEM_CDMA_H */
