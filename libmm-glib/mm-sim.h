/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _MM_SIM_H_
#define _MM_SIM_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-sim.h"

G_BEGIN_DECLS

#define MM_TYPE_SIM            (mm_sim_get_type ())
#define MM_SIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM, MMSim))
#define MM_SIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_SIM, MMSimClass))
#define MM_IS_SIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM))
#define MM_IS_SIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_SIM))
#define MM_SIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_SIM, MMSimClass))

typedef struct _MMSim MMSim;
typedef struct _MMSimClass MMSimClass;

/**
 * MMSim:
 *
 * The #MMSim structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMSim {
    /*< private >*/
    MmGdbusSimProxy parent;
    gpointer unused;
};

struct _MMSimClass {
    /*< private >*/
    MmGdbusSimProxyClass parent;
};

GType mm_sim_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSim, g_object_unref)

const gchar *mm_sim_get_path                (MMSim *self);
gchar       *mm_sim_dup_path                (MMSim *self);

gboolean     mm_sim_get_active              (MMSim *self);

const gchar *mm_sim_get_identifier          (MMSim *self);
gchar       *mm_sim_dup_identifier          (MMSim *self);

const gchar *mm_sim_get_imsi                (MMSim *self);
gchar       *mm_sim_dup_imsi                (MMSim *self);

const gchar *mm_sim_get_eid                 (MMSim *self);
gchar       *mm_sim_dup_eid                 (MMSim *self);

const gchar *mm_sim_get_operator_identifier (MMSim *self);
gchar       *mm_sim_dup_operator_identifier (MMSim *self);

const gchar *mm_sim_get_operator_name       (MMSim *self);
gchar       *mm_sim_dup_operator_name       (MMSim *self);

const gchar * const  *mm_sim_get_emergency_numbers (MMSim *self);
gchar               **mm_sim_dup_emergency_numbers (MMSim *self);

GList*       mm_sim_get_preferred_networks  (MMSim *self);

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

void     mm_sim_set_preferred_networks        (MMSim *self,
                                               const GList *preferred_networks,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean mm_sim_set_preferred_networks_finish (MMSim *self,
                                               GAsyncResult *res,
                                               GError **error);
gboolean mm_sim_set_preferred_networks_sync   (MMSim *self,
                                               const GList *preferred_networks,
                                               GCancellable *cancellable,
                                               GError **error);

G_END_DECLS

#endif /* _MM_SIM_H_ */
