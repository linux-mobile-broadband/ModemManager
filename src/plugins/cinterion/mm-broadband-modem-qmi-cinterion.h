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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BROADBAND_MODEM_QMI_CINTERION_QMI_H
#define MM_BROADBAND_MODEM_QMI_CINTERION_QMI_H

#include "mm-broadband-modem-qmi.h"

#define MM_TYPE_BROADBAND_MODEM_QMI_CINTERION            (mm_broadband_modem_qmi_cinterion_get_type ())
#define MM_BROADBAND_MODEM_QMI_CINTERION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_QMI_CINTERION, MMBroadbandModemQmiCinterion))
#define MM_BROADBAND_MODEM_QMI_CINTERION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_QMI_CINTERION, MMBroadbandModemQmiCinterionClass))
#define MM_IS_BROADBAND_MODEM_QMI_CINTERION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_QMI_CINTERION))
#define MM_IS_BROADBAND_MODEM_QMI_CINTERION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_QMI_CINTERION))
#define MM_BROADBAND_MODEM_QMI_CINTERION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_QMI_CINTERION, MMBroadbandModemQmiCinterionClass))

typedef struct _MMBroadbandModemQmiCinterion MMBroadbandModemQmiCinterion;
typedef struct _MMBroadbandModemQmiCinterionClass MMBroadbandModemQmiCinterionClass;

struct _MMBroadbandModemQmiCinterion {
    MMBroadbandModemQmi parent;
};

struct _MMBroadbandModemQmiCinterionClass{
    MMBroadbandModemQmiClass parent;
};

GType mm_broadband_modem_qmi_cinterion_get_type (void);

MMBroadbandModemQmiCinterion *mm_broadband_modem_qmi_cinterion_new (const gchar *device,
                                                                    const gchar *physdev,
                                                                    const gchar **drivers,
                                                                    const gchar *plugin,
                                                                    guint16 vendor_id,
                                                                    guint16 product_id);

#endif /* MM_BROADBAND_MODEM_QMI_CINTERION_H */
