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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_SIM_PANTECH_H
#define MM_SIM_PANTECH_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-sim.h"

#define MM_TYPE_SIM_PANTECH            (mm_sim_pantech_get_type ())
#define MM_SIM_PANTECH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM_PANTECH, MMSimPantech))
#define MM_SIM_PANTECH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIM_PANTECH, MMSimPantechClass))
#define MM_IS_SIM_PANTECH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM_PANTECH))
#define MM_IS_SIM_PANTECH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIM_PANTECH))
#define MM_SIM_PANTECH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIM_PANTECH, MMSimPantechClass))

typedef struct _MMSimPantech MMSimPantech;
typedef struct _MMSimPantechClass MMSimPantechClass;

struct _MMSimPantech {
    MMBaseSim parent;
};

struct _MMSimPantechClass {
    MMBaseSimClass parent;
};

GType mm_sim_pantech_get_type (void);

void       mm_sim_pantech_new        (MMBaseModem *modem,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
MMBaseSim *mm_sim_pantech_new_finish (GAsyncResult  *res,
                                      GError       **error);

#endif /* MM_SIM_PANTECH_H */
