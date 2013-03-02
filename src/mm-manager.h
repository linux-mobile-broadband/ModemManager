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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_MANAGER_H
#define MM_MANAGER_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-gdbus-manager.h"

#define MM_TYPE_MANAGER            (mm_manager_get_type ())
#define MM_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MANAGER, MMManager))
#define MM_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MANAGER, MMManagerClass))
#define MM_IS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MANAGER, MMManagerClass))

#define MM_MANAGER_CONNECTION "connection" /* Construct-only */

typedef struct _MMManagerPrivate MMManagerPrivate;

typedef struct {
    MmGdbusOrgFreedesktopModemManager1Skeleton parent;
    MMManagerPrivate *priv;
} MMManager;

typedef struct {
    MmGdbusOrgFreedesktopModemManager1SkeletonClass parent;
} MMManagerClass;

GType mm_manager_get_type (void);

MMManager       *mm_manager_new         (GDBusConnection *bus,
                                         GError **error);

void             mm_manager_start       (MMManager *manager,
                                         gboolean manual_scan);

void             mm_manager_shutdown    (MMManager *manager);

guint32          mm_manager_num_modems  (MMManager *manager);

#endif /* MM_MANAGER_H */
