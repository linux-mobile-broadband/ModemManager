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
 * Copyright (C) 2008 - 2010 Ericsson AB
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *         Dan Williams <dcbw@redhat.com>
 *         Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef MM_BROADBAND_MODEM_MBM_H
#define MM_BROADBAND_MODEM_MBM_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_MBM            (mm_broadband_modem_mbm_get_type ())
#define MM_BROADBAND_MODEM_MBM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_MBM, MMBroadbandModemMbm))
#define MM_BROADBAND_MODEM_MBM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_MBM, MMBroadbandModemMbmClass))
#define MM_IS_BROADBAND_MODEM_MBM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_MBM))
#define MM_IS_BROADBAND_MODEM_MBM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_MBM))
#define MM_BROADBAND_MODEM_MBM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_MBM, MMBroadbandModemMbmClass))

typedef struct _MMBroadbandModemMbm MMBroadbandModemMbm;
typedef struct _MMBroadbandModemMbmClass MMBroadbandModemMbmClass;
typedef struct _MMBroadbandModemMbmPrivate MMBroadbandModemMbmPrivate;

struct _MMBroadbandModemMbm {
    MMBroadbandModem parent;
    MMBroadbandModemMbmPrivate *priv;
};

struct _MMBroadbandModemMbmClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_mbm_get_type (void);

MMBroadbandModemMbm *mm_broadband_modem_mbm_new (const gchar *device,
                                                 const gchar **drivers,
                                                 const gchar *plugin,
                                                 guint16 vendor_id,
                                                 guint16 product_id);

#endif /* MM_BROADBAND_MODEM_MBM_H */
