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
 * Copyright (C) 2018-2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BROADBAND_MODEM_FOXCONN_T77W968_H
#define MM_BROADBAND_MODEM_FOXCONN_T77W968_H

#include "mm-broadband-modem-mbim.h"

#define MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968            (mm_broadband_modem_foxconn_t77w968_get_type ())
#define MM_BROADBAND_MODEM_FOXCONN_T77W968(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968, MMBroadbandModemFoxconnT77w968))
#define MM_BROADBAND_MODEM_FOXCONN_T77W968_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968, MMBroadbandModemFoxconnT77w968Class))
#define MM_IS_BROADBAND_MODEM_FOXCONN_T77W968(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968))
#define MM_IS_BROADBAND_MODEM_FOXCONN_T77W968_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968))
#define MM_BROADBAND_MODEM_FOXCONN_T77W968_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_FOXCONN_T77W968, MMBroadbandModemFoxconnT77w968Class))

typedef struct _MMBroadbandModemFoxconnT77w968 MMBroadbandModemFoxconnT77w968;
typedef struct _MMBroadbandModemFoxconnT77w968Class MMBroadbandModemFoxconnT77w968Class;
typedef struct _MMBroadbandModemFoxconnT77w968Private MMBroadbandModemFoxconnT77w968Private;

struct _MMBroadbandModemFoxconnT77w968 {
    MMBroadbandModemMbim parent;
    MMBroadbandModemFoxconnT77w968Private *priv;
};

struct _MMBroadbandModemFoxconnT77w968Class{
    MMBroadbandModemMbimClass parent;
};

GType mm_broadband_modem_foxconn_t77w968_get_type (void);

MMBroadbandModemFoxconnT77w968 *mm_broadband_modem_foxconn_t77w968_new (const gchar  *device,
									const gchar **driver,
									const gchar  *plugin,
									guint16       vendor_id,
									guint16       product_id);

#endif /* MM_BROADBAND_MODEM_FOXCONN_T77W968_H */
