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

#define MM_SIM_PATH           "sim-path"
#define MM_SIM_CONNECTION     "sim-connection"
#define MM_SIM_MODEM          "sim-modem"

struct _MMSim {
    MmGdbusSimSkeleton parent;
    MMSimPrivate *priv;
};

struct _MMSimClass {
    MmGdbusSimSkeletonClass parent;
};

GType mm_sim_get_type (void);

void   mm_sim_new        (MMBaseModem *modem,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data);
MMSim *mm_sim_new_finish (GAsyncInitable *initable,
                          GAsyncResult  *res,
                          GError       **error);

void     mm_sim_initialize        (MMSim *self,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean mm_sim_initialize_finish (MMSim *self,
                                   GAsyncResult *result,
                                   GError **error);

#endif /* MM_SIM_H */

