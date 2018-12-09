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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BROADBAND_MODEM_QMI_QUECTEL_H
#define MM_BROADBAND_MODEM_QMI_QUECTEL_H

#include "mm-broadband-modem-qmi.h"

#define MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL            (mm_broadband_modem_qmi_quectel_get_type ())
#define MM_BROADBAND_MODEM_QMI_QUECTEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL, MMBroadbandModemQmiQuectel))
#define MM_BROADBAND_MODEM_QMI_QUECTEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL, MMBroadbandModemQmiQuectelClass))
#define MM_IS_BROADBAND_MODEM_QMI_QUECTEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL))
#define MM_IS_BROADBAND_MODEM_QMI_QUECTEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL))
#define MM_BROADBAND_MODEM_QMI_QUECTEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_QMI_QUECTEL, MMBroadbandModemQmiQuectelClass))

typedef struct _MMBroadbandModemQmiQuectel MMBroadbandModemQmiQuectel;
typedef struct _MMBroadbandModemQmiQuectelClass MMBroadbandModemQmiQuectelClass;

struct _MMBroadbandModemQmiQuectel {
    MMBroadbandModemQmi parent;
};

struct _MMBroadbandModemQmiQuectelClass{
    MMBroadbandModemQmiClass parent;
};

GType mm_broadband_modem_qmi_quectel_get_type (void);

MMBroadbandModemQmiQuectel *mm_broadband_modem_qmi_quectel_new (const gchar  *device,
                                                                const gchar **drivers,
                                                                const gchar  *plugin,
                                                                guint16       vendor_id,
                                                                guint16       product_id);

#endif /* MM_BROADBAND_MODEM_QMI_QUECTEL_H */
