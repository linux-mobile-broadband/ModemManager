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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 *
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef MM_BROADBAND_MODEM_WAVECOM_H
#define MM_BROADBAND_MODEM_WAVECOM_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_WAVECOM            (mm_broadband_modem_wavecom_get_type ())
#define MM_BROADBAND_MODEM_WAVECOM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_WAVECOM, MMBroadbandModemWavecom))
#define MM_BROADBAND_MODEM_WAVECOM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_WAVECOM, MMBroadbandModemWavecomClass))
#define MM_IS_BROADBAND_MODEM_WAVECOM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_WAVECOM))
#define MM_IS_BROADBAND_MODEM_WAVECOM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_WAVECOM))
#define MM_BROADBAND_MODEM_WAVECOM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_WAVECOM, MMBroadbandModemWavecomClass))

typedef struct _MMBroadbandModemWavecom MMBroadbandModemWavecom;
typedef struct _MMBroadbandModemWavecomClass MMBroadbandModemWavecomClass;

struct _MMBroadbandModemWavecom {
    MMBroadbandModem parent;
};

struct _MMBroadbandModemWavecomClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_wavecom_get_type (void);

MMBroadbandModemWavecom *mm_broadband_modem_wavecom_new (const gchar *device,
                                                         const gchar *physdev,
                                                         const gchar **drivers,
                                                         const gchar *plugin,
                                                         guint16 vendor_id,
                                                         guint16 product_id);

#endif /* MM_BROADBAND_MODEM_WAVECOM_H */
