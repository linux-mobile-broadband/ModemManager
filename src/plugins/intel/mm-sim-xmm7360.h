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
 * Copyright (C) 2024 Thomas Vogt
 */

#ifndef MM_SIM_XMM7360_H
#define MM_SIM_XMM7360_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-sim.h"

typedef enum {
    XMM7360_GEN_PIN_OP_DISABLE = 2,
    XMM7360_GEN_PIN_OP_ENABLE = 3,
    XMM7360_GEN_PIN_OP_CHANGE = 4,
} Xmm7360GenPinOp;

#define MM_TYPE_SIM_XMM7360            (mm_sim_xmm7360_get_type ())
#define MM_SIM_XMM7360(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM_XMM7360, MMSimXmm7360))
#define MM_SIM_XMM7360_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIM_XMM7360, MMSimXmm7360Class))
#define MM_IS_SIM_XMM7360(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM_XMM7360))
#define MM_IS_SIM_XMM7360_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIM_XMM7360))
#define MM_SIM_XMM7360_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIM_XMM7360, MMSimXmm7360Class))

typedef struct _MMSimXmm7360 MMSimXmm7360;
typedef struct _MMSimXmm7360Class MMSimXmm7360Class;

struct _MMSimXmm7360 {
    MMBaseSim            parent;
};

struct _MMSimXmm7360Class {
    MMBaseSimClass parent;
};

GType mm_sim_xmm7360_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSimXmm7360, g_object_unref)

void       mm_sim_xmm7360_new        (MMBaseModem *modem,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
MMBaseSim *mm_sim_xmm7360_new_finish (GAsyncResult  *res,
                                      GError       **error);
MMBaseSim *mm_sim_xmm7360_new_initialized (MMBaseModem     *modem,
                                           guint            slot_number,
                                           gboolean         active,
                                           MMSimType        sim_type,
                                           MMSimEsimStatus  esim_status,
                                           const gchar     *sim_identifier,
                                           const gchar     *imsi,
                                           const gchar     *eid,
                                           const gchar     *operator_identifier,
                                           const gchar     *operator_name,
                                           const GStrv      emergency_numbers);
#endif /* MM_SIM_XMM7360_H */
