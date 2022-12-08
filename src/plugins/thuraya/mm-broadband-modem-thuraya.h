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
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
 */

#ifndef MM_BROADBAND_MODEM_THURAYA_H
#define MM_BROADBAND_MODEM_THURAYA_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_THURAYA            (mm_broadband_modem_thuraya_get_type ())
#define MM_BROADBAND_MODEM_THURAYA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_THURAYA, MMBroadbandModemThuraya))
#define MM_BROADBAND_MODEM_THURAYA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_THURAYA, MMBroadbandModemThurayaClass))
#define MM_IS_BROADBAND_MODEM_THURAYA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_THURAYA))
#define MM_IS_BROADBAND_MODEM_THURAYA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_THURAYA))
#define MM_BROADBAND_MODEM_THURAYA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_THURAYA, MMBroadbandModemThurayaClass))

typedef struct _MMBroadbandModemThuraya MMBroadbandModemThuraya;
typedef struct _MMBroadbandModemThurayaClass MMBroadbandModemThurayaClass;

struct _MMBroadbandModemThuraya {
    MMBroadbandModem parent;
};

struct _MMBroadbandModemThurayaClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_thuraya_get_type (void);

MMBroadbandModemThuraya *mm_broadband_modem_thuraya_new (const gchar *device,
                                                         const gchar **drivers,
                                                         const gchar *plugin,
                                                         guint16 vendor_id,
                                                         guint16 product_id);

#endif /* MM_BROADBAND_MODEM_THURAYA_H */
