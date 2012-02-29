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

#ifndef MM_SIM_H
#define MM_SIM_H

#include <glib.h>
#include <glib-object.h>

#include <mm-gdbus-sim.h>
#include "mm-base-modem.h"

#define MM_TYPE_SIM            (mm_sim_get_type ())
#define MM_SIM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM, MMSim))
#define MM_SIM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIM, MMSimClass))
#define MM_IS_SIM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM))
#define MM_IS_SIM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIM))
#define MM_SIM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIM, MMSimClass))

typedef struct _MMSim MMSim;
typedef struct _MMSimClass MMSimClass;
typedef struct _MMSimPrivate MMSimPrivate;

/* Properties */
#define MM_SIM_PATH           "sim-path"
#define MM_SIM_CONNECTION     "sim-connection"
#define MM_SIM_MODEM          "sim-modem"

/* Signals */
#define MM_SIM_PIN_LOCK_ENABLED "pin-lock-enabled"

struct _MMSim {
    MmGdbusSimSkeleton parent;
    MMSimPrivate *priv;
};

struct _MMSimClass {
    MmGdbusSimSkeletonClass parent;

    /* Load SIM identifier (async) */
    void (* load_sim_identifier) (MMSim *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gchar * (* load_sim_identifier_finish) (MMSim *self,
                                            GAsyncResult *res,
                                            GError **error);

    /* Load IMSI (async) */
    void (* load_imsi) (MMSim *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    gchar * (* load_imsi_finish) (MMSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Load operator identifier (async) */
    void (* load_operator_identifier) (MMSim *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gchar * (* load_operator_identifier_finish) (MMSim *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Load operator name (async) */
    void (* load_operator_name) (MMSim *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
    gchar * (* load_operator_name_finish) (MMSim *self,
                                           GAsyncResult *res,
                                           GError **error);

    /* Change PIN (async) */
    void (* change_pin) (MMSim *self,
                         const gchar *old_pin,
                         const gchar *new_pin,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* change_pin_finish) (MMSim *self,
                                    GAsyncResult *res,
                                    GError **error);

    /* Enable PIN (async) */
    void (* enable_pin) (MMSim *self,
                         const gchar *pin,
                         gboolean enabled,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (* enable_pin_finish) (MMSim *self,
                                    GAsyncResult *res,
                                    GError **error);

    /* Send PIN (async) */
    void (* send_pin) (MMSim *self,
                       const gchar *pin,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gboolean (* send_pin_finish) (MMSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Send PUK (async) */
    void (* send_puk) (MMSim *self,
                       const gchar *puk,
                       const gchar *new_pin,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gboolean (* send_puk_finish) (MMSim *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Signals */
    void (*pin_lock_enabled) (MMSim *self,
                              gboolean enabled);
};

GType mm_sim_get_type (void);

void   mm_sim_new        (MMBaseModem *modem,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data);
MMSim *mm_sim_new_finish (GAsyncResult  *res,
                          GError       **error);

void     mm_sim_initialize        (MMSim *self,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_sim_initialize_finish (MMSim *self,
                                   GAsyncResult *result,
                                   GError **error);

void     mm_sim_send_pin        (MMSim *self,
                                 const gchar *pin,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_sim_send_pin_finish (MMSim *self,
                                 GAsyncResult *res,
                                 GError **error);

void     mm_sim_send_puk        (MMSim *self,
                                 const gchar *puk,
                                 const gchar *new_pin,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
gboolean mm_sim_send_puk_finish (MMSim *self,
                                 GAsyncResult *res,
                                 GError **error);

void mm_sim_export (MMSim *self);

const gchar *mm_sim_get_path (MMSim *sim);

#endif /* MM_SIM_H */
