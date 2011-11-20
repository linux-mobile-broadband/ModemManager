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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef _MM_SIM_H_
#define _MM_SIM_H_

#include <ModemManager.h>
#include <mm-gdbus-sim.h>

G_BEGIN_DECLS

typedef MmGdbusSim     MMSim;
#define MM_TYPE_SIM(o) MM_GDBUS_TYPE_SIM (o)
#define MM_SIM(o)      MM_GDBUS_SIM(o)
#define MM_IS_SIM(o)   MM_GDBUS_IS_SIM(o)

const gchar *mm_sim_get_path                (MMSim *self);
const gchar *mm_sim_get_identifier          (MMSim *self);
const gchar *mm_sim_get_imsi                (MMSim *self);
const gchar *mm_sim_get_operator_identifier (MMSim *self);
const gchar *mm_sim_get_operator_name       (MMSim *self);

gchar *mm_sim_dup_path                (MMSim *self);
gchar *mm_sim_dup_identifier          (MMSim *self);
gchar *mm_sim_dup_imsi                (MMSim *self);
gchar *mm_sim_dup_operator_identifier (MMSim *self);
gchar *mm_sim_dup_operator_name       (MMSim *self);

void     mm_sim_send_pin        (MMSim *self,
                                 const gchar *pin,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_sim_send_pin_finish (MMSim *self,
                                 GAsyncResult *res,
                                 GError **error);
gboolean mm_sim_send_pin_sync   (MMSim *self,
                                 const gchar *pin,
                                 GCancellable *cancellable,
                                 GError **error);

void     mm_sim_send_puk        (MMSim *self,
                                 const gchar *puk,
                                 const gchar *pin,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_sim_send_puk_finish (MMSim *self,
                                 GAsyncResult *res,
                                 GError **error);
gboolean mm_sim_send_puk_sync   (MMSim *self,
                                 const gchar *puk,
                                 const gchar *pin,
                                 GCancellable *cancellable,
                                 GError **error);

void     mm_sim_enable_pin        (MMSim *self,
                                   const gchar *pin,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_sim_enable_pin_finish (MMSim *self,
                                   GAsyncResult *res,
                                   GError **error);
gboolean mm_sim_enable_pin_sync   (MMSim *self,
                                   const gchar *pin,
                                   GCancellable *cancellable,
                                   GError **error);


void     mm_sim_disable_pin        (MMSim *self,
                                    const gchar *pin,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_sim_disable_pin_finish (MMSim *self,
                                    GAsyncResult *res,
                                    GError **error);
gboolean mm_sim_disable_pin_sync   (MMSim *self,
                                    const gchar *pin,
                                    GCancellable *cancellable,
                                    GError **error);

void     mm_sim_change_pin        (MMSim *self,
                                   const gchar *old_pin,
                                   const gchar *new_pin,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_sim_change_pin_finish (MMSim *self,
                                   GAsyncResult *res,
                                   GError **error);
gboolean mm_sim_change_pin_sync   (MMSim *self,
                                   const gchar *old_pin,
                                   const gchar *new_pin,
                                   GCancellable *cancellable,
                                   GError **error);

G_END_DECLS

#endif /* _MM_SIM_H_ */
