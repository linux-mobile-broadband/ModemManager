/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_MODEM_TIME_H_
#define _MM_MODEM_TIME_H_

#include <ModemManager.h>
#include <libmm-common.h>

G_BEGIN_DECLS

typedef MmGdbusModemTime      MMModemTime;
#define MM_TYPE_MODEM_TIME(o) MM_GDBUS_TYPE_MODEM_TIME (o)
#define MM_MODEM_TIME(o)      MM_GDBUS_MODEM_TIME(o)
#define MM_IS_MODEM_TIME(o)   MM_GDBUS_IS_MODEM_TIME(o)

const gchar *mm_modem_time_get_path (MMModemTime *self);
gchar       *mm_modem_time_dup_path (MMModemTime *self);

void   mm_modem_time_get_network_time        (MMModemTime *self,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gchar *mm_modem_time_get_network_time_finish (MMModemTime *self,
                                              GAsyncResult *res,
                                              GError **error);
gchar *mm_modem_time_get_network_time_sync   (MMModemTime *self,
                                              GCancellable *cancellable,
                                              GError **error);

MMNetworkTimezone *mm_modem_time_get_network_timezone (MMModemTime *self);

G_END_DECLS

#endif /* _MM_MODEM_TIME_H_ */
