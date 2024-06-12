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
 * Copyright (C) 2024 Fibocom Wireless Inc.
 */

#ifndef MM_SHARED_MTK_H
#define MM_SHARED_MTK_H

#include <config.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"

#define MM_TYPE_SHARED_MTK mm_shared_mtk_get_type ()
G_DECLARE_INTERFACE (MMSharedMtk, mm_shared_mtk, MM, SHARED_MTK, MMIfaceModem)

struct _MMSharedMtkInterface {
    GTypeInterface g_iface;
};

void            mm_shared_mtk_load_unlock_retries (MMIfaceModem         *self,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);

MMUnlockRetries *mm_shared_mtk_load_unlock_retries_finish (MMIfaceModem  *self,
                                                           GAsyncResult  *res,
                                                           GError        **error);

#endif /* MM_SHARED_MTK_H */