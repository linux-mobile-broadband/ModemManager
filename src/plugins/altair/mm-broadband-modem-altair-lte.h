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
 * Copyright (C) 2013 Altair Semiconductor
 *
 * Author: Ori Inbar <ori.inbar@altair-semi.com>
 */

#ifndef MM_BROADBAND_MODEM_ALTAIR_LTE_H
#define MM_BROADBAND_MODEM_ALTAIR_LTE_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE            (mm_broadband_modem_altair_lte_get_type ())
#define MM_BROADBAND_MODEM_ALTAIR_LTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE, MMBroadbandModemAltairLte))
#define MM_BROADBAND_MODEM_ALTAIR_LTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE, MMBroadbandModemAltairLteClass))
#define MM_IS_BROADBAND_MODEM_ALTAIR_LTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE))
#define MM_IS_BROADBAND_MODEM_ALTAIR_LTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE))
#define MM_BROADBAND_MODEM_ALTAIR_LTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_ALTAIR_LTE, MMBroadbandModemAltairLteClass))

typedef struct _MMBroadbandModemAltairLte MMBroadbandModemAltairLte;
typedef struct _MMBroadbandModemAltairLteClass MMBroadbandModemAltairLteClass;
typedef struct _MMBroadbandModemAltairLtePrivate MMBroadbandModemAltairLtePrivate;

struct _MMBroadbandModemAltairLte {
    MMBroadbandModem parent;
    MMBroadbandModemAltairLtePrivate *priv;
};

struct _MMBroadbandModemAltairLteClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_altair_lte_get_type (void);

MMBroadbandModemAltairLte *mm_broadband_modem_altair_lte_new (const gchar *device,
                                                              const gchar *physdev,
                                                              const gchar **drivers,
                                                              const gchar *plugin,
                                                              guint16 vendor_id,
                                                              guint16 product_id);

gboolean mm_broadband_modem_altair_lte_is_sim_refresh_detach_in_progress (MMBroadbandModem *self);

#endif /* MM_BROADBAND_MODEM_ALTAIR_LTE_H */
