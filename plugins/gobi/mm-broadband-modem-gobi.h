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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google Inc.
 */

#ifndef MM_BROADBAND_MODEM_GOBI_H
#define MM_BROADBAND_MODEM_GOBI_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_GOBI            (mm_broadband_modem_gobi_get_type ())
#define MM_BROADBAND_MODEM_GOBI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_GOBI, MMBroadbandModemGobi))
#define MM_BROADBAND_MODEM_GOBI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_GOBI, MMBroadbandModemGobiClass))
#define MM_IS_BROADBAND_MODEM_GOBI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_GOBI))
#define MM_IS_BROADBAND_MODEM_GOBI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_GOBI))
#define MM_BROADBAND_MODEM_GOBI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_GOBI, MMBroadbandModemGobiClass))

typedef struct _MMBroadbandModemGobi MMBroadbandModemGobi;
typedef struct _MMBroadbandModemGobiClass MMBroadbandModemGobiClass;

struct _MMBroadbandModemGobi {
    MMBroadbandModem parent;
};

struct _MMBroadbandModemGobiClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_gobi_get_type (void);

MMBroadbandModemGobi *mm_broadband_modem_gobi_new (const gchar *device,
                                                   const gchar **drivers,
                                                   const gchar *plugin,
                                                   guint16 vendor_id,
                                                   guint16 product_id);

#endif /* MM_BROADBAND_MODEM_GOBI_H */
