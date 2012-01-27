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
 * Copyright (C) 2011 The Chromium OS Authors
 */

#ifndef MM_MODEM_TIME_H
#define MM_MODEM_TIME_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_TIME      (mm_modem_time_get_type ())
#define MM_MODEM_TIME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_TIME, MMModemTime))
#define MM_IS_MODEM_TIME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_TIME))
#define MM_MODEM_TIME_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_TIME, MMModemTime))

#define MM_DBUS_INTERFACE_MODEM_TIME "org.freedesktop.ModemManager.Modem.Time"

#define MM_MODEM_TIME_NETWORK_TIMEZONE "network-timezone"

typedef struct _MMModemTime MMModemTime;

typedef void (*MMModemTimeGetNetworkTimeFn) (MMModemTime *self,
                                             const char *network_time,
                                             GError *error,
                                             gpointer user_data);

struct _MMModemTime {
    GTypeInterface g_iface;

    /* Methods */
    void (*get_network_time) (MMModemTime *self,
                              MMModemTimeGetNetworkTimeFn callback,
                              gpointer user_data);

    gboolean (*poll_network_timezone) (MMModemTime *self,
                                       MMModemFn callback,
                                       gpointer user_data);
};

GType mm_modem_time_get_type (void);

void mm_modem_time_get_network_time (MMModemTime *self,
                                     MMModemTimeGetNetworkTimeFn callback,
                                     gpointer user_data);

gboolean mm_modem_time_poll_network_timezone (MMModemTime *self,
                                              MMModemFn callback,
                                              gpointer user_data);

#endif /* MM_MODEM_TIME_H */
