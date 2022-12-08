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
 * Copyright (C) 2022 Fibocom Wireless Inc.
 */

#ifndef MM_SHARED_FIBOCOM_H
#define MM_SHARED_FIBOCOM_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem.h"

#define MM_TYPE_SHARED_FIBOCOM               (mm_shared_fibocom_get_type ())
#define MM_SHARED_FIBOCOM(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SHARED_FIBOCOM, MMSharedFibocom))
#define MM_IS_SHARED_FIBOCOM(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SHARED_FIBOCOM))
#define MM_SHARED_FIBOCOM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_SHARED_FIBOCOM, MMSharedFibocom))

typedef struct _MMSharedFibocom MMSharedFibocom;

struct _MMSharedFibocom {
    GTypeInterface g_iface;

    /* Peek 3GPP interface of the parent class of the object */
    MMIfaceModem3gpp * (* peek_parent_3gpp_interface) (MMSharedFibocom *self);
};

GType mm_shared_fibocom_get_type (void);

void     mm_shared_fibocom_set_initial_eps_bearer_settings        (MMIfaceModem3gpp    *self,
                                                                   MMBearerProperties  *config,
                                                                   GAsyncReadyCallback  callback,
                                                                   gpointer             user_data);
gboolean mm_shared_fibocom_set_initial_eps_bearer_settings_finish (MMIfaceModem3gpp    *self,
                                                                   GAsyncResult        *res,
                                                                   GError             **error);

#endif /* MM_SHARED_FIBOCOM_H */
