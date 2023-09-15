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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BROADBAND_MODEM_PANTECH_H
#define MM_BROADBAND_MODEM_PANTECH_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_PANTECH            (mm_broadband_modem_pantech_get_type ())
#define MM_BROADBAND_MODEM_PANTECH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_PANTECH, MMBroadbandModemPantech))
#define MM_BROADBAND_MODEM_PANTECH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_PANTECH, MMBroadbandModemPantechClass))
#define MM_IS_BROADBAND_MODEM_PANTECH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_PANTECH))
#define MM_IS_BROADBAND_MODEM_PANTECH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_PANTECH))
#define MM_BROADBAND_MODEM_PANTECH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_PANTECH, MMBroadbandModemPantechClass))

typedef struct _MMBroadbandModemPantech MMBroadbandModemPantech;
typedef struct _MMBroadbandModemPantechClass MMBroadbandModemPantechClass;

struct _MMBroadbandModemPantech {
    MMBroadbandModem parent;
};

struct _MMBroadbandModemPantechClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_pantech_get_type (void);

MMBroadbandModemPantech *mm_broadband_modem_pantech_new (const gchar *device,
                                                         const gchar *physdev,
                                                         const gchar **drivers,
                                                         const gchar *plugin,
                                                         guint16 vendor_id,
                                                         guint16 product_id);

#endif /* MM_BROADBAND_MODEM_PANTECH_H */
