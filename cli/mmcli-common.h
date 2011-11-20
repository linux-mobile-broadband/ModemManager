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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MMCLI_COMMON_H_
#define _MMCLI_COMMON_H_

#include <gio/gio.h>
#include <libmm-glib.h>

void       mmcli_get_manager        (GDBusConnection *connection,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
MMManager *mmcli_get_manager_finish (GAsyncResult *res);
MMManager *mmcli_get_manager_sync   (GDBusConnection *connection);


void     mmcli_get_modem        (GDBusConnection *connection,
                                 const gchar *modem_str,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
MMModem *mmcli_get_modem_finish (GAsyncResult *res);
MMModem *mmcli_get_modem_sync   (GDBusConnection *connection,
                                 const gchar *modem_str);

#endif /* _MMCLI_COMMON_H_ */
