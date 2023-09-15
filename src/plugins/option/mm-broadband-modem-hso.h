/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your hso) any later version.
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

#ifndef MM_BROADBAND_MODEM_HSO_H
#define MM_BROADBAND_MODEM_HSO_H

#include "mm-broadband-modem-option.h"

#define MM_TYPE_BROADBAND_MODEM_HSO            (mm_broadband_modem_hso_get_type ())
#define MM_BROADBAND_MODEM_HSO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_HSO, MMBroadbandModemHso))
#define MM_BROADBAND_MODEM_HSO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_HSO, MMBroadbandModemHsoClass))
#define MM_IS_BROADBAND_MODEM_HSO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_HSO))
#define MM_IS_BROADBAND_MODEM_HSO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_HSO))
#define MM_BROADBAND_MODEM_HSO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_HSO, MMBroadbandModemHsoClass))

typedef struct _MMBroadbandModemHso MMBroadbandModemHso;
typedef struct _MMBroadbandModemHsoClass MMBroadbandModemHsoClass;
typedef struct _MMBroadbandModemHsoPrivate MMBroadbandModemHsoPrivate;

struct _MMBroadbandModemHso {
    MMBroadbandModemOption parent;
    MMBroadbandModemHsoPrivate *priv;
};

struct _MMBroadbandModemHsoClass{
    MMBroadbandModemOptionClass parent;
};

GType mm_broadband_modem_hso_get_type (void);

MMBroadbandModemHso *mm_broadband_modem_hso_new (const gchar *device,
                                                 const gchar *physdev,
                                                 const gchar **drivers,
                                                 const gchar *plugin,
                                                 guint16 vendor_id,
                                                 guint16 product_id);

#endif /* MM_BROADBAND_MODEM_HSO_H */
