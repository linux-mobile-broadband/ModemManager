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
 * Copyright (C) 2019 James Wah
 * Copyright (C) 2020 Marinus Enzinger <marinus@enzingerm.de>
 * Copyright (C) 2023 Shane Parslow
 * Copyright (c) 2024 Thomas Vogt
 */

#ifndef MM_BROADBAND_MODEM_XMM7360_H
#define MM_BROADBAND_MODEM_XMM7360_H

#include "mm-broadband-modem.h"
#include "mm-port-serial-xmmrpc-xmm7360.h"

#define MM_TYPE_BROADBAND_MODEM_XMM7360            (mm_broadband_modem_xmm7360_get_type ())
#define MM_BROADBAND_MODEM_XMM7360(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_XMM7360, MMBroadbandModemXmm7360))
#define MM_BROADBAND_MODEM_XMM7360_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_XMM7360, MMBroadbandModemXmm7360Class))
#define MM_IS_BROADBAND_MODEM_XMM7360(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_XMM7360))
#define MM_IS_BROADBAND_MODEM_XMM7360_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_XMM7360))
#define MM_BROADBAND_MODEM_XMM7360_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_XMM7360, MMBroadbandModemXmm7360Class))

typedef struct _MMBroadbandModemXmm7360 MMBroadbandModemXmm7360;
typedef struct _MMBroadbandModemXmm7360Class MMBroadbandModemXmm7360Class;
typedef struct _MMBroadbandModemXmm7360Private MMBroadbandModemXmm7360Private;

struct _MMBroadbandModemXmm7360 {
    MMBroadbandModem parent;
    MMBroadbandModemXmm7360Private *priv;
};

struct _MMBroadbandModemXmm7360Class{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_xmm7360_get_type (void);

MMBroadbandModemXmm7360 *mm_broadband_modem_xmm7360_new (const gchar  *device,
                                                         const gchar  *physdev,
                                                         const gchar **driver,
                                                         const gchar  *plugin,
                                                         guint16       vendor_id,
                                                         guint16       product_id);

MMPortSerialXmmrpcXmm7360 *mm_broadband_modem_xmm7360_get_port_xmmrpc (MMBroadbandModemXmm7360 *self);
MMPortSerialXmmrpcXmm7360 *mm_broadband_modem_xmm7360_peek_port_xmmrpc (MMBroadbandModemXmm7360 *self);

void mm_broadband_modem_xmm7360_check_fcc_lock (MMBroadbandModemXmm7360   *self,
                                                GAsyncReadyCallback        callback,
                                                gpointer                   user_data,
                                                MMPortSerialXmmrpcXmm7360 *port,
                                                gboolean                   unlock);

gboolean mm_broadband_modem_xmm7360_check_fcc_lock_finish (MMBroadbandModemXmm7360  *self,
                                                           GAsyncResult             *res,
                                                           GError                  **error);

void mm_broadband_modem_xmm7360_set_unlock_retries (MMBroadbandModemXmm7360 *self,
                                                    MMModemLock              lock_type,
                                                    guint32                  remaining_attempts);

#endif /* MM_BROADBAND_MODEM_XMM7360_H */
