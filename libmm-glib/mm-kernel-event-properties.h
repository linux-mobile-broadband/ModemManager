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
 * Copyright (C) 2016 Velocloud, Inc.
 */

#ifndef MM_KERNEL_EVENT_PROPERTIES_H
#define MM_KERNEL_EVENT_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_KERNEL_EVENT_PROPERTIES            (mm_kernel_event_properties_get_type ())
#define MM_KERNEL_EVENT_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_KERNEL_EVENT_PROPERTIES, MMKernelEventProperties))
#define MM_KERNEL_EVENT_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_KERNEL_EVENT_PROPERTIES, MMKernelEventPropertiesClass))
#define MM_IS_KERNEL_EVENT_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_KERNEL_EVENT_PROPERTIES))
#define MM_IS_KERNEL_EVENT_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_KERNEL_EVENT_PROPERTIES))
#define MM_KERNEL_EVENT_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_KERNEL_EVENT_PROPERTIES, MMKernelEventPropertiesClass))

typedef struct _MMKernelEventProperties MMKernelEventProperties;
typedef struct _MMKernelEventPropertiesClass MMKernelEventPropertiesClass;
typedef struct _MMKernelEventPropertiesPrivate MMKernelEventPropertiesPrivate;

/**
 * MMKernelEventProperties:
 *
 * The #MMKernelEventProperties structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMKernelEventProperties {
    /*< private >*/
    GObject parent;
    MMKernelEventPropertiesPrivate *priv;
};

struct _MMKernelEventPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_kernel_event_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMKernelEventProperties, g_object_unref)

MMKernelEventProperties *mm_kernel_event_properties_new (void);

void          mm_kernel_event_properties_set_action    (MMKernelEventProperties *self,
                                                        const gchar             *action);
const gchar  *mm_kernel_event_properties_get_action    (MMKernelEventProperties *self);

void          mm_kernel_event_properties_set_subsystem (MMKernelEventProperties *self,
                                                        const gchar             *subsystem);
const gchar  *mm_kernel_event_properties_get_subsystem (MMKernelEventProperties *self);

void          mm_kernel_event_properties_set_name      (MMKernelEventProperties *self,
                                                        const gchar             *name);
const gchar  *mm_kernel_event_properties_get_name      (MMKernelEventProperties *self);

void          mm_kernel_event_properties_set_uid       (MMKernelEventProperties *self,
                                                        const gchar             *uid);
const gchar  *mm_kernel_event_properties_get_uid       (MMKernelEventProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMKernelEventProperties *mm_kernel_event_properties_new_from_string     (const gchar  *str,
                                                                         GError      **error);

MMKernelEventProperties *mm_kernel_event_properties_new_from_dictionary (GVariant  *dictionary,
                                                                         GError   **error);

GVariant                *mm_kernel_event_properties_get_dictionary      (MMKernelEventProperties *self);

#endif

G_END_DECLS

#endif /* MM_KERNEL_EVENT_PROPERTIES_H */
