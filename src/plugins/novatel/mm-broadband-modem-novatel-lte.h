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
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#ifndef MM_BROADBAND_MODEM_NOVATEL_LTE_H
#define MM_BROADBAND_MODEM_NOVATEL_LTE_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE            (mm_broadband_modem_novatel_lte_get_type ())
#define MM_BROADBAND_MODEM_NOVATEL_LTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE, MMBroadbandModemNovatelLte))
#define MM_BROADBAND_MODEM_NOVATEL_LTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE, MMBroadbandModemNovatelLteClass))
#define MM_IS_BROADBAND_MODEM_NOVATEL_LTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE))
#define MM_IS_BROADBAND_MODEM_NOVATEL_LTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE))
#define MM_BROADBAND_MODEM_NOVATEL_LTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_NOVATEL_LTE, MMBroadbandModemNovatelLteClass))

typedef struct _MMBroadbandModemNovatelLte MMBroadbandModemNovatelLte;
typedef struct _MMBroadbandModemNovatelLteClass MMBroadbandModemNovatelLteClass;
typedef struct _MMBroadbandModemNovatelLtePrivate MMBroadbandModemNovatelLtePrivate;

struct _MMBroadbandModemNovatelLte {
    MMBroadbandModem parent;
    MMBroadbandModemNovatelLtePrivate *priv;
};

struct _MMBroadbandModemNovatelLteClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_novatel_lte_get_type (void);

MMBroadbandModemNovatelLte *mm_broadband_modem_novatel_lte_new (const gchar *device,
                                                                const gchar **drivers,
                                                                const gchar *plugin,
                                                                guint16 vendor_id,
                                                                guint16 product_id);

#endif /* MM_BROADBAND_MODEM_NOVATEL_LTE_H */
