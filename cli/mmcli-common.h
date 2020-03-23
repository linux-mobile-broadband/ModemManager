/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MMCLI_COMMON_H_
#define _MMCLI_COMMON_H_

#include <gio/gio.h>

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

void       mmcli_get_manager        (GDBusConnection     *connection,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
MMManager *mmcli_get_manager_finish (GAsyncResult        *res);
MMManager *mmcli_get_manager_sync   (GDBusConnection     *connection);

void     mmcli_get_modem         (GDBusConnection      *connection,
                                  const gchar          *str,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data);
MMObject *mmcli_get_modem_finish (GAsyncResult         *res,
                                  MMManager           **o_manager);
MMObject *mmcli_get_modem_sync   (GDBusConnection      *connection,
                                  const gchar          *str,
                                  MMManager           **o_manager);

void      mmcli_get_bearer        (GDBusConnection      *connection,
                                   const gchar          *str,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data);
MMBearer *mmcli_get_bearer_finish (GAsyncResult         *res,
                                   MMManager           **manager,
                                   MMObject            **object);
MMBearer *mmcli_get_bearer_sync   (GDBusConnection      *connection,
                                   const gchar          *str,
                                   MMManager           **manager,
                                   MMObject            **object);

void   mmcli_get_sim        (GDBusConnection      *connection,
                             const gchar          *str,
                             GCancellable         *cancellable,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data);
MMSim *mmcli_get_sim_finish (GAsyncResult         *res,
                             MMManager           **manager,
                             MMObject            **object);
MMSim *mmcli_get_sim_sync   (GDBusConnection      *connection,
                             const gchar          *str,
                             MMManager           **manager,
                             MMObject            **object);

void   mmcli_get_sms        (GDBusConnection     *connection,
                             const gchar         *str,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data);
MMSms *mmcli_get_sms_finish (GAsyncResult        *res,
                             MMManager          **manager,
                             MMObject           **object);
MMSms *mmcli_get_sms_sync   (GDBusConnection     *connection,
                             const gchar         *str,
                             MMManager          **manager,
                             MMObject           **object);

void    mmcli_get_call        (GDBusConnection      *connection,
                               const gchar          *str,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data);
MMCall *mmcli_get_call_finish (GAsyncResult         *res,
                               MMManager           **manager,
                               MMObject            **object);
MMCall *mmcli_get_call_sync   (GDBusConnection      *connection,
                               const gchar          *str,
                               MMManager           **manager,
                               MMObject            **object);

const gchar *mmcli_get_state_reason_string (MMModemStateChangeReason reason);

GOptionGroup *mmcli_get_common_option_group (void);
const gchar  *mmcli_get_common_modem_string (void);
const gchar  *mmcli_get_common_bearer_string (void);
const gchar  *mmcli_get_common_sim_string (void);
const gchar  *mmcli_get_common_sms_string (void);
const gchar  *mmcli_get_common_call_string (void);

gchar *mmcli_prefix_newlines (const gchar *prefix,
                              const gchar *str);

#endif /* _MMCLI_COMMON_H_ */
