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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#ifndef MM_IFACE_OP_LOCK_H
#define MM_IFACE_OP_LOCK_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

typedef enum {  /*< underscore_name=mm_operation_priority >*/
    MM_OPERATION_PRIORITY_UNKNOWN,
    /* Default operations are scheduled at the end of the list of pending
     * operations */
    MM_OPERATION_PRIORITY_DEFAULT,
    /* An override operation will make all pending operations be cancelled, and
     * it will also disallow adding new operations. This type of operation would
     * be the last one expected in a modem object. */
    MM_OPERATION_PRIORITY_OVERRIDE,
} MMOperationPriority;

typedef enum {
    MM_OPERATION_LOCK_REQUIRED,
    MM_OPERATION_LOCK_ALREADY_ACQUIRED,
} MMOperationLock;

#define MM_TYPE_IFACE_OP_LOCK mm_iface_op_lock_get_type ()
G_DECLARE_INTERFACE (MMIfaceOpLock, mm_iface_op_lock, MM, IFACE_OP_LOCK, GObject)

struct _MMIfaceOpLockInterface {
    GTypeInterface g_iface;

    void (* authorize_and_lock) (MMIfaceOpLock           *self,
                                 GDBusMethodInvocation   *invocation,
                                 const gchar             *authorization,
                                 MMOperationPriority      operation_priority,
                                 const gchar             *operation_description,
                                 GAsyncReadyCallback      callback,
                                 gpointer                 user_data);

    gssize (* authorize_and_lock_finish) (MMIfaceOpLock  *self,
                                          GAsyncResult   *res,
                                          GError        **error);

    void (*unlock) (MMIfaceOpLock                        *self,
                    gssize                                operation_id);
};


void     mm_iface_op_lock_authorize_and_lock (MMIfaceOpLock         *self,
                                              GDBusMethodInvocation *invocation,
                                              const gchar           *authorization,
                                              MMOperationPriority    operation_priority,
                                              const gchar           *operation_description,
                                              GAsyncReadyCallback    callback,
                                              gpointer               user_data);

gssize mm_iface_op_lock_authorize_and_lock_finish (MMIfaceOpLock    *self,
                                                   GAsyncResult     *res,
                                                   GError          **error);

void   mm_iface_op_lock_unlock (MMIfaceOpLock *self,
                                gssize         operation_id);

#endif /* MM_IFACE_OP_LOCK_H */
