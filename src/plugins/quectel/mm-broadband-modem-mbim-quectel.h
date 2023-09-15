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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 */

#ifndef MM_BROADBAND_MODEM_MBIM_QUECTEL_H
#define MM_BROADBAND_MODEM_MBIM_QUECTEL_H

#include "mm-broadband-modem-mbim.h"

#define MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL            (mm_broadband_modem_mbim_quectel_get_type ())
#define MM_BROADBAND_MODEM_MBIM_QUECTEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL, MMBroadbandModemMbimQuectel))
#define MM_BROADBAND_MODEM_MBIM_QUECTEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL, MMBroadbandModemMbimQuectelClass))
#define MM_IS_BROADBAND_MODEM_MBIM_QUECTEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL))
#define MM_IS_BROADBAND_MODEM_MBIM_QUECTEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL))
#define MM_BROADBAND_MODEM_MBIM_QUECTEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL, MMBroadbandModemMbimQuectelClass))

typedef struct _MMBroadbandModemMbimQuectel MMBroadbandModemMbimQuectel;
typedef struct _MMBroadbandModemMbimQuectelClass MMBroadbandModemMbimQuectelClass;

struct _MMBroadbandModemMbimQuectel {
    MMBroadbandModemMbim parent;
};

struct _MMBroadbandModemMbimQuectelClass{
    MMBroadbandModemMbimClass parent;
};

GType mm_broadband_modem_mbim_quectel_get_type (void);

MMBroadbandModemMbimQuectel *mm_broadband_modem_mbim_quectel_new (const gchar  *device,
                                                                  const gchar  *physdev,
                                                                  const gchar **drivers,
                                                                  const gchar  *plugin,
                                                                  guint16       vendor_id,
                                                                  guint16       product_id);

#endif /* MM_BROADBAND_MODEM_MBIM_QUECTEL_H */
