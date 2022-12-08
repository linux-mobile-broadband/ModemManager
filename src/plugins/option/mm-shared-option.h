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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_SHARED_OPTION_H
#define MM_SHARED_OPTION_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"

#define MM_TYPE_SHARED_OPTION                   (mm_shared_option_get_type ())
#define MM_SHARED_OPTION(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_OPTION, MMSharedOption))
#define MM_IS_SHARED_OPTION(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_OPTION))
#define MM_SHARED_OPTION_GET_INTERFACE(obj)     (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_OPTION, MMSharedOption))

typedef struct _MMSharedOption MMSharedOption;

struct _MMSharedOption {
    GTypeInterface g_iface;
};

GType mm_shared_option_get_type (void);

void       mm_shared_option_create_sim        (MMIfaceModem         *self,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data);
MMBaseSim *mm_shared_option_create_sim_finish (MMIfaceModem         *self,
                                               GAsyncResult         *res,
                                               GError              **error);

#endif  /* MM_SHARED_OPTION_H */
