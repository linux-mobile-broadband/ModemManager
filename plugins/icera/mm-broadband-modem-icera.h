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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BROADBAND_MODEM_ICERA_H
#define MM_BROADBAND_MODEM_ICERA_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_ICERA            (mm_broadband_modem_icera_get_type ())
#define MM_BROADBAND_MODEM_ICERA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_ICERA, MMBroadbandModemIcera))
#define MM_BROADBAND_MODEM_ICERA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_ICERA, MMBroadbandModemIceraClass))
#define MM_IS_BROADBAND_MODEM_ICERA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_ICERA))
#define MM_IS_BROADBAND_MODEM_ICERA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_ICERA))
#define MM_BROADBAND_MODEM_ICERA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_ICERA, MMBroadbandModemIceraClass))

#define MM_BROADBAND_MODEM_ICERA_DEFAULT_IP_METHOD "broadband-modem-icera-default-ip-method"

typedef struct _MMBroadbandModemIcera MMBroadbandModemIcera;
typedef struct _MMBroadbandModemIceraClass MMBroadbandModemIceraClass;
typedef struct _MMBroadbandModemIceraPrivate MMBroadbandModemIceraPrivate;

struct _MMBroadbandModemIcera {
    MMBroadbandModem parent;
    MMBroadbandModemIceraPrivate *priv;
};

struct _MMBroadbandModemIceraClass{
    MMBroadbandModemClass parent;
};

G_MODULE_EXPORT
GType mm_broadband_modem_icera_get_type (void);

G_MODULE_EXPORT
MMBroadbandModemIcera *mm_broadband_modem_icera_new (const gchar *device,
                                                     const gchar **drivers,
                                                     const gchar *plugin,
                                                     guint16 vendor_id,
                                                     guint16 product_id);

#endif /* MM_BROADBAND_MODEM_ICERA_H */
