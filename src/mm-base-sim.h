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
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 *
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_BASE_SIM_H
#define MM_BASE_SIM_H

#include <glib.h>
#include <glib-object.h>

#include <mm-gdbus-sim.h>
#include "mm-base-modem.h"

#define MM_TYPE_BASE_SIM            (mm_base_sim_get_type ())
#define MM_BASE_SIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_SIM, MMBaseSim))
#define MM_BASE_SIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_SIM, MMBaseSimClass))
#define MM_IS_BASE_SIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_SIM))
#define MM_IS_BASE_SIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_SIM))
#define MM_BASE_SIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_SIM, MMBaseSimClass))

typedef struct _MMBaseSim MMBaseSim;
typedef struct _MMBaseSimClass MMBaseSimClass;
typedef struct _MMBaseSimPrivate MMBaseSimPrivate;

/* Properties */
#define MM_BASE_SIM_PATH        "sim-path"
#define MM_BASE_SIM_CONNECTION  "sim-connection"
#define MM_BASE_SIM_MODEM       "sim-modem"
#define MM_BASE_SIM_SLOT_NUMBER "sim-slot-number"

/* Signals */
#define MM_BASE_SIM_PIN_LOCK_ENABLED "sim-pin-lock-enabled"

struct _MMBaseSim {
    MmGdbusSimSkeleton parent;
    MMBaseSimPrivate *priv;
};

struct _MMBaseSimClass {
    MmGdbusSimSkeletonClass parent;

    /* Wait SIM ready (async) */
    void     (* wait_sim_ready)        (MMBaseSim            *self,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);
    gboolean (* wait_sim_ready_finish) (MMBaseSim            *self,
                                        GAsyncResult         *res,
                                        GError              **error);

    /* Load SIM identifier (async) */
    void    (* load_sim_identifier)        (MMBaseSim *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
    gchar * (* load_sim_identifier_finish) (MMBaseSim *self,
                                            GAsyncResult *res,
                                            GError **error);

    /* Load IMSI (async) */
    void    (* load_imsi)        (MMBaseSim *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gchar * (* load_imsi_finish) (MMBaseSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Load EID (async) */
    void    (* load_eid)         (MMBaseSim *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gchar * (* load_eid_finish)  (MMBaseSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Load operator identifier (async) */
    void    (* load_operator_identifier)        (MMBaseSim *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
    gchar * (* load_operator_identifier_finish) (MMBaseSim *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Load operator name (async) */
    void    (* load_operator_name)        (MMBaseSim *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
    gchar * (* load_operator_name_finish) (MMBaseSim *self,
                                           GAsyncResult *res,
                                           GError **error);

    /* Load emergency numbers (async) */
    void  (* load_emergency_numbers)        (MMBaseSim *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
    GStrv (* load_emergency_numbers_finish) (MMBaseSim *self,
                                             GAsyncResult *res,
                                             GError **error);

    /* Change PIN (async) */
    void     (* change_pin)        (MMBaseSim *self,
                                    const gchar *old_pin,
                                    const gchar *new_pin,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gboolean (* change_pin_finish) (MMBaseSim *self,
                                    GAsyncResult *res,
                                    GError **error);

    /* Enable PIN (async) */
    void     (* enable_pin)        (MMBaseSim *self,
                                    const gchar *pin,
                                    gboolean enabled,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gboolean (* enable_pin_finish) (MMBaseSim *self,
                                    GAsyncResult *res,
                                    GError **error);

    /* Send PIN (async) */
    void     (* send_pin)        (MMBaseSim *self,
                                  const gchar *pin,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gboolean (* send_pin_finish) (MMBaseSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Send PUK (async) */
    void     (* send_puk)        (MMBaseSim *self,
                                  const gchar *puk,
                                  const gchar *new_pin,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gboolean (* send_puk_finish) (MMBaseSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Signals */
    void     (* pin_lock_enabled) (MMBaseSim *self,
                                   gboolean enabled);
};

GType mm_base_sim_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseSim, g_object_unref)

void         mm_base_sim_new                        (MMBaseModem *modem,
                                                     GCancellable *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
MMBaseSim   *mm_base_sim_new_finish                 (GAsyncResult  *res,
                                                     GError       **error);

void         mm_base_sim_initialize                 (MMBaseSim *self,
                                                     GCancellable *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean     mm_base_sim_initialize_finish          (MMBaseSim *self,
                                                     GAsyncResult *result,
                                                     GError **error);

MMBaseSim   *mm_base_sim_new_initialized            (MMBaseModem *modem,
                                                     guint        slot_number,
                                                     gboolean     active,
                                                     const gchar *sim_identifier,
                                                     const gchar *imsi,
                                                     const gchar *eid,
                                                     const gchar *operator_identifier,
                                                     const gchar *operator_name,
                                                     const GStrv  emergency_numbers);

void         mm_base_sim_send_pin                   (MMBaseSim *self,
                                                     const gchar *pin,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean     mm_base_sim_send_pin_finish            (MMBaseSim *self,
                                                     GAsyncResult *res,
                                                     GError **error);

void         mm_base_sim_send_puk                   (MMBaseSim *self,
                                                     const gchar *puk,
                                                     const gchar *new_pin,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean     mm_base_sim_send_puk_finish            (MMBaseSim *self,
                                                     GAsyncResult *res,
                                                     GError **error);

void         mm_base_sim_export                     (MMBaseSim *self);

const gchar *mm_base_sim_get_path                   (MMBaseSim *sim);

guint        mm_base_sim_get_slot_number            (MMBaseSim *self);

void         mm_base_sim_load_sim_identifier        (MMBaseSim *self,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gchar       *mm_base_sim_load_sim_identifier_finish (MMBaseSim *self,
                                                     GAsyncResult *res,
                                                     GError **error);

gboolean     mm_base_sim_is_emergency_number (MMBaseSim   *self,
                                              const gchar *number);

#endif /* MM_BASE_SIM_H */
