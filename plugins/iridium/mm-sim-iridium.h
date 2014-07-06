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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH.
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef MM_SIM_IRIDIUM_H
#define MM_SIM_IRIDIUM_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-sim.h"

#define MM_TYPE_SIM_IRIDIUM            (mm_sim_iridium_get_type ())
#define MM_SIM_IRIDIUM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIM_IRIDIUM, MMSimIridium))
#define MM_SIM_IRIDIUM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIM_IRIDIUM, MMSimIridiumClass))
#define MM_IS_SIM_IRIDIUM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIM_IRIDIUM))
#define MM_IS_SIM_IRIDIUM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIM_IRIDIUM))
#define MM_SIM_IRIDIUM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIM_IRIDIUM, MMSimIridiumClass))

typedef struct _MMSimIridium MMSimIridium;
typedef struct _MMSimIridiumClass MMSimIridiumClass;

struct _MMSimIridium {
    MMBaseSim parent;
};

struct _MMSimIridiumClass {
    MMBaseSimClass parent;
};

GType mm_sim_iridium_get_type (void);

void       mm_sim_iridium_new        (MMBaseModem *modem,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
MMBaseSim *mm_sim_iridium_new_finish (GAsyncResult  *res,
                                      GError       **error);

#endif /* MM_SIM_IRIDIUM_H */
