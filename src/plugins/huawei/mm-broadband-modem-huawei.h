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

#ifndef MM_BROADBAND_MODEM_HUAWEI_H
#define MM_BROADBAND_MODEM_HUAWEI_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_HUAWEI            (mm_broadband_modem_huawei_get_type ())
#define MM_BROADBAND_MODEM_HUAWEI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_HUAWEI, MMBroadbandModemHuawei))
#define MM_BROADBAND_MODEM_HUAWEI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_HUAWEI, MMBroadbandModemHuaweiClass))
#define MM_IS_BROADBAND_MODEM_HUAWEI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_HUAWEI))
#define MM_IS_BROADBAND_MODEM_HUAWEI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_HUAWEI))
#define MM_BROADBAND_MODEM_HUAWEI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_HUAWEI, MMBroadbandModemHuaweiClass))

typedef struct _MMBroadbandModemHuawei MMBroadbandModemHuawei;
typedef struct _MMBroadbandModemHuaweiClass MMBroadbandModemHuaweiClass;
typedef struct _MMBroadbandModemHuaweiPrivate MMBroadbandModemHuaweiPrivate;

struct _MMBroadbandModemHuawei {
    MMBroadbandModem parent;
    MMBroadbandModemHuaweiPrivate *priv;
};

struct _MMBroadbandModemHuaweiClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_huawei_get_type (void);

MMBroadbandModemHuawei *mm_broadband_modem_huawei_new (const gchar *device,
                                                       const gchar *physdev,
                                                       const gchar **driver,
                                                       const gchar *plugin,
                                                       guint16 vendor_id,
                                                       guint16 product_id);

MMPortSerialAt *mm_broadband_modem_huawei_peek_port_at_for_data (MMBroadbandModemHuawei *self,
                                                                 MMPort *port);
GList          *mm_broadband_modem_huawei_get_at_port_list      (MMBroadbandModemHuawei *self);

#endif /* MM_BROADBAND_MODEM_HUAWEI_H */
