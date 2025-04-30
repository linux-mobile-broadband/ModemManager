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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-op-lock.h"

G_DEFINE_INTERFACE (MMIfaceOpLock, mm_iface_op_lock, G_TYPE_OBJECT)

/*****************************************************************************/

void
mm_iface_op_lock_authorize_and_lock (MMIfaceOpLock         *self,
                                     GDBusMethodInvocation *invocation,
                                     const gchar           *authorization,
                                     MMOperationPriority    operation_priority,
                                     const gchar           *operation_description,
                                     GAsyncReadyCallback    callback,
                                     gpointer               user_data)
{
    g_assert (MM_IFACE_OP_LOCK_GET_IFACE (self)->authorize_and_lock != NULL);

    MM_IFACE_OP_LOCK_GET_IFACE (self)->authorize_and_lock (self,
                                                           invocation,
                                                           authorization,
                                                           operation_priority,
                                                           operation_description,
                                                           callback,
                                                           user_data);
}

gssize
mm_iface_op_lock_authorize_and_lock_finish (MMIfaceOpLock  *self,
                                            GAsyncResult   *res,
                                            GError        **error)
{
    g_assert (MM_IFACE_OP_LOCK_GET_IFACE (self)->authorize_and_lock_finish != NULL);

    return MM_IFACE_OP_LOCK_GET_IFACE (self)->authorize_and_lock_finish (self, res, error);
}

void
mm_iface_op_lock_unlock (MMIfaceOpLock  *self,
                         gssize          operation_id)
{
    g_assert (MM_IFACE_OP_LOCK_GET_IFACE (self)->unlock != NULL);

    return MM_IFACE_OP_LOCK_GET_IFACE (self)->unlock (self, operation_id);
}

/*****************************************************************************/

static void
mm_iface_op_lock_default_init (MMIfaceOpLockInterface *iface)
{
}
