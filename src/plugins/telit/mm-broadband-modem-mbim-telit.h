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
 * Copyright (C) 2019 Daniele Palmas <dnlplm@gmail.com>
 */

#ifndef MM_BROADBAND_MODEM_MBIM_TELIT_H
#define MM_BROADBAND_MODEM_MBIM_TELIT_H

#include "mm-broadband-modem-mbim.h"

#define MM_TYPE_BROADBAND_MODEM_MBIM_TELIT              (mm_broadband_modem_mbim_telit_get_type ())
#define MM_BROADBAND_MODEM_MBIM_TELIT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_MBIM_TELIT, MMBroadbandModemMbimTelit))
#define MM_BROADBAND_MODEM_MBIM_TELIT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM_TELIT, MMBroadbandModemMbimTelitClass))
#define MM_IS_BROADBAND_MODEM_MBIM_TELIT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_MBIM_TELIT))
#define MM_IS_BROADBAND_MODEM_MBIM_TELIT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_MBIM_TELIT))
#define MM_BROADBAND_MODEM_MBIM_TELIT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_MBIM_TELIT, MMBroadbandModemMbimTelitClass))

typedef struct _MMBroadbandModemMbimTelit MMBroadbandModemMbimTelit;
typedef struct _MMBroadbandModemMbimTelitClass MMBroadbandModemMbimTelitClass;

struct _MMBroadbandModemMbimTelit {
    MMBroadbandModemMbim parent;
};

struct _MMBroadbandModemMbimTelitClass{
    MMBroadbandModemMbimClass parent;
};

GType mm_broadband_modem_mbim_telit_get_type (void);

MMBroadbandModemMbimTelit *mm_broadband_modem_mbim_telit_new (const gchar  *device,
                                                              const gchar  *physdev,
                                                              const gchar **drivers,
                                                              const gchar  *plugin,
                                                              guint16       vendor_id,
                                                              guint16       product_id,
                                                              guint16       subsystem_vendor_id);

#endif /* MM_BROADBAND_MODEM_TELIT_H */
