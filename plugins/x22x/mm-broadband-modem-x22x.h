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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_BROADBAND_MODEM_X22X_H
#define MM_BROADBAND_MODEM_X22X_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_X22X            (mm_broadband_modem_x22x_get_type ())
#define MM_BROADBAND_MODEM_X22X(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_X22X, MMBroadbandModemX22x))
#define MM_BROADBAND_MODEM_X22X_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_X22X, MMBroadbandModemX22xClass))
#define MM_IS_BROADBAND_MODEM_X22X(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_X22X))
#define MM_IS_BROADBAND_MODEM_X22X_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_X22X))
#define MM_BROADBAND_MODEM_X22X_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_X22X, MMBroadbandModemX22xClass))

typedef struct _MMBroadbandModemX22x MMBroadbandModemX22x;
typedef struct _MMBroadbandModemX22xClass MMBroadbandModemX22xClass;
typedef struct _MMBroadbandModemX22xPrivate MMBroadbandModemX22xPrivate;

struct _MMBroadbandModemX22x {
    MMBroadbandModem parent;
    MMBroadbandModemX22xPrivate *priv;
};

struct _MMBroadbandModemX22xClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_x22x_get_type (void);

MMBroadbandModemX22x *mm_broadband_modem_x22x_new (const gchar *device,
                                                   const gchar **drivers,
                                                   const gchar *plugin,
                                                   guint16 vendor_id,
                                                   guint16 product_id);

#endif /* MM_BROADBAND_MODEM_X22X_H */
