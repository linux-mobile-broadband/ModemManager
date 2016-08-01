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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BROADBAND_MODEM_UBLOX_H
#define MM_BROADBAND_MODEM_UBLOX_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_UBLOX            (mm_broadband_modem_ublox_get_type ())
#define MM_BROADBAND_MODEM_UBLOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_UBLOX, MMBroadbandModemUblox))
#define MM_BROADBAND_MODEM_UBLOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_UBLOX, MMBroadbandModemUbloxClass))
#define MM_IS_BROADBAND_MODEM_UBLOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_UBLOX))
#define MM_IS_BROADBAND_MODEM_UBLOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_UBLOX))
#define MM_BROADBAND_MODEM_UBLOX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_UBLOX, MMBroadbandModemUbloxClass))

typedef struct _MMBroadbandModemUblox MMBroadbandModemUblox;
typedef struct _MMBroadbandModemUbloxClass MMBroadbandModemUbloxClass;
typedef struct _MMBroadbandModemUbloxPrivate MMBroadbandModemUbloxPrivate;

struct _MMBroadbandModemUblox {
    MMBroadbandModem parent;
    MMBroadbandModemUbloxPrivate *priv;
};

struct _MMBroadbandModemUbloxClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_ublox_get_type (void);

MMBroadbandModemUblox *mm_broadband_modem_ublox_new (const gchar *device,
                                                     const gchar **drivers,
                                                     const gchar *plugin,
                                                     guint16 vendor_id,
                                                     guint16 product_id);

#endif /* MM_BROADBAND_MODEM_UBLOX_H */
