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
 * Copyright (C) 2016 Velocloud, Inc.
 */

#ifndef MM_KERNEL_DEVICE_GENERIC_H
#define MM_KERNEL_DEVICE_GENERIC_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device.h"

#define MM_TYPE_KERNEL_DEVICE_GENERIC            (mm_kernel_device_generic_get_type ())
#define MM_KERNEL_DEVICE_GENERIC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_KERNEL_DEVICE_GENERIC, MMKernelDeviceGeneric))
#define MM_KERNEL_DEVICE_GENERIC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_KERNEL_DEVICE_GENERIC, MMKernelDeviceGenericClass))
#define MM_IS_KERNEL_DEVICE_GENERIC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_KERNEL_DEVICE_GENERIC))
#define MM_IS_KERNEL_DEVICE_GENERIC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_KERNEL_DEVICE_GENERIC))
#define MM_KERNEL_DEVICE_GENERIC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_KERNEL_DEVICE_GENERIC, MMKernelDeviceGenericClass))

typedef struct _MMKernelDeviceGeneric        MMKernelDeviceGeneric;
typedef struct _MMKernelDeviceGenericClass   MMKernelDeviceGenericClass;
typedef struct _MMKernelDeviceGenericPrivate MMKernelDeviceGenericPrivate;

struct _MMKernelDeviceGeneric {
    MMKernelDevice parent;
    MMKernelDeviceGenericPrivate *priv;
};

struct _MMKernelDeviceGenericClass {
    MMKernelDeviceClass parent;
};

GType mm_kernel_device_generic_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMKernelDeviceGeneric, g_object_unref)

MMKernelDevice *mm_kernel_device_generic_new            (MMKernelEventProperties  *properties,
                                                         GError                  **error);
MMKernelDevice *mm_kernel_device_generic_new_with_rules (MMKernelEventProperties  *properties,
                                                         GArray                   *rules,
                                                         GError                  **error);

#endif /* MM_KERNEL_DEVICE_GENERIC_H */
