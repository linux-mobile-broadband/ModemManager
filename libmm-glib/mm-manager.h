/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2011 - 2012 Google, Inc.
 *
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#ifndef _MM_MANAGER_H_
#define _MM_MANAGER_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-kernel-event-properties.h"

G_BEGIN_DECLS

#define MM_TYPE_MANAGER            (mm_manager_get_type ())
#define MM_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MANAGER, MMManager))
#define MM_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MANAGER, MMManagerClass))
#define MM_IS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MANAGER, MMManagerClass))

typedef struct _MMManager MMManager;
typedef struct _MMManagerClass MMManagerClass;
typedef struct _MMManagerPrivate MMManagerPrivate;

/**
 * MMManager:
 *
 * The #MMManager structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMManager {
    /*< private >*/
    MmGdbusObjectManagerClient parent;
    MMManagerPrivate *priv;
};

struct _MMManagerClass {
    /*< private >*/
    MmGdbusObjectManagerClientClass parent;
};

GType mm_manager_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMManager, g_object_unref)

void mm_manager_new (
    GDBusConnection               *connection,
    GDBusObjectManagerClientFlags  flags,
    GCancellable                  *cancellable,
    GAsyncReadyCallback            callback,
    gpointer                       user_data);
MMManager *mm_manager_new_finish (
    GAsyncResult  *res,
    GError       **error);
MMManager *mm_manager_new_sync (
    GDBusConnection                *connection,
    GDBusObjectManagerClientFlags   flags,
    GCancellable                   *cancellable,
    GError                        **error);

GDBusProxy *mm_manager_peek_proxy (MMManager *manager);
GDBusProxy *mm_manager_get_proxy  (MMManager *manager);

const gchar *mm_manager_get_version (MMManager *manager);

void mm_manager_set_logging (MMManager           *manager,
                             const gchar         *level,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data);
gboolean mm_manager_set_logging_finish (MMManager     *manager,
                                        GAsyncResult  *res,
                                        GError       **error);
gboolean mm_manager_set_logging_sync (MMManager     *manager,
                                      const gchar   *level,
                                      GCancellable  *cancellable,
                                      GError       **error);

void mm_manager_scan_devices (MMManager           *manager,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data);
gboolean mm_manager_scan_devices_finish (MMManager     *manager,
                                         GAsyncResult  *res,
                                         GError       **error);
gboolean mm_manager_scan_devices_sync (MMManager     *manager,
                                       GCancellable  *cancellable,
                                       GError       **error);

void     mm_manager_report_kernel_event        (MMManager                *manager,
                                                MMKernelEventProperties  *properties,
                                                GCancellable             *cancellable,
                                                GAsyncReadyCallback       callback,
                                                gpointer                  user_data);
gboolean mm_manager_report_kernel_event_finish (MMManager                *manager,
                                                GAsyncResult             *res,
                                                GError                  **error);
gboolean mm_manager_report_kernel_event_sync   (MMManager                *manager,
                                                MMKernelEventProperties  *properties,
                                                GCancellable             *cancellable,
                                                GError                  **error);

void     mm_manager_inhibit_device        (MMManager           *manager,
                                           const gchar         *uid,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);
gboolean mm_manager_inhibit_device_finish (MMManager           *manager,
                                           GAsyncResult        *res,
                                           GError             **error);
gboolean mm_manager_inhibit_device_sync   (MMManager           *manager,
                                           const gchar         *uid,
                                           GCancellable        *cancellable,
                                           GError             **error);

void     mm_manager_uninhibit_device        (MMManager           *manager,
                                             const gchar         *uid,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data);
gboolean mm_manager_uninhibit_device_finish (MMManager           *manager,
                                             GAsyncResult        *res,
                                             GError             **error);
gboolean mm_manager_uninhibit_device_sync   (MMManager           *manager,
                                             const gchar         *uid,
                                             GCancellable        *cancellable,
                                             GError             **error);

G_END_DECLS

#endif /* _MM_MANAGER_H_ */
